#include "rayreuse/solver/single_frequency_solver.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "rayreuse/cache/ray_path_cache.hpp"
#include "rayreuse/error.hpp"
#include "rayreuse/field/beam_epsilon.hpp"
#include "rayreuse/field/frequency_projector.hpp"
#include "rayreuse/field/geometric_gaussian_influence.hpp"
#include "rayreuse/field/geometric_hat_influence.hpp"
#include "rayreuse/field/pressure_scaling.hpp"
#include "rayreuse/field/ray_centered_cerveny_influence.hpp"
#include "rayreuse/field/simple_gaussian_influence.hpp"
#include "rayreuse/model/sound_speed_evaluator.hpp"
#include "rayreuse/ray/geometry_tracer.hpp"

namespace rayreuse {
namespace {

using Clock = std::chrono::steady_clock;

[[nodiscard]] double elapsedSeconds(Clock::time_point begin,
                                    Clock::time_point end) {
  return std::chrono::duration<double>(end - begin).count();
}

void requireSimulationFrequency(const SimulationCase& simulation,
                                double frequency) {
  const std::vector<double>& frequencies = simulation.frequencies().values();
  if (!std::isfinite(frequency) ||
      !std::binary_search(frequencies.begin(), frequencies.end(), frequency)) {
    throw ValidationError(
        "requested frequency does not belong to the simulation");
  }
}

void accumulateFrequencyTimings(SingleFrequencyTimings& total,
                                const SingleFrequencyTimings& value) {
  total.traceSeconds += value.traceSeconds;
  total.projectSeconds += value.projectSeconds;
  total.influenceSeconds += value.influenceSeconds;
  total.scaleSeconds += value.scaleSeconds;
  accumulateCartesianCervenyStatistics(total.influenceStatistics,
                                       value.influenceStatistics);
}

}  // namespace

double semiCoherentLloydMirrorFactor(double frequency, double sourceSoundSpeed,
                                     double sourceDepth,
                                     double launchAngleRadians) {
  return semiCoherentProjectedSourceAmplitude(1.0, frequency, sourceSoundSpeed,
                                              sourceDepth, launchAngleRadians);
}

double semiCoherentProjectedSourceAmplitude(double baseAmplitude,
                                            double frequency,
                                            double sourceSoundSpeed,
                                            double sourceDepth,
                                            double launchAngleRadians) {
  if (!std::isfinite(baseAmplitude) || baseAmplitude < 0.0) {
    throw ValidationError(
        "semi-coherent base source amplitude must be finite and non-negative");
  }
  if (!std::isfinite(frequency) || frequency <= 0.0) {
    throw ValidationError(
        "semi-coherent Lloyd frequency must be positive and finite");
  }
  if (!std::isfinite(sourceSoundSpeed) || sourceSoundSpeed <= 0.0) {
    throw ValidationError(
        "semi-coherent Lloyd source sound speed must be positive and finite");
  }
  if (!std::isfinite(sourceDepth) || !std::isfinite(launchAngleRadians)) {
    throw ValidationError(
        "semi-coherent Lloyd source depth and launch angle must be finite");
  }
  const double angularFrequency = 2.0 * std::numbers::pi * frequency;
  const double argument = (angularFrequency / sourceSoundSpeed) * sourceDepth *
                          std::sin(launchAngleRadians);
  const double amplitude = baseAmplitude *
                           static_cast<double>(std::sqrt(2.0F)) *
                           std::abs(std::sin(argument));
  if (!std::isfinite(amplitude)) {
    throw ValidationError(
        "semi-coherent projected source amplitude is invalid");
  }
  return amplitude;
}

SingleFrequencyResult SingleFrequencySolver::solve(
    const SimulationCase& simulation, double epsilonMultiplier,
    double loopRange, CartesianCervenySettings influenceSettings) {
  if (simulation.frequencies().size() != 1U) {
    throw ValidationError(
        "single-frequency solve requires exactly one frequency");
  }
  return solveAtFrequency(simulation, simulation.frequencies().values().front(),
                          epsilonMultiplier, loopRange, influenceSettings);
}

RayFanTraceResult SingleFrequencySolver::traceRayFan(
    const SimulationCase& simulation) {
  return traceSourceFan(simulation, 0U);
}

RayFanTraceResult SingleFrequencySolver::traceSourceFan(
    const SimulationCase& simulation, std::size_t sourceIndex) {
  if (sourceIndex >= simulation.sourceCount()) {
    throw ValidationError("source index is out of range");
  }
  const LaunchFanPlan& launchFan = simulation.launchFanPlan();
  const Source& source = simulation.sources()[sourceIndex];
  GeometryTracer tracer(simulation);
  RayPathCache rayCache;
  rayCache.reserve(launchFan.launchAngleCount);

  const Clock::time_point traceBegin = Clock::now();
  std::size_t totalRayPointCount = 0U;
  for (std::size_t launchIndex = 0U;
       launchIndex < launchFan.launchAngles.size(); ++launchIndex) {
    const double launchAngle = launchFan.launchAngles[launchIndex];
    RayPath path = tracer.trace(source, launchAngle);
    if (path.terminationReason != RayTerminationReason::ExitedDomain) {
      // F2CPP diagnostic format (source index, launch index, angle, reason).
      // RayReuse RayPath carries no terminationDetail field, so the optional
      // F2CPP ", detail: ..." tail has no counterpart here.
      throw ValidationError(
          "single-frequency solve encountered a ray that did not "
          "exit the spatial domain normally (source index " +
          std::to_string(sourceIndex) + ", launch index " +
          std::to_string(launchIndex) + ", angle " +
          std::to_string(launchAngle) + ", reason " +
          std::to_string(static_cast<int>(path.terminationReason)) + ")");
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

std::vector<RayFanTraceResult> SingleFrequencySolver::traceAllSourceFans(
    const SimulationCase& simulation) {
  std::vector<RayFanTraceResult> sourceTraces;
  sourceTraces.reserve(simulation.sourceCount());
  for (std::size_t sourceIndex = 0U; sourceIndex < simulation.sourceCount();
       ++sourceIndex) {
    sourceTraces.push_back(traceSourceFan(simulation, sourceIndex));
  }
  return sourceTraces;
}

std::size_t SingleFrequencyResult::sourceCount() const noexcept {
  return 1U + additionalSourceWorkspaces.size();
}

const FrequencyWorkspace& SingleFrequencyResult::sourceWorkspace(
    std::size_t sourceIndex) const {
  if (sourceIndex == 0U) {
    return workspace;
  }
  if (sourceIndex > additionalSourceWorkspaces.size()) {
    throw ValidationError("source workspace index is out of range");
  }
  return additionalSourceWorkspaces[sourceIndex - 1U];
}

SingleFrequencyResult SingleFrequencySolver::solveFrequencyFromSourceCache(
    const SimulationCase& simulation, double frequency,
    const RayPathCache& rayCache, std::size_t sourceIndex,
    double epsilonMultiplier, double loopRange,
    CartesianCervenySettings influenceSettings,
    WorkspaceDelivery delivery) {
  requireSimulationFrequency(simulation, frequency);
  if (!isTransmissionLossMode(simulation.runMode())) {
    throw ValidationError(
        "single-frequency field solver requires a transmission-loss run "
        "mode");
  }
  if (!rayCache.frozen()) {
    throw ValidationError("frequency projection requires a frozen ray cache");
  }
  if (sourceIndex >= simulation.sourceCount()) {
    throw ValidationError("source index is out of range");
  }
  const Source& source = simulation.sources()[sourceIndex];
  if (rayCache.size() > 0U && !rayCache.at(0U).points.empty() &&
      rayCache.at(0U).points.front().position.depth != source.depth) {
    // The frozen cache schema carries no source field (frozen decision), so
    // pairing is validated structurally: a per-source cache starts at the
    // source depth it was traced from.
    throw ValidationError(
        "frequency projection requires a ray cache traced from the "
        "requested source");
  }

  const LaunchFanPlan& launchFan = simulation.launchFanPlan();
  const GeometrySspEvaluator soundSpeedProfile(
      simulation.environment().soundSpeedProfile());
  const SoundSpeedSample sourceSample =
      soundSpeedProfile.evaluate(Vec2{.range = 0.0, .depth = source.depth}, 0U);
  const double sourceSoundSpeed = sourceSample.soundSpeed;
  if (simulation.beamFamily() != BeamFamily::CervenyGaussian &&
      simulation.beamFamily() != BeamFamily::GeometricHat &&
      simulation.beamFamily() != BeamFamily::GeometricGaussian &&
      simulation.beamFamily() != BeamFamily::SimpleGaussian) {
    throw ValidationError(
        "single-frequency TL solver supports only Cerveny, Cartesian or "
        "ray-centered geometric hat, and Cartesian geometric B/S beams");
  }
  if (simulation.beamFamily() == BeamFamily::SimpleGaussian &&
      simulation.runMode() != SimulationRunMode::Coherent) {
    throw ValidationError("simple Gaussian TL requires coherent pressure");
  }
  std::optional<FrequencyWorkspace> coherentWorkspace;
  std::optional<IntensityWorkspace> intensityWorkspace;
  switch (fieldAccumulationKind(simulation.runMode())) {
    case FieldAccumulationKind::ComplexPressure:
      coherentWorkspace.emplace(frequency, simulation.receivers());
      break;
    case FieldAccumulationKind::Intensity:
      intensityWorkspace.emplace(frequency, simulation.receivers());
      break;
    case FieldAccumulationKind::None:
      throw ValidationError(
          "field solver cannot accumulate a non-field run mode");
  }
  const FrequencyProjector projector(simulation.environment());
  std::optional<CartesianCervenyInfluence> cartesianCervenyInfluence;
  std::optional<RayCenteredCervenyInfluence> rayCenteredCervenyInfluence;
  std::optional<GeometricHatInfluence> geometricHatInfluence;
  std::optional<GeometricGaussianInfluence> geometricGaussianInfluence;
  std::optional<SimpleGaussianInfluence> simpleGaussianInfluence;
  if (simulation.beamFamily() == BeamFamily::GeometricHat) {
    geometricHatInfluence.emplace(simulation.receivers(),
                                  simulation.cervenyCoordinateSystem(),
                                  simulation.sourceGeometry());
  } else if (simulation.beamFamily() == BeamFamily::GeometricGaussian) {
    geometricGaussianInfluence.emplace(simulation.receivers(),
                                       simulation.sourceGeometry());
  } else if (simulation.beamFamily() == BeamFamily::SimpleGaussian) {
    simpleGaussianInfluence.emplace(simulation.receivers(),
                                    simulation.integrator().stepLength,
                                    simulation.sourceGeometry());
  } else {
    switch (simulation.cervenyCoordinateSystem()) {
      case CervenyCoordinateSystem::Cartesian:
        // Origin and F2CPP preserve the Cartesian Cerveny P/V/H selector in
        // the environment/model/PRT lifecycle, but InfluenceCervenyCart does
        // not read it.
        cartesianCervenyInfluence.emplace(
            simulation.environment(), simulation.receivers(), influenceSettings,
            simulation.beamWidthMode(), simulation.sourceGeometry());
        break;
      case CervenyCoordinateSystem::RayCentered:
        rayCenteredCervenyInfluence.emplace(
            simulation.environment(), simulation.receivers(), influenceSettings,
            simulation.beamWidthMode(), simulation.runMode(),
            simulation.fieldComponent(), simulation.sourceGeometry());
        break;
    }
  }

  double projectSeconds = 0.0;
  double influenceSeconds = 0.0;
  std::size_t totalRayPointCount = 0U;
  CartesianCervenyStatistics influenceStatistics;
  for (const RayPath& path : rayCache.paths()) {
    totalRayPointCount += path.points.size();
    const Clock::time_point projectBegin = Clock::now();
    const double patternAmplitude =
        simulation.sourceBeamPattern().amplitudeForLaunchAngle(
            path.launchAngle);
    const double baseSourceAmplitude = source.amplitude * patternAmplitude;
    const double projectedSourceAmplitude =
        usesLloydMirror(simulation.runMode())
            ? semiCoherentProjectedSourceAmplitude(
                  baseSourceAmplitude, frequency, sourceSoundSpeed,
                  source.depth, path.launchAngle)
            : baseSourceAmplitude;
    if (!std::isfinite(projectedSourceAmplitude) ||
        projectedSourceAmplitude < 0.0) {
      throw ValidationError(
          "source beam pattern produced an invalid projected amplitude");
    }
    const RayFrequencyState frequencyState =
        projector.project(path, frequency, projectedSourceAmplitude);
    const Clock::time_point projectEnd = Clock::now();
    if (simpleGaussianInfluence.has_value()) {
      static_cast<void>(simpleGaussianInfluence->accumulate(
          *coherentWorkspace, path, frequencyState, launchFan.launchAngleStep));
    } else if (geometricGaussianInfluence.has_value() &&
               coherentWorkspace.has_value()) {
      static_cast<void>(geometricGaussianInfluence->accumulate(
          *coherentWorkspace, path, frequencyState, launchFan.launchAngleStep));
    } else if (geometricGaussianInfluence.has_value()) {
      static_cast<void>(geometricGaussianInfluence->accumulateIntensity(
          *intensityWorkspace, path, frequencyState,
          launchFan.launchAngleStep));
    } else if (geometricHatInfluence.has_value() &&
               coherentWorkspace.has_value()) {
      static_cast<void>(geometricHatInfluence->accumulate(
          *coherentWorkspace, path, frequencyState, launchFan.launchAngleStep));
    } else if (geometricHatInfluence.has_value()) {
      static_cast<void>(geometricHatInfluence->accumulateIntensity(
          *intensityWorkspace, path, frequencyState,
          launchFan.launchAngleStep));
    } else if (coherentWorkspace.has_value()) {
      const BeamEpsilon epsilon = pickBeamEpsilon(
          simulation.beamWidthMode(), frequency, sourceSoundSpeed,
          sourceSample.soundSpeedGradient.depth, path.launchAngle,
          launchFan.launchAngleStep, loopRange, epsilonMultiplier);
      if (cartesianCervenyInfluence.has_value()) {
        static_cast<void>(cartesianCervenyInfluence->accumulatePrevalidated(
            *coherentWorkspace, path, frequencyState, epsilon.value,
            influenceSettings.collectStatistics ? &influenceStatistics
                                                : nullptr));
      } else {
        static_cast<void>(rayCenteredCervenyInfluence->accumulate(
            *coherentWorkspace, path, frequencyState, epsilon.value));
      }
    } else {
      const BeamEpsilon epsilon = pickBeamEpsilon(
          simulation.beamWidthMode(), frequency, sourceSoundSpeed,
          sourceSample.soundSpeedGradient.depth, path.launchAngle,
          launchFan.launchAngleStep, loopRange, epsilonMultiplier);
      if (cartesianCervenyInfluence.has_value()) {
        static_cast<void>(
            cartesianCervenyInfluence->accumulateIntensityPrevalidated(
                *intensityWorkspace, path, frequencyState, epsilon.value,
                influenceSettings.collectStatistics ? &influenceStatistics
                                                    : nullptr));
      } else {
        static_cast<void>(rayCenteredCervenyInfluence->accumulateIntensity(
            *intensityWorkspace, path, frequencyState, epsilon.value));
      }
    }
    const Clock::time_point influenceEnd = Clock::now();
    projectSeconds += elapsedSeconds(projectBegin, projectEnd);
    influenceSeconds += elapsedSeconds(projectEnd, influenceEnd);
  }

  const Clock::time_point scaleBegin = Clock::now();
  const bool geometricNormalization =
      simulation.beamFamily() != BeamFamily::CervenyGaussian;
  FrequencyWorkspace workspace =
      coherentWorkspace.has_value() ? std::move(*coherentWorkspace)
      : geometricNormalization
          ? scaleGeometricIntensityToPressure(
                *intensityWorkspace, simulation.receivers(),
                launchFan.launchAngleStep, sourceSoundSpeed,
                simulation.sourceGeometry())
          : scaleCartesianIntensityToPressure(
                *intensityWorkspace, simulation.receivers(),
                launchFan.launchAngleStep, sourceSoundSpeed,
                simulation.sourceGeometry());
  if (coherentWorkspace.has_value() &&
      delivery == WorkspaceDelivery::Scaled) {
    if (geometricNormalization) {
      scaleCoherentGeometricPressure(
          workspace, simulation.receivers(), launchFan.launchAngleStep,
          sourceSoundSpeed, simulation.sourceGeometry());
    } else {
      scaleCoherentCartesianPressure(
          workspace, simulation.receivers(), launchFan.launchAngleStep,
          sourceSoundSpeed, simulation.sourceGeometry());
    }
  }
  const Clock::time_point scaleEnd = Clock::now();

  return SingleFrequencyResult{
      .workspace = std::move(workspace),
      .rayCount = rayCache.size(),
      .totalRayPointCount = totalRayPointCount,
      .rayCacheBytes = rayCache.memoryFootprintBytes(),
      .timings = SingleFrequencyTimings{
          .traceSeconds = 0.0,
          .projectSeconds = projectSeconds,
          .influenceSeconds = influenceSeconds,
          .scaleSeconds = delivery == WorkspaceDelivery::Raw
                              ? 0.0
                              : elapsedSeconds(scaleBegin, scaleEnd),
          .influenceStatistics = influenceStatistics}};
}

SingleFrequencyResult SingleFrequencySolver::solveFrequencyFromCache(
    const SimulationCase& simulation, double frequency,
    const RayPathCache& rayCache, double epsilonMultiplier, double loopRange,
    CartesianCervenySettings influenceSettings) {
  return solveFrequencyFromSourceCache(simulation, frequency, rayCache, 0U,
                                       epsilonMultiplier, loopRange,
                                       influenceSettings);
}

SingleFrequencyResult SingleFrequencySolver::solveAtFrequency(
    const SimulationCase& simulation, double frequency,
    double epsilonMultiplier, double loopRange,
    CartesianCervenySettings influenceSettings) {
  requireSimulationFrequency(simulation, frequency);
  // F2CPP structure: trace each source's fan independently, then project each
  // frozen per-source cache with that source's source-term inputs.
  const std::vector<RayFanTraceResult> sourceTraces =
      traceAllSourceFans(simulation);
  std::vector<FrequencyWorkspace> sourceWorkspaces;
  sourceWorkspaces.reserve(sourceTraces.size());
  SingleFrequencyTimings totalTimings;
  std::size_t rayCount = 0U;
  std::size_t totalRayPointCount = 0U;
  std::size_t peakRayCacheBytes = 0U;
  for (std::size_t sourceIndex = 0U; sourceIndex < sourceTraces.size();
       ++sourceIndex) {
    SingleFrequencyResult sourceResult = solveFrequencyFromSourceCache(
        simulation, frequency, sourceTraces[sourceIndex].cache, sourceIndex,
        epsilonMultiplier, loopRange, influenceSettings);
    sourceResult.timings.traceSeconds = sourceTraces[sourceIndex].traceSeconds;
    rayCount += sourceResult.rayCount;
    totalRayPointCount += sourceResult.totalRayPointCount;
    peakRayCacheBytes = std::max(peakRayCacheBytes, sourceResult.rayCacheBytes);
    accumulateFrequencyTimings(totalTimings, sourceResult.timings);
    sourceWorkspaces.push_back(std::move(sourceResult.workspace));
  }
  std::vector<FrequencyWorkspace> additionalSourceWorkspaces;
  additionalSourceWorkspaces.reserve(sourceWorkspaces.size() - 1U);
  for (std::size_t index = 1U; index < sourceWorkspaces.size(); ++index) {
    additionalSourceWorkspaces.push_back(std::move(sourceWorkspaces[index]));
  }
  return SingleFrequencyResult{
      .workspace = std::move(sourceWorkspaces.front()),
      .additionalSourceWorkspaces = std::move(additionalSourceWorkspaces),
      .rayCount = rayCount,
      .totalRayPointCount = totalRayPointCount,
      .rayCacheBytes = peakRayCacheBytes,
      .timings = totalTimings};
}

}  // namespace rayreuse
