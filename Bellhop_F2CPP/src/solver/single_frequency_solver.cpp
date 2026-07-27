#include "bellhop/solver/single_frequency_solver.hpp"

#include <chrono>
#include <cstddef>

#include "bellhop/cache/ray_path_cache.hpp"
#include "bellhop/error.hpp"
#include "bellhop/field/beam_epsilon.hpp"
#include "bellhop/field/frequency_projector.hpp"
#include "bellhop/field/pressure_scaling.hpp"
#include "bellhop/model/c_linear_ssp.hpp"
#include "bellhop/ray/geometry_tracer.hpp"

namespace bellhop {
namespace {

using Clock = std::chrono::steady_clock;

[[nodiscard]] double elapsedSeconds(Clock::time_point begin,
                                    Clock::time_point end) {
  return std::chrono::duration<double>(end - begin).count();
}

}  // namespace

SingleFrequencyResult SingleFrequencySolver::solve(
    const SimulationCase& simulation,
    double epsilonMultiplier, double loopRange,
    CartesianCervenySettings influenceSettings) {
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

  const double frequency =
      simulation.frequencies().values().front();
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
  for (const RayPath& path : rayCache.paths()) {
    const Clock::time_point projectBegin = Clock::now();
    const RayFrequencyState frequencyState =
        projector.project(
            path, frequency, simulation.source().amplitude);
    const Clock::time_point projectEnd = Clock::now();
    static_cast<void>(influence.accumulate(
        workspace, path, frequencyState, epsilon.value));
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
              .traceSeconds =
                  elapsedSeconds(traceBegin, traceEnd),
              .projectSeconds = projectSeconds,
              .influenceSeconds = influenceSeconds,
              .scaleSeconds =
                  elapsedSeconds(scaleBegin, scaleEnd)}};
}

}  // namespace bellhop
