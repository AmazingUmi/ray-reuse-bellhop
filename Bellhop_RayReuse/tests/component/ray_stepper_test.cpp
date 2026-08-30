#include "rayreuse/ray/ray_stepper.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "rayreuse/error.hpp"
#include "rayreuse/model/c_linear_ssp.hpp"
#include "rayreuse/model/cubic_spline_frequency_ssp.hpp"
#include "rayreuse/model/cubic_spline_ssp.hpp"
#include "rayreuse/model/environment.hpp"
#include "rayreuse/model/sound_speed_evaluator.hpp"
#include "rayreuse/model/sound_speed_types.hpp"
#include "rayreuse/ray/ray_equations.hpp"
#include "support/test_harness.hpp"

namespace {

using rayreuse::CLinearSsp;
using rayreuse::CubicSplineFrequencySsp;
using rayreuse::CubicSplineSsp;
using rayreuse::GeometrySspEvaluator;
using rayreuse::QuadrilateralSspGrid;
using rayreuse::RayState;
using rayreuse::SharedQuadrilateralSspGrid;
using rayreuse::SoundSpeedHessian;
using rayreuse::SoundSpeedPoint;
using rayreuse::SoundSpeedProfile;
using rayreuse::SoundSpeedSample;
using rayreuse::SspGradientContinuity;
using rayreuse::SspInterpolationKind;
using rayreuse::StepLimitPhase;
using rayreuse::StepLimitRequest;
using rayreuse::stepRay;
using rayreuse::ValidationError;
using rayreuse::Vec2;
using rayreuse::test::Context;

constexpr double kPi = 3.141592653589793238462643383279502884;

SoundSpeedProfile makeConstantProfile() {
  return SoundSpeedProfile(
      {{.depth = 0.0, .soundSpeed = 1500.0, .density = 1000.0},
       {.depth = 1000.0, .soundSpeed = 1500.0, .density = 1000.0}});
}

SoundSpeedProfile makePiecewiseLinearProfile() {
  return SoundSpeedProfile(
      {{.depth = 0.0, .soundSpeed = 1480.0, .density = 1000.0},
       {.depth = 100.0, .soundSpeed = 1500.0, .density = 1000.0},
       {.depth = 300.0, .soundSpeed = 1460.0, .density = 1000.0}});
}

SoundSpeedProfile makeSlopedProfile() {
  return SoundSpeedProfile(
      {{.depth = 0.0, .soundSpeed = 1480.0, .density = 1000.0},
       {.depth = 1000.0, .soundSpeed = 1520.0, .density = 1000.0}});
}

RayState makeRayState(double angleRadians) {
  constexpr double soundSpeed = 1500.0;
  return RayState{
      .position = Vec2{.range = 10.0, .depth = 200.0},
      .slowness = Vec2{.range = std::cos(angleRadians) / soundSpeed,
                       .depth = std::sin(angleRadians) / soundSpeed},
      .dynamicP = {1.25, -0.5},
      .dynamicQ = {2.0, 4.0},
      .soundSpeed = soundSpeed,
      .realTravelTime = 0.75};
}

void testNormalCurvatureFormula(Context& context) {
  constexpr SoundSpeedHessian hessian{
      .rangeRange = 3.0, .rangeDepth = -2.0, .depthDepth = 5.0};
  constexpr Vec2 slowness{.range = 0.25, .depth = -0.5};
  constexpr double expected =
      3.0 * 0.25 - 2.0 * -2.0 * 0.25 * -0.5 + 5.0 * 0.0625;
  constexpr double actual =
      rayreuse::soundSpeedNormalSecondDerivativeOverSquaredSpeed(hessian,
                                                                 slowness);
  static_assert(actual == expected);
  context.checkNear(actual, expected, 0.0,
                    "c_nn/c^2 follows the Step.f90 component formula");
}

void testConstantSpeedAnalyticStep(Context& context) {
  const CLinearSsp ssp(makeConstantProfile());
  constexpr double angle = 37.0 * kPi / 180.0;
  constexpr double stepLength = 120.0;
  const RayState initial = makeRayState(angle);

  const auto result = stepRay(ssp, initial, 0U, stepLength);
  const Vec2 direction{.range = std::cos(angle), .depth = std::sin(angle)};

  context.checkNear(result.endState.position.range,
                    initial.position.range + stepLength * direction.range,
                    2.0e-14,
                    "constant-speed step has analytic range coordinate");
  context.checkNear(result.endState.position.depth,
                    initial.position.depth + stepLength * direction.depth,
                    2.0e-14,
                    "constant-speed step has analytic depth coordinate");
  context.checkNear(result.endState.slowness.range, initial.slowness.range, 0.0,
                    "constant-speed step preserves range slowness");
  context.checkNear(result.endState.slowness.depth, initial.slowness.depth, 0.0,
                    "constant-speed step preserves depth slowness");
  context.checkNear(result.endState.realTravelTime,
                    initial.realTravelTime + stepLength / 1500.0, 1.0e-15,
                    "constant-speed step has analytic real travel time");
  context.checkNear(result.endState.soundSpeed, 1500.0, 0.0,
                    "constant-speed step stores endpoint sound speed");
  context.check(result.segmentIndex == 0U,
                "constant profile retains its segment");

  for (std::size_t index = 0; index < initial.dynamicP.size(); ++index) {
    context.checkNear(result.endState.dynamicP[index], initial.dynamicP[index],
                      0.0, "zero Hessian preserves dynamic p");
    context.checkNear(
        result.endState.dynamicQ[index],
        initial.dynamicQ[index] + stepLength * 1500.0 * initial.dynamicP[index],
        1.0e-10, "constant-speed dynamic q has analytic solution");
  }

  context.checkNear(result.quadrature.stepLength, stepLength, 0.0,
                    "quadrature records actual full step");
  context.checkNear(result.quadrature.startWeight, 0.0, 0.0,
                    "unlimited box step has zero start weight");
  context.checkNear(result.quadrature.midpointWeight, stepLength, 0.0,
                    "unlimited box step uses midpoint weight");
  context.checkNear(result.quadrature.midpoint.range,
                    initial.position.range + 0.5 * stepLength * direction.range,
                    2.0e-14, "quadrature records predictor midpoint range");
  context.checkNear(result.quadrature.midpoint.depth,
                    initial.position.depth + 0.5 * stepLength * direction.depth,
                    2.0e-14, "quadrature records predictor midpoint depth");
}

void testSecondLimiterCanFurtherShortenStep(Context& context) {
  const CLinearSsp ssp(makeConstantProfile());
  const RayState initial = makeRayState(0.0);
  std::vector<StepLimitRequest> requests;
  const auto limiter = [&requests](const StepLimitRequest& request) {
    requests.push_back(request);
    return request.phase == StepLimitPhase::InitialTangent ? 8.0 : 3.0;
  };

  const auto result = stepRay(ssp, initial, 0U, 10.0, limiter);

  context.check(requests.size() == 2U, "step limiter is invoked twice");
  context.check(requests[0].phase == StepLimitPhase::InitialTangent,
                "first limiter call uses the initial tangent");
  context.checkNear(requests[0].proposedStepLength, 10.0, 0.0,
                    "first limiter sees nominal step");
  context.check(requests[1].phase == StepLimitPhase::PredictedMidpointTangent,
                "second limiter call uses predicted midpoint tangent");
  context.checkNear(requests[1].proposedStepLength, 8.0, 0.0,
                    "second limiter sees first reduced step");
  context.checkNear(result.quadrature.stepLength, 3.0, 0.0,
                    "second limiter sets actual step");
  context.checkNear(result.quadrature.startWeight, 1.875, 1.0e-15,
                    "modified-box start weight follows reduced h");
  context.checkNear(result.quadrature.midpointWeight, 1.125, 1.0e-15,
                    "modified-box midpoint weight follows reduced h");
  context.checkNear(result.quadrature.midpoint.range, 14.0, 1.0e-15,
                    "predictor midpoint remains based on first limited step");
  context.checkNear(result.endState.position.range, 13.0, 1.0e-15,
                    "blended constant-speed result uses actual step");
  context.checkNear(result.endState.realTravelTime,
                    initial.realTravelTime + 3.0 / 1500.0, 1.0e-15,
                    "limited step integrates actual travel time");
}

void testCLinearSegmentJump(Context& context) {
  const CLinearSsp ssp(makePiecewiseLinearProfile());
  RayState initial{
      .position = Vec2{.range = 0.0, .depth = 90.0},
      .slowness = Vec2{.range = 0.8 / 1498.0, .depth = 0.6 / 1498.0},
      .dynamicP = {1.0, 0.0},
      .dynamicQ = {0.0, 1.0},
      .soundSpeed = 1498.0,
      .realTravelTime = 0.0};

  const auto result = stepRay(ssp, initial, 0U, 30.0);
  context.check(result.segmentIndex == 1U,
                "an unreduced crossing returns the endpoint segment");

  const Vec2 gradientJump{.range = 0.0, .depth = -0.4};
  const Vec2 rayNormal{.range = -result.endState.slowness.depth,
                       .depth = result.endState.slowness.range};
  const double incidenceTangent =
      result.endState.slowness.range / result.endState.slowness.depth;
  const double correction =
      incidenceTangent *
      (2.0 * rayreuse::dot(gradientJump, rayNormal) -
       incidenceTangent *
           rayreuse::dot(gradientJump, result.endState.slowness)) /
      result.endState.soundSpeed;

  context.checkNear(
      result.endState.dynamicP[0],
      initial.dynamicP[0] - result.endState.dynamicQ[0] * correction, 1.0e-15,
      "C-linear crossing applies p jump to first fundamental pair");
  context.checkNear(
      result.endState.dynamicP[1],
      initial.dynamicP[1] - result.endState.dynamicQ[1] * correction, 1.0e-15,
      "C-linear crossing applies p jump to second fundamental pair");
}

// A single interior step over an N²-linear segment proves the nonzero
// d²c/dz² reaches both the predictor and the corrector dynamic equations.
// The C-linear baseline shares the same nodes, so any dynamic difference is
// attributable to the N² curvature rather than to the tabulated profile.
void testN2CurvatureEntersDynamicEquations(Context& context) {
  const SoundSpeedProfile profile = makeSlopedProfile();
  const CLinearSsp cLinear(profile);
  const GeometrySspEvaluator n2Linear(
      SoundSpeedProfile(profile.points(), SspInterpolationKind::N2Linear));
  constexpr double angle = 37.0 * kPi / 180.0;
  RayState initial{.position = Vec2{.range = 10.0, .depth = 200.0},
                   .slowness = Vec2{.range = std::cos(angle) / 1500.0,
                                    .depth = std::sin(angle) / 1500.0},
                   .dynamicP = {1.0, -1.0},
                   .dynamicQ = {0.0, 0.0},
                   .soundSpeed = 1500.0,
                   .realTravelTime = 0.75};

  const auto cResult = stepRay(cLinear, initial, 0U, 120.0);
  const auto n2Result = stepRay(n2Linear, initial, 0U, 120.0);
  context.check(cResult.segmentIndex == 0U && n2Result.segmentIndex == 0U,
                "interior N² step stays inside one segment without a jump");

  // Zero within-segment Hessian keeps the C-linear dynamic p bit-exact.
  context.checkNear(cResult.endState.dynamicP[0], 1.0, 0.0,
                    "C-linear baseline preserves the first dynamic p");
  context.checkNear(cResult.endState.dynamicP[1], -1.0, 0.0,
                    "C-linear baseline preserves the second dynamic p");

  // The N² curvature 3*(dc/dz)^2/c is strictly positive, and the predictor
  // grows dynamicQ with the sign of dynamicP, so p moves away from its
  // initial value in the -curvature*q direction for both fundamental pairs.
  context.check(n2Result.endState.dynamicP[0] < 1.0 - 1.0e-9,
                "N² curvature decreases the first dynamic p");
  context.check(n2Result.endState.dynamicP[1] > -1.0 + 1.0e-9,
                "N² curvature increases the second dynamic p");
  context.check(1.0 - n2Result.endState.dynamicP[0] > 1.0e-9 &&
                    n2Result.endState.dynamicP[1] + 1.0 > 1.0e-9,
                "N² dynamic p deviation from the C-linear baseline is "
                "observable, not rounding noise");

  // dynamicQ integrates c*p, so the curved N² speed profile also separates q
  // from the piecewise-linear baseline.
  context.check(std::abs(n2Result.endState.dynamicQ[0] -
                         cResult.endState.dynamicQ[0]) > 1.0,
                "N² dynamic q deviates from the C-linear baseline");
  context.check(std::abs(n2Result.endState.dynamicQ[1] -
                         cResult.endState.dynamicQ[1]) > 1.0,
                "N² second dynamic q deviates from the C-linear baseline");

  context.check(std::abs(n2Result.endState.soundSpeed -
                         cResult.endState.soundSpeed) > 1.0e-3,
                "N² endpoint samples a different sound speed than C-linear");
  context.check(std::abs(n2Result.endState.realTravelTime -
                         cResult.endState.realTravelTime) > 1.0e-12,
                "N² endpoint accumulates a different real travel time");
}

// Crossing the 100 m node under N²-linear interpolation must execute the same
// Origin-compatible gradient jump as C-linear. The control profile keeps the
// first two nodes and chooses the third so the N² slope of the second segment
// matches the first: the node is then slope-continuous and both profiles feed
// identical segment-0 samples to this step, so the integrated trajectory,
// dynamic q, and dynamic p are bit-identical. The only remaining difference
// is the jump correction each profile applies at the segment change.
void testN2SegmentCrossingAppliesGradientJump(Context& context) {
  const double n2First = 1.0 / (1480.0 * 1480.0);
  const double n2Second = 1.0 / (1500.0 * 1500.0);
  const SoundSpeedProfile jumpProfile(
      {{.depth = 0.0, .soundSpeed = 1480.0, .density = 1000.0},
       {.depth = 100.0, .soundSpeed = 1500.0, .density = 1000.0},
       {.depth = 300.0, .soundSpeed = 1460.0, .density = 1000.0}},
      SspInterpolationKind::N2Linear);
  const SoundSpeedProfile slopeContinuousProfile(
      {{.depth = 0.0, .soundSpeed = 1480.0, .density = 1000.0},
       {.depth = 100.0, .soundSpeed = 1500.0, .density = 1000.0},
       {.depth = 300.0,
        .soundSpeed = 1.0 / std::sqrt(n2Second + 2.0 * (n2Second - n2First)),
        .density = 1000.0}},
      SspInterpolationKind::N2Linear);
  const GeometrySspEvaluator jumpSsp(jumpProfile);
  const GeometrySspEvaluator slopeContinuousSsp(slopeContinuousProfile);

  RayState initial{
      .position = Vec2{.range = 0.0, .depth = 90.0},
      .slowness = Vec2{.range = 0.8 / 1498.0, .depth = 0.6 / 1498.0},
      .dynamicP = {1.0, 0.0},
      .dynamicQ = {0.0, 1.0},
      .soundSpeed = 1498.0,
      .realTravelTime = 0.0};

  const auto jumpResult = stepRay(jumpSsp, initial, 0U, 30.0);
  const auto continuousResult = stepRay(slopeContinuousSsp, initial, 0U, 30.0);
  context.check(jumpResult.segmentIndex == 1U,
                "N² unreduced crossing returns the endpoint segment");
  context.check(continuousResult.segmentIndex == 1U,
                "slope-continuous N² control still records the new segment");

  context.checkNear(jumpResult.endState.position.range,
                    continuousResult.endState.position.range, 0.0,
                    "shared segment-0 samples keep the crossing trajectory "
                    "bit-identical");
  context.checkNear(jumpResult.endState.position.depth,
                    continuousResult.endState.position.depth, 0.0,
                    "crossing trajectory depth is unaffected by the post-node "
                    "N² line");
  context.checkNear(jumpResult.endState.dynamicQ[0],
                    continuousResult.endState.dynamicQ[0], 0.0,
                    "gradient jump leaves the first dynamic q untouched");
  context.checkNear(jumpResult.endState.dynamicQ[1],
                    continuousResult.endState.dynamicQ[1], 0.0,
                    "gradient jump leaves the second dynamic q untouched");

  // Recompute each profile's correction from its own public arrival-side
  // samples, exactly as Step.f90's iSegz branch does.
  const auto jumpCorrection = [&](const GeometrySspEvaluator& ssp,
                                  const auto& result) {
    const rayreuse::SoundSpeedSample initialSample =
        ssp.evaluateAtSegment(initial.position, 0U);
    const rayreuse::SoundSpeedSample endSample =
        ssp.evaluate(result.endState.position, 0U);
    const Vec2 gradientJump =
        endSample.soundSpeedGradient - initialSample.soundSpeedGradient;
    const Vec2 rayNormal{.range = -result.endState.slowness.depth,
                         .depth = result.endState.slowness.range};
    const double incidenceTangent =
        result.endState.slowness.range / result.endState.slowness.depth;
    return incidenceTangent *
           (2.0 * rayreuse::dot(gradientJump, rayNormal) -
            incidenceTangent *
                rayreuse::dot(gradientJump, result.endState.slowness)) /
           endSample.soundSpeed;
  };
  const double correctionJump = jumpCorrection(jumpSsp, jumpResult);
  const double correctionContinuous =
      jumpCorrection(slopeContinuousSsp, continuousResult);
  context.check(std::abs(correctionJump) > 1.0e-8,
                "N² node discontinuity produces a material gradient jump");
  context.check(std::abs(correctionJump - correctionContinuous) > 1.0e-8,
                "the node discontinuity dominates the smooth within-step "
                "gradient evolution");

  for (std::size_t index = 0; index < initial.dynamicP.size(); ++index) {
    context.checkNear(
        jumpResult.endState.dynamicP[index],
        continuousResult.endState.dynamicP[index] -
            jumpResult.endState.dynamicQ[index] *
                (correctionJump - correctionContinuous),
        1.0e-10,
        "N² crossing applies the Origin-compatible p jump to dynamic pair " +
            std::to_string(index));
  }
  context.check(std::abs(jumpResult.endState.dynamicP[0] -
                         continuousResult.endState.dynamicP[0]) > 1.0e-6,
                "the N² node jump changes dynamic p by a material amount");
}

// The three interpolation kinds must remain numerically distinguishable over
// the same nodes, so an N² query can never silently fall back to C-linear or
// PCHIP semantics.
void testN2DiffersFromCLinearAndPchip(Context& context) {
  const SoundSpeedProfile profile = makePiecewiseLinearProfile();
  const GeometrySspEvaluator cLinear(
      SoundSpeedProfile(profile.points(), SspInterpolationKind::CLinear));
  const GeometrySspEvaluator pchip(
      SoundSpeedProfile(profile.points(), SspInterpolationKind::Pchip));
  const GeometrySspEvaluator n2Linear(
      SoundSpeedProfile(profile.points(), SspInterpolationKind::N2Linear));
  const RayState initial = makeRayState(37.0 * kPi / 180.0);

  const auto cResult = stepRay(cLinear, initial, 1U, 120.0);
  const auto pResult = stepRay(pchip, initial, 1U, 120.0);
  const auto n2Result = stepRay(n2Linear, initial, 1U, 120.0);
  context.check(cResult.segmentIndex == 1U && pResult.segmentIndex == 1U &&
                    n2Result.segmentIndex == 1U,
                "all three kinds integrate within one segment without a jump");

  context.check(std::abs(n2Result.endState.dynamicP[0] -
                         cResult.endState.dynamicP[0]) > 1.0e-9,
                "N² dynamic p differs from the C-linear result");
  context.check(std::abs(n2Result.endState.dynamicP[0] -
                         pResult.endState.dynamicP[0]) > 1.0e-9,
                "N² dynamic p differs from the PCHIP result");
  context.check(std::abs(pResult.endState.dynamicP[0] -
                         cResult.endState.dynamicP[0]) > 1.0e-9,
                "the PCHIP and C-linear baselines differ from each other");
  context.check(std::abs(n2Result.endState.soundSpeed -
                         cResult.endState.soundSpeed) > 1.0e-6,
                "N² endpoint sound speed differs from C-linear");
  context.check(std::abs(n2Result.endState.soundSpeed -
                         pResult.endState.soundSpeed) > 1.0e-6,
                "N² endpoint sound speed differs from PCHIP");
  context.check(std::abs(n2Result.endState.realTravelTime -
                         cResult.endState.realTravelTime) > 1.0e-12,
                "N² travel time differs from C-linear");
}

// A real GeometrySspEvaluator spline step crosses the 100 m node. Rebuilding
// the predictor/corrector dynamic update without a node jump pins the
// ContinuousAtNodes contract at the production stepRay boundary. Although a
// spline derivative is continuous exactly at the node, the derivative evolves
// across this finite step; accidentally routing it through applyGradientJump
// would mistake that smooth evolution for a jump and materially change p.
void testSplineGradientContinuityAvoidsNodeJump(Context& context) {
  static_assert(CubicSplineSsp::gradientContinuity() ==
                SspGradientContinuity::ContinuousAtNodes);
  static_assert(CubicSplineFrequencySsp::gradientContinuity() ==
                SspGradientContinuity::ContinuousAtNodes);
  context.check(CubicSplineSsp::gradientContinuity() ==
                        SspGradientContinuity::ContinuousAtNodes &&
                    CubicSplineFrequencySsp::gradientContinuity() ==
                        SspGradientContinuity::ContinuousAtNodes,
                "spline selects the ContinuousAtNodes branch that never calls "
                "applyGradientJump");

  const SoundSpeedProfile profile = makePiecewiseLinearProfile();
  const GeometrySspEvaluator spline(
      SoundSpeedProfile(profile.points(), SspInterpolationKind::CubicSpline));
  context.check(
      spline.gradientContinuity() == SspGradientContinuity::ContinuousAtNodes,
      "runtime spline evaluator reports continuous node gradients");

  const Vec2 node{.range = 0.0, .depth = 100.0};
  const auto splineBelow = spline.evaluateAtSegment(node, 0U);
  const auto splineAbove = spline.evaluateAtSegment(node, 1U);
  context.check(std::abs(splineBelow.soundSpeedGradient.depth -
                         splineAbove.soundSpeedGradient.depth) < 1.0e-9,
                "spline gradient is continuous across the node");

  constexpr double stepLength = 30.0;
  RayState initial{
      .position = Vec2{.range = 0.0, .depth = 90.0},
      .slowness = Vec2{.range = 0.8 / 1498.0, .depth = 0.6 / 1498.0},
      .dynamicP = {1.0, 0.25},
      .dynamicQ = {0.5, 1.0},
      .soundSpeed = 1498.0,
      .realTravelTime = 0.0};
  const SoundSpeedSample initialSample = spline.evaluate(initial.position, 0U);
  initial.soundSpeed = initialSample.soundSpeed;

  const double halfStep = 0.5 * stepLength;
  RayState midpoint = initial;
  midpoint.position =
      initial.position + halfStep * initialSample.soundSpeed * initial.slowness;
  midpoint.slowness =
      initial.slowness -
      (halfStep / (initialSample.soundSpeed * initialSample.soundSpeed)) *
          initialSample.soundSpeedGradient;
  const double initialCurvature =
      rayreuse::soundSpeedNormalSecondDerivativeOverSquaredSpeed(
          initialSample.soundSpeedHessian, initial.slowness);
  for (std::size_t index = 0; index < midpoint.dynamicP.size(); ++index) {
    midpoint.dynamicP[index] =
        initial.dynamicP[index] -
        halfStep * initialCurvature * initial.dynamicQ[index];
    midpoint.dynamicQ[index] =
        initial.dynamicQ[index] +
        halfStep * initialSample.soundSpeed * initial.dynamicP[index];
  }
  const SoundSpeedSample midpointSample =
      spline.evaluate(midpoint.position, initialSample.segmentIndex);
  const double midpointCurvature =
      rayreuse::soundSpeedNormalSecondDerivativeOverSquaredSpeed(
          midpointSample.soundSpeedHessian, midpoint.slowness);

  std::array<double, 2> expectedP{};
  std::array<double, 2> expectedQ{};
  for (std::size_t index = 0; index < expectedP.size(); ++index) {
    expectedP[index] = initial.dynamicP[index] - stepLength *
                                                     midpointCurvature *
                                                     midpoint.dynamicQ[index];
    expectedQ[index] = initial.dynamicQ[index] + stepLength *
                                                     midpointSample.soundSpeed *
                                                     midpoint.dynamicP[index];
  }

  const auto result = stepRay(spline, initial, 0U, stepLength);
  context.check(
      result.segmentIndex == 1U && result.endState.position.depth > node.depth,
      "production spline step crosses the interior SSP node");
  for (std::size_t index = 0; index < expectedP.size(); ++index) {
    context.checkNear(result.endState.dynamicP[index], expectedP[index],
                      1.0e-14,
                      "continuous spline crossing preserves the no-jump p "
                      "update for dynamic pair " +
                          std::to_string(index));
    context.checkNear(result.endState.dynamicQ[index], expectedQ[index],
                      1.0e-10,
                      "continuous spline crossing preserves the smooth q "
                      "update for dynamic pair " +
                          std::to_string(index));
  }

  const SoundSpeedSample endSample =
      spline.evaluate(result.endState.position, initialSample.segmentIndex);
  const Vec2 gradientDifference =
      endSample.soundSpeedGradient - initialSample.soundSpeedGradient;
  const Vec2 rayNormal{.range = -result.endState.slowness.depth,
                       .depth = result.endState.slowness.range};
  const double incidenceTangent =
      result.endState.slowness.range / result.endState.slowness.depth;
  const double erroneousJumpCorrection =
      incidenceTangent *
      (2.0 * rayreuse::dot(gradientDifference, rayNormal) -
       incidenceTangent *
           rayreuse::dot(gradientDifference, result.endState.slowness)) /
      endSample.soundSpeed;
  context.check(
      std::abs(result.endState.dynamicQ[1] * erroneousJumpCorrection) > 1.0e-6,
      "routing a smooth spline crossing through applyGradientJump "
      "would cause a detectable dynamic-p regression");
}

// The dynamic-ray equations consume c_nn/c² built from the sample Hessian;
// the spline must therefore ship a materially nonzero d²c/dz² so the p
// update -w * curvature * q actually acts. Zeroing the spline Hessian (the
// C-linear value on the same nodes) collapses the term to exactly zero,
// which is the observable difference a Hessian-suppressing shortcut would
// leave behind. The full stepRay integration through the spline backend is
// covered by testSplineCurvatureEntersDynamicEquations below.
void testSplineHessianFeedsDynamicCurvature(Context& context) {
  const CubicSplineSsp spline(makePiecewiseLinearProfile());
  const CLinearSsp cLinear(makePiecewiseLinearProfile());
  const Vec2 slowness{.range = 0.8 / 1498.0, .depth = 0.6 / 1498.0};

  const SoundSpeedSample splineSample =
      spline.evaluateAtSegment(Vec2{.range = 0.0, .depth = 50.0}, 0U);
  const SoundSpeedSample cSample =
      cLinear.evaluateAtSegment(Vec2{.range = 0.0, .depth = 50.0}, 0U);

  context.check(std::abs(splineSample.soundSpeedHessian.depthDepth) > 1.0e-9,
                "spline sample carries a nonzero depth Hessian");
  context.check(cSample.soundSpeedHessian == SoundSpeedHessian{},
                "C-linear baseline ships an all-zero Hessian");

  const double splineCurvature =
      rayreuse::soundSpeedNormalSecondDerivativeOverSquaredSpeed(
          splineSample.soundSpeedHessian, slowness);
  const double zeroedCurvature =
      rayreuse::soundSpeedNormalSecondDerivativeOverSquaredSpeed(
          SoundSpeedHessian{}, slowness);
  const double cCurvature =
      rayreuse::soundSpeedNormalSecondDerivativeOverSquaredSpeed(
          cSample.soundSpeedHessian, slowness);

  context.check(std::abs(splineCurvature) > 1.0e-12,
                "spline Hessian yields a materially nonzero dynamic "
                "curvature term");
  context.check(zeroedCurvature == 0.0 && cCurvature == 0.0,
                "a zeroed Hessian contributes exactly zero, so the spline "
                "curvature genuinely enters the p/q update");
}

// End-to-end spline leg of testN2CurvatureEntersDynamicEquations: a single
// interior step integrated through the GeometrySspEvaluator CubicSpline
// backend must let the nonzero spline Hessian act on the dynamic p/q
// equations, while the C-linear baseline on identical nodes stays bit-exact
// because its within-segment Hessian is zero. Any collapse of the spline
// Hessian (a Hessian-suppressing shortcut) would make these checks fail by
// reproducing the baseline exactly.
void testSplineCurvatureEntersDynamicEquations(Context& context) {
  const SoundSpeedProfile profile = makePiecewiseLinearProfile();
  const CLinearSsp cLinear(profile);
  const GeometrySspEvaluator spline(
      SoundSpeedProfile(profile.points(), SspInterpolationKind::CubicSpline));
  constexpr double angle = 37.0 * kPi / 180.0;
  RayState initial{.position = Vec2{.range = 10.0, .depth = 50.0},
                   .slowness = Vec2{.range = std::cos(angle) / 1490.0,
                                    .depth = std::sin(angle) / 1490.0},
                   .dynamicP = {1.0, -1.0},
                   .dynamicQ = {0.0, 0.0},
                   .soundSpeed = 1490.0,
                   .realTravelTime = 0.75};

  const auto cResult = stepRay(cLinear, initial, 0U, 30.0);
  const auto splineResult = stepRay(spline, initial, 0U, 30.0);
  context.check(cResult.segmentIndex == 0U && splineResult.segmentIndex == 0U,
                "interior spline step stays inside one segment without a jump");

  // Zero within-segment Hessian keeps the C-linear dynamic p bit-exact.
  context.checkNear(cResult.endState.dynamicP[0], 1.0, 0.0,
                    "C-linear baseline preserves the first dynamic p");
  context.checkNear(cResult.endState.dynamicP[1], -1.0, 0.0,
                    "C-linear baseline preserves the second dynamic p");

  // The spline curvature is nonzero inside the segment (pinned by
  // testSplineHessianFeedsDynamicCurvature on this profile), so dynamic p
  // moves away from its initial value for both fundamental pairs and the
  // deviation is far above rounding noise.
  context.check(std::abs(splineResult.endState.dynamicP[0] - 1.0) > 1.0e-9,
                "spline curvature changes the first dynamic p");
  context.check(std::abs(splineResult.endState.dynamicP[1] + 1.0) > 1.0e-9,
                "spline curvature changes the second dynamic p");
  context.check(std::abs(splineResult.endState.dynamicP[0] -
                         cResult.endState.dynamicP[0]) > 1.0e-9,
                "spline dynamic p deviates from the C-linear baseline "
                "observably");

  // dynamicQ integrates c*p, so the curved spline speed profile also
  // separates q from the piecewise-linear baseline.
  context.check(std::abs(splineResult.endState.dynamicQ[0] -
                         cResult.endState.dynamicQ[0]) > 1.0e-9,
                "spline dynamic q deviates from the C-linear baseline");
  context.check(std::abs(splineResult.endState.dynamicQ[1] -
                         cResult.endState.dynamicQ[1]) > 1.0e-9,
                "spline second dynamic q deviates from the C-linear baseline");

  context.check(std::abs(splineResult.endState.soundSpeed -
                         cResult.endState.soundSpeed) > 1.0e-6,
                "spline endpoint samples a different sound speed than "
                "C-linear");
  context.check(std::abs(splineResult.endState.realTravelTime -
                         cResult.endState.realTravelTime) > 1.0e-12,
                "spline endpoint accumulates a different real travel time");
}

void testValidation(Context& context) {
  const CLinearSsp ssp(makeConstantProfile());
  const RayState valid = makeRayState(0.0);

  context.expectThrows<ValidationError>(
      [&ssp, &valid] { static_cast<void>(stepRay(ssp, valid, 0U, 0.0)); },
      "zero nominal step is rejected");
  context.expectThrows<ValidationError>(
      [&ssp, &valid] {
        static_cast<void>(
            stepRay(ssp, valid, 0U, std::numeric_limits<double>::quiet_NaN()));
      },
      "non-finite nominal step is rejected");

  RayState invalid = valid;
  invalid.dynamicQ[1] = std::numeric_limits<double>::infinity();
  context.expectThrows<ValidationError>(
      [&ssp, &invalid] { static_cast<void>(stepRay(ssp, invalid, 0U, 1.0)); },
      "non-finite dynamic state is rejected");

  invalid = valid;
  invalid.soundSpeed = 0.0;
  context.expectThrows<ValidationError>(
      [&ssp, &invalid] { static_cast<void>(stepRay(ssp, invalid, 0U, 1.0)); },
      "non-positive state sound speed is rejected");

  context.expectThrows<ValidationError>(
      [&ssp, &valid] {
        static_cast<void>(
            stepRay(ssp, valid, 0U, 1.0, [](const StepLimitRequest&) {
              return std::numeric_limits<double>::infinity();
            }));
      },
      "non-finite limiter result is rejected");
  context.expectThrows<ValidationError>(
      [&ssp, &valid] {
        static_cast<void>(stepRay(ssp, valid, 0U, 1.0,
                                  [](const StepLimitRequest&) { return 2.0; }));
      },
      "a limiter cannot increase its proposed step");
}

SoundSpeedProfile makeQuadrilateralProfile(std::vector<SoundSpeedPoint> points,
                                           std::vector<double> ranges,
                                           std::vector<double> speedsDepthMajor,
                                           std::size_t depthCount,
                                           std::size_t rangeCount) {
  return SoundSpeedProfile(
      std::move(points), SspInterpolationKind::Quadrilateral,
      std::make_shared<const QuadrilateralSspGrid>(
          QuadrilateralSspGrid{.rangesMeters = std::move(ranges),
                               .speedsDepthMajor = std::move(speedsDepthMajor),
                               .depthCount = depthCount,
                               .rangeCount = rangeCount}));
}

// One internal depth node at 100 m and a single range cell, so a downward
// crossing changes the depth segment only. Every cell keeps one constant depth
// slope across its range columns (crz = 0), so the integrated dynamic p stays
// bit-exactly at its initial value and the entire p change is the depth jump.
SoundSpeedProfile makeQDepthJumpProfile() {
  return makeQuadrilateralProfile(
      {{.depth = 0.0, .soundSpeed = 1500.0, .density = 1000.0},
       {.depth = 100.0, .soundSpeed = 1505.0, .density = 1000.0},
       {.depth = 200.0, .soundSpeed = 1515.0, .density = 1000.0}},
      {0.0, 1000.0}, {1500.0, 1540.0, 1505.0, 1545.0, 1515.0, 1555.0}, 3U, 2U);
}

// An internal depth node at 100 m and an internal range node at 350 m. Each
// matrix row continues the previous row by a constant offset (5 then 10 m/s),
// so every cell has one depth slope across its range columns (crz = 0) while
// the one-sided depth and range gradients both jump at the corner (350, 100).
SoundSpeedProfile makeQCornerProfile() {
  return makeQuadrilateralProfile(
      {{.depth = 0.0, .soundSpeed = 1500.0, .density = 1000.0},
       {.depth = 100.0, .soundSpeed = 1505.0, .density = 1000.0},
       {.depth = 200.0, .soundSpeed = 1515.0, .density = 1000.0}},
      {0.0, 350.0, 800.0},
      {1500.0, 1540.0, 1560.0, 1505.0, 1545.0, 1565.0, 1515.0, 1555.0, 1575.0},
      3U, 3U);
}

// Dyadic grid (range nodes at 256 m and 512 m, uniform sound speed 1024 m/s).
// With a slowness of (2^-11, 2^-12) the unit tangent is exactly (0.5, 0.25),
// so one unlimited step of 256 m from range 128 lands on the node 256 with
// zero rounding: exact-node ownership can be asserted without tolerance.
SoundSpeedProfile makeQDyadicProfile() {
  return makeQuadrilateralProfile(
      {{.depth = 0.0, .soundSpeed = 1024.0, .density = 1000.0},
       {.depth = 200.0, .soundSpeed = 1024.0, .density = 1000.0}},
      {0.0, 256.0, 512.0}, {1024.0, 1024.0, 1024.0, 1024.0, 1024.0, 1024.0}, 2U,
      3U);
}

// Cross-gradient grid with a deliberately steep third column, so the dc/dr
// jump at the 350 m node is large enough to yield a material dynamic-p
// correction in a single 30 m crossing step.
SoundSpeedProfile makeQCrossGradientProfile() {
  return makeQuadrilateralProfile(
      {{.depth = 0.0, .soundSpeed = 1490.0, .density = 1000.0},
       {.depth = 100.0, .soundSpeed = 1490.0, .density = 1100.0}},
      {0.0, 350.0, 800.0}, {1500.0, 1540.0, 1660.0, 1500.0, 1520.0, 1560.0}, 2U,
      3U);
}

RayState makeDiagonalRayState(double range, double depth) {
  return RayState{
      .position = Vec2{.range = range, .depth = depth},
      .slowness = Vec2{.range = 0.8 / 1498.0, .depth = 0.6 / 1498.0},
      .dynamicP = {1.0, 0.25},
      .dynamicQ = {0.5, 1.0},
      .soundSpeed = 1498.0,
      .realTravelTime = 0.0};
}

// Recomputes the Step.f90 jump correction from public evaluator samples,
// exactly as applyGradientJump forms it, for either crossing branch.
double jumpCorrection(const GeometrySspEvaluator& ssp, const RayState& initial,
                      const RayState& end, bool depthBranch) {
  const SoundSpeedSample initialSample = ssp.evaluate(initial.position, 0U, 0U);
  const SoundSpeedSample endSample = ssp.evaluate(end.position, 0U, 0U);
  const Vec2 gradientJump =
      endSample.soundSpeedGradient - initialSample.soundSpeedGradient;
  const Vec2 rayNormal{.range = -end.slowness.depth,
                       .depth = end.slowness.range};
  const double normalGradientJump = rayreuse::dot(gradientJump, rayNormal);
  const double tangentGradientJump = rayreuse::dot(gradientJump, end.slowness);
  const double incidenceTangent =
      depthBranch ? end.slowness.range / end.slowness.depth
                  : -end.slowness.depth / end.slowness.range;
  return incidenceTangent *
         (2.0 * normalGradientJump - incidenceTangent * tangentGradientJump) /
         endSample.soundSpeed;
}

// A Q step across the internal depth node at 100 m keeps the depth-segment
// hint machinery of the existing C/N2 semantics: the result segment advances,
// the range segment stays zero, and the p update is exactly the depth-branch
// jump recomputed from public samples.
void testQCrossDepthSegmentKeepsDepthJumpSemantics(Context& context) {
  const GeometrySspEvaluator ssp(makeQDepthJumpProfile());
  const RayState initial = makeDiagonalRayState(100.0, 90.0);

  const auto result = stepRay(ssp, initial, 0U, 30.0);
  context.check(result.segmentIndex == 1U,
                "Q depth crossing advances the depth segment hint");
  context.check(result.rangeSegmentIndex == 0U,
                "Q depth crossing leaves the single range segment at zero");
  context.check(result.endState.position.depth > 100.0,
                "Q depth crossing lands below the internal depth node");

  const double correction = jumpCorrection(ssp, initial, result.endState, true);
  context.check(std::abs(correction) > 1.0e-8,
                "the Q depth node carries a material gradient jump");
  for (std::size_t index = 0; index < initial.dynamicP.size(); ++index) {
    context.checkNear(
        result.endState.dynamicP[index],
        initial.dynamicP[index] - result.endState.dynamicQ[index] * correction,
        1.0e-15,
        "Q depth crossing applies the iSegz jump to dynamic pair " +
            std::to_string(index));
  }
}

// One step across the corner (350 m, 100 m) changes both segment indices at
// once. Step.f90 hands a simultaneous crossing to the depth branch with a
// single jump: the result must match the depth-branch correction alone and
// stay materially away from the range-branch or double-correction values.
void testQCornerCrossingPrefersDepthBranchSingleJump(Context& context) {
  const GeometrySspEvaluator ssp(makeQCornerProfile());
  const RayState initial = makeDiagonalRayState(340.0, 90.0);

  const auto result = stepRay(ssp, initial, 0U, 30.0);
  context.check(result.segmentIndex == 1U && result.rangeSegmentIndex == 1U,
                "the corner step crosses both the depth and the range node");
  context.check(result.endState.position.range > 350.0 &&
                    result.endState.position.depth > 100.0,
                "the corner step lands beyond the corner node");

  const double depthCorrection =
      jumpCorrection(ssp, initial, result.endState, true);
  const double rangeCorrection =
      jumpCorrection(ssp, initial, result.endState, false);
  context.check(std::abs(depthCorrection - rangeCorrection) > 1.0e-8,
                "the two corner branches produce materially different "
                "corrections");
  for (std::size_t index = 0; index < initial.dynamicP.size(); ++index) {
    context.checkNear(
        result.endState.dynamicP[index],
        initial.dynamicP[index] -
            result.endState.dynamicQ[index] * depthCorrection,
        1.0e-15,
        "corner crossing applies only the depth-branch jump to pair " +
            std::to_string(index));
    context.check(
        std::abs(result.endState.dynamicP[index] -
                 (initial.dynamicP[index] -
                  result.endState.dynamicQ[index] * rangeCorrection)) > 1.0e-9,
        "corner crossing does not use the range-branch correction on pair " +
            std::to_string(index));
    context.check(
        std::abs(result.endState.dynamicP[index] -
                 (initial.dynamicP[index] -
                  result.endState.dynamicQ[index] *
                      (depthCorrection + rangeCorrection))) > 1.0e-9,
        "corner crossing does not stack a second range correction on pair " +
            std::to_string(index));
  }
}

// Exact range-node landing through the production step: on the dyadic grid an
// unlimited step lands bit-exactly on 256 m, which the left-open/right-open
// cell rule assigns to the right-hand cell. A hinted right cell at the node is
// retained, a leftward step relocates to the left cell, and the limiter sees
// the sample's relocated range hint rather than the raw caller hint.
void testQExactRangeNodeLandingAndHintRetention(Context& context) {
  const GeometrySspEvaluator ssp(makeQDyadicProfile());
  const RayState landing{
      .position = Vec2{.range = 128.0, .depth = 100.0},
      .slowness = Vec2{.range = 0.00048828125, .depth = 0.000244140625},
      .dynamicP = {1.0, 0.25},
      .dynamicQ = {0.5, 1.0},
      .soundSpeed = 1024.0,
      .realTravelTime = 0.0};

  const auto result = stepRay(ssp, landing, 0U, 0U, 256.0);
  context.checkNear(result.endState.position.range, 256.0, 0.0,
                    "dyadic landing reaches the range node exactly");
  context.check(result.rangeSegmentIndex == 1U,
                "an exact range node belongs to the right-hand cell");
  context.check(result.segmentIndex == 0U,
                "the dyadic landing stays inside its depth segment");

  std::vector<StepLimitRequest> requests;
  const auto recordingLimiter = [&requests](const StepLimitRequest& request) {
    requests.push_back(request);
    return request.proposedStepLength;
  };
  RayState atNode = landing;
  atNode.position = Vec2{.range = 256.0, .depth = 100.0};
  const auto retained = stepRay(ssp, atNode, 0U, 1U, 64.0, recordingLimiter);
  context.check(retained.rangeSegmentIndex == 1U,
                "a hinted right cell at the node is retained");
  context.checkNear(retained.endState.position.range, 288.0, 0.0,
                    "the retained-cell step advances into the right cell");
  context.check(requests.size() == 2U &&
                    requests[0].initialRangeSegmentIndex == 1U &&
                    requests[1].initialRangeSegmentIndex == 1U,
                "the limiter observes the sample's retained range hint");

  RayState leftward = atNode;
  leftward.slowness = Vec2{.range = -0.00048828125, .depth = 0.000244140625};
  const auto relocated = stepRay(ssp, leftward, 0U, 1U, 64.0);
  context.checkNear(relocated.endState.position.range, 224.0, 0.0,
                    "the leftward step leaves the node exactly");
  context.check(relocated.rangeSegmentIndex == 0U,
                "a query left of its hinted cell relocates to the left cell");
}

// The stepper must reject a Q step whose endpoint leaves the range grid, both
// ahead of the back node and before the front node, instead of extrapolating.
void testQOutsideGridRangeRejected(Context& context) {
  const GeometrySspEvaluator ssp(makeQCrossGradientProfile());
  RayState ahead = makeDiagonalRayState(799.9, 50.0);
  ahead.slowness = Vec2{.range = 0.0001, .depth = 0.0};
  ahead.soundSpeed = 1560.0;
  context.expectThrows<ValidationError>(
      [&ssp, &ahead] { static_cast<void>(stepRay(ssp, ahead, 0U, 1U, 1.0)); },
      "a Q step ending beyond the grid back is rejected");

  RayState before = makeDiagonalRayState(-1.0, 50.0);
  context.expectThrows<ValidationError>(
      [&ssp, &before] { static_cast<void>(stepRay(ssp, before, 0U, 0U, 1.0)); },
      "a Q step starting before the grid front is rejected");
}

// A crossing of the 350 m range node under the cross-gradient profile must
// actually fire the range-branch jump in dynamic p. The whole modified-box
// integration is rebuilt from public evaluator samples (the predictor keeps
// this step inside the left cell, and an unlimited step has start weight
// zero), so the expected p is the hand-integrated no-jump update followed by
// exactly one -tz/tr jump correction.
void testQRangeJumpActuallyChangesDynamicP(Context& context) {
  const GeometrySspEvaluator ssp(makeQCrossGradientProfile());
  const RayState initial = makeDiagonalRayState(330.0, 50.0);
  constexpr double stepLength = 30.0;
  constexpr double halfStep = 0.5 * stepLength;

  const auto result = stepRay(ssp, initial, 0U, 30.0);
  context.check(result.rangeSegmentIndex == 1U && result.segmentIndex == 0U,
                "the range crossing advances only the range segment");

  // Manual predictor, mirroring predictorState() from public samples.
  const SoundSpeedSample initialSample = ssp.evaluate(initial.position, 0U, 0U);
  const Vec2 initialTangent = initialSample.soundSpeed * initial.slowness;
  RayState midpoint = initial;
  midpoint.position = initial.position + halfStep * initialTangent;
  const double initialSpeedSquared =
      initialSample.soundSpeed * initialSample.soundSpeed;
  midpoint.slowness = initial.slowness - (halfStep / initialSpeedSquared) *
                                             initialSample.soundSpeedGradient;
  const double initialCurvature =
      rayreuse::soundSpeedNormalSecondDerivativeOverSquaredSpeed(
          initialSample.soundSpeedHessian, initial.slowness);
  for (std::size_t index = 0; index < midpoint.dynamicP.size(); ++index) {
    midpoint.dynamicP[index] =
        initial.dynamicP[index] -
        halfStep * initialCurvature * initial.dynamicQ[index];
    midpoint.dynamicQ[index] =
        initial.dynamicQ[index] +
        halfStep * initialSample.soundSpeed * initial.dynamicP[index];
  }
  context.check(
      midpoint.position.range < 350.0,
      "the predictor midpoint of this fixture stays inside the left cell");

  // Unlimited-step corrector: start weight zero, midpoint weight = step.
  const SoundSpeedSample midpointSample =
      ssp.evaluate(midpoint.position, initialSample.segmentIndex,
                   initialSample.rangeSegmentIndex);
  const double midpointCurvature =
      rayreuse::soundSpeedNormalSecondDerivativeOverSquaredSpeed(
          midpointSample.soundSpeedHessian, midpoint.slowness);
  std::array<double, 2> expectedP{};
  std::array<double, 2> expectedQ{};
  for (std::size_t index = 0; index < expectedP.size(); ++index) {
    expectedP[index] = initial.dynamicP[index] - stepLength *
                                                     midpointCurvature *
                                                     midpoint.dynamicQ[index];
    expectedQ[index] = initial.dynamicQ[index] + stepLength *
                                                     midpointSample.soundSpeed *
                                                     midpoint.dynamicP[index];
  }
  for (std::size_t index = 0; index < expectedP.size(); ++index) {
    context.checkNear(result.endState.dynamicQ[index], expectedQ[index],
                      1.0e-10,
                      "range crossing leaves the hand-integrated dynamic q "
                      "of pair " +
                          std::to_string(index) + " untouched");
  }

  const double correction =
      jumpCorrection(ssp, initial, result.endState, false);
  context.check(std::abs(correction) > 1.0e-8,
                "the cross-gradient range node produces a material jump");
  for (std::size_t index = 0; index < expectedP.size(); ++index) {
    context.check(
        std::abs(result.endState.dynamicP[index] - expectedP[index]) > 1.0e-6,
        "the range jump observably moves dynamic p of pair " +
            std::to_string(index) + " away from the no-jump update");
    context.checkNear(result.endState.dynamicP[index],
                      expectedP[index] - expectedQ[index] * correction, 1.0e-10,
                      "range crossing applies the -tz/tr jump to dynamic "
                      "pair " +
                          std::to_string(index));
  }
}

// All four range-independent kinds must stay bit-identical between the legacy
// single depth-hint overload and the new two-hint entry with range hint zero,
// including the StepLimitRequest payloads handed to the limiter, and the
// CLinearSsp direct overload must agree with the dispatched path.
void testNonQRangeHintZeroIsBitIdentical(Context& context) {
  const std::vector<SoundSpeedPoint> nodes =
      makePiecewiseLinearProfile().points();
  const RayState initial = makeRayState(37.0 * kPi / 180.0);

  for (const SspInterpolationKind kind :
       {SspInterpolationKind::CLinear, SspInterpolationKind::Pchip,
        SspInterpolationKind::N2Linear, SspInterpolationKind::CubicSpline}) {
    const GeometrySspEvaluator ssp(SoundSpeedProfile(nodes, kind));
    std::vector<StepLimitRequest> singleRequests;
    std::vector<StepLimitRequest> doubleRequests;
    const auto singleRecorder = [&singleRequests](const StepLimitRequest& r) {
      singleRequests.push_back(r);
      return r.proposedStepLength;
    };
    const auto doubleRecorder = [&doubleRequests](const StepLimitRequest& r) {
      doubleRequests.push_back(r);
      return r.proposedStepLength;
    };

    const auto single = stepRay(ssp, initial, 1U, 120.0, singleRecorder);
    const auto dual = stepRay(ssp, initial, 1U, 0U, 120.0, doubleRecorder);

    const std::string label =
        "non-Q kind " + std::to_string(static_cast<int>(kind));
    context.check(
        single.endState.position == dual.endState.position &&
            single.endState.slowness == dual.endState.slowness &&
            single.endState.soundSpeed == dual.endState.soundSpeed &&
            single.endState.realTravelTime == dual.endState.realTravelTime,
        label + " keeps position/slowness/speed/travel bit-identical");
    context.check(single.endState.dynamicP == dual.endState.dynamicP &&
                      single.endState.dynamicQ == dual.endState.dynamicQ,
                  label + " keeps dynamic states bit-identical");
    context.check(
        single.quadrature.stepLength == dual.quadrature.stepLength &&
            single.quadrature.startWeight == dual.quadrature.startWeight &&
            single.quadrature.midpointWeight ==
                dual.quadrature.midpointWeight &&
            single.quadrature.midpoint == dual.quadrature.midpoint,
        label + " keeps quadrature bit-identical");
    context.check(single.segmentIndex == dual.segmentIndex &&
                      single.rangeSegmentIndex == dual.rangeSegmentIndex,
                  label + " reports identical segment indices");
    context.check(
        singleRequests.size() == 2U && doubleRequests.size() == 2U,
        label + " still invokes the limiter twice through both entries");
    for (std::size_t call = 0; call < singleRequests.size(); ++call) {
      context.check(singleRequests[call].phase == doubleRequests[call].phase &&
                        singleRequests[call].initialSegmentIndex ==
                            doubleRequests[call].initialSegmentIndex &&
                        singleRequests[call].initialRangeSegmentIndex == 0U &&
                        doubleRequests[call].initialRangeSegmentIndex == 0U &&
                        singleRequests[call].nominalStepLength ==
                            doubleRequests[call].nominalStepLength &&
                        singleRequests[call].proposedStepLength ==
                            doubleRequests[call].proposedStepLength,
                    label + " limiter call " + std::to_string(call) +
                        " carries the same payload with range hint zero");
    }

    if (kind == SspInterpolationKind::CLinear) {
      const CLinearSsp concrete(SoundSpeedProfile(nodes, kind));
      const auto direct = stepRay(concrete, initial, 1U, 120.0);
      context.check(
          direct.endState.dynamicP == dual.endState.dynamicP &&
              direct.quadrature.stepLength == dual.quadrature.stepLength &&
              direct.rangeSegmentIndex == 0U,
          "the CLinearSsp direct overload matches the dispatched "
          "two-hint path");
    }
  }
}

}  // namespace

int main() {
  Context context;
  testNormalCurvatureFormula(context);
  testConstantSpeedAnalyticStep(context);
  testSecondLimiterCanFurtherShortenStep(context);
  testCLinearSegmentJump(context);
  testN2CurvatureEntersDynamicEquations(context);
  testN2SegmentCrossingAppliesGradientJump(context);
  testN2DiffersFromCLinearAndPchip(context);
  testSplineGradientContinuityAvoidsNodeJump(context);
  testSplineHessianFeedsDynamicCurvature(context);
  testSplineCurvatureEntersDynamicEquations(context);
  testValidation(context);
  testQCrossDepthSegmentKeepsDepthJumpSemantics(context);
  testQCornerCrossingPrefersDepthBranchSingleJump(context);
  testQExactRangeNodeLandingAndHintRetention(context);
  testQOutsideGridRangeRejected(context);
  testQRangeJumpActuallyChangesDynamicP(context);
  testNonQRangeHintZeroIsBitIdentical(context);

  if (context.failureCount() != 0) {
    std::cerr << context.failureCount()
              << " ray-stepper test assertion(s) failed\n";
    return 1;
  }

  std::cout << "All Bellhop RayReuse ray-stepper tests passed\n";
  return 0;
}
