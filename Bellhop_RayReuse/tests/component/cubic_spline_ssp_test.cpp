#include "rayreuse/model/cubic_spline_ssp.hpp"

#include <cmath>
#include <complex>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "rayreuse/acoustics/attenuation.hpp"
#include "rayreuse/error.hpp"
#include "rayreuse/model/cubic_spline_frequency_ssp.hpp"
#include "rayreuse/numerics/cubic_spline_coefficients.hpp"
#include "support/test_harness.hpp"

namespace {

using rayreuse::AttenuationUnit;
using rayreuse::computeCubicSplineCoefficients;
using rayreuse::convertAttenuation;
using rayreuse::CubicSplineFrequencySsp;
using rayreuse::CubicSplineSsp;
using rayreuse::FrancoisGarrisonParameters;
using rayreuse::RawAttenuation;
using rayreuse::SoundSpeedProfile;
using rayreuse::SspInterpolationKind;
using rayreuse::ValidationError;
using rayreuse::Vec2;
using rayreuse::VolumeAttenuation;
using rayreuse::VolumeAttenuationModel;
using rayreuse::test::Context;

// The concrete CubicSplineSsp under test does not consume the profile kind
// metadata; it is set to SspInterpolationKind::CubicSpline (wired up by G01)
// purely so the fixture metadata matches the real parser output.
SoundSpeedProfile makeProfile(bool attenuating) {
  const auto attenuation = [attenuating](double value) {
    return RawAttenuation{.value = attenuating ? value : 0.0,
                          .unit = AttenuationUnit::DecibelsPerWavelength};
  };
  return SoundSpeedProfile({{.depth = 0.0,
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
  const auto sample =
      profile.evaluateAtSegment(Vec2{.range = 0.0, .depth = depth}, segment);
  context.checkNear(sample.soundSpeed, expectedSoundSpeed, 4.0e-12,
                    std::string(description) + " sound speed");
  context.checkNear(sample.soundSpeedGradient.depth, expectedGradient, 4.0e-15,
                    std::string(description) + " gradient");
  context.checkNear(sample.soundSpeedHessian.depthDepth, expectedCurvature,
                    2.0e-17, std::string(description) + " curvature");
  context.checkNear(sample.density, expectedDensity, 3.0e-12,
                    std::string(description) + " density");
}

void testNotAKnotFortranOracle(Context& context) {
  const CubicSplineSsp profile(makeProfile(false));
  checkRealOracle(context, profile, 20.0, 0U, 1513.87282436185433,
                  0.466328370332946995, -0.0213084588942712462,
                  1011.42857142857156, "first-segment interior");
  checkRealOracle(context, profile, 125.0, 1U, 1486.55189281557887,
                  -0.594560697692848694, 0.00110104807473228837,
                  1072.08333333333348, "middle-segment interior");
  checkRealOracle(context, profile, 255.0, 2U, 1496.71156768540277,
                  1.35200730333739494, 0.0288461519411176044,
                  1151.36363636363626, "last-segment interior");
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
  const auto above = profile.evaluate(Vec2{.range = 0.0, .depth = -20.0}, 2U);
  const auto below = profile.evaluate(Vec2{.range = 0.0, .depth = 330.0}, 0U);
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
       {.depth = 100.0, .soundSpeed = 1520.0, .density = 1100.0}}));
  const auto linearSample =
      linear.evaluate(Vec2{.range = 0.0, .depth = 25.0}, 0U);
  context.checkNear(linearSample.soundSpeed, 1505.0, 0.0,
                    "two-point spline is exactly linear");
  context.checkNear(linearSample.soundSpeedHessian.depthDepth, 0.0, 0.0,
                    "two-point spline has zero curvature");

  const CubicSplineSsp quadratic(SoundSpeedProfile(
      {{.depth = 0.0, .soundSpeed = 1500.0, .density = 1000.0},
       {.depth = 100.0, .soundSpeed = 1510.0, .density = 1000.0},
       {.depth = 200.0, .soundSpeed = 1540.0, .density = 1000.0}}));
  const auto left =
      quadratic.evaluateAtSegment(Vec2{.range = 0.0, .depth = 50.0}, 0U);
  const auto right =
      quadratic.evaluateAtSegment(Vec2{.range = 0.0, .depth = 150.0}, 1U);
  context.checkNear(left.soundSpeedHessian.depthDepth,
                    right.soundSpeedHessian.depthDepth, 3.0e-18,
                    "three-point spline is one global quadratic");
}

void testKernelValidation(Context& context) {
  const std::vector<double> nodes{0.0, 100.0, 200.0};
  const std::vector<std::complex<double>> values{
      {1500.0, 0.0}, {1510.0, 0.0}, {1540.0, 0.0}};
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(computeCubicSplineCoefficients(
            std::vector<double>{0.0, 100.0},
            std::vector<std::complex<double>>{
                {1500.0, 0.0}, {1510.0, 0.0}, {1540.0, 0.0}}));
      },
      "spline kernel rejects mismatched node/value arrays");
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(computeCubicSplineCoefficients(
            std::vector<double>{0.0},
            std::vector<std::complex<double>>{{1500.0, 0.0}}));
      },
      "spline kernel rejects a single point");
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(computeCubicSplineCoefficients(
            std::vector<double>{0.0, std::numeric_limits<double>::quiet_NaN(),
                                200.0},
            values));
      },
      "spline kernel rejects a non-finite node");
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(computeCubicSplineCoefficients(
            std::vector<double>{0.0, 200.0, 200.0}, values));
      },
      "spline kernel rejects non-increasing nodes");
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(computeCubicSplineCoefficients(
            nodes, std::vector<std::complex<double>>{
                       {1500.0, 0.0},
                       {std::numeric_limits<double>::infinity(), 0.0},
                       {1540.0, 0.0}}));
      },
      "spline kernel rejects a non-finite value");
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
        static_cast<void>(
            profile.evaluateAtSegment(Vec2{.range = 0.0, .depth = 125.0}, 0U));
      },
      "spline explicit segment rejects an outside depth");
}

// F2CPP complex oracle (100 Hz, attenuating profile): the node attenuation is
// converted first and the complex not-a-knot spline then reproduces the frozen
// Fortran real value, imaginary value, and real-part gradient/curvature at an
// interior query point. Density and segment identity still come from the real
// evaluator.
void testComplexFortranOracle(Context& context) {
  const CubicSplineFrequencySsp profile(makeProfile(true), 100.0);
  const auto sample =
      profile.evaluateAtSegment(Vec2{.range = 0.0, .depth = 125.0}, 1U);
  context.checkNear(sample.soundSpeed, 1486.55189281557887, 4.0e-12,
                    "complex spline real value matches Fortran");
  context.checkNear(sample.imaginarySoundSpeed, 6.81897020568671142, 4.0e-14,
                    "complex spline imaginary value matches Fortran");
  context.checkNear(sample.soundSpeedGradient.depth, -0.594560697692848694,
                    4.0e-15, "complex spline gradient matches Fortran");
  context.checkNear(sample.soundSpeedHessian.depthDepth, 0.00110104807473228837,
                    4.0e-18, "complex spline curvature matches Fortran");
  context.checkNear(sample.density, 1072.08333333333348, 3.0e-12,
                    "complex spline keeps the real-evaluator density");
  context.check(sample.segmentIndex == 1U,
                "complex spline keeps the real-evaluator segment identity");
  context.check(profile.frequency() == 100.0,
                "frequency evaluator retains its target frequency");
  context.check(!profile.isLossless(), "attenuating spline is not lossless");
  context.check(!profile.uniformComplexSoundSpeed().has_value(),
                "nonuniform spline has no uniform complex fast path");
}

// Lossless nodes produce an exactly zero imaginary state, and the F2CPP
// uniform fast path compares node complex sound speeds with exact equality.
void testLosslessAndUniformComplexSoundSpeed(Context& context) {
  const CubicSplineFrequencySsp lossless(makeProfile(false), 100.0);
  context.check(lossless.isLossless(), "zero-attenuation spline is lossless");
  context.check(!lossless.uniformComplexSoundSpeed().has_value(),
                "varying lossless spline has no uniform fast path");
  const auto losslessSample =
      lossless.evaluateAtSegment(Vec2{.range = 0.0, .depth = 125.0}, 1U);
  context.check(losslessSample.imaginarySoundSpeed == 0.0,
                "lossless spline interior carries exactly zero imaginary "
                "sound speed");
  context.checkNear(losslessSample.soundSpeed, 1486.55189281557887, 4.0e-12,
                    "lossless complex spline matches the real oracle");

  const CubicSplineFrequencySsp uniform(
      SoundSpeedProfile(
          {{.depth = 0.0, .soundSpeed = 1500.0, .density = 1000.0},
           {.depth = 100.0, .soundSpeed = 1500.0, .density = 1000.0}}),
      250.0);
  context.check(uniform.isLossless(), "uniform lossless spline is lossless");
  context.check(uniform.uniformComplexSoundSpeed().has_value() &&
                    *uniform.uniformComplexSoundSpeed() ==
                        std::complex<double>{1500.0, 0.0},
                "uniform lossless fast path exposes the exact node value");

  const RawAttenuation attenuating{
      .value = 0.5, .unit = AttenuationUnit::DecibelsPerWavelength};
  const CubicSplineFrequencySsp uniformLossy(
      SoundSpeedProfile({{.depth = 0.0,
                          .soundSpeed = 1500.0,
                          .density = 1000.0,
                          .attenuation = attenuating},
                         {.depth = 100.0,
                          .soundSpeed = 1500.0,
                          .density = 1000.0,
                          .attenuation = attenuating}}),
      250.0);
  context.check(!uniformLossy.isLossless(),
                "uniform attenuating spline is not lossless");
  context.check(
      uniformLossy.uniformComplexSoundSpeed().has_value() &&
          *uniformLossy.uniformComplexSoundSpeed() ==
              std::complex<double>{
                  1500.0, convertAttenuation(attenuating, 250.0, 1500.0)
                              .imaginarySoundSpeed},
      "uniform attenuating fast path compares complex node values exactly");
}

// Thorp volume attenuation is genuinely frequency dependent, so 50 Hz and
// 250 Hz evaluators over one frozen profile must produce different imaginary
// states while every real observable stays bit-identical (the complex
// arithmetic mixes channels only through real scalar factors). Repeating an
// evaluation after the other frequency ran must be bit-stable, proving there
// is no shared mutable coefficient state and no cross-frequency contamination.
void testTwoFrequencyIndependence(Context& context) {
  const auto thorp = []() {
    return RawAttenuation{.value = 0.0,
                          .unit = AttenuationUnit::DecibelsPerWavelength,
                          .volumeModel = VolumeAttenuationModel::Thorp};
  };
  const SoundSpeedProfile profile({{.depth = 0.0,
                                    .soundSpeed = 1500.0,
                                    .density = 1000.0,
                                    .attenuation = thorp()},
                                   {.depth = 70.0,
                                    .soundSpeed = 1515.0,
                                    .density = 1040.0,
                                    .attenuation = thorp()},
                                   {.depth = 190.0,
                                    .soundSpeed = 1460.0,
                                    .density = 1110.0,
                                    .attenuation = thorp()},
                                   {.depth = 300.0,
                                    .soundSpeed = 1590.0,
                                    .density = 1180.0,
                                    .attenuation = thorp()}});

  const CubicSplineFrequencySsp low(profile, 50.0);
  const CubicSplineFrequencySsp high(profile, 250.0);
  context.check(low.frequency() == 50.0 && high.frequency() == 250.0,
                "per-frequency evaluators retain their own frequencies");
  context.check(!low.isLossless() && !high.isLossless(),
                "Thorp attenuators are lossy at both frequencies");
  context.check(!low.uniformComplexSoundSpeed().has_value(),
                "Thorp spline keeps the general interpolation path");

  for (const auto& [segment, depth] :
       {std::pair<std::size_t, double>{0U, 17.5}, {1U, 100.0}, {2U, 217.5}}) {
    const auto lowSample =
        low.evaluateAtSegment(Vec2{.range = 0.0, .depth = depth}, segment);
    const auto highSample =
        high.evaluateAtSegment(Vec2{.range = 0.0, .depth = depth}, segment);

    context.check(lowSample.soundSpeed == highSample.soundSpeed &&
                      lowSample.soundSpeedGradient.depth ==
                          highSample.soundSpeedGradient.depth &&
                      lowSample.soundSpeedHessian.depthDepth ==
                          highSample.soundSpeedHessian.depthDepth &&
                      lowSample.density == highSample.density &&
                      lowSample.segmentIndex == highSample.segmentIndex,
                  "real observables are bit-identical across frequencies");
    context.check(std::abs(lowSample.imaginarySoundSpeed -
                           highSample.imaginarySoundSpeed) > 1.0e-9,
                  "Thorp imaginary sound speed separates 50 Hz from 250 Hz");
    context.check(std::isfinite(lowSample.imaginarySoundSpeed) &&
                      std::isfinite(highSample.imaginarySoundSpeed),
                  "both frequency states remain finite");
  }

  const auto repeated =
      low.evaluateAtSegment(Vec2{.range = 0.0, .depth = 125.0}, 1U);
  const auto original =
      low.evaluateAtSegment(Vec2{.range = 0.0, .depth = 125.0}, 1U);
  context.check(
      repeated.soundSpeed == original.soundSpeed &&
          repeated.imaginarySoundSpeed == original.imaginarySoundSpeed &&
          repeated.soundSpeedGradient.depth ==
              original.soundSpeedGradient.depth &&
          repeated.soundSpeedHessian.depthDepth ==
              original.soundSpeedHessian.depthDepth,
      "repeating a projection at one frequency is bit-stable");
}

// Cubic interpolation of sharply varying node attenuation legitimately
// undershoots below zero between nodes. F2CPP validates interior imaginary
// sound speed as finite only: the evaluation must succeed and must not clamp,
// take an absolute value, or reject the negative value the way the PCHIP
// path does.
void testInteriorImaginaryIsFiniteOnly(Context& context) {
  const auto attenuation = [](double value) {
    return RawAttenuation{.value = value,
                          .unit = AttenuationUnit::DecibelsPerWavelength};
  };
  const CubicSplineFrequencySsp profile(
      SoundSpeedProfile({{.depth = 0.0,
                          .soundSpeed = 1500.0,
                          .density = 1000.0,
                          .attenuation = attenuation(0.0)},
                         {.depth = 70.0,
                          .soundSpeed = 1515.0,
                          .density = 1040.0,
                          .attenuation = attenuation(8.0)},
                         {.depth = 190.0,
                          .soundSpeed = 1460.0,
                          .density = 1110.0,
                          .attenuation = attenuation(0.0)},
                         {.depth = 300.0,
                          .soundSpeed = 1590.0,
                          .density = 1180.0,
                          .attenuation = attenuation(8.0)}}),
      100.0);

  const auto sample =
      profile.evaluateAtSegment(Vec2{.range = 0.0, .depth = 222.9}, 2U);
  context.check(std::isfinite(sample.imaginarySoundSpeed),
                "interior imaginary sound speed is finite");
  context.check(sample.imaginarySoundSpeed < -1.0,
                "spline interior undershoots to a materially negative "
                "imaginary sound speed without a clamp");
  context.check(std::isfinite(sample.soundSpeed) && sample.soundSpeed > 0.0,
                "real sound speed stays valid in the undershoot region");
}

void testVolumePaths(Context& context) {
  const SoundSpeedProfile source(
      {{.depth = 0.0, .soundSpeed = 1400.0, .density = 1000.0},
       {.depth = 100.0, .soundSpeed = 1500.0, .density = 1000.0},
       {.depth = 200.0, .soundSpeed = 1600.0, .density = 1000.0}},
      SspInterpolationKind::CubicSpline);
  const VolumeAttenuation thorp{.model = VolumeAttenuationModel::Thorp};
  const VolumeAttenuation fg{
      .model = VolumeAttenuationModel::FrancoisGarrison,
      .parameters = FrancoisGarrisonParameters{.temperatureCelsius = 10.0,
                                               .salinityPsu = 35.0,
                                               .pH = 8.0,
                                               .meanDepthMeters = 100.0}};
  const auto layers =
      std::make_shared<const rayreuse::BiologicalAttenuationLayers>(
          rayreuse::BiologicalAttenuationLayers{
              {.minimumDepth = 0.0,
               .maximumDepth = 100.0,
               .resonanceFrequency = 1000.0,
               .qualityFactor = 2.0,
               .attenuationCoefficientDecibelsPerKilometer = 10.0}});
  const VolumeAttenuation biological{
      .model = VolumeAttenuationModel::Biological, .parameters = layers};
  context.check(CubicSplineFrequencySsp(source, 1000.0).isLossless(),
                "S None path is exactly lossless");
  context.check(!CubicSplineFrequencySsp(source, 1000.0, thorp).isLossless(),
                "S Thorp path is lossy");
  context.check(!CubicSplineFrequencySsp(source, 1000.0, fg).isLossless(),
                "S FG path is lossy");
  auto legacyPoints = source.points();
  for (auto& point : legacyPoints) {
    point.attenuation.volumeModel = VolumeAttenuationModel::Thorp;
  }
  const SoundSpeedProfile legacyThorp(std::move(legacyPoints),
                                      SspInterpolationKind::CubicSpline);
  context.check(CubicSplineFrequencySsp(source, 1000.0, thorp)
                        .evaluate(Vec2{.range = 0.0, .depth = 50.0}, 0U)
                        .imaginarySoundSpeed ==
                    CubicSplineFrequencySsp(legacyThorp, 1000.0)
                        .evaluate(Vec2{.range = 0.0, .depth = 50.0}, 0U)
                        .imaginarySoundSpeed,
                "S explicit and legacy Thorp baselines are exact");
  const CubicSplineFrequencySsp bio(source, 1000.0, biological);
  context.check(
      bio.evaluate(Vec2{.range = 0.0, .depth = 0.0}, 0U).imaginarySoundSpeed >
              0.0 &&
          bio.evaluate(Vec2{.range = 0.0, .depth = 100.0}, 0U)
                  .imaginarySoundSpeed > 0.0 &&
          bio.evaluate(Vec2{.range = 0.0, .depth = 200.0}, 1U)
                  .imaginarySoundSpeed == 0.0,
      "S biological endpoints are inclusive and outside node is lossless");
  context.check(
      bio.evaluate(Vec2{.range = 0.0, .depth = 150.0}, 1U).imaginarySoundSpeed >
          0.0,
      "S biological loss is node-first, not query-depth converted");
  const auto low = CubicSplineFrequencySsp(source, 500.0, biological)
                       .evaluate(Vec2{.range = 0.0, .depth = 50.0}, 0U);
  static_cast<void>(CubicSplineFrequencySsp(source, 2000.0, biological)
                        .evaluate(Vec2{.range = 0.0, .depth = 50.0}, 0U));
  const auto repeated = CubicSplineFrequencySsp(source, 500.0, biological)
                            .evaluate(Vec2{.range = 0.0, .depth = 50.0}, 0U);
  context.check(low.imaginarySoundSpeed == repeated.imaginarySoundSpeed,
                "S low/high/low evaluation is deterministic");
}

void testFrequencySspValidation(Context& context) {
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(CubicSplineFrequencySsp(makeProfile(true), 0.0));
      },
      "zero frequency is rejected");
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(CubicSplineFrequencySsp(
            makeProfile(true), std::numeric_limits<double>::quiet_NaN()));
      },
      "non-finite frequency is rejected");
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(CubicSplineFrequencySsp(makeProfile(true), -50.0));
      },
      "negative frequency is rejected");

  const CubicSplineFrequencySsp profile(makeProfile(true), 100.0);
  context.expectThrows<ValidationError>(
      [&profile] {
        static_cast<void>(profile.evaluate(
            Vec2{.range = 0.0,
                 .depth = std::numeric_limits<double>::quiet_NaN()},
            0U));
      },
      "frequency spline rejects a non-finite query");
  context.expectThrows<ValidationError>(
      [&profile] {
        static_cast<void>(
            profile.evaluateAtSegment(Vec2{.range = 0.0, .depth = 125.0}, 0U));
      },
      "frequency spline explicit segment rejects an outside depth");
}

}  // namespace

int main() {
  Context context;
  testNotAKnotFortranOracle(context);
  testNodeContinuityAndExtrapolation(context);
  testTwoAndThreePointDegeneracies(context);
  testKernelValidation(context);
  testValidation(context);
  testComplexFortranOracle(context);
  testLosslessAndUniformComplexSoundSpeed(context);
  testTwoFrequencyIndependence(context);
  testInteriorImaginaryIsFiniteOnly(context);
  testVolumePaths(context);
  testFrequencySspValidation(context);
  if (context.failureCount() != 0) {
    std::cerr << context.failureCount()
              << " cubic-spline SSP test assertion(s) failed\n";
    return 1;
  }
  std::cout << "All Bellhop RayReuse cubic-spline SSP tests passed\n";
  return 0;
}
