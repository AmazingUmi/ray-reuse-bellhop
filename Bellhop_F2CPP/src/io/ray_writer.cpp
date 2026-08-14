#include "bellhop/io/ray_writer.hpp"

#include <cstdint>
#include <iomanip>
#include <limits>
#include <numbers>
#include <string>
#include <system_error>
#include <utility>

#include "bellhop/error.hpp"
#include "bellhop/io/ray_prefix_writer.hpp"

namespace bellhop {
namespace {

std::size_t originTerminalPrefixPointCount(
    const RayPath& path, const RayFrequencyState& frequencyState) {
  if (frequencyState.points.size() != path.points.size()) {
    throw ValidationError(
        "ray writer frequency projection does not match geometry path");
  }
  std::size_t eventIndex = 0U;
  for (std::size_t pointIndex = 1U;
       pointIndex < frequencyState.points.size(); ++pointIndex) {
    while (eventIndex < path.events.size() &&
           path.events[eventIndex].rayPointIndex < pointIndex) {
      ++eventIndex;
    }
    const bool incidentReflectionPoint =
        eventIndex < path.events.size() &&
        path.events[eventIndex].rayPointIndex == pointIndex;
    // Origin tests the cutoff after completing the whole integration
    // iteration.  When that iteration reflects, the incident point and its
    // same-position reflected point are indivisible and the latter is the
    // retained terminal point.
    if (!incidentReflectionPoint &&
        !frequencyState.points[pointIndex].active) {
      return pointIndex + 1U;
    }
  }
  return path.points.size();
}

std::int32_t checkedOriginInt32(std::size_t value, const char* label) {
  if (value > static_cast<std::size_t>(
                  std::numeric_limits<std::int32_t>::max())) {
    throw ValidationError(std::string(label) + " exceeds the RAY int32 limit");
  }
  return static_cast<std::int32_t>(value);
}

}  // namespace

RayWriter::RayWriter(std::filesystem::path outputPath, std::string title,
                     const SimulationCase& simulation)
    : outputPath_(std::move(outputPath)),
      temporaryPath_(outputPath_.string() + ".tmp"),
      simulation_(simulation),
      projector_(simulation.environment()) {
  if (simulation_.runMode() != SimulationRunMode::RayTrace) {
    throw ValidationError("ray writer requires ray-trace run mode");
  }
  output_.open(temporaryPath_, std::ios::out | std::ios::trunc);
  if (!output_.is_open()) {
    throw BellhopError("unable to open temporary ray output: " +
                       temporaryPath_.string());
  }
  static_cast<void>(
      checkedOriginInt32(simulation_.sourceCount(), "RAY source count"));
  static_cast<void>(checkedOriginInt32(
      simulation_.launchFanPlan().launchAngleCount,
      "RAY launch-angle count"));
  title.insert(0U, "BELLHOP- ");
  if (title.size() > 70U) {
    title.resize(70U);
  }
  output_ << std::setprecision(std::numeric_limits<double>::max_digits10)
          << '\'' << title << "'\n"
          << simulation_.frequencies().values().front() << '\n'
          << "1 1 " << simulation_.sourceCount() << '\n'
          << simulation_.launchFanPlan().launchAngleCount << " 1\n"
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

void RayWriter::appendSource(std::size_t sourceIndex,
                             const RayPathCache& cache) {
  if (finalized_ || sourceIndex != nextSourceIndex_) {
    throw ValidationError("ray writer source order is invalid");
  }
  if (!cache.frozen() ||
      cache.size() != simulation_.launchFanPlan().launchAngleCount) {
    throw ValidationError(
        "ray writer requires one complete frozen launch fan per source");
  }
  const double topDepth = simulation_.environment().seaSurface().depth();
  const double bottomDepth = simulation_.environment().seabed().depth();
  const double frequency = simulation_.frequencies().values().front();
  const Source& source = simulation_.sources()[sourceIndex];
  std::size_t launchIndex = 0U;
  for (const RayPath& path : cache.paths()) {
    if (path.launchAngle !=
        simulation_.launchFanPlan().launchAngles[launchIndex]) {
      throw ValidationError(
          "ray writer cache launch angles are out of canonical order");
    }
    const double sourceAmplitude =
        source.amplitude *
        simulation_.sourceBeamPattern().amplitudeForLaunchAngle(
            path.launchAngle);
    const RayFrequencyState frequencyState =
        projector_.project(path, frequency, sourceAmplitude);
    const std::size_t prefixPointCount =
        originTerminalPrefixPointCount(path, frequencyState);
    const ray_output_detail::EncodedRayPrefix encoded =
        ray_output_detail::encodeRayPrefix(path, prefixPointCount, topDepth,
                                           bottomDepth);
    const std::vector<std::size_t>& indices = encoded.pointIndices;
    const std::size_t topBounces = encoded.topBounceCount;
    const std::size_t bottomBounces = encoded.bottomBounceCount;
    static_cast<void>(checkedOriginInt32(indices.size(), "RAY point count"));
    static_cast<void>(checkedOriginInt32(topBounces, "RAY top bounce count"));
    static_cast<void>(
        checkedOriginInt32(bottomBounces, "RAY bottom bounce count"));
    output_ << path.launchAngle * (180.0 / std::numbers::pi) << '\n'
            << indices.size() << ' '
            << topBounces << ' ' << bottomBounces << '\n';
    for (const std::size_t index : indices) {
      output_ << path.points[index].position.range << ' '
              << path.points[index].position.depth << '\n';
    }
    ++launchIndex;
  }
  if (!output_) {
    throw BellhopError("failed while writing temporary ray output");
  }
  ++nextSourceIndex_;
}

void RayWriter::finalize() {
  if (finalized_ || nextSourceIndex_ != simulation_.sourceCount()) {
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

}  // namespace bellhop
