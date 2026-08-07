#include "bellhop/field/frequency_projector.hpp"

#include <cmath>
#include <complex>
#include <cstddef>
#include <limits>
#include <utility>

#include "bellhop/acoustics/boundary_acoustics.hpp"
#include "bellhop/error.hpp"
#include "bellhop/model/c_linear_frequency_ssp.hpp"

namespace bellhop {
namespace {

constexpr double kLegacyActiveAmplitudeThreshold =
    static_cast<double>(0.005F);

void validateProjectionInput(const RayPath& path, double frequency,
                             double sourceAmplitude) {
  if (!std::isfinite(frequency) || frequency <= 0.0) {
    throw ValidationError("projection frequency must be finite and positive");
  }
  if (!std::isfinite(sourceAmplitude) || sourceAmplitude < 0.0) {
    throw ValidationError(
        "projection source amplitude must be finite and non-negative");
  }
  if (path.points.empty()) {
    throw ValidationError("projected ray path must contain at least one point");
  }
  if (path.steps.size() >
      std::numeric_limits<std::size_t>::max() -
          path.events.size()) {
    throw ValidationError(
        "projected ray path transition count overflows size_t");
  }
  if (path.points.size() - 1U !=
      path.steps.size() + path.events.size()) {
    throw ValidationError(
        "projected ray path requires one step or reflection event per "
        "point pair");
  }

  std::size_t previousIndex = 0U;
  bool havePrevious = false;
  for (const ReflectionEvent& event : path.events) {
    if (event.rayPointIndex >= path.points.size() - 1U) {
      throw ValidationError(
          "projection reflection event index is out of range");
    }
    if (havePrevious && event.rayPointIndex <= previousIndex) {
      throw ValidationError(
          "projection reflection events must be strictly ordered");
    }
    previousIndex = event.rayPointIndex;
    havePrevious = true;
  }
  if (!std::isfinite(path.points.front().realTravelTime)) {
    throw ValidationError(
        "projected source travel time must be finite");
  }
}

[[nodiscard]] bool amplitudeRemainsActive(double amplitude) {
  if (!std::isfinite(amplitude)) {
    throw ValidationError("projected amplitude must be finite");
  }
  return !(amplitude < kLegacyActiveAmplitudeThreshold);
}

[[nodiscard]] const BoundaryModel& boundaryForEvent(
    const Environment& environment,
    ReflectionBoundary boundary) {
  switch (boundary) {
    case ReflectionBoundary::SeaSurface:
      return environment.seaSurface();
    case ReflectionBoundary::Seabed:
      return environment.seabed();
  }
  throw ValidationError("projection reflection boundary is invalid");
}

void requireFiniteProjectionPoint(
    const RayFrequencyPoint& point) {
  if (!std::isfinite(point.complexTravelTime.real()) ||
      !std::isfinite(point.complexTravelTime.imag()) ||
      !std::isfinite(point.amplitude) ||
      !std::isfinite(point.reflectionPhase)) {
    throw ValidationError(
        "projected frequency point must contain only finite values");
  }
}

}  // namespace

FrequencyProjector::FrequencyProjector(Environment environment)
    : environment_(std::move(environment)) {}

RayFrequencyState FrequencyProjector::project(
    const RayPath& path, double frequency,
    double sourceAmplitude) const {
  validateProjectionInput(path, frequency, sourceAmplitude);

  CLinearFrequencySsp soundSpeedProfile(
      environment_.soundSpeedProfile(), frequency);
  const bool losslessProfile = soundSpeedProfile.isLossless();
  const std::optional<std::complex<double>> uniformComplexSoundSpeed =
      soundSpeedProfile.uniformComplexSoundSpeed();
  std::size_t segmentIndex = 0U;
  const SoundSpeedSample sourceSample =
      soundSpeedProfile.evaluate(path.points.front().position, segmentIndex);
  segmentIndex = sourceSample.segmentIndex;

  RayFrequencyState result{.frequency = frequency, .points = {}};
  result.points.reserve(path.points.size());
  result.points.push_back(
      RayFrequencyPoint{
          .complexTravelTime =
              {path.points.front().realTravelTime, 0.0},
          .amplitude = sourceAmplitude,
          .reflectionPhase = 0.0,
          .active = true});

  std::size_t stepIndex = 0U;
  std::size_t eventIndex = 0U;
  for (std::size_t edgeIndex = 0U;
       edgeIndex + 1U < path.points.size(); ++edgeIndex) {
    const RayFrequencyPoint& current = result.points.back();
    RayFrequencyPoint next = current;

    const bool isReflection =
        eventIndex < path.events.size() &&
        path.events[eventIndex].rayPointIndex == edgeIndex;
    if (isReflection) {
      const ReflectionEvent& event = path.events[eventIndex];
      const SoundSpeedSample waterSample =
          soundSpeedProfile.evaluate(event.position, segmentIndex);
      segmentIndex = waterSample.segmentIndex;
      const BoundaryModel& boundary =
          boundaryForEvent(environment_, event.boundary);
      const BoundaryAcousticsResult acoustics =
          evaluateBoundaryAcoustics(
              boundary, frequency, waterSample.density,
              event.tangentSlowness, event.normalSlowness);
      next.amplitude *= acoustics.amplitudeMultiplier;
      next.reflectionPhase += acoustics.phaseIncrement;
      next.active =
          current.active && amplitudeRemainsActive(next.amplitude);
      ++eventIndex;
    } else {
      if (stepIndex >= path.steps.size()) {
        throw ValidationError(
            "projection path transition is missing quadrature data");
      }
      const StepQuadrature& quadrature = path.steps[stepIndex];
      if (!std::isfinite(quadrature.startWeight) ||
          !std::isfinite(quadrature.midpointWeight) ||
          quadrature.startWeight < 0.0 ||
          quadrature.midpointWeight < 0.0) {
        throw ValidationError(
            "projection quadrature weights must be finite and non-negative");
      }
      if (losslessProfile) {
        next.complexTravelTime = {
            path.points[edgeIndex + 1U].realTravelTime, 0.0};
      } else if (uniformComplexSoundSpeed.has_value()) {
        next.complexTravelTime +=
            quadrature.startWeight /
                uniformComplexSoundSpeed.value() +
            quadrature.midpointWeight /
                uniformComplexSoundSpeed.value();
      } else {
        const SoundSpeedSample startSample =
            soundSpeedProfile.evaluate(
                path.points[edgeIndex].position, segmentIndex);
        const SoundSpeedSample midpointSample =
            soundSpeedProfile.evaluate(
                quadrature.midpoint, startSample.segmentIndex);
        next.complexTravelTime +=
            quadrature.startWeight /
                std::complex<double>{
                    startSample.soundSpeed,
                    startSample.imaginarySoundSpeed} +
            quadrature.midpointWeight /
                std::complex<double>{
                    midpointSample.soundSpeed,
                    midpointSample.imaginarySoundSpeed};
        const SoundSpeedSample endSample =
            soundSpeedProfile.evaluate(
                path.points[edgeIndex + 1U].position,
                startSample.segmentIndex);
        segmentIndex = endSample.segmentIndex;
      }
      next.active =
          current.active && amplitudeRemainsActive(next.amplitude);
      ++stepIndex;
    }
    requireFiniteProjectionPoint(next);
    result.points.push_back(next);
  }

  if (stepIndex != path.steps.size() ||
      eventIndex != path.events.size()) {
    throw ValidationError(
        "projection did not consume every step and reflection event");
  }
  return result;
}

}  // namespace bellhop
