#include "rayreuse/io/eigenray_writer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numbers>
#include <string>
#include <system_error>

#include "rayreuse/error.hpp"
#include "rayreuse/io/ray_prefix_writer.hpp"

namespace rayreuse {
namespace {
std::int32_t checkedOriginInt32(std::size_t value, const char* label) {
  if (value >
      static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
    throw ValidationError(std::string(label) + " exceeds the E int32 limit");
  }
  return static_cast<std::int32_t>(value);
}
}  // namespace

void EigenrayWriter::write(
    const std::filesystem::path& path, std::string_view title,
    const SimulationCase& simulation, double frequency,
    const RayPathCache& cache,
    const std::vector<std::pair<std::size_t, EigenrayHit>>& hits) {
  if (simulation.runMode() != SimulationRunMode::Eigenray)
    throw ValidationError("eigenray writer requires Eigenray run mode");
  if (!cache.frozen())
    throw ValidationError("eigenray writer requires a frozen cache");
  if (!std::isfinite(frequency) || frequency <= 0.0)
    throw ValidationError(
        "eigenray writer frequency must be positive and finite");
  if (std::find(simulation.frequencies().values().begin(),
                simulation.frequencies().values().end(),
                frequency) == simulation.frequencies().values().end())
    throw ValidationError(
        "eigenray writer frequency is not in the simulation grid");
  if (cache.size() != simulation.launchFanPlan().launchAngleCount) {
    throw ValidationError(
        "eigenray writer requires one complete frozen launch fan");
  }
  static_cast<void>(checkedOriginInt32(cache.size(), "E launch-angle count"));
  std::filesystem::path temporary = path;
  temporary += ".tmp";
  try {
    std::ofstream output(temporary, std::ios::trunc);
    if (!output)
      throw BellhopError("unable to open eigenray output: " +
                         temporary.string());
    std::string outputTitle = "BELLHOP- ";
    outputTitle.append(title);
    if (outputTitle.size() > 70U) outputTitle.resize(70U);
    output << '\'' << outputTitle << "'\n"
           << std::setprecision(std::numeric_limits<double>::max_digits10)
           << frequency << "\n1 1 1\n"
           << simulation.launchFanPlan().launchAngleCount << " 1\n"
           << simulation.environment().seaSurface().depth() << '\n'
           << simulation.environment().seabed().depth() << "\n'rz'\n";
    std::size_t previousLaunchIndex = 0U;
    bool haveHit = false;
    for (const auto& [launchIndex, hit] : hits) {
      if (launchIndex >= cache.size())
        throw ValidationError("eigenray launch index is out of range");
      if (haveHit && launchIndex < previousLaunchIndex)
        throw ValidationError("eigenray writer launch order is invalid");
      const RayPath& pathValue = cache.at(launchIndex);
      if (pathValue.launchAngle !=
          simulation.launchFanPlan().launchAngles[launchIndex])
        throw ValidationError("eigenray writer launch angle is out of order");
      if (hit.receiverRangeIndex >= simulation.receivers().rangeCount() ||
          hit.receiverDepthIndex >= simulation.receivers().depthCount())
        throw ValidationError("eigenray writer receiver index is out of range");
      if (hit.prefixPointCount < 2U ||
          hit.prefixPointCount > pathValue.points.size())
        throw ValidationError("eigenray writer prefix point count is invalid");
      const auto encoded = ray_output_detail::encodeRayPrefix(
          pathValue, hit.prefixPointCount,
          simulation.environment().seaSurface().depth(),
          simulation.environment().seabed().depth());
      static_cast<void>(
          checkedOriginInt32(encoded.pointIndices.size(), "E point count"));
      static_cast<void>(
          checkedOriginInt32(encoded.topBounceCount, "E top bounce count"));
      static_cast<void>(checkedOriginInt32(encoded.bottomBounceCount,
                                           "E bottom bounce count"));
      output << pathValue.launchAngle * (180.0 / std::numbers::pi) << '\n'
             << encoded.pointIndices.size() << ' ' << encoded.topBounceCount
             << ' ' << encoded.bottomBounceCount << '\n';
      for (std::size_t index : encoded.pointIndices)
        output << pathValue.points[index].position.range << ' '
               << pathValue.points[index].position.depth << '\n';
      previousLaunchIndex = launchIndex;
      haveHit = true;
    }
    if (!output) throw BellhopError("failed while writing eigenray output");
    output.close();
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error)
      throw BellhopError("unable to publish eigenray output: " +
                         error.message());
  } catch (...) {
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    throw;
  }
}
}  // namespace rayreuse
