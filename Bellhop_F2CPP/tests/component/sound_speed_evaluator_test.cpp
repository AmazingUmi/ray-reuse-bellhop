#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <limits>
#include <memory>
#include <utility>

#include "bellhop/acoustics/attenuation.hpp"
#include "bellhop/error.hpp"
#include "bellhop/model/c_linear_ssp.hpp"
#include "bellhop/model/n2_linear_ssp.hpp"
#include "bellhop/model/quadrilateral_ssp.hpp"
#include "bellhop/model/sound_speed_evaluator.hpp"
#include "support/test_harness.hpp"

namespace {

using bellhop::CLinearSsp;
using bellhop::AttenuationUnit;
using bellhop::BiologicalAttenuationLayer;
using bellhop::BiologicalAttenuationLayers;
using bellhop::FrequencySspEvaluator;
using bellhop::GeometrySspEvaluator;
using bellhop::N2LinearSsp;
using bellhop::QuadrilateralSsp;
using bellhop::QuadrilateralFrequencySsp;
using bellhop::QuadrilateralSspGrid;
using bellhop::SoundSpeedProfile;
using bellhop::SspGradientContinuity;
using bellhop::SspInterpolationKind;
using bellhop::sspGradientContinuity;
using bellhop::ValidationError;
using bellhop::Vec2;
using bellhop::VolumeAttenuation;
using bellhop::VolumeAttenuationModel;
using bellhop::convertAttenuation;
using bellhop::test::Context;

SoundSpeedProfile makeProfile(
    SspInterpolationKind kind = SspInterpolationKind::CLinear) {
  return SoundSpeedProfile(
      {{.depth = 0.0, .soundSpeed = 1480.0, .density = 1000.0},
       {.depth = 100.0, .soundSpeed = 1500.0, .density = 1020.0},
       {.depth = 300.0, .soundSpeed = 1460.0, .density = 1060.0}},
      kind);
}

SoundSpeedProfile makeQuadrilateralProfile() {
  const auto grid = std::make_shared<const QuadrilateralSspGrid>(
      QuadrilateralSspGrid{
          .rangesMeters = {0.0, 350.0, 750.0},
          .speedsDepthMajor = {1500.0, 1540.0, 1580.0,
                               1500.0, 1520.0, 1540.0},
          .depthCount = 2U,
          .rangeCount = 3U});
  return SoundSpeedProfile(
      {{.depth = 0.0, .soundSpeed = 1490.0, .density = 1000.0},
       {.depth = 100.0, .soundSpeed = 1490.0, .density = 1100.0}},
      SspInterpolationKind::Quadrilateral, grid);
}

SoundSpeedProfile makeDepthLocatorQuadrilateralProfile() {
  const auto grid = std::make_shared<const QuadrilateralSspGrid>(
      QuadrilateralSspGrid{
          .rangesMeters = {0.0, 1000.0},
          .speedsDepthMajor = {1500.0, 1510.0,
                               1520.0, 1530.0,
                               1540.0, 1550.0,
                               1560.0, 1570.0},
          .depthCount = 4U,
          .rangeCount = 2U});
  return SoundSpeedProfile(
      {{.depth = 0.0, .soundSpeed = 1500.0, .density = 1000.0},
       {.depth = 100.0, .soundSpeed = 1520.0, .density = 1000.0},
       {.depth = 200.0, .soundSpeed = 1540.0, .density = 1000.0},
       {.depth = 300.0, .soundSpeed = 1560.0, .density = 1000.0}},
      SspInterpolationKind::Quadrilateral, grid);
}

SoundSpeedProfile makeQuadrilateralFrequencyProfile() {
  const auto grid = std::make_shared<const QuadrilateralSspGrid>(
      QuadrilateralSspGrid{
          .rangesMeters = {0.0, 350.0, 750.0},
          .speedsDepthMajor = {1500.0, 1540.0, 1580.0,
                               1500.0, 1520.0, 1540.0},
          .depthCount = 2U,
          .rangeCount = 3U});
  return SoundSpeedProfile(
      {{.depth = 0.0,
        .soundSpeed = 1400.0,
        .density = 1000.0,
        .attenuation = {
            .value = 0.05,
            .unit = AttenuationUnit::DecibelsPerWavelength}},
       {.depth = 100.0,
        .soundSpeed = 1800.0,
        .density = 1200.0,
        .attenuation = {
            .value = 0.10,
            .unit = AttenuationUnit::DecibelsPerWavelength}}},
      SspInterpolationKind::Quadrilateral, grid);
}

void checkSameSample(Context& context,
                     const bellhop::SoundSpeedSample& expected,
                     const bellhop::SoundSpeedSample& actual) {
  context.check(expected.soundSpeed == actual.soundSpeed,
                "wrapper preserves sound speed exactly");
  context.check(expected.imaginarySoundSpeed == actual.imaginarySoundSpeed,
                "wrapper preserves imaginary sound speed exactly");
  context.check(expected.soundSpeedGradient == actual.soundSpeedGradient,
                "wrapper preserves gradient exactly");
  context.check(expected.soundSpeedHessian == actual.soundSpeedHessian,
                "wrapper preserves Hessian exactly");
  context.check(expected.density == actual.density,
                "wrapper preserves density exactly");
  context.check(expected.segmentIndex == actual.segmentIndex,
                "wrapper preserves segment index exactly");
  context.check(expected.rangeSegmentIndex == actual.rangeSegmentIndex,
                "wrapper preserves range segment index exactly");
}

void testCLinearDispatchIsExact(Context& context) {
  const SoundSpeedProfile profile = makeProfile();
  const CLinearSsp concrete(profile);
  const GeometrySspEvaluator evaluator(profile);

  context.check(evaluator.interpolationKind() ==
                    SspInterpolationKind::CLinear,
                "wrapper reports C-linear interpolation");
  context.check(evaluator.gradientContinuity() ==
                    SspGradientContinuity::DiscontinuousAtNodes,
                "C-linear wrapper reports a gradient jump at nodes");
  context.check(evaluator.segmentCount() == concrete.segmentCount(),
                "wrapper preserves segment count");
  context.check(evaluator.rangeSegmentCount() == 1U &&
                    evaluator.locateRangeSegment(25.0, 0U) == 0U,
                "range-independent wrapper retains range segment zero");

  const FrequencySspEvaluator frequencyEvaluator(profile, 1000.0);
  context.check(
      frequencyEvaluator.rangeSegmentCount() == 1U &&
          frequencyEvaluator.locateRangeSegment(25.0, 0U) == 0U &&
          frequencyEvaluator.minimumRangeForSegment(0U) ==
              -std::numeric_limits<double>::infinity() &&
          frequencyEvaluator.maximumRangeForSegment(0U) ==
              std::numeric_limits<double>::infinity() &&
          frequencyEvaluator.evaluateAtSegments(
              Vec2{.range = 25.0, .depth = 50.0}, 0U, 0U)
                  .rangeSegmentIndex == 0U,
      "range-independent frequency wrapper retains range segment zero");

  for (const auto& [position, hint] :
       std::array<std::pair<Vec2, std::size_t>, 5>{
           std::pair{Vec2{.range = 0.0, .depth = -1.0}, 0U},
           std::pair{Vec2{.range = 25.0, .depth = 50.0}, 0U},
           std::pair{Vec2{.range = 25.0, .depth = 100.0}, 0U},
           std::pair{Vec2{.range = 25.0, .depth = 100.0}, 1U},
           std::pair{Vec2{.range = 0.0, .depth = 301.0}, 1U}}) {
    checkSameSample(context, concrete.evaluate(position, hint),
                    evaluator.evaluate(position, hint));
    context.check(evaluator.evaluate(position, hint).rangeSegmentIndex == 0U,
                  "C-linear samples report range segment zero");
  }
}

void testRecognizedKindsDoNotFallback(Context& context) {
  context.check(
      sspGradientContinuity(SspInterpolationKind::N2Linear) ==
          SspGradientContinuity::DiscontinuousAtNodes &&
          sspGradientContinuity(SspInterpolationKind::CLinear) ==
              SspGradientContinuity::DiscontinuousAtNodes &&
          sspGradientContinuity(SspInterpolationKind::Quadrilateral) ==
              SspGradientContinuity::DiscontinuousAtNodes,
      "N/C/Q interpolation kinds retain gradient jumps");
  context.check(
      sspGradientContinuity(SspInterpolationKind::Pchip) ==
          SspGradientContinuity::ContinuousAtNodes &&
          sspGradientContinuity(SspInterpolationKind::CubicSpline) ==
              SspGradientContinuity::ContinuousAtNodes,
      "P/S interpolation kinds have continuous gradients");

  const GeometrySspEvaluator pchip(makeProfile(SspInterpolationKind::Pchip));
  context.check(pchip.interpolationKind() == SspInterpolationKind::Pchip &&
                    pchip.gradientContinuity() ==
                        SspGradientContinuity::ContinuousAtNodes,
                "PCHIP dispatches to a continuous-gradient backend");

  const SoundSpeedProfile n2Profile =
      makeProfile(SspInterpolationKind::N2Linear);
  const N2LinearSsp n2Concrete(n2Profile);
  const GeometrySspEvaluator n2(n2Profile);
  context.check(n2.interpolationKind() == SspInterpolationKind::N2Linear &&
                    n2.gradientContinuity() ==
                        SspGradientContinuity::DiscontinuousAtNodes,
                "N2-linear dispatches to a discontinuous-gradient backend");
  checkSameSample(
      context,
      n2Concrete.evaluate(Vec2{.range = 20.0, .depth = 150.0}, 1U),
      n2.evaluate(Vec2{.range = 20.0, .depth = 150.0}, 1U));

  const GeometrySspEvaluator spline(
      makeProfile(SspInterpolationKind::CubicSpline));
  context.check(
      spline.interpolationKind() == SspInterpolationKind::CubicSpline &&
          spline.gradientContinuity() ==
              SspGradientContinuity::ContinuousAtNodes,
      "cubic spline dispatches to a continuous-gradient backend");

}

void testQuadrilateralDispatchAndFormula(Context& context) {
  const SoundSpeedProfile profile = makeQuadrilateralProfile();
  const QuadrilateralSsp concrete(profile);
  const GeometrySspEvaluator evaluator(profile);

  context.check(
      evaluator.interpolationKind() == SspInterpolationKind::Quadrilateral &&
          evaluator.gradientContinuity() ==
              SspGradientContinuity::DiscontinuousAtNodes &&
          evaluator.segmentCount() == 1U,
      "Q dispatches to the discontinuous-gradient geometry backend");
  context.check(evaluator.rangeSegmentCount() == 2U &&
                    evaluator.minimumRangeForSegment(0U) == 0.0 &&
                    evaluator.maximumRangeForSegment(0U) == 350.0 &&
                    evaluator.minimumRangeForSegment(1U) == 350.0 &&
                    evaluator.maximumRangeForSegment(1U) == 750.0,
                "Q exposes its range-cell intervals");
  context.check(evaluator.locateRangeSegment(349.0, 1U) == 0U &&
                    evaluator.locateRangeSegment(350.0, 0U) == 1U &&
                    evaluator.locateRangeSegment(350.0, 1U) == 1U &&
                    evaluator.locateRangeSegment(750.0, 0U) == 1U,
                "Q range locator preserves hints and right-open boundaries");

  for (const Vec2 position :
       std::array<Vec2, 4>{Vec2{.range = 0.0, .depth = 0.0},
                           Vec2{.range = 175.0, .depth = 50.0},
                           Vec2{.range = 350.0, .depth = 50.0},
                           Vec2{.range = 750.0, .depth = 100.0}}) {
    checkSameSample(context, concrete.evaluate(position, 0U),
                    evaluator.evaluate(position, 0U));
  }

  const auto lowerLeftNode =
      evaluator.evaluate(Vec2{.range = 0.0, .depth = 0.0}, 0U);
  const auto upperRightNode =
      evaluator.evaluate(Vec2{.range = 750.0, .depth = 100.0}, 0U);
  context.check(lowerLeftNode.soundSpeed == 1500.0 &&
                    upperRightNode.soundSpeed == 1540.0,
                "Q returns the tabulated sound speed exactly at grid nodes");

  const auto interior =
      evaluator.evaluate(Vec2{.range = 175.0, .depth = 50.0}, 0U);
  context.checkNear(interior.soundSpeed, 1515.0, 0.0,
                    "Q interpolates the real sound speed bilinearly");
  context.checkNear(interior.soundSpeedGradient.range, 30.0 / 350.0,
                    1.0e-15,
                    "Q returns dc/dr");
  context.checkNear(interior.soundSpeedGradient.depth, -0.1, 1.0e-15,
                    "Q returns dc/dz");
  context.checkNear(interior.soundSpeedHessian.rangeDepth, -0.2 / 350.0,
                    1.0e-15,
                    "Q returns the bilinear cross derivative");
  constexpr double finiteDifferenceStep = 1.0;
  const auto rangeMinus = evaluator.evaluate(
      Vec2{.range = 175.0 - finiteDifferenceStep, .depth = 50.0}, 0U);
  const auto rangePlus = evaluator.evaluate(
      Vec2{.range = 175.0 + finiteDifferenceStep, .depth = 50.0}, 0U);
  const auto depthMinus = evaluator.evaluate(
      Vec2{.range = 175.0, .depth = 50.0 - finiteDifferenceStep}, 0U);
  const auto depthPlus = evaluator.evaluate(
      Vec2{.range = 175.0, .depth = 50.0 + finiteDifferenceStep}, 0U);
  context.checkNear(
      (rangePlus.soundSpeedGradient.depth -
       rangeMinus.soundSpeedGradient.depth) /
          (2.0 * finiteDifferenceStep),
      interior.soundSpeedHessian.rangeDepth, 2.0e-16,
      "Q cross derivative matches the range finite difference of dc/dz");
  context.checkNear(
      (depthPlus.soundSpeedGradient.range -
       depthMinus.soundSpeedGradient.range) /
          (2.0 * finiteDifferenceStep),
      interior.soundSpeedHessian.rangeDepth, 2.0e-16,
      "Q cross derivative matches the depth finite difference of dc/dr");
  context.check(interior.soundSpeedHessian.rangeRange == 0.0 &&
                    interior.soundSpeedHessian.depthDepth == 0.0 &&
                    interior.imaginarySoundSpeed == 0.0,
                "Q geometry has zero pure second derivatives and no loss");
  context.checkNear(interior.density, 1050.0, 0.0,
                    "Q density comes from the ENV reference profile");
  context.check(
      profile.quadrilateralRealSoundSpeedAt(
          Vec2{.range = 175.0, .depth = 50.0}) == interior.soundSpeed,
      "Q launch-source helper uses the geometry evaluator arithmetic");
  context.check(interior.rangeSegmentIndex == 0U,
                "Q interior sample reports its first range cell");

  const auto internalRangeNode =
      evaluator.evaluate(Vec2{.range = 350.0, .depth = 50.0}, 0U);
  context.checkNear(internalRangeNode.soundSpeed, 1530.0, 0.0,
                    "Q is continuous at an internal range node");
  context.checkNear(internalRangeNode.soundSpeedGradient.range, 0.075,
                    1.0e-15,
                    "internal range node selects the right-hand cell");
  context.checkNear(internalRangeNode.soundSpeedGradient.depth, -0.2,
                    1.0e-15,
                    "internal range node preserves its column depth slope");
  context.checkNear(internalRangeNode.soundSpeedHessian.rangeDepth, -0.0005,
                    1.0e-15,
                    "internal range node uses the right-hand cross derivative");
  context.check(internalRangeNode.rangeSegmentIndex == 1U,
                "Q internal range node reports the right-hand cell");

  const auto rightEndpoint =
      evaluator.evaluate(Vec2{.range = 750.0, .depth = 50.0}, 0U);
  context.checkNear(rightEndpoint.soundSpeed, 1560.0, 0.0,
                    "Q accepts the final range node");
  context.checkNear(rightEndpoint.soundSpeedGradient.range, 0.075, 1.0e-15,
                    "final range node uses the last cell");
  context.check(rightEndpoint.rangeSegmentIndex == 1U,
                "final range node reports the last cell");

  checkSameSample(
      context, internalRangeNode,
      evaluator.evaluateAtSegments(
          Vec2{.range = 350.0, .depth = 50.0}, 0U, 1U));
  context.expectThrows<ValidationError>(
      [&evaluator] {
        static_cast<void>(evaluator.evaluateAtSegments(
            Vec2{.range = 350.0, .depth = 50.0}, 0U, 0U));
      },
      "Q explicit two-dimensional segment evaluation rejects the left cell "
      "at its open upper range boundary");

  context.expectThrows<ValidationError>(
      [&evaluator] {
        static_cast<void>(
            evaluator.evaluate(Vec2{.range = -1.0, .depth = 50.0}, 0U));
      },
      "Q rejects a query below the range grid");
  context.expectThrows<ValidationError>(
      [&evaluator] {
        static_cast<void>(
            evaluator.evaluate(Vec2{.range = 751.0, .depth = 50.0}, 0U));
      },
      "Q rejects a query above the range grid");
  context.expectThrows<ValidationError>(
      [&evaluator] {
        static_cast<void>(evaluator.evaluateAtSegment(
            Vec2{.range = 175.0, .depth = 101.0}, 0U));
      },
      "Q explicit-segment evaluation rejects a depth outside that segment");

  const GeometrySspEvaluator depthLocator(
      makeDepthLocatorQuadrilateralProfile());
  context.check(depthLocator.locateSegment(200.0, 0U) == 2U,
                "Q non-adjacent exact depth node selects the deeper cell");
  context.check(depthLocator.locateSegment(200.0, 1U) == 1U,
                "Q exact depth node retains an adjacent hinted cell");
}

void testQuadrilateralFrequencyEvaluation(Context& context) {
  const SoundSpeedProfile profile = makeQuadrilateralFrequencyProfile();
  const VolumeAttenuation thorp{
      .model = VolumeAttenuationModel::Thorp};
  const GeometrySspEvaluator geometry(profile);

  for (const double frequency : {1000.0, 5000.0}) {
    const QuadrilateralFrequencySsp concrete(profile, frequency, thorp);
    const FrequencySspEvaluator evaluator(profile, frequency, thorp);
    context.check(
        evaluator.interpolationKind() ==
                SspInterpolationKind::Quadrilateral &&
            evaluator.gradientContinuity() ==
                SspGradientContinuity::DiscontinuousAtNodes &&
            evaluator.frequency() == frequency &&
            evaluator.segmentCount() == 1U &&
            evaluator.rangeSegmentCount() == 2U,
        "Q frequency evaluator exposes its interpolation and segment shape");
    context.check(!evaluator.isLossless() &&
                      !evaluator.uniformComplexSoundSpeed().has_value(),
                  "nonuniform lossy Q does not expose a uniform shortcut");
    context.check(evaluator.minimumRangeForSegment(0U) == 0.0 &&
                      evaluator.maximumRangeForSegment(0U) == 350.0 &&
                      evaluator.minimumRangeForSegment(1U) == 350.0 &&
                      evaluator.maximumRangeForSegment(1U) == 750.0 &&
                      evaluator.locateRangeSegment(350.0, 0U) == 1U,
                  "Q frequency evaluator exposes right-open range cells");

    const Vec2 position{.range = 175.0, .depth = 25.0};
    const auto actual = evaluator.evaluateAtSegments(position, 0U, 0U);
    const auto concreteSample =
        concrete.evaluateAtSegments(position, 0U, 0U);
    const auto realSample = geometry.evaluateAtSegments(position, 0U, 0U);
    checkSameSample(context, concreteSample, actual);
    context.check(actual.soundSpeed == realSample.soundSpeed &&
                      actual.soundSpeedGradient ==
                          realSample.soundSpeedGradient &&
                      actual.soundSpeedHessian ==
                          realSample.soundSpeedHessian &&
                      actual.density == realSample.density,
                  "Q frequency real fields delegate exactly to Q geometry");

    const double topImaginary =
        convertAttenuation(profile.points()[0U].attenuation, thorp,
                           frequency, 1400.0, 0.0)
            .imaginarySoundSpeed;
    const double bottomImaginary =
        convertAttenuation(profile.points()[1U].attenuation, thorp,
                           frequency, 1800.0, 100.0)
            .imaginarySoundSpeed;
    const double expectedImaginary =
        0.75 * topImaginary + 0.25 * bottomImaginary;
    context.checkNear(actual.imaginarySoundSpeed, expectedImaginary,
                      1.0e-18,
                      "Q converts ENV reference nodes before depth interpolation");

    const double wrongTop =
        convertAttenuation(profile.points()[0U].attenuation, thorp,
                           frequency, 1520.0, 0.0)
            .imaginarySoundSpeed;
    const double wrongBottom =
        convertAttenuation(profile.points()[1U].attenuation, thorp,
                           frequency, 1510.0, 100.0)
            .imaginarySoundSpeed;
    context.check(
        std::abs(actual.imaginarySoundSpeed -
                 (0.75 * wrongTop + 0.25 * wrongBottom)) > 1.0e-4,
        "Q imaginary conversion does not substitute local matrix real speed");
    context.checkNear(actual.density, 1050.0, 0.0,
                      "Q frequency density remains ENV depth interpolation");

    const auto secondCell = evaluator.evaluate(
        Vec2{.range = 500.0, .depth = 25.0}, 0U, 1U);
    context.check(secondCell.rangeSegmentIndex == 1U &&
                      secondCell.imaginarySoundSpeed ==
                          actual.imaginarySoundSpeed,
                  "Q imaginary sound speed is depth-only across range cells");
    context.expectThrows<ValidationError>(
        [&evaluator] {
          static_cast<void>(evaluator.evaluate(
              Vec2{.range = -1.0, .depth = 25.0}, 0U, 0U));
        },
        "Q frequency evaluator rejects range below its grid");
    context.expectThrows<ValidationError>(
        [&evaluator] {
          static_cast<void>(evaluator.evaluate(
              Vec2{.range = 751.0, .depth = 25.0}, 0U, 0U));
        },
        "Q frequency evaluator rejects range above its grid");
  }

  const auto uniformGrid = std::make_shared<const QuadrilateralSspGrid>(
      QuadrilateralSspGrid{
          .rangesMeters = {0.0, 100.0},
          .speedsDepthMajor = {1500.0, 1500.0, 1500.0, 1500.0},
          .depthCount = 2U,
          .rangeCount = 2U});
  const SoundSpeedProfile uniformProfile(
      {{.depth = 0.0, .soundSpeed = 1400.0, .density = 1000.0},
       {.depth = 100.0, .soundSpeed = 1800.0, .density = 1000.0}},
      SspInterpolationKind::Quadrilateral, uniformGrid);
  const FrequencySspEvaluator uniform(uniformProfile, 1000.0);
  context.check(uniform.isLossless(), "zero-loss Q is lossless");
  context.check(
      uniform.uniformComplexSoundSpeed() ==
          std::optional<std::complex<double>>(
              std::complex<double>{1500.0, 0.0}),
      "uniform Q shortcut uses the real matrix rather than ENV reference c");
}

void testBiologicalConversionUsesSspNodeDepth(Context& context) {
  const VolumeAttenuation biological{
      .model = VolumeAttenuationModel::Biological,
      .parameters =
          std::make_shared<const BiologicalAttenuationLayers>(
              BiologicalAttenuationLayers{
                  BiologicalAttenuationLayer{
                      .minimumDepth = 100.0,
                      .maximumDepth = 100.0,
                      .resonanceFrequency = 1000.0,
                      .qualityFactor = 10.0,
                      .attenuationCoefficientDecibelsPerKilometer =
                          1.0e-6}}),
  };
  for (const SspInterpolationKind kind :
       {SspInterpolationKind::CLinear,
        SspInterpolationKind::N2Linear,
        SspInterpolationKind::Pchip,
        SspInterpolationKind::CubicSpline}) {
    const SoundSpeedProfile profile(
        {{.depth = 0.0, .soundSpeed = 1500.0, .density = 1000.0},
         {.depth = 100.0, .soundSpeed = 1500.0, .density = 1000.0},
         {.depth = 300.0, .soundSpeed = 1500.0, .density = 1000.0}},
        kind);
    const FrequencySspEvaluator evaluator(profile, 1000.0, biological);
    const auto inside = evaluator.evaluateAtSegment(
        {.range = 0.0, .depth = 100.0}, 0U);
    context.check(
        inside.imaginarySoundSpeed > 0.0,
        "every frequency SSP backend converts the biological layer-interior "
        "node using its own depth");
  }
}

}  // namespace

int main() {
  Context context;
  testCLinearDispatchIsExact(context);
  testRecognizedKindsDoNotFallback(context);
  testQuadrilateralDispatchAndFormula(context);
  testQuadrilateralFrequencyEvaluation(context);
  testBiologicalConversionUsesSspNodeDepth(context);
  if (context.failureCount() != 0) {
    std::cerr << context.failureCount()
              << " sound-speed evaluator test assertion(s) failed\n";
    return 1;
  }
  std::cout << "All Bellhop F2CPP sound-speed evaluator tests passed\n";
  return 0;
}
