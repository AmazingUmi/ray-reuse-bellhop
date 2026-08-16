#include "bellhop/solver/single_frequency_solver.hpp"

#include <chrono>
#include <cstddef>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "bellhop/cache/ray_path_cache.hpp"
#include "bellhop/error.hpp"
#include "bellhop/field/beam_epsilon.hpp"
#include "bellhop/field/frequency_projector.hpp"
#include "bellhop/field/geometric_gaussian_influence.hpp"
#include "bellhop/field/geometric_hat_influence.hpp"
#include "bellhop/field/pressure_scaling.hpp"
#include "bellhop/field/ray_centered_cerveny_influence.hpp"
#include "bellhop/field/simple_gaussian_influence.hpp"
#include "bellhop/model/sound_speed_evaluator.hpp"
#include "bellhop/ray/geometry_tracer.hpp"

namespace bellhop {
namespace {

using Clock = std::chrono::steady_clock;

[[nodiscard]] double elapsedSeconds(Clock::time_point begin,
                                    Clock::time_point end) {
  return std::chrono::duration<double>(end - begin).count();
}

[[nodiscard]] std::size_t checkedAdd(std::size_t lhs, std::size_t rhs,
                                     const char* label) {
  if (rhs > std::numeric_limits<std::size_t>::max() - lhs) {
    throw ValidationError(std::string(label) + " exceeds size_t capacity");
  }
  return lhs + rhs;
}

[[nodiscard]] std::size_t checkedMultiply(std::size_t lhs, std::size_t rhs,
                                          const char* label) {
  if (lhs != 0U && rhs > std::numeric_limits<std::size_t>::max() / lhs) {
    throw ValidationError(std::string(label) + " exceeds size_t capacity");
  }
  return lhs * rhs;
}

}  // namespace

double semiCoherentLloydMirrorFactor(
    double frequency, double sourceSoundSpeed, double sourceDepth,
    double launchAngleRadians) {
  return semiCoherentProjectedSourceAmplitude(
      1.0, frequency, sourceSoundSpeed, sourceDepth,
      launchAngleRadians);
}

double semiCoherentProjectedSourceAmplitude(
    double baseAmplitude, double frequency, double sourceSoundSpeed,
    double sourceDepth, double launchAngleRadians) {
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
  const double angularFrequency =
      (2.0 * std::numbers::pi) * frequency;
  const double argument =
      (angularFrequency / sourceSoundSpeed) * sourceDepth *
      std::sin(launchAngleRadians);
  const double amplitude =
      baseAmplitude * static_cast<double>(std::sqrt(2.0F)) *
      std::abs(std::sin(argument));
  if (!std::isfinite(amplitude)) {
    throw ValidationError(
        "semi-coherent projected source amplitude is invalid");
  }
  return amplitude;
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

SingleFrequencyResult SingleFrequencySolver::solve(
    const SimulationCase& simulation,
    double epsilonMultiplier, double loopRange,
    CartesianCervenySettings influenceSettings,
    BeamWidthMode widthMode,
    BoundaryCurvatureMode curvatureMode,
    std::size_t influenceThreadCount) {
  if (!isTransmissionLossMode(simulation.runMode())) {
    throw ValidationError(
        "single-frequency field solver requires a transmission-loss run mode");
  }
  if (influenceThreadCount == 0U ||
      influenceThreadCount > kMaximumCartesianCervenyThreadCount) {
    throw ValidationError(
        "influence thread count must lie in [1, 256]");
  }
  if (influenceThreadCount > 1U &&
      (simulation.sourceCount() != 1U ||
       simulation.beamFamily() != BeamFamily::CervenyGaussian ||
       simulation.cervenyCoordinateSystem() !=
           CervenyCoordinateSystem::Cartesian)) {
    throw ValidationError(
        "inner influence parallelism requires one source and Cartesian "
        "Cerveny beams");
  }
  switch (simulation.beamFamily()) {
    case BeamFamily::CervenyGaussian:
      break;
    case BeamFamily::GeometricHat:
    case BeamFamily::GeometricGaussian:
    case BeamFamily::SimpleGaussian:
      if (curvatureMode != BoundaryCurvatureMode::Standard) {
        throw ValidationError(
            "non-Cerveny beams require the standard reflection "
            "curvature condition");
      }
      break;
    default:
      throw ValidationError("beam family is invalid");
  }
  const LaunchFanPlan& launchFan = simulation.launchFanPlan();
  const std::size_t rayCount = checkedMultiply(
      launchFan.launchAngleCount, simulation.sourceCount(), "ray count");
  if (rayCount > kMaximumRunRayCount) {
    throw ValidationError("ray count exceeds the supported run limit");
  }
  GeometryTracer tracer(simulation, curvatureMode);
  std::size_t totalRayPointCount = 0U;
  const double frequency =
      simulation.frequencies().values().front();
  const GeometrySspEvaluator soundSpeedProfile(
      simulation.environment().soundSpeedProfile());
  const FrequencyProjector projector(
      simulation.environment());
  std::optional<CartesianCervenyInfluence> cartesianInfluence;
  std::optional<RayCenteredCervenyInfluence> rayCenteredInfluence;
  std::optional<GeometricHatInfluence> geometricHatInfluence;
  std::optional<GeometricGaussianInfluence> geometricGaussianInfluence;
  std::optional<SimpleGaussianInfluence> simpleGaussianInfluence;
  if (simulation.beamFamily() == BeamFamily::GeometricHat) {
    geometricHatInfluence.emplace(
        simulation.receivers(), simulation.cervenyCoordinateSystem(),
        simulation.sourceGeometry(), simulation.runMode());
  } else if (simulation.beamFamily() == BeamFamily::GeometricGaussian) {
    geometricGaussianInfluence.emplace(
        simulation.receivers(), simulation.sourceGeometry(),
        simulation.runMode());
  } else if (simulation.beamFamily() == BeamFamily::SimpleGaussian) {
    simpleGaussianInfluence.emplace(
        simulation.receivers(), simulation.integrator().stepLength,
        simulation.cervenyCoordinateSystem(), simulation.sourceGeometry(),
        simulation.runMode());
  } else {
    switch (simulation.cervenyCoordinateSystem()) {
      case CervenyCoordinateSystem::Cartesian:
        cartesianInfluence.emplace(
            simulation.environment(), simulation.receivers(),
            influenceSettings, widthMode, simulation.sourceGeometry(),
            simulation.runMode(), influenceThreadCount);
        break;
      case CervenyCoordinateSystem::RayCentered:
        rayCenteredInfluence.emplace(
            simulation.environment(), simulation.receivers(),
            influenceSettings, widthMode, simulation.sourceGeometry(),
            simulation.runMode(), simulation.fieldComponent());
        break;
      default:
        throw ValidationError("Cerveny coordinate system is invalid");
    }
  }

  double traceSeconds = 0.0;
  double projectSeconds = 0.0;
  double influenceSeconds = 0.0;
  double scaleSeconds = 0.0;
  std::size_t peakRayCacheBytes = 0U;
  std::vector<FrequencyWorkspace> workspaces;
  workspaces.reserve(simulation.sourceCount());
  for (std::size_t sourceIndex = 0U;
       sourceIndex < simulation.sourceCount(); ++sourceIndex) {
    const Source& source = simulation.sources()[sourceIndex];
    RayPathCache rayCache;
    rayCache.reserve(launchFan.launchAngleCount);
    const Clock::time_point traceBegin = Clock::now();
    for (std::size_t launchIndex = 0U;
         launchIndex < launchFan.launchAngles.size(); ++launchIndex) {
      const double launchAngle = launchFan.launchAngles[launchIndex];
      RayPath path = tracer.trace(source, launchAngle);
      if (path.terminationReason != RayTerminationReason::ExitedDomain) {
        throw ValidationError(
            "single-frequency solve encountered a ray that did not "
            "exit the spatial domain normally (source index " +
            std::to_string(sourceIndex) + ", launch index " +
            std::to_string(launchIndex) + ", angle " +
            std::to_string(launchAngle) + ", reason " +
            std::to_string(static_cast<int>(path.terminationReason)) +
            (path.terminationDetail.empty()
                 ? ")"
                 : ", detail: " + path.terminationDetail + ")"));
      }
      totalRayPointCount = checkedAdd(
          totalRayPointCount, path.points.size(), "total ray point count");
      rayCache.append(std::move(path));
    }
    rayCache.freeze();
    const Clock::time_point traceEnd = Clock::now();
    traceSeconds += elapsedSeconds(traceBegin, traceEnd);
    peakRayCacheBytes =
        std::max(peakRayCacheBytes, rayCache.memoryFootprintBytes());

    const SoundSpeedSample sourceSample =
        soundSpeedProfile.evaluate(
            Vec2{.range = 0.0, .depth = source.depth}, 0U);
    const double sourceSoundSpeed = sourceSample.soundSpeed;
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
      default:
        throw ValidationError("field accumulation kind is invalid");
    }
    for (const RayPath& path : rayCache.paths()) {
      const Clock::time_point projectBegin = Clock::now();
      const double patternAmplitude =
          simulation.sourceBeamPattern().amplitudeForLaunchAngle(
              path.launchAngle);
      const double baseSourceAmplitude =
          source.amplitude * patternAmplitude;
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
      if (geometricHatInfluence.has_value()) {
        if (coherentWorkspace.has_value()) {
          static_cast<void>(geometricHatInfluence->accumulate(
              *coherentWorkspace, path, frequencyState,
              launchFan.launchAngleStep));
        } else {
          static_cast<void>(geometricHatInfluence->accumulateIntensity(
              *intensityWorkspace, path, frequencyState,
              launchFan.launchAngleStep));
        }
      } else if (geometricGaussianInfluence.has_value()) {
        if (coherentWorkspace.has_value()) {
          static_cast<void>(geometricGaussianInfluence->accumulate(
              *coherentWorkspace, path, frequencyState,
              launchFan.launchAngleStep));
        } else {
          static_cast<void>(
              geometricGaussianInfluence->accumulateIntensity(
                  *intensityWorkspace, path, frequencyState,
                  launchFan.launchAngleStep));
        }
      } else if (simpleGaussianInfluence.has_value()) {
        if (!coherentWorkspace.has_value()) {
          throw ValidationError(
              "simple Gaussian influence requires coherent pressure");
        }
        static_cast<void>(simpleGaussianInfluence->accumulate(
            *coherentWorkspace, path, frequencyState,
            launchFan.launchAngleStep));
      } else {
        const BeamEpsilon epsilon = pickBeamEpsilon(
            widthMode, frequency, sourceSoundSpeed,
            sourceSample.soundSpeedGradient.depth, path.launchAngle,
            launchFan.launchAngleStep, loopRange, epsilonMultiplier);
        if (coherentWorkspace.has_value() &&
            cartesianInfluence.has_value()) {
          static_cast<void>(cartesianInfluence->accumulate(
              *coherentWorkspace, path, frequencyState, epsilon.value));
        } else if (coherentWorkspace.has_value()) {
          static_cast<void>(rayCenteredInfluence->accumulate(
              *coherentWorkspace, path, frequencyState, epsilon.value));
        } else if (cartesianInfluence.has_value()) {
          static_cast<void>(cartesianInfluence->accumulateIntensity(
              *intensityWorkspace, path, frequencyState, epsilon.value));
        } else {
          static_cast<void>(rayCenteredInfluence->accumulateIntensity(
              *intensityWorkspace, path, frequencyState, epsilon.value));
        }
      }
      const Clock::time_point influenceEnd = Clock::now();
      projectSeconds += elapsedSeconds(projectBegin, projectEnd);
      influenceSeconds += elapsedSeconds(projectEnd, influenceEnd);
    }
    const Clock::time_point scaleBegin = Clock::now();
    FrequencyWorkspace workspace =
        coherentWorkspace.has_value()
            ? std::move(*coherentWorkspace)
            : scaleIntensityToPressure(
                  *intensityWorkspace, simulation.receivers(),
                  launchFan.launchAngleStep, sourceSoundSpeed,
                  simulation.sourceGeometry(),
                  simulation.beamFamily() == BeamFamily::CervenyGaussian
                      ? PressureNormalization::Cerveny
                      : PressureNormalization::Geometric);
    if (coherentWorkspace.has_value()) {
      scaleCoherentPressure(
          workspace, simulation.receivers(), launchFan.launchAngleStep,
          sourceSoundSpeed, simulation.sourceGeometry(),
          simulation.beamFamily() == BeamFamily::CervenyGaussian
              ? PressureNormalization::Cerveny
              : PressureNormalization::Geometric);
    }
    const Clock::time_point scaleEnd = Clock::now();
    scaleSeconds += elapsedSeconds(scaleBegin, scaleEnd);
    workspaces.push_back(std::move(workspace));
  }

  std::vector<FrequencyWorkspace> additionalSourceWorkspaces;
  additionalSourceWorkspaces.reserve(workspaces.size() - 1U);
  for (std::size_t index = 1U; index < workspaces.size(); ++index) {
    additionalSourceWorkspaces.push_back(std::move(workspaces[index]));
  }
  return SingleFrequencyResult{
      .workspace = std::move(workspaces.front()),
      .additionalSourceWorkspaces =
          std::move(additionalSourceWorkspaces),
      .rayCount = rayCount,
      .totalRayPointCount = totalRayPointCount,
      .rayCacheBytes = peakRayCacheBytes,
      .influenceThreadCount =
          cartesianInfluence.has_value()
              ? cartesianInfluence->threadCount()
              : 1U,
      .timings =
          SingleFrequencyTimings{
              .traceSeconds = traceSeconds,
              .projectSeconds = projectSeconds,
              .influenceSeconds = influenceSeconds,
              .scaleSeconds = scaleSeconds}};
}

}  // namespace bellhop
