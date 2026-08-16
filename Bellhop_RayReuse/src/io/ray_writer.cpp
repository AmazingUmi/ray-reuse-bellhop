#include "rayreuse/io/ray_writer.hpp"

#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numbers>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>

#include "rayreuse/error.hpp"
#include "rayreuse/io/ray_prefix_writer.hpp"

namespace rayreuse {
namespace {

void requireFinite(double value, const char* label) {
  if (!std::isfinite(value)) {
    throw ValidationError(std::string(label) + " must be finite");
  }
}

std::int32_t checkedOriginInt32(std::size_t value, const char* label) {
  if (value >
      static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
    throw ValidationError(std::string(label) + " exceeds the RAY int32 limit");
  }
  return static_cast<std::int32_t>(value);
}

}  // namespace

SourceBeamPattern readSourceBeamPattern(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input.is_open()) {
    throw BellhopError("unable to open source beam pattern file: " +
                       path.string());
  }
  std::string line;
  std::size_t count = 0U;
  bool haveCount = false;
  std::vector<SourceBeamPatternSample> samples;
  while (std::getline(input, line)) {
    const std::size_t comment = line.find('!');
    if (comment != std::string::npos) {
      line.resize(comment);
    }
    std::istringstream record(line);
    std::vector<double> values;
    double value = 0.0;
    while (record >> value) {
      values.push_back(value);
    }
    if (values.empty()) {
      continue;
    }
    if (!haveCount) {
      if (values.size() != 1U || values.front() < 2.0 ||
          std::floor(values.front()) != values.front() ||
          values.front() >
              static_cast<double>(std::numeric_limits<std::size_t>::max())) {
        throw ValidationError(
            "source beam pattern requires an integer count of at least two");
      }
      count = static_cast<std::size_t>(values.front());
      haveCount = true;
      samples.reserve(count);
      continue;
    }
    if (values.size() != 2U) {
      throw ValidationError(
          "source beam pattern point requires angle and "
          "power");
    }
    samples.push_back({.angleDegrees = values[0], .powerDecibels = values[1]});
  }
  if (!haveCount || samples.size() != count) {
    throw ValidationError("source beam pattern requires at least two samples");
  }
  return SourceBeamPattern::directional(std::move(samples));
}

RayWriter::RayWriter(std::filesystem::path outputPath, std::string title,
                     const SimulationCase& simulation, double frequency,
                     std::vector<double> launchAngles)
    : outputPath_(std::move(outputPath)),
      temporaryPath_(outputPath_.string() + ".tmp"),
      simulation_(simulation),
      frequency_(frequency),
      launchAngles_(std::move(launchAngles)),
      projector_(simulation.environment()) {
  if (simulation_.runMode() != SimulationRunMode::RayTrace) {
    throw ValidationError("ray writer requires ray-trace run mode");
  }
  requireFinite(frequency_, "R product frequency");
  if (frequency_ <= 0.0) {
    throw ValidationError("R product frequency must be positive");
  }
  if (simulation_.frequencies().size() != 1U) {
    throw ValidationError(
        "multi-frequency R products have no defined "
        "output schema");
  }
  if (frequency_ != simulation_.frequencies().values().front()) {
    throw ValidationError(
        "R product frequency does not match the "
        "single simulation frequency");
  }
  output_.open(temporaryPath_, std::ios::out | std::ios::trunc);
  if (!output_.is_open()) {
    throw BellhopError("unable to open temporary ray output: " +
                       temporaryPath_.string());
  }
  if (launchAngles_.empty()) {
    launchAngles_ = simulation_.launchFanPlan().launchAngles;
  }
  if (launchAngles_.empty()) {
    throw ValidationError("R writer requires at least one launch angle");
  }
  static_cast<void>(
      checkedOriginInt32(launchAngles_.size(), "RAY launch-angle count"));
  title.insert(0U, "BELLHOP- ");
  if (title.size() > 70U) {
    title.resize(70U);
  }
  output_ << std::setprecision(std::numeric_limits<double>::max_digits10)
          << '\'' << title << "'\n"
          << frequency_ << '\n'
          << "1 1 1\n"
          << launchAngles_.size() << " 1\n"
          << simulation_.environment().seaSurface().depth() << '\n'
          << simulation_.environment().seabed().depth() << '\n'
          << "'rz'\n";
  if (!output_) {
    throw BellhopError("failed while writing temporary RAY header");
  }
}

RayWriter::~RayWriter() {
  if (!finalized_) {
    output_.close();
    std::error_code ignored;
    std::filesystem::remove(temporaryPath_, ignored);
  }
}

void RayWriter::append(const RayPathCache& cache) {
  if (finalized_ || appended_) {
    throw ValidationError("ray writer accepts exactly one launch fan");
  }
  if (!cache.frozen() || cache.size() != launchAngles_.size()) {
    throw ValidationError("ray writer requires one complete frozen launch fan");
  }
  const double topDepth = simulation_.environment().seaSurface().depth();
  const double bottomDepth = simulation_.environment().seabed().depth();
  std::size_t launchIndex = 0U;
  for (const RayPath& path : cache.paths()) {
    if (path.launchAngle != launchAngles_[launchIndex]) {
      throw ValidationError(
          "ray writer cache launch angles are out of "
          "canonical order");
    }
    const double sourceAmplitude =
        simulation_.source().amplitude *
        simulation_.sourceBeamPattern().amplitudeForLaunchAngle(
            path.launchAngle);
    const RayFrequencyState frequencyState =
        projector_.project(path, frequency_, sourceAmplitude);
    const std::size_t prefixPointCount =
        ray_output_detail::terminalPrefixPointCount(path, frequencyState);
    const ray_output_detail::EncodedRayPrefix encoded =
        ray_output_detail::encodeRayPrefix(path, prefixPointCount, topDepth,
                                           bottomDepth);
    static_cast<void>(
        checkedOriginInt32(encoded.pointIndices.size(), "RAY point count"));
    static_cast<void>(
        checkedOriginInt32(encoded.topBounceCount, "RAY top bounce count"));
    static_cast<void>(checkedOriginInt32(encoded.bottomBounceCount,
                                         "RAY bottom bounce count"));
    output_ << path.launchAngle * (180.0 / std::numbers::pi) << '\n'
            << encoded.pointIndices.size() << ' ' << encoded.topBounceCount
            << ' ' << encoded.bottomBounceCount << '\n';
    for (const std::size_t index : encoded.pointIndices) {
      output_ << path.points[index].position.range << ' '
              << path.points[index].position.depth << '\n';
    }
    ++launchIndex;
  }
  if (!output_) {
    throw BellhopError("failed while writing temporary ray output");
  }
  appended_ = true;
}

void RayWriter::finalize() {
  if (finalized_ || !appended_) {
    throw ValidationError("ray writer cannot finalize an incomplete run");
  }
  output_.close();
  if (!output_) {
    throw BellhopError("failed to finalize temporary ray output");
  }
  std::error_code error;
  std::filesystem::rename(temporaryPath_, outputPath_, error);
  if (error) {
    throw BellhopError("unable to publish ray output: " + error.message());
  }
  finalized_ = true;
}

}  // namespace rayreuse
