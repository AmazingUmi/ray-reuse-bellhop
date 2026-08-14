#include "bellhop/io/eigenray_writer.hpp"

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

std::int32_t checkedOriginInt32(std::size_t value, const char* label) {
  if (value > static_cast<std::size_t>(
                  std::numeric_limits<std::int32_t>::max())) {
    throw ValidationError(std::string(label) + " exceeds the RAY int32 limit");
  }
  return static_cast<std::int32_t>(value);
}

void writeHeader(std::ofstream& output, std::string title,
                 const SimulationCase& simulation) {
  title.insert(0U, "BELLHOP- ");
  if (title.size() > 70U) {
    title.resize(70U);
  }
  output << std::setprecision(std::numeric_limits<double>::max_digits10)
         << '\'' << title << "'\n"
         << simulation.frequencies().values().front() << '\n'
         << "1 1 " << simulation.sourceCount() << '\n'
         << simulation.launchFanPlan().launchAngleCount << " 1\n"
         << simulation.environment().seaSurface().depth() << '\n'
         << simulation.environment().seabed().depth() << '\n'
         << "'rz'\n";
}

}  // namespace

EigenrayWriter::EigenrayWriter(std::filesystem::path outputPath,
                               std::string title,
                               const SimulationCase& simulation)
    : outputPath_(std::move(outputPath)),
      temporaryPath_(outputPath_.string() + ".tmp"),
      simulation_(simulation) {
  if (!isEigenrayMode(simulation_.runMode())) {
    throw ValidationError("eigenray writer requires Eigenray mode");
  }
  static_cast<void>(
      checkedOriginInt32(simulation_.sourceCount(), "E source count"));
  static_cast<void>(checkedOriginInt32(
      simulation_.launchFanPlan().launchAngleCount, "E launch-angle count"));
  output_.open(temporaryPath_, std::ios::out | std::ios::trunc);
  if (!output_.is_open()) {
    throw BellhopError("unable to open temporary eigenray output: " +
                       temporaryPath_.string());
  }
  writeHeader(output_, std::move(title), simulation_);
  if (!output_) {
    throw BellhopError("failed while writing temporary eigenray header");
  }
}

EigenrayWriter::~EigenrayWriter() {
  if (!finalized_) {
    output_.close();
    std::error_code ignored;
    std::filesystem::remove(temporaryPath_, ignored);
  }
}

void EigenrayWriter::appendHit(std::size_t sourceIndex,
                               std::size_t launchIndex,
                               const RayPathCache& cache,
                               const RayPath& path,
                               const EigenrayHit& hit) {
  if (finalized_ || sourceIndex >= simulation_.sourceCount()) {
    throw ValidationError("eigenray writer source order is invalid");
  }
  if (!cache.frozen() ||
      cache.size() != simulation_.launchFanPlan().launchAngleCount) {
    throw ValidationError(
        "eigenray writer requires one complete frozen launch fan per source");
  }
  if (launchIndex >= cache.size() || &cache.at(launchIndex) != &path) {
    throw ValidationError("eigenray writer path identity is invalid");
  }
  if (path.launchAngle !=
      simulation_.launchFanPlan().launchAngles[launchIndex]) {
    throw ValidationError("eigenray writer launch angle is out of order");
  }
  if (haveHit_ &&
      (sourceIndex < lastSourceIndex_ ||
       (sourceIndex == lastSourceIndex_ && launchIndex < lastLaunchIndex_))) {
    throw ValidationError("eigenray writer launch order is invalid");
  }
  if (hit.receiverRangeIndex >= simulation_.receivers().rangeCount() ||
      hit.receiverDepthIndex >= simulation_.receivers().receiversPerRange()) {
    throw ValidationError("eigenray writer receiver index is out of range");
  }
  if (hit.prefixPointCount < 2U || hit.prefixPointCount > path.points.size()) {
    throw ValidationError("eigenray writer prefix point count is invalid");
  }
  const auto encoded = ray_output_detail::encodeRayPrefix(
      path, hit.prefixPointCount,
      simulation_.environment().seaSurface().depth(),
      simulation_.environment().seabed().depth());
  static_cast<void>(checkedOriginInt32(encoded.pointIndices.size(),
                                       "E point count"));
  static_cast<void>(checkedOriginInt32(encoded.topBounceCount,
                                       "E top bounce count"));
  static_cast<void>(checkedOriginInt32(encoded.bottomBounceCount,
                                       "E bottom bounce count"));
  output_ << path.launchAngle * (180.0 / std::numbers::pi) << '\n'
          << encoded.pointIndices.size() << ' ' << encoded.topBounceCount
          << ' ' << encoded.bottomBounceCount << '\n';
  for (const std::size_t index : encoded.pointIndices) {
    output_ << path.points[index].position.range << ' '
            << path.points[index].position.depth << '\n';
  }
  if (!output_) {
    throw BellhopError("failed while writing temporary eigenray output");
  }
  lastSourceIndex_ = sourceIndex;
  lastLaunchIndex_ = launchIndex;
  haveHit_ = true;
}

void EigenrayWriter::finalize() {
  if (finalized_) {
    throw ValidationError("eigenray writer is already finalized");
  }
  output_.close();
  if (!output_) {
    throw BellhopError("failed to finalize temporary eigenray output");
  }
  std::error_code error;
  std::filesystem::rename(temporaryPath_, outputPath_, error);
  if (error) {
    throw BellhopError("unable to publish eigenray output: " + error.message());
  }
  finalized_ = true;
}

}  // namespace bellhop
