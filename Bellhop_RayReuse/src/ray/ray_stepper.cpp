#include "rayreuse/ray/ray_stepper.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>

#include "rayreuse/error.hpp"
#include "rayreuse/ray/ray_equations.hpp"

namespace rayreuse {
namespace {

[[nodiscard]] bool finiteArray(const std::array<double, 2>& values) noexcept {
  return std::isfinite(values[0]) && std::isfinite(values[1]);
}

void validateState(const RayState& state, std::string_view name) {
  if (!isFinite(state.position) || !isFinite(state.slowness) ||
      !finiteArray(state.dynamicP) || !finiteArray(state.dynamicQ) ||
      !std::isfinite(state.soundSpeed) ||
      !std::isfinite(state.realTravelTime)) {
    throw ValidationError(std::string(name) +
                          " must contain only finite values");
  }
  if (state.soundSpeed <= 0.0) {
    throw ValidationError(std::string(name) + " sound speed must be positive");
  }
  if (state.realTravelTime < 0.0) {
    throw ValidationError(std::string(name) +
                          " travel time must be non-negative");
  }
}

[[nodiscard]] double applyLimiter(const StepLimiter& limiter,
                                  StepLimitPhase phase, Vec2 initialPosition,
                                  Vec2 unitTangent,
                                  std::size_t initialSegmentIndex,
                                  std::size_t initialRangeSegmentIndex,
                                  double nominalStepLength,
                                  double proposedStepLength) {
  if (!limiter) {
    return proposedStepLength;
  }

  const double limitedStep =
      limiter(StepLimitRequest{.phase = phase,
                               .initialPosition = initialPosition,
                               .unitTangent = unitTangent,
                               .initialSegmentIndex = initialSegmentIndex,
                               .initialRangeSegmentIndex = initialRangeSegmentIndex,
                               .nominalStepLength = nominalStepLength,
                               .proposedStepLength = proposedStepLength});
  if (!std::isfinite(limitedStep) || limitedStep <= 0.0) {
    throw ValidationError(
        "step limiter must return a finite, positive step length");
  }

  const double comparisonTolerance =
      16.0 * std::numeric_limits<double>::epsilon() *
      std::max({1.0, proposedStepLength, limitedStep});
  if (limitedStep > proposedStepLength + comparisonTolerance) {
    throw ValidationError(
        "step limiter cannot increase the proposed step length");
  }
  return std::min(limitedStep, proposedStepLength);
}

[[nodiscard]] RayState predictorState(const RayState& initialState,
                                      const SoundSpeedSample& initialSample,
                                      double halfStep) {
  RayState midpoint = initialState;
  const double soundSpeedSquared =
      initialSample.soundSpeed * initialSample.soundSpeed;
  const double curvature = soundSpeedNormalSecondDerivativeOverSquaredSpeed(
      initialSample.soundSpeedHessian, initialState.slowness);

  const Vec2 initialUnitTangent =
      initialSample.soundSpeed * initialState.slowness;
  midpoint.position = {.range = std::fma(halfStep, initialUnitTangent.range,
                                         initialState.position.range),
                       .depth = std::fma(halfStep, initialUnitTangent.depth,
                                         initialState.position.depth)};
  midpoint.slowness =
      initialState.slowness -
      (halfStep / soundSpeedSquared) * initialSample.soundSpeedGradient;
  for (std::size_t index = 0; index < midpoint.dynamicP.size(); ++index) {
    midpoint.dynamicP[index] =
        initialState.dynamicP[index] -
        halfStep * curvature * initialState.dynamicQ[index];
    midpoint.dynamicQ[index] =
        initialState.dynamicQ[index] +
        halfStep * initialSample.soundSpeed * initialState.dynamicP[index];
  }
  midpoint.realTravelTime =
      initialState.realTravelTime + halfStep / initialSample.soundSpeed;
  midpoint.soundSpeed = initialSample.soundSpeed;
  validateState(midpoint, "ray-step predictor state");
  return midpoint;
}

void applyGradientJump(RayState& endState, const SoundSpeedSample& initialSample,
                       const SoundSpeedSample& endSample) {
  const bool crossedDepthSegment =
      endSample.segmentIndex != initialSample.segmentIndex;
  const bool crossedRangeSegment =
      endSample.rangeSegmentIndex != initialSample.rangeSegmentIndex;
  if (!crossedDepthSegment && !crossedRangeSegment) {
    return;
  }

  const Vec2 gradientJump =
      endSample.soundSpeedGradient - initialSample.soundSpeedGradient;
  const Vec2 rayNormal{.range = -endState.slowness.depth,
                       .depth = endState.slowness.range};
  const double normalGradientJump = dot(gradientJump, rayNormal);
  const double tangentGradientJump = dot(gradientJump, endState.slowness);

  // Step.f90 gives a simultaneous depth/range crossing to the depth branch.
  const double incidenceTangent =
      crossedDepthSegment
          ? endState.slowness.range / endState.slowness.depth
          : -endState.slowness.depth / endState.slowness.range;
  const double jumpCorrection =
      incidenceTangent *
      (2.0 * normalGradientJump - incidenceTangent * tangentGradientJump) /
      endSample.soundSpeed;

  if (!std::isfinite(jumpCorrection)) {
    throw ValidationError(
        "SSP gradient jump produced a non-finite dynamic correction");
  }
  for (std::size_t index = 0; index < endState.dynamicP.size(); ++index) {
    endState.dynamicP[index] -= endState.dynamicQ[index] * jumpCorrection;
  }
}

// Range-independent template body retained for the concrete CLinearSsp fast
// path, whose evaluate() has no range-segment hint. Its StepLimitRequest
// always carries range segment zero.
template <typename SspType>
RayStepResult stepRayImpl(const SspType& soundSpeedProfile,
                          const RayState& initialState,
                          std::size_t initialSegmentIndex,
                          double nominalStepLength,
                          const StepLimiter& limiter) {
  validateState(initialState, "initial ray state");
  if (!std::isfinite(nominalStepLength) || nominalStepLength <= 0.0) {
    throw ValidationError(
        "nominal ray-step length must be finite and positive");
  }

  const SoundSpeedSample initialSample =
      soundSpeedProfile.evaluate(initialState.position, initialSegmentIndex);
  const Vec2 initialUnitTangent =
      initialSample.soundSpeed * initialState.slowness;

  const double predictorStep = applyLimiter(
      limiter, StepLimitPhase::InitialTangent, initialState.position,
      initialUnitTangent, initialSample.segmentIndex, 0U, nominalStepLength,
      nominalStepLength);
  const double halfPredictorStep = 0.5 * predictorStep;
  RayState midpoint =
      predictorState(initialState, initialSample, halfPredictorStep);

  const SoundSpeedSample midpointSample =
      soundSpeedProfile.evaluate(midpoint.position, initialSample.segmentIndex);
  midpoint.soundSpeed = midpointSample.soundSpeed;
  const Vec2 midpointUnitTangent =
      midpointSample.soundSpeed * midpoint.slowness;

  const double actualStep = applyLimiter(
      limiter, StepLimitPhase::PredictedMidpointTangent, initialState.position,
      midpointUnitTangent, initialSample.segmentIndex, 0U, nominalStepLength,
      predictorStep);

  const double midpointBlend = actualStep / (2.0 * halfPredictorStep);
  const double startBlend = 1.0 - midpointBlend;
  const double startWeight = actualStep * startBlend;
  const double midpointWeight = actualStep * midpointBlend;

  const double initialSoundSpeedSquared =
      initialSample.soundSpeed * initialSample.soundSpeed;
  const double midpointSoundSpeedSquared =
      midpointSample.soundSpeed * midpointSample.soundSpeed;
  const double initialCurvature =
      soundSpeedNormalSecondDerivativeOverSquaredSpeed(
          initialSample.soundSpeedHessian, initialState.slowness);
  const double midpointCurvature =
      soundSpeedNormalSecondDerivativeOverSquaredSpeed(
          midpointSample.soundSpeedHessian, midpoint.slowness);

  RayState endState = initialState;
  endState.position = {
      .range = std::fma(midpointWeight, midpointUnitTangent.range,
                        std::fma(startWeight, initialUnitTangent.range,
                                 initialState.position.range)),
      .depth = std::fma(midpointWeight, midpointUnitTangent.depth,
                        std::fma(startWeight, initialUnitTangent.depth,
                                 initialState.position.depth))};
  endState.slowness = initialState.slowness -
                      (startWeight / initialSoundSpeedSquared) *
                          initialSample.soundSpeedGradient -
                      (midpointWeight / midpointSoundSpeedSquared) *
                          midpointSample.soundSpeedGradient;
  endState.realTravelTime = initialState.realTravelTime +
                            startWeight / initialSample.soundSpeed +
                            midpointWeight / midpointSample.soundSpeed;

  for (std::size_t index = 0; index < endState.dynamicP.size(); ++index) {
    endState.dynamicP[index] =
        initialState.dynamicP[index] -
        startWeight * initialCurvature * initialState.dynamicQ[index] -
        midpointWeight * midpointCurvature * midpoint.dynamicQ[index];
    endState.dynamicQ[index] =
        initialState.dynamicQ[index] +
        startWeight * initialSample.soundSpeed * initialState.dynamicP[index] +
        midpointWeight * midpointSample.soundSpeed * midpoint.dynamicP[index];
  }

  const SoundSpeedSample endSample =
      soundSpeedProfile.evaluate(endState.position, initialSample.segmentIndex);
  endState.soundSpeed = endSample.soundSpeed;
  if (soundSpeedProfile.gradientContinuity() ==
      SspGradientContinuity::DiscontinuousAtNodes) {
    applyGradientJump(endState, initialSample, endSample);
  }
  validateState(endState, "ray-step result state");

  return RayStepResult{
      .endState = endState,
      .quadrature = StepQuadrature{.stepLength = actualStep,
                                   .startWeight = startWeight,
                                   .midpointWeight = midpointWeight,
                                   .midpoint = midpoint.position},
      .segmentIndex = endSample.segmentIndex};
}

}  // namespace

RayStepResult stepRay(const GeometrySspEvaluator& soundSpeedProfile,
                      const RayState& initialState,
                      std::size_t initialSegmentIndex,
                      std::size_t initialRangeSegmentIndex,
                      double nominalStepLength,
                      const StepLimiter& limiter) {
  validateState(initialState, "initial ray state");
  if (!std::isfinite(nominalStepLength) || nominalStepLength <= 0.0) {
    throw ValidationError(
        "nominal ray-step length must be finite and positive");
  }

  const SoundSpeedSample initialSample = soundSpeedProfile.evaluate(
      initialState.position, initialSegmentIndex, initialRangeSegmentIndex);
  const Vec2 initialUnitTangent =
      initialSample.soundSpeed * initialState.slowness;

  const double predictorStep = applyLimiter(
      limiter, StepLimitPhase::InitialTangent, initialState.position,
      initialUnitTangent, initialSample.segmentIndex,
      initialSample.rangeSegmentIndex, nominalStepLength, nominalStepLength);
  const double halfPredictorStep = 0.5 * predictorStep;
  RayState midpoint =
      predictorState(initialState, initialSample, halfPredictorStep);

  const SoundSpeedSample midpointSample = soundSpeedProfile.evaluate(
      midpoint.position, initialSample.segmentIndex,
      initialSample.rangeSegmentIndex);
  midpoint.soundSpeed = midpointSample.soundSpeed;
  const Vec2 midpointUnitTangent =
      midpointSample.soundSpeed * midpoint.slowness;

  const double actualStep = applyLimiter(
      limiter, StepLimitPhase::PredictedMidpointTangent, initialState.position,
      midpointUnitTangent, initialSample.segmentIndex,
      initialSample.rangeSegmentIndex, nominalStepLength, predictorStep);

  const double midpointBlend = actualStep / (2.0 * halfPredictorStep);
  const double startBlend = 1.0 - midpointBlend;
  const double startWeight = actualStep * startBlend;
  const double midpointWeight = actualStep * midpointBlend;

  const double initialSoundSpeedSquared =
      initialSample.soundSpeed * initialSample.soundSpeed;
  const double midpointSoundSpeedSquared =
      midpointSample.soundSpeed * midpointSample.soundSpeed;
  const double initialCurvature =
      soundSpeedNormalSecondDerivativeOverSquaredSpeed(
          initialSample.soundSpeedHessian, initialState.slowness);
  const double midpointCurvature =
      soundSpeedNormalSecondDerivativeOverSquaredSpeed(
          midpointSample.soundSpeedHessian, midpoint.slowness);

  RayState endState = initialState;
  endState.position = {
      .range =
          std::fma(midpointWeight, midpointUnitTangent.range,
                   std::fma(startWeight, initialUnitTangent.range,
                            initialState.position.range)),
      .depth =
          std::fma(midpointWeight, midpointUnitTangent.depth,
                   std::fma(startWeight, initialUnitTangent.depth,
                            initialState.position.depth))};
  endState.slowness =
      initialState.slowness -
      (startWeight / initialSoundSpeedSquared) *
          initialSample.soundSpeedGradient -
      (midpointWeight / midpointSoundSpeedSquared) *
          midpointSample.soundSpeedGradient;
  endState.realTravelTime =
      initialState.realTravelTime +
      startWeight / initialSample.soundSpeed +
      midpointWeight / midpointSample.soundSpeed;

  for (std::size_t index = 0; index < endState.dynamicP.size(); ++index) {
    endState.dynamicP[index] =
        initialState.dynamicP[index] -
        startWeight * initialCurvature * initialState.dynamicQ[index] -
        midpointWeight * midpointCurvature * midpoint.dynamicQ[index];
    endState.dynamicQ[index] =
        initialState.dynamicQ[index] +
        startWeight * initialSample.soundSpeed * initialState.dynamicP[index] +
        midpointWeight * midpointSample.soundSpeed * midpoint.dynamicP[index];
  }

  const SoundSpeedSample endSample = soundSpeedProfile.evaluate(
      endState.position, initialSample.segmentIndex,
      initialSample.rangeSegmentIndex);
  endState.soundSpeed = endSample.soundSpeed;
  if (soundSpeedProfile.gradientContinuity() ==
      SspGradientContinuity::DiscontinuousAtNodes) {
    applyGradientJump(endState, initialSample, endSample);
  }
  validateState(endState, "ray-step result state");

  return RayStepResult{
      .endState = endState,
      .quadrature =
          StepQuadrature{.stepLength = actualStep,
                         .startWeight = startWeight,
                         .midpointWeight = midpointWeight,
                         .midpoint = midpoint.position},
      .segmentIndex = endSample.segmentIndex,
      .rangeSegmentIndex = endSample.rangeSegmentIndex};
}

RayStepResult stepRay(const GeometrySspEvaluator& soundSpeedProfile,
                      const RayState& initialState,
                      std::size_t initialSegmentIndex, double nominalStepLength,
                      const StepLimiter& limiter) {
  return stepRay(soundSpeedProfile, initialState, initialSegmentIndex, 0U,
                 nominalStepLength, limiter);
}

RayStepResult stepRay(const CLinearSsp& soundSpeedProfile,
                      const RayState& initialState,
                      std::size_t initialSegmentIndex, double nominalStepLength,
                      const StepLimiter& limiter) {
  return stepRayImpl(soundSpeedProfile, initialState, initialSegmentIndex,
                     nominalStepLength, limiter);
}

}  // namespace rayreuse
