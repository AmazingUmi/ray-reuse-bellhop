#include "rayreuse/io/environment_parser.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <exception>
#include <fstream>
#include <istream>
#include <limits>
#include <numbers>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "rayreuse/error.hpp"

namespace rayreuse {
namespace {

constexpr std::size_t kMaximumRayPoints = 2'000'000U;
constexpr std::size_t kMaximumSspPoints = 100'001U;
constexpr std::size_t kMaximumVectorValues = 2'000'000U;
constexpr double kKilometersToMeters = 1000.0;
constexpr double kDensityInputToSi = 1000.0;

struct Record {
  std::size_t lineNumber{};
  std::vector<std::string> tokens;
};

[[noreturn]] void fail(const std::string& sourceName, std::size_t lineNumber,
                       const std::string& message) {
  std::ostringstream detail;
  detail << sourceName;
  if (lineNumber != 0U) {
    detail << ':' << lineNumber;
  }
  detail << ": " << message;
  throw ValidationError(detail.str());
}

[[nodiscard]] Record tokenizeRecord(const std::string& line,
                                    std::size_t lineNumber,
                                    const std::string& sourceName) {
  Record record{.lineNumber = lineNumber};
  std::string token;
  char quote = '\0';

  const auto flushToken = [&] {
    if (!token.empty()) {
      record.tokens.push_back(token);
      token.clear();
    }
  };

  for (std::size_t index = 0U; index < line.size(); ++index) {
    const char character = line[index];
    if (quote != '\0') {
      if (character == quote) {
        if (index + 1U < line.size() && line[index + 1U] == quote) {
          token.push_back(quote);
          ++index;
        } else {
          quote = '\0';
          flushToken();
        }
      } else {
        token.push_back(character);
      }
      continue;
    }

    if (character == '!') {
      break;
    }
    if (character == '\'' || character == '"') {
      if (!token.empty()) {
        fail(sourceName, lineNumber,
             "a quoted token must begin after a separator");
      }
      quote = character;
      continue;
    }
    if (character == '/') {
      flushToken();
      break;
    }
    if (std::isspace(static_cast<unsigned char>(character)) != 0 ||
        character == ',') {
      flushToken();
      continue;
    }
    token.push_back(character);
  }

  if (quote != '\0') {
    fail(sourceName, lineNumber, "unterminated quoted token");
  }
  flushToken();
  return record;
}

class RecordReader {
 public:
  RecordReader(std::istream& input, std::string sourceName)
      : input_(input), sourceName_(std::move(sourceName)) {}

  [[nodiscard]] Record require(std::string_view fieldName) {
    while (true) {
      std::string line;
      if (!std::getline(input_, line)) {
        fail(sourceName_, lineNumber_,
             "unexpected end of file while reading " + std::string(fieldName));
      }
      ++lineNumber_;
      Record record = tokenizeRecord(line, lineNumber_, sourceName_);
      if (!record.tokens.empty()) {
        return record;
      }
    }
  }

  void requireEnd() {
    std::string line;
    while (std::getline(input_, line)) {
      ++lineNumber_;
      const Record record = tokenizeRecord(line, lineNumber_, sourceName_);
      if (!record.tokens.empty()) {
        fail(sourceName_, lineNumber_, "unexpected trailing environment input");
      }
    }
    if (input_.bad()) {
      fail(sourceName_, lineNumber_,
           "I/O failure while reading environment input");
    }
  }

  [[nodiscard]] const std::string& sourceName() const noexcept {
    return sourceName_;
  }

 private:
  std::istream& input_;
  std::string sourceName_;
  std::size_t lineNumber_{};
};

void requireTokenCount(const Record& record, std::size_t expected,
                       const std::string& sourceName,
                       std::string_view fieldName) {
  if (record.tokens.size() != expected) {
    fail(sourceName, record.lineNumber,
         std::string(fieldName) + " requires " + std::to_string(expected) +
             " value(s), found " + std::to_string(record.tokens.size()));
  }
}

[[nodiscard]] double parseDouble(const Record& record, std::size_t index,
                                 const std::string& sourceName,
                                 std::string_view fieldName) {
  if (index >= record.tokens.size()) {
    fail(sourceName, record.lineNumber, "missing " + std::string(fieldName));
  }
  std::string normalized = record.tokens[index];
  std::replace(normalized.begin(), normalized.end(), 'D', 'E');
  std::replace(normalized.begin(), normalized.end(), 'd', 'e');
  try {
    std::size_t consumed = 0U;
    const double value = std::stod(normalized, &consumed);
    if (consumed != normalized.size() || !std::isfinite(value)) {
      fail(sourceName, record.lineNumber,
           std::string(fieldName) + " must be a finite floating-point value");
    }
    return value;
  } catch (const std::exception&) {
    fail(sourceName, record.lineNumber,
         std::string(fieldName) + " must be a finite floating-point value");
  }
}

[[nodiscard]] std::size_t parseCount(
    const Record& record, std::size_t index, const std::string& sourceName,
    std::string_view fieldName, bool allowZero = false,
    std::size_t maximum = kMaximumVectorValues) {
  if (index >= record.tokens.size()) {
    fail(sourceName, record.lineNumber, "missing " + std::string(fieldName));
  }
  try {
    std::size_t consumed = 0U;
    const long long signedValue = std::stoll(record.tokens[index], &consumed);
    if (consumed != record.tokens[index].size() || signedValue < 0 ||
        (!allowZero && signedValue == 0)) {
      fail(sourceName, record.lineNumber,
           std::string(fieldName) + (allowZero
                                         ? " must be a non-negative integer"
                                         : " must be a positive integer"));
    }
    const auto value = static_cast<unsigned long long>(signedValue);
    if (value > static_cast<unsigned long long>(maximum)) {
      fail(sourceName, record.lineNumber,
           std::string(fieldName) + " exceeds the supported limit");
    }
    return static_cast<std::size_t>(value);
  } catch (const std::exception&) {
    fail(sourceName, record.lineNumber,
         std::string(fieldName) + (allowZero ? " must be a non-negative integer"
                                             : " must be a positive integer"));
  }
}

[[nodiscard]] int parsePositiveInt(const Record& record, std::size_t index,
                                   const std::string& sourceName,
                                   std::string_view fieldName) {
  const std::size_t value =
      parseCount(record, index, sourceName, fieldName, false,
                 static_cast<std::size_t>(std::numeric_limits<int>::max()));
  return static_cast<int>(value);
}

void requireExactlyZero(double value, const Record& record,
                        const std::string& sourceName,
                        std::string_view fieldName) {
  if (value != 0.0) {
    fail(sourceName, record.lineNumber,
         std::string(fieldName) +
             " must be zero in the supported flat-boundary subset");
  }
}

[[nodiscard]] std::vector<double> parseVector(RecordReader& reader,
                                              std::size_t count,
                                              std::string_view fieldName,
                                              bool legacySinglePrecision,
                                              double outputScale) {
  const Record record = reader.require(fieldName);
  const std::string& sourceName = reader.sourceName();
  std::vector<double> values;
  values.reserve(count);

  if (record.tokens.size() == count) {
    for (std::size_t index = 0U; index < count; ++index) {
      const double value = parseDouble(record, index, sourceName, fieldName);
      values.push_back(legacySinglePrecision
                           ? static_cast<double>(static_cast<float>(value))
                           : value);
    }
  } else if (count >= 3U && record.tokens.size() == 2U) {
    if (legacySinglePrecision) {
      const float first =
          static_cast<float>(parseDouble(record, 0U, sourceName, fieldName));
      const float last =
          static_cast<float>(parseDouble(record, 1U, sourceName, fieldName));
      const float delta = (last - first) / static_cast<float>(count - 1U);
      for (std::size_t index = 0U; index < count; ++index) {
        values.push_back(
            static_cast<double>(first + static_cast<float>(index) * delta));
      }
    } else {
      const double first = parseDouble(record, 0U, sourceName, fieldName);
      const double last = parseDouble(record, 1U, sourceName, fieldName);
      const double delta = (last - first) / static_cast<double>(count - 1U);
      for (std::size_t index = 0U; index < count; ++index) {
        values.push_back(first + static_cast<double>(index) * delta);
      }
    }
  } else {
    fail(sourceName, record.lineNumber,
         std::string(fieldName) + " requires either " + std::to_string(count) +
             " explicit values or two subtabulation endpoints");
  }

  for (double& value : values) {
    value *= outputScale;
    if (!std::isfinite(value)) {
      fail(sourceName, record.lineNumber,
           std::string(fieldName) + " conversion produced a non-finite value");
    }
  }
  return values;
}

void requireUniformRanges(const std::vector<double>& ranges,
                          const Record& record, const std::string& sourceName) {
  if (ranges.size() < 2U) {
    fail(sourceName, record.lineNumber,
         "Cartesian Cerveny requires at least two receiver ranges");
  }
  const double delta = ranges[1U] - ranges[0U];
  for (std::size_t index = 2U; index < ranges.size(); ++index) {
    const double expected = ranges.front() + static_cast<double>(index) * delta;
    const double tolerance =
        32.0 * std::numeric_limits<double>::epsilon() *
        std::max({1.0, std::abs(expected), std::abs(ranges[index])});
    if (std::abs(ranges[index] - expected) > tolerance) {
      fail(sourceName, record.lineNumber,
           "Cartesian Cerveny receiver ranges must be equally spaced");
    }
  }
}

[[nodiscard]] RawAttenuation makeAttenuation(
    double value, VolumeAttenuationModel volumeModel) {
  return RawAttenuation{.value = value,
                        .unit = AttenuationUnit::DecibelsPerWavelength,
                        .referenceFrequency = 1.0,
                        .powerLawExponent = 1.0,
                        .transitionFrequency = 1.0,
                        .volumeModel = volumeModel};
}

[[nodiscard]] bool depthMatches(double value, double bottomDepth) {
  return std::abs(value - bottomDepth) <
         100.0 * static_cast<double>(std::numeric_limits<float>::epsilon());
}

[[nodiscard]] std::string canonicalRunType(const Record& record,
                                           const std::string& sourceName) {
  requireTokenCount(record, 1U, sourceName, "run type");
  std::string runType = record.tokens.front();
  if (runType.size() > 7U) {
    fail(sourceName, record.lineNumber, "run type exceeds seven characters");
  }
  runType.resize(7U, ' ');
  if (runType[0U] != 'C' || runType[1U] != 'C' || runType[2U] != ' ' ||
      (runType[3U] != ' ' && runType[3U] != 'R') ||
      (runType[4U] != ' ' && runType[4U] != 'R') ||
      (runType[5U] != ' ' && runType[5U] != '2') || runType[6U] != ' ') {
    fail(sourceName, record.lineNumber,
         "only coherent Cartesian point-source rectilinear 2-D "
         "run type 'CC' is supported");
  }
  runType[3U] = 'R';
  runType[4U] = 'R';
  runType[5U] = '2';
  return runType;
}

}  // namespace

ParsedEnvironment EnvironmentParser::parse(
    std::istream& input, std::string sourceName,
    std::optional<std::vector<double>> frequencyOverrideHz) {
  RecordReader reader(input, std::move(sourceName));
  const std::string& source = reader.sourceName();

  const Record titleRecord = reader.require("title");
  requireTokenCount(titleRecord, 1U, source, "title");
  std::string title = titleRecord.tokens.front();
  if (title.empty() || title.size() > 71U) {
    fail(source, titleRecord.lineNumber,
         "title must contain between 1 and 71 characters");
  }

  const Record frequencyRecord = reader.require("frequency");
  requireTokenCount(frequencyRecord, 1U, source, "frequency");
  const double frequency =
      parseDouble(frequencyRecord, 0U, source, "frequency");
  if (frequency <= 0.0) {
    fail(source, frequencyRecord.lineNumber, "frequency must be positive");
  }

  const Record mediaRecord = reader.require("medium count");
  requireTokenCount(mediaRecord, 1U, source, "medium count");
  if (parseCount(mediaRecord, 0U, source, "medium count") != 1U) {
    fail(source, mediaRecord.lineNumber, "only one water medium is supported");
  }

  const Record topOptionsRecord = reader.require("top/SSP options");
  requireTokenCount(topOptionsRecord, 1U, source, "top/SSP options");
  const std::string& topOptions = topOptionsRecord.tokens.front();
  if (topOptions != "CVW" && topOptions != "CVWT") {
    fail(source, topOptionsRecord.lineNumber,
         "only C-linear SSP, vacuum surface, dB/wavelength "
         "attenuation with optional Thorp ('CVW' or 'CVWT') is supported");
  }
  const VolumeAttenuationModel volumeModel = topOptions == "CVWT"
                                                 ? VolumeAttenuationModel::Thorp
                                                 : VolumeAttenuationModel::None;

  const Record waterRecord = reader.require("water-column header");
  requireTokenCount(waterRecord, 3U, source, "water-column header");
  static_cast<void>(parseCount(waterRecord, 0U, source, "water mesh count"));
  requireExactlyZero(
      parseDouble(waterRecord, 1U, source, "surface RMS roughness"),
      waterRecord, source, "surface RMS roughness");
  const double bottomDepth =
      parseDouble(waterRecord, 2U, source, "bottom depth");

  std::vector<SoundSpeedPoint> soundSpeedPoints;
  soundSpeedPoints.reserve(64U);
  while (true) {
    if (soundSpeedPoints.size() >= kMaximumSspPoints) {
      fail(source, waterRecord.lineNumber,
           "sound-speed profile exceeds the supported point limit");
    }
    const Record pointRecord = reader.require("sound-speed profile point");
    if (pointRecord.tokens.size() != 2U && pointRecord.tokens.size() != 5U &&
        pointRecord.tokens.size() != 6U) {
      fail(source, pointRecord.lineNumber,
           "a sound-speed profile point requires 2, 5, or 6 values");
    }
    const double depth = parseDouble(pointRecord, 0U, source, "SSP depth");
    const double soundSpeed =
        parseDouble(pointRecord, 1U, source, "SSP sound speed");
    const double shearSoundSpeed =
        pointRecord.tokens.size() >= 5U
            ? parseDouble(pointRecord, 2U, source, "SSP shear sound speed")
            : 0.0;
    const double densityInput =
        pointRecord.tokens.size() >= 5U
            ? parseDouble(pointRecord, 3U, source, "SSP density")
            : 1.0;
    const double attenuationValue =
        pointRecord.tokens.size() >= 5U
            ? parseDouble(pointRecord, 4U, source, "SSP attenuation")
            : 0.0;
    const double shearAttenuation =
        pointRecord.tokens.size() == 6U
            ? parseDouble(pointRecord, 5U, source, "SSP shear attenuation")
            : 0.0;
    if (shearSoundSpeed != 0.0 || shearAttenuation != 0.0) {
      fail(source, pointRecord.lineNumber,
           "the water-column SSP cannot contain shear properties");
    }
    if (densityInput <= 0.0) {
      fail(source, pointRecord.lineNumber, "SSP density must be positive");
    }
    soundSpeedPoints.push_back(SoundSpeedPoint{
        .depth = depth,
        .soundSpeed = soundSpeed,
        .density = densityInput * kDensityInputToSi,
        .attenuation = makeAttenuation(attenuationValue, volumeModel)});
    if (depthMatches(depth, bottomDepth)) {
      break;
    }
    if (depth > bottomDepth) {
      fail(source, pointRecord.lineNumber,
           "SSP depth passed the declared bottom depth");
    }
  }
  if (soundSpeedPoints.size() < 2U) {
    fail(source, waterRecord.lineNumber,
         "sound-speed profile requires at least two points");
  }

  const double surfaceDepth = soundSpeedPoints.front().depth;
  const BoundaryModel seaSurface = BoundaryModel::vacuum(surfaceDepth);

  const Record bottomOptionsRecord = reader.require("bottom options");
  requireTokenCount(bottomOptionsRecord, 2U, source, "bottom options");
  const std::string& bottomOption = bottomOptionsRecord.tokens.front();
  requireExactlyZero(
      parseDouble(bottomOptionsRecord, 1U, source, "bottom RMS roughness"),
      bottomOptionsRecord, source, "bottom RMS roughness");

  BoundaryModel seabed = BoundaryModel::rigid(bottomDepth);
  if (bottomOption == "A") {
    const Record materialRecord = reader.require("bottom acoustic half-space");
    if (materialRecord.tokens.size() != 5U &&
        materialRecord.tokens.size() != 6U) {
      fail(source, materialRecord.lineNumber,
           "an acoustic half-space requires 5 or 6 values");
    }
    const double materialDepth =
        parseDouble(materialRecord, 0U, source, "half-space depth");
    if (!depthMatches(materialDepth, bottomDepth)) {
      fail(source, materialRecord.lineNumber,
           "half-space depth must match the declared bottom depth");
    }
    const double compressionalSoundSpeed = parseDouble(
        materialRecord, 1U, source, "half-space compressional sound speed");
    const double shearSoundSpeed =
        parseDouble(materialRecord, 2U, source, "half-space shear sound speed");
    const double densityInput =
        parseDouble(materialRecord, 3U, source, "half-space density");
    const double compressionalAttenuation = parseDouble(
        materialRecord, 4U, source, "half-space compressional attenuation");
    const double shearAttenuation =
        materialRecord.tokens.size() == 6U
            ? parseDouble(materialRecord, 5U, source,
                          "half-space shear attenuation")
            : 0.0;
    if (shearSoundSpeed != 0.0 || shearAttenuation != 0.0) {
      fail(source, materialRecord.lineNumber,
           "elastic half-spaces are not supported");
    }
    if (densityInput <= 0.0) {
      fail(source, materialRecord.lineNumber,
           "half-space density must be positive");
    }
    seabed = BoundaryModel::acousticHalfSpace(
        bottomDepth,
        AcousticMaterial{
            .compressionalSoundSpeed = compressionalSoundSpeed,
            .shearSoundSpeed = 0.0,
            .density = densityInput * kDensityInputToSi,
            .compressionalAttenuation =
                makeAttenuation(compressionalAttenuation, volumeModel),
            .shearAttenuation = makeAttenuation(0.0, volumeModel)});
  } else if (bottomOption != "R") {
    fail(source, bottomOptionsRecord.lineNumber,
         "only rigid ('R') and acoustic ('A') flat bottoms are supported");
  }

  const Record sourceCountRecord = reader.require("source-depth count");
  requireTokenCount(sourceCountRecord, 1U, source, "source-depth count");
  const std::size_t sourceCount =
      parseCount(sourceCountRecord, 0U, source, "source-depth count");
  if (sourceCount != 1U) {
    fail(source, sourceCountRecord.lineNumber,
         "Bellhop RayReuse supports exactly one source depth");
  }
  const std::vector<double> sourceDepths =
      parseVector(reader, sourceCount, "source depths", true, 1.0);

  const Record receiverDepthCountRecord =
      reader.require("receiver-depth count");
  requireTokenCount(receiverDepthCountRecord, 1U, source,
                    "receiver-depth count");
  const std::size_t receiverDepthCount =
      parseCount(receiverDepthCountRecord, 0U, source, "receiver-depth count");
  std::vector<double> receiverDepths =
      parseVector(reader, receiverDepthCount, "receiver depths", true, 1.0);

  const Record receiverRangeCountRecord =
      reader.require("receiver-range count");
  requireTokenCount(receiverRangeCountRecord, 1U, source,
                    "receiver-range count");
  const std::size_t receiverRangeCount =
      parseCount(receiverRangeCountRecord, 0U, source, "receiver-range count");
  std::vector<double> receiverRanges =
      parseVector(reader, receiverRangeCount, "receiver ranges", false,
                  kKilometersToMeters);
  requireUniformRanges(receiverRanges, receiverRangeCountRecord, source);

  const Record runTypeRecord = reader.require("run type");
  static_cast<void>(canonicalRunType(runTypeRecord, source));

  const Record launchCountRecord = reader.require("launch-angle count");
  requireTokenCount(launchCountRecord, 1U, source, "launch-angle count");
  const std::size_t requestedLaunchCount =
      parseCount(launchCountRecord, 0U, source, "launch-angle count", true);

  const Record launchAngleRecord = reader.require("launch-angle endpoints");
  requireTokenCount(launchAngleRecord, 2U, source, "launch-angle endpoints");
  const double minimumLaunchAngleDegrees =
      parseDouble(launchAngleRecord, 0U, source, "minimum launch angle");
  const double maximumLaunchAngleDegrees =
      parseDouble(launchAngleRecord, 1U, source, "maximum launch angle");
  const double degreesToRadians = std::numbers::pi / 180.0;

  const Record integratorRecord = reader.require("integrator settings");
  requireTokenCount(integratorRecord, 3U, source, "integrator settings");
  double stepLength = parseDouble(integratorRecord, 0U, source, "step length");
  const double depthLimit =
      parseDouble(integratorRecord, 1U, source, "depth box limit");
  const double rangeLimit =
      parseDouble(integratorRecord, 2U, source, "range box limit") *
      kKilometersToMeters;
  if (stepLength < 0.0) {
    fail(source, integratorRecord.lineNumber,
         "step length must be non-negative");
  }
  if (stepLength == 0.0) {
    stepLength = (bottomDepth - surfaceDepth) / 10.0;
  }

  const Record beamRecord = reader.require("Cerveny beam settings");
  requireTokenCount(beamRecord, 3U, source, "Cerveny beam settings");
  if (beamRecord.tokens.front() != "MS") {
    fail(source, beamRecord.lineNumber,
         "only minimum-width, standard-curvature beam type 'MS' "
         "is supported");
  }
  const double epsilonMultiplier =
      parseDouble(beamRecord, 1U, source, "epsilon multiplier");
  const double loopRange =
      parseDouble(beamRecord, 2U, source, "beam loop range") *
      kKilometersToMeters;
  if (epsilonMultiplier <= 0.0 || loopRange <= 0.0) {
    fail(source, beamRecord.lineNumber,
         "epsilon multiplier and beam loop range must be positive");
  }

  const Record imageRecord = reader.require("image/window settings");
  requireTokenCount(imageRecord, 3U, source, "image/window settings");
  const std::size_t imageCount =
      parseCount(imageRecord, 0U, source, "image count", false, 3U);
  const int beamWindow =
      parsePositiveInt(imageRecord, 1U, source, "beam window");
  if (imageRecord.tokens[2U] != "P") {
    fail(source, imageRecord.lineNumber,
         "only pressure component 'P' is supported");
  }
  reader.requireEnd();

  Environment environment(SoundSpeedProfile(std::move(soundSpeedPoints)),
                          seaSurface, seabed);
  ReceiverGrid receivers(std::move(receiverDepths), std::move(receiverRanges));
  std::vector<double> frequencies = frequencyOverrideHz.has_value()
                                        ? std::move(*frequencyOverrideHz)
                                        : std::vector<double>{frequency};
  SimulationCase simulationCase(
      std::move(environment),
      Source{.depth = sourceDepths.front(), .amplitude = 1.0},
      std::move(receivers), FrequencyGrid(std::move(frequencies)),
      LaunchFan{
          .minimumAngle = minimumLaunchAngleDegrees * degreesToRadians,
          .maximumAngle = maximumLaunchAngleDegrees * degreesToRadians,
          .explicitLaunchAngleCount =
              requestedLaunchCount == 0U
                  ? std::nullopt
                  : std::optional<std::size_t>(requestedLaunchCount),
          .inputDegreeBounds =
              LaunchAngleDegreeBounds{.minimum = minimumLaunchAngleDegrees,
                                      .maximum = maximumLaunchAngleDegrees}},
      IntegratorSettings{.stepLength = stepLength,
                         .rangeLimit = rangeLimit,
                         .depthLimit = depthLimit,
                         .maximumRayPoints = kMaximumRayPoints});

  return ParsedEnvironment{
      .title = std::move(title),
      .simulationCase = std::move(simulationCase),
      .beam = CartesianCervenyInput{
          .epsilonMultiplier = epsilonMultiplier,
          .loopRange = loopRange,
          .influence = CartesianCervenySettings{.imageCount = imageCount,
                                                .beamWindow = beamWindow}}};
}

ParsedEnvironment EnvironmentParser::parseFile(
    const std::filesystem::path& path,
    std::optional<std::vector<double>> frequencyOverrideHz) {
  std::ifstream input(path);
  if (!input.is_open()) {
    throw BellhopError("unable to open environment file: " + path.string());
  }
  return parse(input, path.string(), std::move(frequencyOverrideHz));
}

}  // namespace rayreuse
