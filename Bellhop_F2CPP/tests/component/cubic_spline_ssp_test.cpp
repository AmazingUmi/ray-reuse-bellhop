#include <cmath>
#include <iostream>
#include <limits>
#include <string>

#include "bellhop/error.hpp"
#include "bellhop/model/cubic_spline_frequency_ssp.hpp"
#include "bellhop/model/cubic_spline_ssp.hpp"
#include "support/test_harness.hpp"

namespace {

using bellhop::AttenuationUnit;
using bellhop::CubicSplineFrequencySsp;
using bellhop::CubicSplineSsp;
using bellhop::SoundSpeedProfile;
using bellhop::SspInterpolationKind;
using bellhop::ValidationError;
using bellhop::Vec2;
using bellhop::test::Context;

SoundSpeedProfile makeProfile(bool attenuating) {
  const auto attenuation = [attenuating](double value) {
    return bellhop::RawAttenuation{
        .value = attenuating ? value : 0.0,
        .unit = AttenuationUnit::DecibelsPerWavelength};
  };
  return SoundSpeedProfile(
      {{.depth = 0.0,
        .soundSpeed = 1500.0,
        .density = 1000.0,
        .attenuation = attenuation(0.1)},
       {.depth = 70.0,
        .soundSpeed = 1515.0,
        .density = 1040.0,
        .attenuation = attenuation(0.2)},
       {.depth = 190.0,
        .soundSpeed = 1460.0,
        .density = 1110.0,
        .attenuation = attenuation(0.3)},
       {.depth = 300.0,
        .soundSpeed = 1590.0,
        .density = 1180.0,
        .attenuation = attenuation(0.4)}},
      SspInterpolationKind::CubicSpline);
}

void checkRealOracle(Context& context, const CubicSplineSsp& profile,
                     double depth, std::size_t segment,
                     double expectedSoundSpeed, double expectedGradient,
                     double expectedCurvature, double expectedDensity,
                     const char* description) {
  const auto sample = profile.evaluateAtSegment(
      Vec2{.range = 0.0, .depth = depth}, segment);
  context.checkNear(sample.soundSpeed, expectedSoundSpeed, 4.0e-12,
                    std::string(description) + " sound speed");
  context.checkNear(sample.soundSpeedGradient.depth, expectedGradient,
                    4.0e-15, std::string(description) + " gradient");
  context.checkNear(sample.soundSpeedHessian.depthDepth,
                    expectedCurvature, 2.0e-17,
                    std::string(description) + " curvature");
  context.checkNear(sample.density, expectedDensity, 3.0e-12,
                    std::string(description) + " density");
}

void testNotAKnotFortranOracle(Context& context) {
  const CubicSplineSsp profile(makeProfile(false));
  checkRealOracle(context, profile, 20.0, 0U,
                  1513.87282436185433, 0.466328370332946995,
                  -0.0213084588942712462, 1011.42857142857156,
                  "first-segment interior");
  checkRealOracle(context, profile, 125.0, 1U,
                  1486.55189281557887, -0.594560697692848694,
                  0.00110104807473228837, 1072.08333333333348,
                  "middle-segment interior");
  checkRealOracle(context, profile, 255.0, 2U,
                  1496.71156768540277, 1.35200730333739494,
                  0.0288461519411176044, 1151.36363636363626,
                  "last-segment interior");
}

void testNodeContinuityAndExtrapolation(Context& context) {
  const CubicSplineSsp profile(makeProfile(false));
  const Vec2 node{.range = 0.0, .depth = 190.0};
  const auto left = profile.evaluate(node, 1U);
  const auto right = profile.evaluate(node, 2U);
  context.check(left.segmentIndex == 1U && right.segmentIndex == 2U,
                "spline node retains the arrival-side segment");
  context.checkNear(left.soundSpeed, right.soundSpeed, 2.0e-6,
                    "spline sound speed is continuous to oracle rounding");
  context.checkNear(left.soundSpeedGradient.depth,
                    right.soundSpeedGradient.depth, 2.0e-15,
                    "spline gradient is continuous at a node");
  context.checkNear(left.soundSpeedHessian.depthDepth,
                    right.soundSpeedHessian.depthDepth, 2.0e-17,
                    "spline curvature is continuous at a node");
  const auto above = profile.evaluate(
      Vec2{.range = 0.0, .depth = -20.0}, 2U);
  const auto below = profile.evaluate(
      Vec2{.range = 0.0, .depth = 330.0}, 0U);
  context.check(above.segmentIndex == 0U && below.segmentIndex == 2U,
                "spline extrapolation selects an edge polynomial");
  context.checkNear(above.soundSpeed, 1475.89640107327500, 5.0e-12,
                    "first cubic extrapolates above the profile");
  context.checkNear(below.soundSpeed, 1694.24828666174153, 5.0e-12,
                    "last cubic extrapolates below the profile");
}

void testTwoAndThreePointDegeneracies(Context& context) {
  const CubicSplineSsp linear(SoundSpeedProfile(
      {{.depth = 0.0, .soundSpeed = 1500.0, .density = 1000.0},
       {.depth = 100.0, .soundSpeed = 1520.0, .density = 1100.0}},
      SspInterpolationKind::CubicSpline));
  const auto linearSample = linear.evaluate(
      Vec2{.range = 0.0, .depth = 25.0}, 0U);
  context.checkNear(linearSample.soundSpeed, 1505.0, 0.0,
                    "two-point spline is exactly linear");
  context.checkNear(linearSample.soundSpeedHessian.depthDepth, 0.0, 0.0,
                    "two-point spline has zero curvature");

  const CubicSplineSsp quadratic(SoundSpeedProfile(
      {{.depth = 0.0, .soundSpeed = 1500.0, .density = 1000.0},
       {.depth = 100.0, .soundSpeed = 1510.0, .density = 1000.0},
       {.depth = 200.0, .soundSpeed = 1540.0, .density = 1000.0}},
      SspInterpolationKind::CubicSpline));
  const auto left = quadratic.evaluateAtSegment(
      Vec2{.range = 0.0, .depth = 50.0}, 0U);
  const auto right = quadratic.evaluateAtSegment(
      Vec2{.range = 0.0, .depth = 150.0}, 1U);
  context.checkNear(left.soundSpeedHessian.depthDepth,
                    right.soundSpeedHessian.depthDepth, 3.0e-18,
                    "three-point spline is one global quadratic");
}

void testComplexFortranOracle(Context& context) {
  const CubicSplineFrequencySsp profile(makeProfile(true), 100.0);
  const auto sample = profile.evaluateAtSegment(
      Vec2{.range = 0.0, .depth = 125.0}, 1U);
  context.checkNear(sample.soundSpeed, 1486.55189281557887, 4.0e-12,
                    "complex spline real value matches Fortran");
  context.checkNear(sample.imaginarySoundSpeed, 6.81897020568671142,
                    4.0e-14,
                    "complex spline imaginary value matches Fortran");
  context.checkNear(sample.soundSpeedGradient.depth,
                    -0.594560697692848694, 4.0e-15,
                    "complex spline gradient matches Fortran");
  context.checkNear(sample.soundSpeedHessian.depthDepth,
                    0.00110104807473228837, 4.0e-18,
                    "complex spline curvature matches Fortran");
  context.check(!profile.isLossless(),
                "attenuating spline is not lossless");
  context.check(!profile.uniformComplexSoundSpeed().has_value(),
                "nonuniform spline has no uniform complex fast path");
}

void testValidation(Context& context) {
  const CubicSplineSsp profile(makeProfile(false));
  context.expectThrows<ValidationError>(
      [&profile] {
        static_cast<void>(profile.evaluate(
            Vec2{.range = 0.0,
                 .depth = std::numeric_limits<double>::quiet_NaN()},
            0U));
      },
      "spline rejects a non-finite query");
  context.expectThrows<ValidationError>(
      [&profile] {
        static_cast<void>(profile.evaluateAtSegment(
            Vec2{.range = 0.0, .depth = 125.0}, 0U));
      },
      "spline explicit segment rejects an outside depth");
}

}  // namespace

int main() {
  Context context;
  testNotAKnotFortranOracle(context);
  testNodeContinuityAndExtrapolation(context);
  testTwoAndThreePointDegeneracies(context);
  testComplexFortranOracle(context);
  testValidation(context);
  if (context.failureCount() != 0) {
    std::cerr << context.failureCount()
              << " cubic-spline SSP test assertion(s) failed\n";
    return 1;
  }
  std::cout << "All Bellhop F2CPP cubic-spline SSP tests passed\n";
  return 0;
}
