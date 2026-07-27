#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <vector>

#include "bellhop/error.hpp"
#include "bellhop/model/c_linear_ssp.hpp"
#include "bellhop/model/environment.hpp"
#include "bellhop/ray/ray_equations.hpp"
#include "bellhop/ray/ray_stepper.hpp"
#include "support/test_harness.hpp"

namespace {

using bellhop::CLinearSsp;
using bellhop::RayState;
using bellhop::SoundSpeedHessian;
using bellhop::SoundSpeedPoint;
using bellhop::SoundSpeedProfile;
using bellhop::StepLimitPhase;
using bellhop::StepLimitRequest;
using bellhop::ValidationError;
using bellhop::Vec2;
using bellhop::stepRay;
using bellhop::test::Context;

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

RayState makeRayState(double angleRadians) {
  constexpr double soundSpeed = 1500.0;
  return RayState{
      .position = Vec2{.range = 10.0, .depth = 200.0},
      .slowness =
          Vec2{.range = std::cos(angleRadians) / soundSpeed,
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
      bellhop::soundSpeedNormalSecondDerivativeOverSquaredSpeed(
          hessian, slowness);
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
  const Vec2 direction{.range = std::cos(angle),
                       .depth = std::sin(angle)};

  context.checkNear(result.endState.position.range,
                    initial.position.range + stepLength * direction.range,
                    2.0e-14,
                    "constant-speed step has analytic range coordinate");
  context.checkNear(result.endState.position.depth,
                    initial.position.depth + stepLength * direction.depth,
                    2.0e-14,
                    "constant-speed step has analytic depth coordinate");
  context.checkNear(result.endState.slowness.range, initial.slowness.range,
                    0.0, "constant-speed step preserves range slowness");
  context.checkNear(result.endState.slowness.depth, initial.slowness.depth,
                    0.0, "constant-speed step preserves depth slowness");
  context.checkNear(result.endState.realTravelTime,
                    initial.realTravelTime + stepLength / 1500.0, 1.0e-15,
                    "constant-speed step has analytic real travel time");
  context.checkNear(result.endState.soundSpeed, 1500.0, 0.0,
                    "constant-speed step stores endpoint sound speed");
  context.check(result.segmentIndex == 0U,
                "constant profile retains its segment");

  for (std::size_t index = 0; index < initial.dynamicP.size(); ++index) {
    context.checkNear(result.endState.dynamicP[index],
                      initial.dynamicP[index], 0.0,
                      "zero Hessian preserves dynamic p");
    context.checkNear(
        result.endState.dynamicQ[index],
        initial.dynamicQ[index] +
            stepLength * 1500.0 * initial.dynamicP[index],
        1.0e-10, "constant-speed dynamic q has analytic solution");
  }

  context.checkNear(result.quadrature.stepLength, stepLength, 0.0,
                    "quadrature records actual full step");
  context.checkNear(result.quadrature.startWeight, 0.0, 0.0,
                    "unlimited box step has zero start weight");
  context.checkNear(result.quadrature.midpointWeight, stepLength, 0.0,
                    "unlimited box step uses midpoint weight");
  context.checkNear(result.quadrature.midpoint.range,
                    initial.position.range +
                        0.5 * stepLength * direction.range,
                    2.0e-14, "quadrature records predictor midpoint range");
  context.checkNear(result.quadrature.midpoint.depth,
                    initial.position.depth +
                        0.5 * stepLength * direction.depth,
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
  context.check(requests[1].phase ==
                    StepLimitPhase::PredictedMidpointTangent,
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
      .slowness = Vec2{.range = 0.8 / 1498.0,
                       .depth = 0.6 / 1498.0},
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
      (2.0 * bellhop::dot(gradientJump, rayNormal) -
       incidenceTangent *
           bellhop::dot(gradientJump, result.endState.slowness)) /
      result.endState.soundSpeed;

  context.checkNear(
      result.endState.dynamicP[0],
      initial.dynamicP[0] - result.endState.dynamicQ[0] * correction,
      1.0e-15, "C-linear crossing applies p jump to first fundamental pair");
  context.checkNear(
      result.endState.dynamicP[1],
      initial.dynamicP[1] - result.endState.dynamicQ[1] * correction,
      1.0e-15, "C-linear crossing applies p jump to second fundamental pair");
}

void testValidation(Context& context) {
  const CLinearSsp ssp(makeConstantProfile());
  const RayState valid = makeRayState(0.0);

  context.expectThrows<ValidationError>(
      [&ssp, &valid] {
        static_cast<void>(stepRay(ssp, valid, 0U, 0.0));
      },
      "zero nominal step is rejected");
  context.expectThrows<ValidationError>(
      [&ssp, &valid] {
        static_cast<void>(stepRay(
            ssp, valid, 0U, std::numeric_limits<double>::quiet_NaN()));
      },
      "non-finite nominal step is rejected");

  RayState invalid = valid;
  invalid.dynamicQ[1] = std::numeric_limits<double>::infinity();
  context.expectThrows<ValidationError>(
      [&ssp, &invalid] {
        static_cast<void>(stepRay(ssp, invalid, 0U, 1.0));
      },
      "non-finite dynamic state is rejected");

  invalid = valid;
  invalid.soundSpeed = 0.0;
  context.expectThrows<ValidationError>(
      [&ssp, &invalid] {
        static_cast<void>(stepRay(ssp, invalid, 0U, 1.0));
      },
      "non-positive state sound speed is rejected");

  context.expectThrows<ValidationError>(
      [&ssp, &valid] {
        static_cast<void>(stepRay(
            ssp, valid, 0U, 1.0, [](const StepLimitRequest&) {
              return std::numeric_limits<double>::infinity();
            }));
      },
      "non-finite limiter result is rejected");
  context.expectThrows<ValidationError>(
      [&ssp, &valid] {
        static_cast<void>(stepRay(
            ssp, valid, 0U, 1.0,
            [](const StepLimitRequest&) { return 2.0; }));
      },
      "a limiter cannot increase its proposed step");
}

}  // namespace

int main() {
  Context context;
  testNormalCurvatureFormula(context);
  testConstantSpeedAnalyticStep(context);
  testSecondLimiterCanFurtherShortenStep(context);
  testCLinearSegmentJump(context);
  testValidation(context);

  if (context.failureCount() != 0) {
    std::cerr << context.failureCount()
              << " ray-stepper test assertion(s) failed\n";
    return 1;
  }

  std::cout << "All Bellhop F2CPP ray-stepper tests passed\n";
  return 0;
}
