#include "rayreuse/solver/single_frequency_solver.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <vector>

#include "rayreuse/cache/ray_path_cache.hpp"
#include "rayreuse/error.hpp"
#include "rayreuse/field/beam_epsilon.hpp"
#include "rayreuse/field/frequency_projector.hpp"
#include "rayreuse/field/pressure_scaling.hpp"
#include "rayreuse/model/c_linear_ssp.hpp"
#include "rayreuse/ray/geometry_tracer.hpp"

namespace rayreuse {
namespace {

using Clock = std::chrono::steady_clock;

[[nodiscard]] double elapsedSeconds(Clock::time_point begin,
                                    Clock::time_point end) {
  return std::chrono::duration<double>(end - begin).count();
}

void requireSimulationFrequency(
    const SimulationCase& simulation, double frequency) {
  const std::vector<double>& frequencies =
      simulation.frequencies().values();
  if (!std::isfinite(frequency) ||
      !std::binary_search(
          frequencies.begin(), frequencies.end(), frequency)) {
    throw ValidationError(
        "requested frequency does not belong to the simulation");
  }
}

}  // namespace

SingleFrequencyResult SingleFrequencySolver::solve(
    const SimulationCase& simulation,
    double epsilonMultiplier, double loopRange,
    CartesianCervenySettings influenceSettings) {
  if (simulation.frequencies().size() != 1U) {
    throw ValidationError(
        "single-frequency solve requires exactly one frequency");
  }
  return solveAtFrequency(
      simulation, simulation.frequencies().values().front(),
      epsilonMultiplier, loopRange, influenceSettings);
}

RayFanTraceResult SingleFrequencySolver::traceRayFan(
    const SimulationCase& simulation) {
  const LaunchFanPlan& launchFan = simulation.launchFanPlan();
  GeometryTracer tracer(simulation);
  RayPathCache rayCache;
  rayCache.reserve(launchFan.launchAngleCount);

  const Clock::time_point traceBegin = Clock::now();
  std::size_t totalRayPointCount = 0U;
  for (const double launchAngle : launchFan.launchAngles) {
    RayPath path =
        tracer.trace(simulation.source(), launchAngle);
    if (path.terminationReason !=
        RayTerminationReason::ExitedDomain) {
      throw ValidationError(
          "single-frequency solve encountered a ray that did not "
          "exit the spatial domain normally");
    }
    totalRayPointCount += path.points.size();
    rayCache.append(std::move(path));
  }
  rayCache.freeze();
  const Clock::time_point traceEnd = Clock::now();

  return RayFanTraceResult{
      .cache = std::move(rayCache),
      .totalRayPointCount = totalRayPointCount,
      .traceSeconds = elapsedSeconds(traceBegin, traceEnd)};
}

SingleFrequencyResult SingleFrequencySolver::solveFrequencyFromCache(
    const SimulationCase& simulation, double frequency,
    const RayPathCache& rayCache, double epsilonMultiplier,
    double loopRange,
    CartesianCervenySettings influenceSettings) {
  requireSimulationFrequency(simulation, frequency);
  if (!rayCache.frozen()) {
    throw ValidationError(
        "frequency projection requires a frozen ray cache");
  }

  const LaunchFanPlan& launchFan = simulation.launchFanPlan();
  const CLinearSsp soundSpeedProfile(
      simulation.environment().soundSpeedProfile());
  const double sourceSoundSpeed =
      soundSpeedProfile
          .evaluate(
              Vec2{
                  .range = 0.0,
                  .depth = simulation.source().depth},
              0U)
          .soundSpeed;
  const BeamEpsilon epsilon =
      pickMinimumWidthEpsilon(
          frequency, sourceSoundSpeed, loopRange,
          epsilonMultiplier);

  FrequencyWorkspace workspace(
      frequency, simulation.receivers());
  const FrequencyProjector projector(
      simulation.environment());
  const CartesianCervenyInfluence influence(
      simulation.environment(), simulation.receivers(),
      influenceSettings);

  double projectSeconds = 0.0;
  double influenceSeconds = 0.0;
  std::size_t totalRayPointCount = 0U;
  CartesianCervenyStatistics influenceStatistics;
  for (const RayPath& path : rayCache.paths()) {
    totalRayPointCount += path.points.size();
    const Clock::time_point projectBegin = Clock::now();
    const RayFrequencyState frequencyState =
        projector.project(
            path, frequency, simulation.source().amplitude);
    const Clock::time_point projectEnd = Clock::now();
    static_cast<void>(influence.accumulatePrevalidated(
        workspace, path, frequencyState, epsilon.value,
        influenceSettings.collectStatistics
            ? &influenceStatistics
            : nullptr));
    const Clock::time_point influenceEnd = Clock::now();
    projectSeconds += elapsedSeconds(projectBegin, projectEnd);
    influenceSeconds +=
        elapsedSeconds(projectEnd, influenceEnd);
  }

  const Clock::time_point scaleBegin = Clock::now();
  scaleCoherentCartesianPointPressure(
      workspace, simulation.receivers(),
      launchFan.launchAngleStep, sourceSoundSpeed);
  const Clock::time_point scaleEnd = Clock::now();

  return SingleFrequencyResult{
      .workspace = std::move(workspace),
      .rayCount = rayCache.size(),
      .totalRayPointCount = totalRayPointCount,
      .rayCacheBytes = rayCache.memoryFootprintBytes(),
      .timings =
          SingleFrequencyTimings{
              .traceSeconds = 0.0,
              .projectSeconds = projectSeconds,
              .influenceSeconds = influenceSeconds,
              .scaleSeconds =
                  elapsedSeconds(scaleBegin, scaleEnd),
              .influenceStatistics = influenceStatistics}};
}

SingleFrequencyResult SingleFrequencySolver::solveAtFrequency(
    const SimulationCase& simulation, double frequency,
    double epsilonMultiplier, double loopRange,
    CartesianCervenySettings influenceSettings) {
  requireSimulationFrequency(simulation, frequency);
  RayFanTraceResult trace = traceRayFan(simulation);
  SingleFrequencyResult result = solveFrequencyFromCache(
      simulation, frequency, trace.cache, epsilonMultiplier,
      loopRange, influenceSettings);
  result.timings.traceSeconds = trace.traceSeconds;
  return result;
}

}  // namespace rayreuse
