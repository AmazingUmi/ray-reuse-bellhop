#include "rayreuse/solver/single_frequency_solver.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <optional>
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
#include "rayreuse/model/c_linear_ssp.hpp"
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
  const LaunchFanPlan& launchFan = simulation.launchFanPlan();
  GeometryTracer tracer(simulation);
  RayPathCache rayCache;
  rayCache.reserve(launchFan.launchAngleCount);

  const Clock::time_point traceBegin = Clock::now();
  std::size_t totalRayPointCount = 0U;
  for (const double launchAngle : launchFan.launchAngles) {
    RayPath path = tracer.trace(simulation.source(), launchAngle);
    if (path.terminationReason != RayTerminationReason::ExitedDomain) {
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
    const RayPathCache& rayCache, double epsilonMultiplier, double loopRange,
    CartesianCervenySettings influenceSettings) {
  requireSimulationFrequency(simulation, frequency);
  if (!isTransmissionLossMode(simulation.runMode())) {
    throw ValidationError(
        "single-frequency field solver requires a transmission-loss run "
        "mode");
  }
  if (!rayCache.frozen()) {
    throw ValidationError("frequency projection requires a frozen ray cache");
  }

  const LaunchFanPlan& launchFan = simulation.launchFanPlan();
  const CLinearSsp soundSpeedProfile(
      simulation.environment().soundSpeedProfile());
  const SoundSpeedSample sourceSample = soundSpeedProfile.evaluate(
      Vec2{.range = 0.0, .depth = simulation.source().depth}, 0U);
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
                                  simulation.cervenyCoordinateSystem());
  } else if (simulation.beamFamily() == BeamFamily::GeometricGaussian) {
    geometricGaussianInfluence.emplace(simulation.receivers());
  } else if (simulation.beamFamily() == BeamFamily::SimpleGaussian) {
    simpleGaussianInfluence.emplace(simulation.receivers(),
                                    simulation.integrator().stepLength);
  } else {
    switch (simulation.cervenyCoordinateSystem()) {
      case CervenyCoordinateSystem::Cartesian:
        // Origin and F2CPP preserve the Cartesian Cerveny P/V/H selector in
        // the environment/model/PRT lifecycle, but InfluenceCervenyCart does
        // not read it.
        cartesianCervenyInfluence.emplace(
            simulation.environment(), simulation.receivers(), influenceSettings,
            simulation.beamWidthMode());
        break;
      case CervenyCoordinateSystem::RayCentered:
        rayCenteredCervenyInfluence.emplace(
            simulation.environment(), simulation.receivers(), influenceSettings,
            simulation.beamWidthMode(), simulation.runMode(),
            simulation.fieldComponent());
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
    const double baseSourceAmplitude =
        simulation.source().amplitude * patternAmplitude;
    const double projectedSourceAmplitude =
        usesLloydMirror(simulation.runMode())
            ? semiCoherentProjectedSourceAmplitude(
                  baseSourceAmplitude, frequency, sourceSoundSpeed,
                  simulation.source().depth, path.launchAngle)
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
          ? scaleGeometricPointIntensityToPressure(
                *intensityWorkspace, simulation.receivers(),
                launchFan.launchAngleStep, sourceSoundSpeed)
          : scaleCartesianPointIntensityToPressure(
                *intensityWorkspace, simulation.receivers(),
                launchFan.launchAngleStep, sourceSoundSpeed);
  if (coherentWorkspace.has_value()) {
    if (geometricNormalization) {
      scaleCoherentGeometricPointPressure(workspace, simulation.receivers(),
                                          launchFan.launchAngleStep,
                                          sourceSoundSpeed);
    } else {
      scaleCoherentCartesianPointPressure(workspace, simulation.receivers(),
                                          launchFan.launchAngleStep,
                                          sourceSoundSpeed);
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
          .scaleSeconds = elapsedSeconds(scaleBegin, scaleEnd),
          .influenceStatistics = influenceStatistics}};
}

SingleFrequencyResult SingleFrequencySolver::solveAtFrequency(
    const SimulationCase& simulation, double frequency,
    double epsilonMultiplier, double loopRange,
    CartesianCervenySettings influenceSettings) {
  requireSimulationFrequency(simulation, frequency);
  RayFanTraceResult trace = traceRayFan(simulation);
  SingleFrequencyResult result =
      solveFrequencyFromCache(simulation, frequency, trace.cache,
                              epsilonMultiplier, loopRange, influenceSettings);
  result.timings.traceSeconds = trace.traceSeconds;
  return result;
}

}  // namespace rayreuse
