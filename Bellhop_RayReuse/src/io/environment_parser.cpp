#include "rayreuse/io/environment_parser.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <exception>
#include <fstream>
#include <istream>
#include <limits>
#include <memory>
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
  Record record{.lineNumber = lineNumber, .tokens = {}};
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

[[nodiscard]] std::vector<double> parseFrequencies(
    const Record& record, const std::string& sourceName) {
  if (record.tokens.empty()) {
    fail(sourceName, record.lineNumber,
         "frequency record requires at least one value");
  }
  std::vector<double> frequencies;
  frequencies.reserve(record.tokens.size());
  for (std::size_t index = 0U; index < record.tokens.size(); ++index) {
    const double frequency =
        parseDouble(record, index, sourceName, "frequency");
    if (frequency <= 0.0) {
      fail(sourceName, record.lineNumber, "frequencies must be positive");
    }
    if (!frequencies.empty() && frequency <= frequencies.back()) {
      fail(sourceName, record.lineNumber,
           "frequencies must be strictly increasing");
    }
    frequencies.push_back(frequency);
  }
  return frequencies;
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

[[nodiscard]] AttenuationUnit parseAttenuationUnit(
    char option, const Record& record, const std::string& sourceName) {
  switch (option) {
    case 'N':
      return AttenuationUnit::NepersPerMeter;
    case 'F':
      return AttenuationUnit::DecibelsPerMeterKilohertz;
    case 'M':
      return AttenuationUnit::DecibelsPerMeter;
    case 'W':
      return AttenuationUnit::DecibelsPerWavelength;
    case 'Q':
      return AttenuationUnit::QualityFactor;
    case 'L':
      return AttenuationUnit::LossParameter;
    default:
      fail(sourceName, record.lineNumber,
           "unknown attenuation unit '" + std::string(1U, option) +
               "'; expected N, F, M, W, Q, or L");
  }
}

[[nodiscard]] RawAttenuation makeAttenuation(
    double value, AttenuationUnit unit, VolumeAttenuationModel volumeModel) {
  return RawAttenuation{.value = value,
                        .unit = unit,
                        .referenceFrequency = 1.0,
                        .powerLawExponent = 1.0,
                        .transitionFrequency = 1.0,
                        .volumeModel = volumeModel};
}

struct ParsedAcousticHalfSpace {
  double materialDepth{};
  AcousticMaterial material;
};

[[nodiscard]] ParsedAcousticHalfSpace readAcousticHalfSpace(
    RecordReader& reader, AttenuationUnit attenuationUnit,
    VolumeAttenuationModel volumeModel, std::string_view boundaryName) {
  const std::string fieldName =
      std::string(boundaryName) + " acoustic half-space";
  const Record record = reader.require(fieldName);
  const std::string& source = reader.sourceName();
  if (record.tokens.size() != 5U && record.tokens.size() != 6U) {
    fail(source, record.lineNumber, fieldName + " requires 5 or 6 values");
  }
  const double materialDepth =
      parseDouble(record, 0U, source, "half-space depth");
  const double compressionalSoundSpeed =
      parseDouble(record, 1U, source, "half-space compressional sound speed");
  const double shearSoundSpeed =
      parseDouble(record, 2U, source, "half-space shear sound speed");
  const double densityInput =
      parseDouble(record, 3U, source, "half-space density");
  const double compressionalAttenuation =
      parseDouble(record, 4U, source, "half-space compressional attenuation");
  const double shearAttenuation =
      record.tokens.size() == 6U
          ? parseDouble(record, 5U, source, "half-space shear attenuation")
          : 0.0;
  if (compressionalSoundSpeed <= 0.0 || shearSoundSpeed < 0.0 ||
      densityInput <= 0.0 || compressionalAttenuation < 0.0 ||
      shearAttenuation < 0.0 ||
      (shearSoundSpeed == 0.0 && shearAttenuation != 0.0)) {
    fail(source, record.lineNumber,
         "half-space requires positive compressional speed/density, "
         "non-negative shear speed/attenuation, and zero shear loss "
         "when shear speed is zero");
  }
  return ParsedAcousticHalfSpace{
      .materialDepth = materialDepth,
      .material = AcousticMaterial{
          .compressionalSoundSpeed = compressionalSoundSpeed,
          .shearSoundSpeed = shearSoundSpeed,
          .density = densityInput * kDensityInputToSi,
          .compressionalAttenuation = makeAttenuation(
              compressionalAttenuation, attenuationUnit, volumeModel),
          .shearAttenuation =
              makeAttenuation(shearAttenuation, attenuationUnit, volumeModel)}};
}

struct ParsedGrainSizeHalfSpace {
  double materialDepth{};
  double meanGrainSize{};
};

[[nodiscard]] ParsedGrainSizeHalfSpace readGrainSizeHalfSpace(
    RecordReader& reader, std::string_view boundaryName) {
  const std::string fieldName = std::string(boundaryName) + " grain size";
  const Record record = reader.require(fieldName);
  requireTokenCount(record, 2U, reader.sourceName(), fieldName);
  return ParsedGrainSizeHalfSpace{
      .materialDepth = parseDouble(record, 0U, reader.sourceName(),
                                   "grain-size half-space depth"),
      .meanGrainSize =
          parseDouble(record, 1U, reader.sourceName(), "mean grain size")};
}

[[nodiscard]] bool depthMatches(double value, double bottomDepth) {
  return std::abs(value - bottomDepth) <
         100.0 * static_cast<double>(std::numeric_limits<float>::epsilon());
}

[[nodiscard]] std::filesystem::path boundaryPath(
    const std::filesystem::path& environmentPath, std::string_view extension) {
  std::filesystem::path path = environmentPath;
  path.replace_extension(extension);
  return path;
}

struct ParsedBoundaryFile {
  BoundaryGeometry geometry;
  SharedLongBoundaryMaterials longMaterials;
};

[[nodiscard]] SharedTabulatedReflectionTable readReflectionTable(
    const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input.is_open()) {
    throw BellhopError("unable to open reflection coefficient file: " +
                       path.string());
  }
  RecordReader reader(input, path.string());
  const std::string& source = reader.sourceName();
  const Record countRecord = reader.require("reflection coefficient count");
  requireTokenCount(countRecord, 1U, source, "reflection coefficient count");
  const std::size_t count = parseCount(countRecord, 0U, source,
                                       "reflection coefficient count", false);
  if (count < 2U) {
    fail(source, countRecord.lineNumber,
         "reflection coefficient table requires at least two points");
  }
  TabulatedReflectionTable points;
  points.reserve(count);
  for (std::size_t index = 0U; index < count; ++index) {
    const Record pointRecord = reader.require("reflection coefficient point");
    requireTokenCount(pointRecord, 3U, source, "reflection coefficient point");
    const double angle =
        parseDouble(pointRecord, 0U, source, "reflection coefficient angle");
    const double magnitude = parseDouble(pointRecord, 1U, source,
                                         "reflection coefficient magnitude");
    const double phaseDegrees =
        parseDouble(pointRecord, 2U, source, "reflection coefficient phase");
    if (magnitude < 0.0) {
      fail(source, pointRecord.lineNumber,
           "reflection coefficient magnitude must be non-negative");
    }
    if (!points.empty() && points.back().angleDegrees >= angle) {
      fail(source, pointRecord.lineNumber,
           "reflection coefficient angles must be strictly increasing");
    }
    points.push_back(TabulatedReflectionPoint{
        .angleDegrees = angle,
        .magnitude = magnitude,
        .phaseRadians = (std::numbers::pi / 180.0) * phaseDegrees});
  }
  reader.requireEnd();
  return std::make_shared<const TabulatedReflectionTable>(std::move(points));
}

[[nodiscard]] ParsedBoundaryFile readBoundaryFile(
    const std::filesystem::path& path, double referenceDepth,
    BoundaryOrientation orientation, AttenuationUnit attenuationUnit,
    VolumeAttenuationModel volumeModel) {
  std::ifstream input(path);
  if (!input.is_open()) {
    throw BellhopError("unable to open boundary file: " + path.string());
  }

  RecordReader reader(input, path.string());
  const std::string& source = reader.sourceName();
  const Record formatRecord = reader.require("boundary format");
  requireTokenCount(formatRecord, 1U, source, "boundary format");
  const std::string& format = formatRecord.tokens.front();
  if (format != "LS" && format != "LL") {
    fail(source, formatRecord.lineNumber,
         "RR-B1 supports only piecewise-linear 'LS'/'LL' boundaries");
  }
  const bool longFormat = format == "LL";

  const Record countRecord = reader.require("boundary point count");
  requireTokenCount(countRecord, 1U, source, "boundary point count");
  const std::size_t pointCount =
      parseCount(countRecord, 0U, source, "boundary point count");
  if (pointCount < 2U) {
    fail(source, countRecord.lineNumber,
         "piecewise-linear boundary requires at least two points");
  }

  std::vector<Vec2> nodes;
  nodes.reserve(pointCount);
  std::vector<AcousticMaterial> nodeMaterials;
  if (longFormat) {
    nodeMaterials.reserve(pointCount);
  }
  for (std::size_t index = 0U; index < pointCount; ++index) {
    const Record pointRecord = reader.require("boundary point");
    requireTokenCount(pointRecord, longFormat ? 7U : 2U, source,
                      "boundary point");
    const double range =
        parseDouble(pointRecord, 0U, source, "boundary range") *
        kKilometersToMeters;
    const double depth = parseDouble(pointRecord, 1U, source, "boundary depth");
    if (!std::isfinite(range)) {
      fail(source, pointRecord.lineNumber,
           "boundary range conversion produced a non-finite value");
    }
    if (!nodes.empty() && nodes.back().range >= range) {
      fail(source, pointRecord.lineNumber,
           "boundary ranges must be strictly increasing");
    }
    if (orientation == BoundaryOrientation::Upper && depth < referenceDepth) {
      fail(source, pointRecord.lineNumber,
           "top boundary depth must not be above the first SSP depth");
    }
    if (orientation == BoundaryOrientation::Lower && depth > referenceDepth) {
      fail(source, pointRecord.lineNumber,
           "bottom boundary depth must not be below the last SSP depth");
    }
    nodes.push_back(Vec2{.range = range, .depth = depth});
    if (longFormat) {
      const double compressionalSoundSpeed = parseDouble(
          pointRecord, 2U, source, "boundary compressional sound speed");
      const double shearSoundSpeed =
          parseDouble(pointRecord, 3U, source, "boundary shear sound speed");
      const double densityInput =
          parseDouble(pointRecord, 4U, source, "boundary material density");
      const double compressionalAttenuation = parseDouble(
          pointRecord, 5U, source, "boundary compressional attenuation");
      const double shearAttenuation =
          parseDouble(pointRecord, 6U, source, "boundary shear attenuation");
      if (compressionalSoundSpeed <= 0.0 || shearSoundSpeed < 0.0 ||
          densityInput <= 0.0 || compressionalAttenuation < 0.0 ||
          shearAttenuation < 0.0 ||
          (shearSoundSpeed == 0.0 && shearAttenuation != 0.0)) {
        fail(source, pointRecord.lineNumber,
             "long-format boundary requires positive compressional speed/"
             "density, non-negative shear speed/attenuation, and zero shear "
             "loss when shear speed is zero");
      }
      nodeMaterials.push_back(AcousticMaterial{
          .compressionalSoundSpeed = compressionalSoundSpeed,
          .shearSoundSpeed = shearSoundSpeed,
          .density = densityInput * kDensityInputToSi,
          .compressionalAttenuation = makeAttenuation(
              compressionalAttenuation, attenuationUnit, volumeModel),
          .shearAttenuation =
              makeAttenuation(shearAttenuation, attenuationUnit, volumeModel)});
    }
  }
  reader.requireEnd();
  SharedLongBoundaryMaterials longMaterials;
  if (longFormat) {
    longMaterials = std::make_shared<const std::vector<AcousticMaterial>>(
        std::move(nodeMaterials));
  }
  return ParsedBoundaryFile{.geometry = BoundaryGeometry::piecewiseLinear(
                                std::move(nodes), referenceDepth, orientation),
                            .longMaterials = std::move(longMaterials)};
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

[[nodiscard]] ParsedEnvironment parseEnvironment(
    std::istream& input, std::string sourceName,
    const std::optional<std::filesystem::path>& environmentPath,
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
  std::vector<double> environmentFrequencies =
      parseFrequencies(frequencyRecord, source);

  const Record mediaRecord = reader.require("medium count");
  requireTokenCount(mediaRecord, 1U, source, "medium count");
  if (parseCount(mediaRecord, 0U, source, "medium count") != 1U) {
    fail(source, mediaRecord.lineNumber, "only one water medium is supported");
  }

  const Record topOptionsRecord = reader.require("top/SSP options");
  requireTokenCount(topOptionsRecord, 1U, source, "top/SSP options");
  std::string topOptions = topOptionsRecord.tokens.front();
  if (topOptions.size() < 3U || topOptions.size() > 5U) {
    fail(source, topOptionsRecord.lineNumber,
         "top/SSP options must contain between three and five characters");
  }
  topOptions.resize(5U, ' ');
  if (topOptions[0U] != 'C' ||
      (topOptions[1U] != 'V' && topOptions[1U] != 'R' &&
       topOptions[1U] != 'A' && topOptions[1U] != 'G' &&
       topOptions[1U] != 'F') ||
      (topOptions[3U] != ' ' && topOptions[3U] != 'T') ||
      (topOptions[4U] != ' ' && topOptions[4U] != '~' &&
       topOptions[4U] != '*')) {
    fail(source, topOptionsRecord.lineNumber,
         "RR-B1 supports C-linear SSP, V/R/A/G/F surfaces, optional Thorp, "
         "and optional piecewise-linear topography");
  }
  const AttenuationUnit attenuationUnit =
      parseAttenuationUnit(topOptions[2U], topOptionsRecord, source);
  const VolumeAttenuationModel volumeModel = topOptions[3U] == 'T'
                                                 ? VolumeAttenuationModel::Thorp
                                                 : VolumeAttenuationModel::None;
  std::optional<ParsedAcousticHalfSpace> topAcousticHalfSpace;
  std::optional<ParsedGrainSizeHalfSpace> topGrainSizeHalfSpace;
  if (topOptions[1U] == 'A') {
    topAcousticHalfSpace =
        readAcousticHalfSpace(reader, attenuationUnit, volumeModel, "top");
  } else if (topOptions[1U] == 'G') {
    topGrainSizeHalfSpace = readGrainSizeHalfSpace(reader, "top");
  }
  const bool hasTopography = topOptions[4U] == '~' || topOptions[4U] == '*';

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
        .attenuation =
            makeAttenuation(attenuationValue, attenuationUnit, volumeModel)});
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
  BoundaryGeometry seaSurfaceGeometry =
      BoundaryGeometry::flat(surfaceDepth, BoundaryOrientation::Upper);
  SharedLongBoundaryMaterials seaSurfaceLongMaterials;
  if (hasTopography) {
    if (!environmentPath.has_value()) {
      fail(source, topOptionsRecord.lineNumber,
           "topography requires parseFile so the sibling .ati file can be "
           "resolved");
    }
    ParsedBoundaryFile boundary = readBoundaryFile(
        boundaryPath(*environmentPath, ".ati"), surfaceDepth,
        BoundaryOrientation::Upper, attenuationUnit, volumeModel);
    seaSurfaceGeometry = std::move(boundary.geometry);
    seaSurfaceLongMaterials = std::move(boundary.longMaterials);
  }
  BoundaryModel seaSurface = BoundaryModel::vacuum(seaSurfaceGeometry);
  if (topOptions[1U] == 'R') {
    seaSurface = BoundaryModel::rigid(std::move(seaSurfaceGeometry));
  } else if (topOptions[1U] == 'A') {
    if (!topAcousticHalfSpace.has_value() ||
        !depthMatches(topAcousticHalfSpace->materialDepth, surfaceDepth)) {
      fail(source, topOptionsRecord.lineNumber,
           "top half-space depth must match the first SSP depth");
    }
    seaSurface = BoundaryModel::acousticHalfSpace(
        std::move(seaSurfaceGeometry), topAcousticHalfSpace->material,
        std::move(seaSurfaceLongMaterials));
  } else if (topOptions[1U] == 'G') {
    if (seaSurfaceLongMaterials || !topGrainSizeHalfSpace.has_value()) {
      fail(source, topOptionsRecord.lineNumber,
           "grain-size surfaces do not support long-format altimetry");
    }
    if (!depthMatches(topGrainSizeHalfSpace->materialDepth, surfaceDepth)) {
      fail(source, topOptionsRecord.lineNumber,
           "top grain-size depth must match the first SSP depth");
    }
    seaSurface = BoundaryModel::grainSizeHalfSpace(
        std::move(seaSurfaceGeometry), topGrainSizeHalfSpace->meanGrainSize);
  } else if (topOptions[1U] == 'F') {
    if (seaSurfaceLongMaterials || !environmentPath.has_value()) {
      fail(source, topOptionsRecord.lineNumber,
           "top tabulated reflection requires short geometry and parseFile "
           "for the sibling .trc file");
    }
    seaSurface = BoundaryModel::tabulatedReflection(
        std::move(seaSurfaceGeometry),
        readReflectionTable(boundaryPath(*environmentPath, ".trc")));
  }

  const Record bottomOptionsRecord = reader.require("bottom options");
  requireTokenCount(bottomOptionsRecord, 2U, source, "bottom options");
  std::string bottomOption = bottomOptionsRecord.tokens.front();
  if (bottomOption.empty() || bottomOption.size() > 2U) {
    fail(source, bottomOptionsRecord.lineNumber,
         "bottom options must contain one or two characters");
  }
  bottomOption.resize(2U, ' ');
  if (bottomOption[1U] != ' ' && bottomOption[1U] != '~' &&
      bottomOption[1U] != '*') {
    fail(source, bottomOptionsRecord.lineNumber,
         "only optional bottom topography '~' or '*' is supported");
  }
  const bool hasBathymetry = bottomOption[1U] == '~' || bottomOption[1U] == '*';
  requireExactlyZero(
      parseDouble(bottomOptionsRecord, 1U, source, "bottom RMS roughness"),
      bottomOptionsRecord, source, "bottom RMS roughness");

  BoundaryGeometry seabedGeometry =
      BoundaryGeometry::flat(bottomDepth, BoundaryOrientation::Lower);
  SharedLongBoundaryMaterials seabedLongMaterials;
  if (hasBathymetry) {
    if (!environmentPath.has_value()) {
      fail(source, bottomOptionsRecord.lineNumber,
           "bathymetry requires parseFile so the sibling .bty file can be "
           "resolved");
    }
    ParsedBoundaryFile boundary = readBoundaryFile(
        boundaryPath(*environmentPath, ".bty"), bottomDepth,
        BoundaryOrientation::Lower, attenuationUnit, volumeModel);
    seabedGeometry = std::move(boundary.geometry);
    seabedLongMaterials = std::move(boundary.longMaterials);
  }

  BoundaryModel seabed = BoundaryModel::rigid(seabedGeometry);
  if (bottomOption[0U] == 'A') {
    const ParsedAcousticHalfSpace acousticHalfSpace =
        readAcousticHalfSpace(reader, attenuationUnit, volumeModel, "bottom");
    if (!depthMatches(acousticHalfSpace.materialDepth, bottomDepth)) {
      fail(source, bottomOptionsRecord.lineNumber,
           "half-space depth must match the declared bottom depth");
    }
    seabed = BoundaryModel::acousticHalfSpace(std::move(seabedGeometry),
                                              acousticHalfSpace.material,
                                              std::move(seabedLongMaterials));
  } else if (bottomOption[0U] == 'G') {
    if (seabedLongMaterials) {
      fail(source, bottomOptionsRecord.lineNumber,
           "grain-size bottoms do not support long-format bathymetry");
    }
    const ParsedGrainSizeHalfSpace grainSizeHalfSpace =
        readGrainSizeHalfSpace(reader, "bottom");
    if (!depthMatches(grainSizeHalfSpace.materialDepth, bottomDepth)) {
      fail(source, bottomOptionsRecord.lineNumber,
           "grain-size depth must match the declared bottom depth");
    }
    seabed = BoundaryModel::grainSizeHalfSpace(
        std::move(seabedGeometry), grainSizeHalfSpace.meanGrainSize);
  } else if (bottomOption[0U] == 'F') {
    if (seabedLongMaterials || !environmentPath.has_value()) {
      fail(source, bottomOptionsRecord.lineNumber,
           "tabulated bottom reflection requires short geometry and "
           "parseFile for the sibling .brc file");
    }
    seabed = BoundaryModel::tabulatedReflection(
        std::move(seabedGeometry),
        readReflectionTable(boundaryPath(*environmentPath, ".brc")));
  } else if (bottomOption[0U] == 'V') {
    seabed = BoundaryModel::vacuum(std::move(seabedGeometry));
  } else if (bottomOption[0U] != 'R') {
    fail(source, bottomOptionsRecord.lineNumber,
         "only vacuum ('V'), rigid ('R'), acoustic ('A'), grain-size ('G'), "
         "and tabulated-reflection ('F') bottoms are supported");
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
                                        : std::move(environmentFrequencies);
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

ParsedEnvironment EnvironmentParser::parse(
    std::istream& input, std::string sourceName,
    std::optional<std::vector<double>> frequencyOverrideHz) {
  return parseEnvironment(input, std::move(sourceName), std::nullopt,
                          std::move(frequencyOverrideHz));
}

ParsedEnvironment EnvironmentParser::parseFile(
    const std::filesystem::path& path,
    std::optional<std::vector<double>> frequencyOverrideHz) {
  std::ifstream input(path);
  if (!input.is_open()) {
    throw BellhopError("unable to open environment file: " + path.string());
  }
  return parseEnvironment(input, path.string(), path,
                          std::move(frequencyOverrideHz));
}

}  // namespace rayreuse
