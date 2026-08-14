#include "bellhop/io/ray_writer.hpp"

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <numbers>
#include <system_error>
#include <utility>
#include <vector>

#include "bellhop/error.hpp"

namespace bellhop {
namespace {

constexpr std::size_t kMaximumWrittenRayPoints = 500'000U;

std::int32_t checkedOriginInt32(std::size_t value, const char* label) {
  if (value > static_cast<std::size_t>(
                  std::numeric_limits<std::int32_t>::max())) {
    throw ValidationError(std::string(label) + " exceeds the RAY int32 limit");
  }
  return static_cast<std::int32_t>(value);
}

std::size_t bounceCount(const RayPath& path, ReflectionBoundary boundary) {
  return static_cast<std::size_t>(std::count_if(
      path.events.begin(), path.events.end(),
      [boundary](const ReflectionEvent& event) {
        return event.boundary == boundary;
      }));
}

std::vector<std::size_t> writtenPointIndices(
    const RayPath& path, double topDepth, double bottomDepth) {
  if (path.points.empty()) {
    throw ValidationError("ray writer cannot write an empty ray path");
  }
  const std::size_t skip =
      std::max(path.points.size() / kMaximumWrittenRayPoints,
               std::size_t{1U});
  std::vector<std::size_t> indices{0U};
  indices.reserve(std::min(path.points.size(), kMaximumWrittenRayPoints + 2U));
  for (std::size_t index = 1U; index < path.points.size(); ++index) {
    const double depth = path.points[index].position.depth;
    const bool nearBoundary =
        std::min(bottomDepth - depth, depth - topDepth) < 0.2;
    const bool stridePoint = ((index + 1U) % skip) == 0U;
    const bool terminal = index + 1U == path.points.size();
    if (nearBoundary || stridePoint || terminal) {
      indices.push_back(index);
    }
  }
  return indices;
}

}  // namespace

RayWriter::RayWriter(std::filesystem::path outputPath, std::string title,
                     const SimulationCase& simulation)
    : outputPath_(std::move(outputPath)),
      temporaryPath_(outputPath_.string() + ".tmp"),
      simulation_(simulation) {
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
  std::size_t launchIndex = 0U;
  for (const RayPath& path : cache.paths()) {
    if (path.launchAngle !=
        simulation_.launchFanPlan().launchAngles[launchIndex]) {
      throw ValidationError(
          "ray writer cache launch angles are out of canonical order");
    }
    const std::vector<std::size_t> indices =
        writtenPointIndices(path, topDepth, bottomDepth);
    const std::size_t topBounces =
        bounceCount(path, ReflectionBoundary::SeaSurface);
    const std::size_t bottomBounces =
        bounceCount(path, ReflectionBoundary::Seabed);
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
