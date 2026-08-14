#include "bellhop/io/environment_parser.hpp"

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

#include "bellhop/error.hpp"

namespace bellhop {
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

[[noreturn]] void fail(const std::string& sourceName,
                       std::size_t lineNumber,
                       const std::string& message) {
  std::ostringstream detail;
  detail << sourceName;
  if (lineNumber != 0U) {
    detail << ':' << lineNumber;
  }
  detail << ": " << message;
  throw ValidationError(detail.str());
}

[[nodiscard]] Record tokenizeRecord(
    const std::string& line, std::size_t lineNumber,
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
        if (index + 1U < line.size() &&
            line[index + 1U] == quote) {
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
             "unexpected end of file while reading " +
                 std::string(fieldName));
      }
      ++lineNumber_;
      Record record =
          tokenizeRecord(line, lineNumber_, sourceName_);
      if (!record.tokens.empty()) {
        return record;
      }
    }
  }

  void requireEnd() {
    std::string line;
    while (std::getline(input_, line)) {
      ++lineNumber_;
      const Record record =
          tokenizeRecord(line, lineNumber_, sourceName_);
      if (!record.tokens.empty()) {
        fail(sourceName_, lineNumber_,
             "unexpected trailing environment input");
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
         std::string(fieldName) + " requires " +
             std::to_string(expected) + " value(s), found " +
             std::to_string(record.tokens.size()));
  }
}

[[nodiscard]] double parseDouble(
    const Record& record, std::size_t index,
    const std::string& sourceName, std::string_view fieldName) {
  if (index >= record.tokens.size()) {
    fail(sourceName, record.lineNumber,
         "missing " + std::string(fieldName));
  }
  std::string normalized = record.tokens[index];
  std::replace(normalized.begin(), normalized.end(), 'D', 'E');
  std::replace(normalized.begin(), normalized.end(), 'd', 'e');
  try {
    std::size_t consumed = 0U;
    const double value = std::stod(normalized, &consumed);
    if (consumed != normalized.size() || !std::isfinite(value)) {
      fail(sourceName, record.lineNumber,
           std::string(fieldName) +
               " must be a finite floating-point value");
    }
    return value;
  } catch (const std::exception&) {
    fail(sourceName, record.lineNumber,
         std::string(fieldName) +
             " must be a finite floating-point value");
  }
}

[[nodiscard]] std::size_t parseCount(
    const Record& record, std::size_t index,
    const std::string& sourceName, std::string_view fieldName,
    bool allowZero = false,
    std::size_t maximum = kMaximumVectorValues) {
  if (index >= record.tokens.size()) {
    fail(sourceName, record.lineNumber,
         "missing " + std::string(fieldName));
  }
  try {
    std::size_t consumed = 0U;
    const long long signedValue =
        std::stoll(record.tokens[index], &consumed);
    if (consumed != record.tokens[index].size() ||
        signedValue < 0 ||
        (!allowZero && signedValue == 0)) {
      fail(sourceName, record.lineNumber,
           std::string(fieldName) +
               (allowZero
                    ? " must be a non-negative integer"
                    : " must be a positive integer"));
    }
    const auto value =
        static_cast<unsigned long long>(signedValue);
    if (value > static_cast<unsigned long long>(maximum)) {
      fail(sourceName, record.lineNumber,
           std::string(fieldName) + " exceeds the supported limit");
    }
    return static_cast<std::size_t>(value);
  } catch (const std::exception&) {
    fail(sourceName, record.lineNumber,
         std::string(fieldName) +
             (allowZero
                  ? " must be a non-negative integer"
                  : " must be a positive integer"));
  }
}

[[nodiscard]] int parsePositiveInt(
    const Record& record, std::size_t index,
    const std::string& sourceName, std::string_view fieldName) {
  const std::size_t value = parseCount(
      record, index, sourceName, fieldName, false,
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

[[nodiscard]] std::vector<double> parseVector(
    RecordReader& reader, std::size_t count,
    std::string_view fieldName, bool legacySinglePrecision,
    double outputScale) {
  const Record record = reader.require(fieldName);
  const std::string& sourceName = reader.sourceName();
  std::vector<double> values;
  values.reserve(count);

  if (record.tokens.size() == count) {
    for (std::size_t index = 0U; index < count; ++index) {
      const double value =
          parseDouble(record, index, sourceName, fieldName);
      values.push_back(
          legacySinglePrecision
              ? static_cast<double>(static_cast<float>(value))
              : value);
    }
  } else if (count >= 3U && record.tokens.size() == 2U) {
    if (legacySinglePrecision) {
      const float first = static_cast<float>(
          parseDouble(record, 0U, sourceName, fieldName));
      const float last = static_cast<float>(
          parseDouble(record, 1U, sourceName, fieldName));
      const float delta =
          (last - first) / static_cast<float>(count - 1U);
      for (std::size_t index = 0U; index < count; ++index) {
        values.push_back(static_cast<double>(
            first + static_cast<float>(index) * delta));
      }
    } else {
      const double first =
          parseDouble(record, 0U, sourceName, fieldName);
      const double last =
          parseDouble(record, 1U, sourceName, fieldName);
      const double delta =
          (last - first) / static_cast<double>(count - 1U);
      for (std::size_t index = 0U; index < count; ++index) {
        values.push_back(
            first + static_cast<double>(index) * delta);
      }
    }
  } else {
    fail(sourceName, record.lineNumber,
         std::string(fieldName) + " requires either " +
             std::to_string(count) +
             " explicit values or two subtabulation endpoints");
  }

  for (double& value : values) {
    value *= outputScale;
    if (!std::isfinite(value)) {
      fail(sourceName, record.lineNumber,
           std::string(fieldName) +
               " conversion produced a non-finite value");
    }
  }
  return values;
}

void requireUniformRanges(const std::vector<double>& ranges,
                          const Record& record,
                          const std::string& sourceName) {
  if (ranges.size() < 2U) {
    fail(sourceName, record.lineNumber,
         "Cartesian Cerveny requires at least two receiver ranges");
  }
  const double delta = ranges[1U] - ranges[0U];
  for (std::size_t index = 2U; index < ranges.size(); ++index) {
    const double expected =
        ranges.front() + static_cast<double>(index) * delta;
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
    double value, AttenuationUnit unit) {
  return RawAttenuation{
      .value = value,
      .unit = unit,
      .referenceFrequency = 1.0,
      .powerLawExponent = 1.0,
      .transitionFrequency = 1.0};
}

[[nodiscard]] bool depthMatches(double value, double bottomDepth) {
  return std::abs(value - bottomDepth) <
         100.0 * static_cast<double>(
                     std::numeric_limits<float>::epsilon());
}

struct ParsedRunType {
  SimulationRunMode runMode{};
  ReceiverGridLayout receiverLayout{};
  SourceGeometry sourceGeometry{};
  CervenyCoordinateSystem cervenyCoordinateSystem{};
  BeamFamily beamFamily{};
  bool usesSourceBeamPattern{};
};

[[nodiscard]] ParsedRunType canonicalRunType(
    const Record& record, const std::string& sourceName) {
  requireTokenCount(record, 1U, sourceName, "run type");
  std::string runType = record.tokens.front();
  if (runType.size() > 7U) {
    fail(sourceName, record.lineNumber,
         "run type exceeds seven characters");
  }
  runType.resize(7U, ' ');
  const bool commonOptionsValid =
      (runType[2U] == ' ' || runType[2U] == 'O' || runType[2U] == '*') &&
      (runType[3U] == ' ' || runType[3U] == 'R' || runType[3U] == 'X') &&
      (runType[5U] == ' ' || runType[5U] == '2') &&
      runType[6U] == ' ';
  const bool transmissionLoss =
      (runType[0U] == 'C' || runType[0U] == 'I' ||
       runType[0U] == 'S') &&
      (runType[1U] == ' ' || runType[1U] == 'C' ||
       runType[1U] == 'R' || runType[1U] == 'G' ||
       runType[1U] == '^' || runType[1U] == 'g' ||
       runType[1U] == 'B' || runType[1U] == 'S') &&
      (runType[4U] == ' ' || runType[4U] == 'R' || runType[4U] == 'I');
  const bool rayTrace =
      runType[0U] == 'R' &&
      (runType[1U] == ' ' || runType[1U] == 'G') &&
      (runType[4U] == ' ' || runType[4U] == 'R');
  if ((runType[0U] == 'C' || runType[0U] == 'I' ||
       runType[0U] == 'S') &&
      runType[1U] == 'b') {
    fail(sourceName, record.lineNumber,
         "ray-centered geometric Gaussian beams are not implemented in "
         "Bellhop 2-D");
  }
  if (!commonOptionsValid || (!transmissionLoss && !rayTrace)) {
    fail(sourceName, record.lineNumber,
         "only supported 2-D Cerveny, geometric-hat, geometric-Gaussian, "
         "or simple-Gaussian TL and "
         "unshifted geometric 2-D ray-trace run types are supported");
  }
  if (runType[3U] == ' ') {
    runType[3U] = 'R';
  }
  runType[5U] = '2';
  SimulationRunMode mode = SimulationRunMode::RayTrace;
  if (!rayTrace) {
    switch (runType[0U]) {
      case 'C':
        mode = SimulationRunMode::CoherentTransmissionLoss;
        break;
      case 'I':
        mode = SimulationRunMode::IncoherentTransmissionLoss;
        break;
      case 'S':
        mode = SimulationRunMode::SemiCoherentTransmissionLoss;
        break;
      default:
        fail(sourceName, record.lineNumber,
             "unknown transmission-loss coherence mode");
    }
  }
  BeamFamily beamFamily = BeamFamily::CervenyGaussian;
  CervenyCoordinateSystem coordinateSystem =
      CervenyCoordinateSystem::Cartesian;
  if (!rayTrace) {
    switch (runType[1U]) {
      case 'C':
        beamFamily = BeamFamily::CervenyGaussian;
        break;
      case 'R':
        beamFamily = BeamFamily::CervenyGaussian;
        coordinateSystem = CervenyCoordinateSystem::RayCentered;
        break;
      case 'g':
        beamFamily = BeamFamily::GeometricHat;
        coordinateSystem = CervenyCoordinateSystem::RayCentered;
        break;
      case 'B':
        beamFamily = BeamFamily::GeometricGaussian;
        break;
      case 'S':
        beamFamily = BeamFamily::SimpleGaussian;
        break;
      case ' ':
      case 'G':
      case '^':
        beamFamily = BeamFamily::GeometricHat;
        break;
      default:
        fail(sourceName, record.lineNumber, "unknown beam family");
    }
  }
  return ParsedRunType{
      .runMode = mode,
      .receiverLayout =
          runType[4U] == 'I' ? ReceiverGridLayout::Irregular
                             : ReceiverGridLayout::Rectilinear,
      .sourceGeometry =
          runType[3U] == 'X' ? SourceGeometry::Line
                             : SourceGeometry::Point,
      .cervenyCoordinateSystem = coordinateSystem,
      .beamFamily = beamFamily,
      .usesSourceBeamPattern = runType[2U] == '*'};
}

[[nodiscard]] std::filesystem::path boundaryPath(
    const std::filesystem::path& environmentPath,
    std::string_view extension) {
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
    throw BellhopError(
        "unable to open bottom reflection coefficient file: " +
        path.string());
  }
  RecordReader reader(input, path.string());
  const std::string& source = reader.sourceName();
  const Record countRecord = reader.require("reflection coefficient count");
  requireTokenCount(countRecord, 1U, source, "reflection coefficient count");
  const std::size_t count = parseCount(
      countRecord, 0U, source, "reflection coefficient count", false);
  if (count < 2U) {
    fail(source, countRecord.lineNumber,
         "reflection coefficient table requires at least two points");
  }
  TabulatedReflectionTable points;
  points.reserve(count);
  for (std::size_t index = 0U; index < count; ++index) {
    const Record pointRecord = reader.require("reflection coefficient point");
    requireTokenCount(pointRecord, 3U, source, "reflection coefficient point");
    const double angle = parseDouble(
        pointRecord, 0U, source, "reflection coefficient angle");
    const double magnitude = parseDouble(
        pointRecord, 1U, source, "reflection coefficient magnitude");
    const double phaseDegrees = parseDouble(
        pointRecord, 2U, source, "reflection coefficient phase");
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
        .phaseRadians =
            (std::numbers::pi / 180.0) * phaseDegrees});
  }
  reader.requireEnd();
  return std::make_shared<const TabulatedReflectionTable>(std::move(points));
}

[[nodiscard]] SourceBeamPattern readSourceBeamPattern(
    const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input.is_open()) {
    throw BellhopError("unable to open source beam pattern file: " +
                       path.string());
  }
  RecordReader reader(input, path.string());
  const std::string& source = reader.sourceName();
  const Record countRecord = reader.require("source beam pattern count");
  requireTokenCount(countRecord, 1U, source, "source beam pattern count");
  const std::size_t count = parseCount(
      countRecord, 0U, source, "source beam pattern count", false);
  if (count < 2U) {
    fail(source, countRecord.lineNumber,
         "source beam pattern requires at least two points");
  }
  std::vector<SourceBeamPatternSample> samples;
  samples.reserve(count);
  for (std::size_t index = 0U; index < count; ++index) {
    const Record pointRecord = reader.require("source beam pattern point");
    requireTokenCount(pointRecord, 2U, source, "source beam pattern point");
    samples.push_back(SourceBeamPatternSample{
        .angleDegrees = parseDouble(
            pointRecord, 0U, source, "source beam pattern angle"),
        .powerDecibels = parseDouble(
            pointRecord, 1U, source, "source beam pattern power")});
  }
  reader.requireEnd();
  return SourceBeamPattern::directional(std::move(samples));
}

[[nodiscard]] SharedQuadrilateralSspGrid readQuadrilateralSspGrid(
    const std::filesystem::path& path, std::size_t depthCount) {
  std::ifstream input(path);
  if (!input.is_open()) {
    throw BellhopError("unable to open quadrilateral SSP file: " +
                       path.string());
  }
  RecordReader reader(input, path.string());
  const std::string& source = reader.sourceName();
  const Record countRecord = reader.require("quadrilateral SSP range count");
  requireTokenCount(countRecord, 1U, source, "quadrilateral SSP range count");
  const std::size_t rangeCount = parseCount(
      countRecord, 0U, source, "quadrilateral SSP range count", false);
  if (rangeCount < 2U) {
    fail(source, countRecord.lineNumber,
         "quadrilateral SSP requires at least two range profiles");
  }
  if (rangeCount > std::numeric_limits<std::size_t>::max() / depthCount) {
    fail(source, countRecord.lineNumber,
         "quadrilateral SSP grid dimensions overflow");
  }
  if (rangeCount * depthCount > kMaximumVectorValues) {
    fail(source, countRecord.lineNumber,
         "quadrilateral SSP grid exceeds the supported sample limit");
  }
  const Record rangesRecord = reader.require("quadrilateral SSP ranges");
  requireTokenCount(rangesRecord, rangeCount, source, "quadrilateral SSP ranges");
  std::vector<double> ranges;
  ranges.reserve(rangeCount);
  for (std::size_t index = 0U; index < rangeCount; ++index) {
    const double rangeKilometers = parseDouble(
        rangesRecord, index, source, "quadrilateral SSP range");
    const double rangeMeters = rangeKilometers * kKilometersToMeters;
    if (!std::isfinite(rangeMeters)) {
      fail(source, rangesRecord.lineNumber,
           "quadrilateral SSP range conversion produced a non-finite value");
    }
    if (!ranges.empty() && ranges.back() >= rangeMeters) {
      fail(source, rangesRecord.lineNumber,
           "quadrilateral SSP ranges must be strictly increasing");
    }
    ranges.push_back(rangeMeters);
  }
  std::vector<double> speeds;
  speeds.reserve(depthCount * rangeCount);
  for (std::size_t depthIndex = 0U; depthIndex < depthCount; ++depthIndex) {
    const Record rowRecord = reader.require("quadrilateral SSP speed row");
    requireTokenCount(rowRecord, rangeCount, source,
                      "quadrilateral SSP speed row");
    for (std::size_t rangeIndex = 0U; rangeIndex < rangeCount; ++rangeIndex) {
      const double speed = parseDouble(
          rowRecord, rangeIndex, source, "quadrilateral SSP sound speed");
      if (speed <= 0.0) {
        fail(source, rowRecord.lineNumber,
             "quadrilateral SSP sound speeds must be positive");
      }
      speeds.push_back(speed);
    }
  }
  reader.requireEnd();
  return std::make_shared<const QuadrilateralSspGrid>(
      QuadrilateralSspGrid{.rangesMeters = std::move(ranges),
                           .speedsDepthMajor = std::move(speeds),
                           .depthCount = depthCount,
                           .rangeCount = rangeCount});
}

[[nodiscard]] ParsedBoundaryFile readBoundaryFile(
    const std::filesystem::path& path, double referenceDepth,
    BoundaryOrientation orientation, AttenuationUnit attenuationUnit) {
  std::ifstream input(path);
  if (!input.is_open()) {
    throw BellhopError(
        "unable to open boundary file: " + path.string());
  }

  RecordReader reader(input, path.string());
  const std::string& source = reader.sourceName();
  const Record formatRecord = reader.require("boundary format");
  requireTokenCount(formatRecord, 1U, source, "boundary format");
  const std::string& format = formatRecord.tokens.front();
  if (format == "CS" || format == "CL") {
    fail(source, formatRecord.lineNumber,
         "curvilinear short boundary format must use canonical 'C', not '" +
             format + "'");
  }
  if (format != "LS" && format != "LL" && format != "C") {
    fail(source, formatRecord.lineNumber,
         "only piecewise-linear 'LS'/'LL' and canonical curvilinear "
         "short format 'C' are supported");
  }
  const bool longFormat = format == "LL";

  const Record countRecord = reader.require("boundary point count");
  requireTokenCount(
      countRecord, 1U, source, "boundary point count");
  const std::size_t pointCount = parseCount(
      countRecord, 0U, source, "boundary point count");
  if (pointCount < 2U) {
    fail(source, countRecord.lineNumber,
         "short-format boundary requires at least two points");
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
    const double depth =
        parseDouble(pointRecord, 1U, source, "boundary depth");
    if (!std::isfinite(range)) {
      fail(source, pointRecord.lineNumber,
           "boundary range conversion produced a non-finite value");
    }
    if (!nodes.empty() && nodes.back().range >= range) {
      fail(source, pointRecord.lineNumber,
           "boundary ranges must be strictly increasing");
    }
    if (orientation == BoundaryOrientation::Upper &&
        depth < referenceDepth) {
      fail(source, pointRecord.lineNumber,
           "top boundary depth must not be above the first SSP depth");
    }
    if (orientation == BoundaryOrientation::Lower &&
        depth > referenceDepth) {
      fail(source, pointRecord.lineNumber,
           "bottom boundary depth must not be below the last SSP depth");
    }
    nodes.push_back(Vec2{.range = range, .depth = depth});
    if (longFormat) {
      const double compressionalSoundSpeed = parseDouble(
          pointRecord, 2U, source,
          "boundary compressional sound speed");
      const double shearSoundSpeed = parseDouble(
          pointRecord, 3U, source, "boundary shear sound speed");
      const double densityInput = parseDouble(
          pointRecord, 4U, source, "boundary material density");
      const double compressionalAttenuation = parseDouble(
          pointRecord, 5U, source,
          "boundary compressional attenuation");
      const double shearAttenuation = parseDouble(
          pointRecord, 6U, source, "boundary shear attenuation");
      if (compressionalSoundSpeed <= 0.0 || densityInput <= 0.0 ||
          compressionalAttenuation < 0.0) {
        fail(source, pointRecord.lineNumber,
             "long-format boundary requires positive compressional speed/"
             "density and non-negative attenuation");
      }
      if (shearSoundSpeed != 0.0 || shearAttenuation != 0.0) {
        fail(source, pointRecord.lineNumber,
             "elastic long-format boundary materials are not supported");
      }
      nodeMaterials.push_back(AcousticMaterial{
          .compressionalSoundSpeed = compressionalSoundSpeed,
          .shearSoundSpeed = 0.0,
          .density = densityInput * kDensityInputToSi,
          .compressionalAttenuation = makeAttenuation(
              compressionalAttenuation, attenuationUnit),
          .shearAttenuation = makeAttenuation(0.0, attenuationUnit)});
    }
  }
  reader.requireEnd();
  BoundaryGeometry geometry =
      format == "C"
          ? BoundaryGeometry::curvilinear(
                std::move(nodes), referenceDepth, orientation)
          : BoundaryGeometry::piecewiseLinear(
                std::move(nodes), referenceDepth, orientation);
  SharedLongBoundaryMaterials longMaterials;
  if (longFormat) {
    longMaterials =
        std::make_shared<const std::vector<AcousticMaterial>>(
            std::move(nodeMaterials));
  }
  return ParsedBoundaryFile{
      .geometry = std::move(geometry),
      .longMaterials = std::move(longMaterials)};
}

[[nodiscard]] ParsedEnvironment parseEnvironment(
    std::istream& input, std::string sourceName,
    const std::optional<std::filesystem::path>& environmentPath) {

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
    fail(source, frequencyRecord.lineNumber,
         "frequency must be positive");
  }

  const Record mediaRecord = reader.require("medium count");
  requireTokenCount(mediaRecord, 1U, source, "medium count");
  if (parseCount(
          mediaRecord, 0U, source, "medium count") != 1U) {
    fail(source, mediaRecord.lineNumber,
         "only one water medium is supported");
  }

  const Record topOptionsRecord =
      reader.require("top/SSP options");
  requireTokenCount(
      topOptionsRecord, 1U, source, "top/SSP options");
  std::string topOptions = topOptionsRecord.tokens.front();
  if (topOptions.size() < 3U || topOptions.size() > 6U) {
    fail(source, topOptionsRecord.lineNumber,
         "top/SSP options must contain between three and six characters");
  }
  topOptions.resize(6U, ' ');
  SspInterpolationKind interpolationKind{};
  switch (topOptions.front()) {
    case 'C':
      interpolationKind = SspInterpolationKind::CLinear;
      break;
    case 'P':
      interpolationKind = SspInterpolationKind::Pchip;
      break;
    case 'N':
      interpolationKind = SspInterpolationKind::N2Linear;
      break;
    case 'S':
      interpolationKind = SspInterpolationKind::CubicSpline;
      break;
    case 'Q':
      interpolationKind = SspInterpolationKind::Quadrilateral;
      break;
    default:
      fail(source, topOptionsRecord.lineNumber,
           "unknown SSP interpolation option '" +
               std::string(1U, topOptions.front()) + "'");
  }
  if (topOptions[1U] != 'V' ||
      (topOptions[3U] != ' ' && topOptions[3U] != 'T' &&
       topOptions[3U] != 'F' && topOptions[3U] != 'B') ||
      (topOptions[4U] != ' ' && topOptions[4U] != '~' &&
       topOptions[4U] != '*') ||
      topOptions[5U] != ' ') {
    fail(source, topOptionsRecord.lineNumber,
         "only a vacuum surface, N/F/M/W/Q/L attenuation units with "
         "optional T/F/B volume attenuation, and optional boundary "
         "topography are "
         "supported");
  }
  const AttenuationUnit attenuationUnit = parseAttenuationUnit(
      topOptions[2U], topOptionsRecord, source);
  VolumeAttenuation volumeAttenuation;
  switch (topOptions[3U]) {
    case ' ':
      break;
    case 'T':
      volumeAttenuation.model = VolumeAttenuationModel::Thorp;
      break;
    case 'F': {
      volumeAttenuation.model =
          VolumeAttenuationModel::FrancoisGarrison;
      const Record parametersRecord =
          reader.require("Francois-Garrison parameters");
      requireTokenCount(parametersRecord, 4U, source,
                        "Francois-Garrison parameters");
      const FrancoisGarrisonParameters parameters{
          .temperatureCelsius = parseDouble(
              parametersRecord, 0U, source, "water temperature"),
          .salinityPsu = parseDouble(
              parametersRecord, 1U, source, "salinity"),
          .pH = parseDouble(parametersRecord, 2U, source, "pH"),
          .meanDepthMeters = parseDouble(
              parametersRecord, 3U, source, "mean depth"),
      };
      if (parameters.temperatureCelsius <= -273.0 ||
          parameters.salinityPsu < 0.0 ||
          parameters.meanDepthMeters < 0.0) {
        fail(source, parametersRecord.lineNumber,
             "Francois-Garrison temperature must exceed -273 C and "
             "salinity/mean depth must be non-negative");
      }
      volumeAttenuation.parameters = parameters;
      break;
    }
    case 'B': {
      volumeAttenuation.model = VolumeAttenuationModel::Biological;
      const Record countRecord =
          reader.require("biological layer count");
      requireTokenCount(countRecord, 1U, source,
                        "biological layer count");
      const std::size_t layerCount = parseCount(
          countRecord, 0U, source, "biological layer count", true, 200U);
      BiologicalAttenuationLayers layers;
      layers.reserve(layerCount);
      for (std::size_t index = 0U; index < layerCount; ++index) {
        const Record layerRecord =
            reader.require("biological attenuation layer");
        requireTokenCount(layerRecord, 5U, source,
                          "biological attenuation layer");
        BiologicalAttenuationLayer layer{
            .minimumDepth = parseDouble(
                layerRecord, 0U, source, "biological layer top depth"),
            .maximumDepth = parseDouble(
                layerRecord, 1U, source, "biological layer bottom depth"),
            .resonanceFrequency = parseDouble(
                layerRecord, 2U, source,
                "biological resonance frequency"),
            .qualityFactor = parseDouble(
                layerRecord, 3U, source, "biological quality factor"),
            .attenuationCoefficientDecibelsPerKilometer = parseDouble(
                layerRecord, 4U, source,
                "biological attenuation coefficient"),
        };
        if (layer.minimumDepth > layer.maximumDepth ||
            layer.resonanceFrequency <= 0.0 ||
            layer.qualityFactor <= 0.0 ||
            layer.attenuationCoefficientDecibelsPerKilometer < 0.0) {
          fail(source, layerRecord.lineNumber,
               "biological layer requires top <= bottom, positive f0/Q, "
               "and non-negative a0");
        }
        layers.push_back(layer);
      }
      volumeAttenuation.parameters =
          std::make_shared<const BiologicalAttenuationLayers>(
              std::move(layers));
      break;
    }
    default:
      fail(source, topOptionsRecord.lineNumber,
           "unknown volume attenuation option");
  }
  const bool hasTopography =
      topOptions[4U] == '~' || topOptions[4U] == '*';

  const Record waterRecord = reader.require("water-column header");
  requireTokenCount(
      waterRecord, 3U, source, "water-column header");
  static_cast<void>(parseCount(
      waterRecord, 0U, source, "water mesh count"));
  requireExactlyZero(
      parseDouble(
          waterRecord, 1U, source, "surface RMS roughness"),
      waterRecord, source, "surface RMS roughness");
  const double bottomDepth =
      parseDouble(
          waterRecord, 2U, source, "bottom depth");

  std::vector<SoundSpeedPoint> soundSpeedPoints;
  soundSpeedPoints.reserve(64U);
  while (true) {
    if (soundSpeedPoints.size() >= kMaximumSspPoints) {
      fail(source, waterRecord.lineNumber,
           "sound-speed profile exceeds the supported point limit");
    }
    const Record pointRecord =
        reader.require("sound-speed profile point");
    if (pointRecord.tokens.size() != 2U &&
        pointRecord.tokens.size() != 5U &&
        pointRecord.tokens.size() != 6U) {
      fail(source, pointRecord.lineNumber,
           "a sound-speed profile point requires 2, 5, or 6 values");
    }
    const double depth =
        parseDouble(pointRecord, 0U, source, "SSP depth");
    const double soundSpeed =
        parseDouble(pointRecord, 1U, source, "SSP sound speed");
    const double shearSoundSpeed =
        pointRecord.tokens.size() >= 5U
            ? parseDouble(
                  pointRecord, 2U, source, "SSP shear sound speed")
            : 0.0;
    const double densityInput =
        pointRecord.tokens.size() >= 5U
            ? parseDouble(
                  pointRecord, 3U, source, "SSP density")
            : 1.0;
    const double attenuationValue =
        pointRecord.tokens.size() >= 5U
            ? parseDouble(
                  pointRecord, 4U, source, "SSP attenuation")
            : 0.0;
    const double shearAttenuation =
        pointRecord.tokens.size() == 6U
            ? parseDouble(
                  pointRecord, 5U, source, "SSP shear attenuation")
            : 0.0;
    if (shearSoundSpeed != 0.0 || shearAttenuation != 0.0) {
      fail(source, pointRecord.lineNumber,
           "the water-column SSP cannot contain shear properties");
    }
    if (densityInput <= 0.0) {
      fail(source, pointRecord.lineNumber,
           "SSP density must be positive");
    }
    soundSpeedPoints.push_back(
        SoundSpeedPoint{
            .depth = depth,
            .soundSpeed = soundSpeed,
            .density = densityInput * kDensityInputToSi,
            .attenuation =
                makeAttenuation(
                    attenuationValue, attenuationUnit)});
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
  SharedQuadrilateralSspGrid quadrilateralGrid;
  if (interpolationKind == SspInterpolationKind::Quadrilateral) {
    if (!environmentPath.has_value()) {
      fail(source, topOptionsRecord.lineNumber,
           "quadrilateral SSP requires parseFile so the sibling .ssp file "
           "can be resolved");
    }
    quadrilateralGrid = readQuadrilateralSspGrid(
        boundaryPath(*environmentPath, ".ssp"), soundSpeedPoints.size());
  }

  const double surfaceDepth = soundSpeedPoints.front().depth;
  BoundaryGeometry seaSurfaceGeometry = BoundaryGeometry::flat(
      surfaceDepth, BoundaryOrientation::Upper);
  if (hasTopography) {
    if (!environmentPath.has_value()) {
      fail(source, topOptionsRecord.lineNumber,
           "topography requires parseFile so the sibling .ati file can be "
           "resolved");
    }
    seaSurfaceGeometry = readBoundaryFile(
        boundaryPath(*environmentPath, ".ati"), surfaceDepth,
        BoundaryOrientation::Upper, attenuationUnit)
                             .geometry;
  }
  const BoundaryModel seaSurface =
      BoundaryModel::vacuum(std::move(seaSurfaceGeometry));

  const Record bottomOptionsRecord =
      reader.require("bottom options");
  requireTokenCount(
      bottomOptionsRecord, 2U, source, "bottom options");
  std::string bottomOption = bottomOptionsRecord.tokens.front();
  if (bottomOption.empty() || bottomOption.size() > 2U) {
    fail(source, bottomOptionsRecord.lineNumber,
         "bottom options must contain one or two characters");
  }
  bottomOption.resize(2U, ' ');
  if (bottomOption[1U] != ' ' && bottomOption[1U] != '~' &&
      bottomOption[1U] != '*') {
    fail(source, bottomOptionsRecord.lineNumber,
         "only optional bottom topography '~' or '*' is "
         "supported");
  }
  const bool hasBathymetry =
      bottomOption[1U] == '~' || bottomOption[1U] == '*';
  requireExactlyZero(
      parseDouble(
          bottomOptionsRecord, 1U, source, "bottom RMS roughness"),
      bottomOptionsRecord, source, "bottom RMS roughness");

  BoundaryGeometry seabedGeometry = BoundaryGeometry::flat(
      bottomDepth, BoundaryOrientation::Lower);
  SharedLongBoundaryMaterials seabedLongMaterials;
  if (hasBathymetry) {
    if (!environmentPath.has_value()) {
      fail(source, bottomOptionsRecord.lineNumber,
           "bathymetry requires parseFile so the sibling .bty file can be "
           "resolved");
    }
    ParsedBoundaryFile boundary = readBoundaryFile(
        boundaryPath(*environmentPath, ".bty"), bottomDepth,
        BoundaryOrientation::Lower, attenuationUnit);
    seabedGeometry = std::move(boundary.geometry);
    seabedLongMaterials = std::move(boundary.longMaterials);
  }

  BoundaryModel seabed = BoundaryModel::rigid(seabedGeometry);
  if (bottomOption[0U] == 'A') {
    const Record materialRecord =
        reader.require("bottom acoustic half-space");
    if (materialRecord.tokens.size() != 5U &&
        materialRecord.tokens.size() != 6U) {
      fail(source, materialRecord.lineNumber,
           "an acoustic half-space requires 5 or 6 values");
    }
    const double materialDepth =
        parseDouble(
            materialRecord, 0U, source, "half-space depth");
    if (!depthMatches(materialDepth, bottomDepth)) {
      fail(source, materialRecord.lineNumber,
           "half-space depth must match the declared bottom depth");
    }
    const double compressionalSoundSpeed =
        parseDouble(
            materialRecord, 1U, source,
            "half-space compressional sound speed");
    const double shearSoundSpeed =
        parseDouble(
            materialRecord, 2U, source,
            "half-space shear sound speed");
    const double densityInput =
        parseDouble(
            materialRecord, 3U, source, "half-space density");
    const double compressionalAttenuation =
        parseDouble(
            materialRecord, 4U, source,
            "half-space compressional attenuation");
    const double shearAttenuation =
        materialRecord.tokens.size() == 6U
            ? parseDouble(
                  materialRecord, 5U, source,
                  "half-space shear attenuation")
            : 0.0;
    if (compressionalSoundSpeed <= 0.0 || shearSoundSpeed < 0.0 ||
        densityInput <= 0.0 || compressionalAttenuation < 0.0 ||
        shearAttenuation < 0.0 ||
        (shearSoundSpeed == 0.0 && shearAttenuation != 0.0)) {
      fail(source, materialRecord.lineNumber,
           "half-space requires positive compressional speed/density, "
           "non-negative shear speed/attenuation, and zero shear loss "
           "when shear speed is zero");
    }
    seabed = BoundaryModel::acousticHalfSpace(
        std::move(seabedGeometry),
        AcousticMaterial{
            .compressionalSoundSpeed =
                compressionalSoundSpeed,
            .shearSoundSpeed = shearSoundSpeed,
            .density = densityInput * kDensityInputToSi,
            .compressionalAttenuation = makeAttenuation(
                compressionalAttenuation, attenuationUnit),
            .shearAttenuation = makeAttenuation(
                shearAttenuation, attenuationUnit)},
        std::move(seabedLongMaterials));
  } else if (bottomOption[0U] == 'G') {
    if (seabedLongMaterials) {
      fail(source, bottomOptionsRecord.lineNumber,
           "grain-size bottoms do not support long-format bathymetry");
    }
    const Record grainSizeRecord = reader.require("bottom grain size");
    requireTokenCount(grainSizeRecord, 2U, source, "bottom grain size");
    const double materialDepth = parseDouble(
        grainSizeRecord, 0U, source, "grain-size half-space depth");
    if (!depthMatches(materialDepth, bottomDepth)) {
      fail(source, grainSizeRecord.lineNumber,
           "grain-size half-space depth must match the declared bottom depth");
    }
    const double meanGrainSize = parseDouble(
        grainSizeRecord, 1U, source, "mean grain size");
    seabed = BoundaryModel::grainSizeHalfSpace(
        std::move(seabedGeometry), meanGrainSize);
  } else if (bottomOption[0U] == 'F') {
    if (seabedLongMaterials) {
      fail(source, bottomOptionsRecord.lineNumber,
           "tabulated-reflection bottoms do not support long-format "
           "bathymetry");
    }
    if (!environmentPath.has_value()) {
      fail(source, bottomOptionsRecord.lineNumber,
           "tabulated reflection requires parseFile so the sibling .brc "
           "file can be resolved");
    }
    seabed = BoundaryModel::tabulatedReflection(
        std::move(seabedGeometry),
        readReflectionTable(boundaryPath(*environmentPath, ".brc")));
  } else if (bottomOption[0U] != 'R') {
    fail(source, bottomOptionsRecord.lineNumber,
         "only rigid ('R'), acoustic ('A'), grain-size ('G'), and "
         "tabulated-reflection ('F') bottoms are supported");
  }

  const Record sourceCountRecord =
      reader.require("source-depth count");
  requireTokenCount(
      sourceCountRecord, 1U, source, "source-depth count");
  const std::size_t sourceCount = parseCount(
      sourceCountRecord, 0U, source, "source-depth count");
  const std::vector<double> sourceDepths = parseVector(
      reader, sourceCount, "source depths", true, 1.0);

  const Record receiverDepthCountRecord =
      reader.require("receiver-depth count");
  requireTokenCount(
      receiverDepthCountRecord, 1U, source,
      "receiver-depth count");
  const std::size_t receiverDepthCount = parseCount(
      receiverDepthCountRecord, 0U, source,
      "receiver-depth count");
  std::vector<double> receiverDepths = parseVector(
      reader, receiverDepthCount, "receiver depths", true, 1.0);

  const Record receiverRangeCountRecord =
      reader.require("receiver-range count");
  requireTokenCount(
      receiverRangeCountRecord, 1U, source,
      "receiver-range count");
  const std::size_t receiverRangeCount = parseCount(
      receiverRangeCountRecord, 0U, source,
      "receiver-range count");
  std::vector<double> receiverRanges = parseVector(
      reader, receiverRangeCount, "receiver ranges", false,
      kKilometersToMeters);

  const Record runTypeRecord = reader.require("run type");
  const ParsedRunType runType = canonicalRunType(runTypeRecord, source);
  if (isTransmissionLossMode(runType.runMode) &&
      (runType.beamFamily == BeamFamily::CervenyGaussian ||
       runType.cervenyCoordinateSystem ==
           CervenyCoordinateSystem::RayCentered)) {
    requireUniformRanges(
        receiverRanges, receiverRangeCountRecord, source);
  }
  if (runType.runMode == SimulationRunMode::RayTrace &&
      runType.usesSourceBeamPattern) {
    fail(source, runTypeRecord.lineNumber,
         "directional source beam patterns in ray-trace mode are not yet "
         "supported because they can change the written terminal prefix");
  }
  if (runType.runMode == SimulationRunMode::RayTrace &&
      (seaSurface.kind() != BoundaryKind::Vacuum ||
       seabed.kind() != BoundaryKind::Rigid)) {
    fail(source, runTypeRecord.lineNumber,
         "the initial ray-trace slice requires vacuum surface and rigid "
         "seabed boundaries");
  }
  const ReceiverGridLayout receiverLayout = runType.receiverLayout;
  if ((runType.beamFamily == BeamFamily::CervenyGaussian ||
       runType.beamFamily == BeamFamily::GeometricHat) &&
      runType.cervenyCoordinateSystem ==
          CervenyCoordinateSystem::RayCentered &&
      receiverLayout == ReceiverGridLayout::Irregular) {
    fail(source, runTypeRecord.lineNumber,
         "ray-centered beam families do not support irregular receiver "
         "grids");
  }
  if (receiverLayout == ReceiverGridLayout::Irregular &&
      receiverDepths.size() != receiverRanges.size()) {
    fail(source, runTypeRecord.lineNumber,
         "irregular receiver grid requires equal depth and range counts");
  }
  if (runType.beamFamily == BeamFamily::SimpleGaussian &&
      (runType.runMode !=
           SimulationRunMode::CoherentTransmissionLoss ||
       runType.sourceGeometry != SourceGeometry::Point ||
       receiverLayout != ReceiverGridLayout::Rectilinear)) {
    fail(source, runTypeRecord.lineNumber,
         "simple Gaussian beams require coherent point-source TL on a "
         "rectilinear receiver grid");
  }
  SourceBeamPattern sourceBeamPattern =
      SourceBeamPattern::omnidirectional();
  if (runType.usesSourceBeamPattern) {
    if (!environmentPath.has_value()) {
      fail(source, runTypeRecord.lineNumber,
           "source beam pattern run type requires file parsing with a "
           "sibling .sbp file");
    }
    sourceBeamPattern =
        readSourceBeamPattern(boundaryPath(*environmentPath, ".sbp"));
  }

  const Record launchCountRecord =
      reader.require("launch-angle count");
  requireTokenCount(
      launchCountRecord, 1U, source, "launch-angle count");
  const std::size_t requestedLaunchCount = parseCount(
      launchCountRecord, 0U, source, "launch-angle count", true);

  const Record launchAngleRecord =
      reader.require("launch-angle endpoints");
  requireTokenCount(
      launchAngleRecord, 2U, source, "launch-angle endpoints");
  const double minimumLaunchAngleDegrees =
      parseDouble(
          launchAngleRecord, 0U, source,
          "minimum launch angle");
  const double maximumLaunchAngleDegrees =
      parseDouble(
          launchAngleRecord, 1U, source,
          "maximum launch angle");
  const double degreesToRadians =
      std::numbers::pi / 180.0;

  const Record integratorRecord =
      reader.require("integrator settings");
  requireTokenCount(
      integratorRecord, 3U, source, "integrator settings");
  double stepLength =
      parseDouble(
          integratorRecord, 0U, source, "step length");
  const double depthLimit =
      parseDouble(
          integratorRecord, 1U, source, "depth box limit");
  const double rangeLimit =
      parseDouble(
          integratorRecord, 2U, source, "range box limit") *
      kKilometersToMeters;
  if (stepLength < 0.0) {
    fail(source, integratorRecord.lineNumber,
         "step length must be non-negative");
  }
  if (stepLength == 0.0) {
    stepLength = (bottomDepth - surfaceDepth) / 10.0;
  }

  double epsilonMultiplier = 1.0;
  double loopRange = 1.0;
  BeamWidthMode beamWidthMode =
      runType.beamFamily == BeamFamily::CervenyGaussian
          ? BeamWidthMode::MinimumWidth
          : BeamWidthMode::SpaceFilling;
  BoundaryCurvatureMode curvatureMode = BoundaryCurvatureMode::Standard;
  std::size_t imageCount = 1U;
  int beamWindow = 1;
  FieldComponent fieldComponent = FieldComponent::Pressure;
  if (runType.beamFamily == BeamFamily::CervenyGaussian &&
      isTransmissionLossMode(runType.runMode)) {
    const Record beamRecord = reader.require("Cerveny beam settings");
    requireTokenCount(beamRecord, 3U, source, "Cerveny beam settings");
    const std::string& beamType = beamRecord.tokens.front();
    if (beamType.size() != 2U) {
      fail(source, beamRecord.lineNumber,
           "Cerveny beam type must contain a width and curvature letter");
    }
    switch (beamType[0U]) {
      case 'F':
        beamWidthMode = BeamWidthMode::SpaceFilling;
        break;
      case 'M':
        beamWidthMode = BeamWidthMode::MinimumWidth;
        break;
      case 'W':
        beamWidthMode = BeamWidthMode::Wkb;
        break;
      default:
        fail(source, beamRecord.lineNumber,
             "Cerveny beam width must be one of 'F', 'M', or 'W'");
    }
    switch (beamType[1U]) {
      case 'D':
        curvatureMode = BoundaryCurvatureMode::Double;
        break;
      case 'S':
        curvatureMode = BoundaryCurvatureMode::Standard;
        break;
      case 'Z':
        curvatureMode = BoundaryCurvatureMode::Zero;
        break;
      default:
        fail(source, beamRecord.lineNumber,
             "Cerveny curvature condition must be one of 'D', 'S', or 'Z'");
    }
    epsilonMultiplier = parseDouble(
        beamRecord, 1U, source, "epsilon multiplier");
    loopRange = parseDouble(
                    beamRecord, 2U, source, "beam loop range") *
                kKilometersToMeters;
    if (epsilonMultiplier <= 0.0 || loopRange <= 0.0) {
      fail(source, beamRecord.lineNumber,
           "epsilon multiplier and beam loop range must be positive");
    }

    const Record imageRecord = reader.require("image/window settings");
    requireTokenCount(imageRecord, 3U, source, "image/window settings");
    imageCount = parseCount(
        imageRecord, 0U, source, "image count", false, 3U);
    beamWindow = parsePositiveInt(
        imageRecord, 1U, source, "beam window");
    if (imageRecord.tokens[2U] == "P") {
      fieldComponent = FieldComponent::Pressure;
    } else if (imageRecord.tokens[2U] == "V") {
      fieldComponent = FieldComponent::Vertical;
    } else if (imageRecord.tokens[2U] == "H") {
      fieldComponent = FieldComponent::Horizontal;
    } else {
      fail(source, imageRecord.lineNumber,
           "field component must be one of 'P', 'V', or 'H'");
    }
  }
  reader.requireEnd();

  Environment environment(
      SoundSpeedProfile(std::move(soundSpeedPoints), interpolationKind,
                        std::move(quadrilateralGrid)),
      seaSurface, seabed, std::move(volumeAttenuation));
  ReceiverGrid receivers(
      std::move(receiverDepths),
      std::move(receiverRanges), receiverLayout);
  std::vector<Source> sources;
  sources.reserve(sourceDepths.size());
  for (const double sourceDepth : sourceDepths) {
    sources.push_back(Source{.depth = sourceDepth, .amplitude = 1.0});
  }
  SimulationCase simulationCase(
      std::move(environment),
      std::move(sources),
      std::move(receivers),
      FrequencyGrid({frequency}),
      LaunchFan{
          .minimumAngle =
              minimumLaunchAngleDegrees * degreesToRadians,
          .maximumAngle =
              maximumLaunchAngleDegrees * degreesToRadians,
          .explicitLaunchAngleCount =
              requestedLaunchCount == 0U
                  ? std::nullopt
                  : std::optional<std::size_t>(
                        requestedLaunchCount),
          .inputDegreeBounds =
              LaunchAngleDegreeBounds{
                  .minimum = minimumLaunchAngleDegrees,
                  .maximum = maximumLaunchAngleDegrees}},
      IntegratorSettings{
          .stepLength = stepLength,
          .rangeLimit = rangeLimit,
          .depthLimit = depthLimit,
          .maximumRayPoints = kMaximumRayPoints},
      std::move(sourceBeamPattern), runType.runMode, fieldComponent,
      runType.sourceGeometry, runType.cervenyCoordinateSystem,
      runType.beamFamily);

  return ParsedEnvironment{
      .title = std::move(title),
      .simulationCase = std::move(simulationCase),
      .beam =
          CartesianCervenyInput{
              .widthMode = beamWidthMode,
              .curvatureMode = curvatureMode,
              .epsilonMultiplier = epsilonMultiplier,
              .loopRange = loopRange,
              .influence =
                  CartesianCervenySettings{
                      .imageCount = imageCount,
                      .beamWindow = beamWindow},
              .family = runType.beamFamily}};
}

}  // namespace

ParsedEnvironment EnvironmentParser::parse(
    std::istream& input, std::string sourceName) {
  return parseEnvironment(
      input, std::move(sourceName), std::nullopt);
}

ParsedEnvironment EnvironmentParser::parseFile(
    const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input.is_open()) {
    throw BellhopError(
        "unable to open environment file: " + path.string());
  }
  return parseEnvironment(input, path.string(), path);
}

}  // namespace bellhop
