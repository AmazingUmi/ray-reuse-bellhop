#include "bellhop/solver/eigenray_solver.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <optional>
#include <string>

#include "bellhop/error.hpp"
#include "bellhop/field/frequency_projector.hpp"
#include "bellhop/field/geometric_gaussian_influence.hpp"
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

}  // namespace

EigenraySolverStatistics EigenraySolver::solve(
    const SimulationCase& simulation, const FrozenEigenrayConsumer& consumer) {
  if (!isEigenrayMode(simulation.runMode())) {
    throw ValidationError("eigenray solver requires Eigenray mode");
  }
  if (simulation.beamFamily() != BeamFamily::GeometricHat &&
      simulation.beamFamily() != BeamFamily::GeometricGaussian) {
    throw ValidationError(
        "eigenray solver supports only geometric-hat or geometric-Gaussian "
        "beams");
  }
  if (!consumer) {
    throw ValidationError("eigenray solver requires a hit consumer");
  }
  if (simulation.frequencies().size() != 1U) {
    throw ValidationError("eigenray solver requires exactly one frequency");
  }

  const LaunchFanPlan& fan = simulation.launchFanPlan();
  const std::size_t rayCount = checkedMultiply(
      fan.launchAngleCount, simulation.sourceCount(), "eigenray ray count");
  if (rayCount > kMaximumRunRayCount) {
    throw ValidationError("eigenray ray count exceeds the supported limit");
  }

  GeometryTracer tracer(simulation);
  const FrequencyProjector projector(simulation.environment());
  std::optional<GeometricHatInfluence> geometricHatInfluence;
  std::optional<GeometricGaussianInfluence> geometricGaussianInfluence;
  if (simulation.beamFamily() == BeamFamily::GeometricHat) {
    geometricHatInfluence.emplace(
        simulation.receivers(), simulation.cervenyCoordinateSystem(),
        simulation.sourceGeometry(), simulation.runMode());
  } else {
    if (simulation.cervenyCoordinateSystem() !=
        CervenyCoordinateSystem::Cartesian) {
      throw ValidationError(
          "geometric-Gaussian eigenrays require Cartesian coordinates");
    }
    geometricGaussianInfluence.emplace(
        simulation.receivers(), simulation.sourceGeometry(),
        simulation.runMode());
  }
  const double frequency = simulation.frequencies().values().front();

  EigenraySolverStatistics statistics;
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
            "eigenray solve encountered an abnormal ray termination at "
            "source " + std::to_string(sourceIndex) + ", launch " +
            std::to_string(launchIndex));
      }
      statistics.totalRayPointCount = checkedAdd(
          statistics.totalRayPointCount, path.points.size(),
          "eigenray point count");
      cache.append(std::move(path));
    }
    cache.freeze();
    statistics.traceSeconds += elapsed(traceBegin, Clock::now());
    statistics.peakRayCacheBytes = std::max(
        statistics.peakRayCacheBytes, cache.memoryFootprintBytes());

    for (std::size_t launchIndex = 0U; launchIndex < cache.size();
         ++launchIndex) {
      const RayPath& path = cache.at(launchIndex);
      const Clock::time_point projectBegin = Clock::now();
      const double projectedSourceAmplitude =
          source.amplitude *
          simulation.sourceBeamPattern().amplitudeForLaunchAngle(
              path.launchAngle);
      if (!std::isfinite(projectedSourceAmplitude) ||
          projectedSourceAmplitude < 0.0) {
        throw ValidationError(
            "source beam pattern produced an invalid eigenray amplitude");
      }
      const RayFrequencyState frequencyState = projector.project(
          path, frequency, projectedSourceAmplitude);
      const Clock::time_point projectEnd = Clock::now();
      const Clock::time_point influenceBegin = projectEnd;
      double pathConsumeSeconds = 0.0;
      const auto collectHit =
          [&](const EigenrayHit& hit) {
            const Clock::time_point consumeBegin = Clock::now();
            consumer(sourceIndex, launchIndex, cache, path, hit);
            pathConsumeSeconds += elapsed(consumeBegin, Clock::now());
            statistics.totalHitCount = checkedAdd(
                statistics.totalHitCount, 1U, "eigenray hit count");
            statistics.totalPrefixPointCount = checkedAdd(
                statistics.totalPrefixPointCount, hit.prefixPointCount,
                "eigenray prefix point count");
          };
      if (geometricHatInfluence.has_value()) {
        geometricHatInfluence->collectEigenrayHits(
            collectHit, path, frequencyState, fan.launchAngleStep);
      } else {
        geometricGaussianInfluence->collectEigenrayHits(
            collectHit, path, frequencyState, fan.launchAngleStep);
      }
      const Clock::time_point influenceEnd = Clock::now();
      statistics.projectSeconds += elapsed(projectBegin, projectEnd);
      statistics.consumeSeconds += pathConsumeSeconds;
      statistics.influenceSeconds +=
          std::max(0.0, elapsed(influenceBegin, influenceEnd) -
                            pathConsumeSeconds);
      ++statistics.projectedRayCount;
    }
  }
  return statistics;
}

}  // namespace bellhop
