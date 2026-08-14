#include "bellhop/solver/arrival_solver.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "bellhop/error.hpp"
#include "bellhop/field/frequency_projector.hpp"
#include "bellhop/field/geometric_hat_influence.hpp"
#include "bellhop/ray/geometry_tracer.hpp"

namespace bellhop {
namespace {

using Clock = std::chrono::steady_clock;

[[nodiscard]] std::size_t checkedAdd(std::size_t left, std::size_t right,
                                     const char* label) {
  if (right > std::numeric_limits<std::size_t>::max() - left) {
    throw ValidationError(std::string(label) + " exceeds size_t capacity");
  }
  return left + right;
}

[[nodiscard]] std::size_t checkedMultiply(std::size_t left,
                                          std::size_t right,
                                          const char* label) {
  if (left != 0U &&
      right > std::numeric_limits<std::size_t>::max() / left) {
    throw ValidationError(std::string(label) + " exceeds size_t capacity");
  }
  return left * right;
}

[[nodiscard]] double elapsed(Clock::time_point begin,
                             Clock::time_point end) {
  return std::chrono::duration<double>(end - begin).count();
}

[[nodiscard]] std::size_t workspaceBytes(
    const ArrivalWorkspace& workspace) {
  std::size_t bytes = sizeof(ArrivalWorkspace);
  bytes = checkedAdd(
      bytes,
      checkedMultiply(workspace.receiverCellCount(),
                      sizeof(std::vector<Arrival>),
                      "arrival workspace cell bytes"),
      "arrival workspace bytes");
  for (std::size_t cell = 0U; cell < workspace.receiverCellCount(); ++cell) {
    bytes = checkedAdd(
        bytes,
        checkedMultiply(workspace.cellAt(cell).size(), sizeof(Arrival),
                        "arrival workspace record bytes"),
        "arrival workspace bytes");
  }
  return bytes;
}

}  // namespace

ArrivalSolverStatistics ArrivalSolver::solve(
    const SimulationCase& simulation,
    const FrozenSourceArrivalConsumer& consumer) {
  if (!isArrivalMode(simulation.runMode())) {
    throw ValidationError("arrival solver requires ASCII or binary arrivals");
  }
  if (simulation.beamFamily() != BeamFamily::GeometricHat) {
    throw ValidationError(
        "arrival solver currently supports only geometric-hat beams");
  }
  if (!consumer) {
    throw ValidationError("arrival solver requires a source consumer");
  }
  if (simulation.frequencies().size() != 1U) {
    throw ValidationError("arrival solver requires exactly one frequency");
  }

  const LaunchFanPlan& fan = simulation.launchFanPlan();
  const std::size_t rayCount = checkedMultiply(
      fan.launchAngleCount, simulation.sourceCount(), "arrival ray count");
  if (rayCount > kMaximumRunRayCount) {
    throw ValidationError("arrival ray count exceeds the supported limit");
  }

  GeometryTracer tracer(simulation);
  const FrequencyProjector projector(simulation.environment());
  const GeometricHatInfluence influence(
      simulation.receivers(), simulation.cervenyCoordinateSystem(),
      simulation.sourceGeometry(), simulation.runMode());
  const double frequency = simulation.frequencies().values().front();

  ArrivalSolverStatistics statistics;
  statistics.sourceCount = simulation.sourceCount();
  statistics.rayCount = rayCount;
  for (std::size_t sourceIndex = 0U;
       sourceIndex < simulation.sourceCount(); ++sourceIndex) {
    const Source& source = simulation.sources()[sourceIndex];
    RayPathCache cache;
    cache.reserve(fan.launchAngleCount);
    const Clock::time_point traceBegin = Clock::now();
    for (std::size_t launchIndex = 0U;
         launchIndex < fan.launchAngles.size(); ++launchIndex) {
      RayPath path = tracer.trace(source, fan.launchAngles[launchIndex]);
      if (path.terminationReason != RayTerminationReason::ExitedDomain) {
        throw ValidationError(
            "arrival solve encountered an abnormal ray termination at source " +
            std::to_string(sourceIndex) + ", launch " +
            std::to_string(launchIndex));
      }
      statistics.totalRayPointCount = checkedAdd(
          statistics.totalRayPointCount, path.points.size(),
          "arrival point count");
      cache.append(std::move(path));
    }
    cache.freeze();
    statistics.traceSeconds += elapsed(traceBegin, Clock::now());
    statistics.peakRayCacheBytes = std::max(
        statistics.peakRayCacheBytes, cache.memoryFootprintBytes());

    ArrivalWorkspace workspace(frequency, simulation.receivers());
    for (const RayPath& path : cache.paths()) {
      const Clock::time_point projectBegin = Clock::now();
      const double projectedSourceAmplitude =
          source.amplitude *
          simulation.sourceBeamPattern().amplitudeForLaunchAngle(
              path.launchAngle);
      if (!std::isfinite(projectedSourceAmplitude) ||
          projectedSourceAmplitude < 0.0) {
        throw ValidationError(
            "source beam pattern produced an invalid arrival amplitude");
      }
      const RayFrequencyState frequencyState = projector.project(
          path, frequency, projectedSourceAmplitude);
      const Clock::time_point projectEnd = Clock::now();
      static_cast<void>(influence.accumulateArrivals(
          workspace, path, frequencyState, fan.launchAngleStep));
      const Clock::time_point influenceEnd = Clock::now();
      statistics.projectSeconds += elapsed(projectBegin, projectEnd);
      statistics.influenceSeconds += elapsed(projectEnd, influenceEnd);
      ++statistics.projectedRayCount;
    }
    statistics.candidateCount = checkedAdd(
        statistics.candidateCount, workspace.candidateCount(),
        "arrival candidate count");
    statistics.saturatedCellCount = checkedAdd(
        statistics.saturatedCellCount, workspace.saturatedCellCount(),
        "arrival saturated-cell count");
    statistics.peakArrivalWorkspaceBytes = std::max(
        statistics.peakArrivalWorkspaceBytes, workspaceBytes(workspace));
    const Clock::time_point consumeBegin = Clock::now();
    consumer(sourceIndex, cache, workspace);
    statistics.consumeSeconds += elapsed(consumeBegin, Clock::now());
  }
  return statistics;
}

}  // namespace bellhop
