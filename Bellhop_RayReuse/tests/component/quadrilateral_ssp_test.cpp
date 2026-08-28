#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "rayreuse/acoustics/attenuation.hpp"
#include "rayreuse/error.hpp"
#include "rayreuse/io/environment_parser.hpp"
#include "rayreuse/model/c_linear_frequency_ssp.hpp"
#include "rayreuse/model/c_linear_ssp.hpp"
#include "rayreuse/model/environment.hpp"
#include "rayreuse/model/quadrilateral_frequency_ssp.hpp"
#include "rayreuse/model/quadrilateral_ssp.hpp"
#include "rayreuse/model/sound_speed_evaluator.hpp"
#include "support/test_harness.hpp"

namespace {

using rayreuse::AttenuationUnit;
using rayreuse::BellhopError;
using rayreuse::convertAttenuation;
using rayreuse::CLinearFrequencySsp;
using rayreuse::CLinearSsp;
using rayreuse::EnvironmentParser;
using rayreuse::FrequencySspEvaluator;
using rayreuse::GeometrySspEvaluator;
using rayreuse::QuadrilateralFrequencySsp;
using rayreuse::QuadrilateralSsp;
using rayreuse::QuadrilateralSspGrid;
using rayreuse::RawAttenuation;
using rayreuse::SharedQuadrilateralSspGrid;
using rayreuse::SoundSpeedPoint;
using rayreuse::SoundSpeedProfile;
using rayreuse::SoundSpeedSample;
using rayreuse::SspGradientContinuity;
using rayreuse::SspInterpolationKind;
using rayreuse::sspGradientContinuity;
using rayreuse::ValidationError;
using rayreuse::Vec2;
using rayreuse::VolumeAttenuationModel;
using rayreuse::test::Context;

// Grid of the shared standard case q_range_dependent_cross_gradient: depths
// [0, 100] m, ranges [0, 350, 800] m, speeds [[1500, 1540, 1580],
// [1500, 1520, 1540]]. Expected values below are hand-derived from the
// bilinear formula, never sampled from the evaluator under test.
SharedQuadrilateralSspGrid makeCrossGradientGrid() {
  return std::make_shared<const QuadrilateralSspGrid>(QuadrilateralSspGrid{
      .rangesMeters = {0.0, 350.0, 800.0},
      .speedsDepthMajor = {1500.0, 1540.0, 1580.0,
                           1500.0, 1520.0, 1540.0},
      .depthCount = 2U,
      .rangeCount = 3U});
}

SoundSpeedProfile makeCrossGradientProfile() {
  return SoundSpeedProfile(
      {{.depth = 0.0, .soundSpeed = 1490.0, .density = 1000.0},
       {.depth = 100.0, .soundSpeed = 1490.0, .density = 1100.0}},
      SspInterpolationKind::Quadrilateral, makeCrossGradientGrid());
}

SoundSpeedProfile makeDepthLocatorProfile() {
  return SoundSpeedProfile(
      {{.depth = 0.0, .soundSpeed = 1500.0, .density = 1000.0},
       {.depth = 100.0, .soundSpeed = 1520.0, .density = 1000.0},
       {.depth = 200.0, .soundSpeed = 1540.0, .density = 1000.0},
       {.depth = 300.0, .soundSpeed = 1560.0, .density = 1000.0}},
      SspInterpolationKind::Quadrilateral,
      std::make_shared<const QuadrilateralSspGrid>(QuadrilateralSspGrid{
          .rangesMeters = {0.0, 1000.0},
          .speedsDepthMajor = {1500.0, 1510.0,
                               1520.0, 1530.0,
                               1540.0, 1550.0,
                               1560.0, 1570.0},
          .depthCount = 4U,
          .rangeCount = 2U}));
}

// ENV reference speeds deliberately differ from every Q-matrix value so the
// frequency evaluator must prove it converts the reference nodes, not the
// local matrix real sound speed.
SoundSpeedProfile makeAttenuatingProfile() {
  return SoundSpeedProfile(
      {{.depth = 0.0,
        .soundSpeed = 1400.0,
        .density = 1000.0,
        .attenuation = {.value = 0.05,
                        .unit = AttenuationUnit::DecibelsPerWavelength}},
       {.depth = 100.0,
        .soundSpeed = 1800.0,
        .density = 1200.0,
        .attenuation = {.value = 0.10,
                        .unit = AttenuationUnit::DecibelsPerWavelength}}},
      SspInterpolationKind::Quadrilateral, makeCrossGradientGrid());
}

SoundSpeedProfile makeThorpProfile() {
  const RawAttenuation thorp{
      .value = 0.0,
      .unit = AttenuationUnit::DecibelsPerWavelength,
      .volumeModel = VolumeAttenuationModel::Thorp};
  return SoundSpeedProfile(
      {{.depth = 0.0,
        .soundSpeed = 1400.0,
        .density = 1000.0,
        .attenuation = thorp},
       {.depth = 100.0,
        .soundSpeed = 1800.0,
        .density = 1200.0,
        .attenuation = thorp}},
      SspInterpolationKind::Quadrilateral, makeCrossGradientGrid());
}

void checkSameSample(Context& context, const SoundSpeedSample& expected,
                     const SoundSpeedSample& actual, const char* what) {
  context.check(expected.soundSpeed == actual.soundSpeed &&
                    expected.imaginarySoundSpeed ==
                        actual.imaginarySoundSpeed &&
                    expected.soundSpeedGradient == actual.soundSpeedGradient &&
                    expected.soundSpeedHessian == actual.soundSpeedHessian &&
                    expected.density == actual.density &&
                    expected.segmentIndex == actual.segmentIndex &&
                    expected.rangeSegmentIndex == actual.rangeSegmentIndex,
                what);
}

void testGridValidation(Context& context) {
  const auto validGrid = makeCrossGradientGrid();
  const std::vector<SoundSpeedPoint> points{
      {.depth = 0.0, .soundSpeed = 1490.0, .density = 1000.0},
      {.depth = 100.0, .soundSpeed = 1490.0, .density = 1100.0}};

  context.check(validGrid->rangesMeters.size() == 3U &&
                    validGrid->speedsDepthMajor.size() == 6U,
                "cross-gradient fixture carries the shared case dimensions");

  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(SoundSpeedProfile(
            points, SspInterpolationKind::Quadrilateral,
            std::make_shared<const QuadrilateralSspGrid>(QuadrilateralSspGrid{
                .rangesMeters = {0.0, 350.0, 350.0},
                .speedsDepthMajor = {1500.0, 1540.0, 1580.0,
                                     1500.0, 1520.0, 1540.0},
                .depthCount = 2U,
                .rangeCount = 3U})));
      },
      "non-increasing quadrilateral ranges are rejected");
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(SoundSpeedProfile(
            points, SspInterpolationKind::Quadrilateral,
            std::make_shared<const QuadrilateralSspGrid>(QuadrilateralSspGrid{
                .rangesMeters = {0.0, 350.0},
                .speedsDepthMajor = {1500.0, 1540.0, 1580.0,
                                     1500.0, 1520.0, 1540.0},
                .depthCount = 2U,
                .rangeCount = 3U})));
      },
      "a range column count that disagrees with the range vector is rejected");
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(SoundSpeedProfile(
            points, SspInterpolationKind::Quadrilateral,
            std::make_shared<const QuadrilateralSspGrid>(QuadrilateralSspGrid{
                .rangesMeters = {0.0, 350.0, 800.0},
                .speedsDepthMajor = {1500.0, 1540.0, 1580.0,
                                     1500.0, 0.0, 1540.0},
                .depthCount = 2U,
                .rangeCount = 3U})));
      },
      "a non-positive matrix sound speed is rejected");
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(SoundSpeedProfile(
            points, SspInterpolationKind::Quadrilateral,
            std::make_shared<const QuadrilateralSspGrid>(QuadrilateralSspGrid{
                .rangesMeters = {0.0, 350.0, 800.0},
                .speedsDepthMajor = {1500.0, 1540.0, 1580.0, 1500.0},
                .depthCount = 2U,
                .rangeCount = 3U})));
      },
      "a matrix shape that disagrees with depthCount x rangeCount is rejected");
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(SoundSpeedProfile(
            points, SspInterpolationKind::Quadrilateral,
            std::make_shared<const QuadrilateralSspGrid>(QuadrilateralSspGrid{
                .rangesMeters = {0.0, 350.0, 800.0},
                .speedsDepthMajor = {1500.0, 1540.0, 1580.0,
                                     1500.0, 1520.0, 1540.0},
                .depthCount = 3U,
                .rangeCount = 3U})));
      },
      "a grid depth count that disagrees with the ENV profile is rejected");
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(SoundSpeedProfile(
            points, SspInterpolationKind::Quadrilateral, {}));
      },
      "a quadrilateral profile without a grid is rejected");
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(SoundSpeedProfile(
            points, SspInterpolationKind::CLinear, validGrid));
      },
      "a non-quadrilateral profile cannot carry a quadrilateral grid");

  const SoundSpeedProfile rangeIndependent(points,
                                            SspInterpolationKind::CLinear);
  context.check(!rangeIndependent.quadrilateralGrid(),
                "non-Q profiles hold no quadrilateral grid storage");
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(
            rangeIndependent.quadrilateralRealSoundSpeedAt(Vec2{}));
      },
      "non-Q profiles expose no quadrilateral launch helper");
}

void testExactRangeNodeOwnership(Context& context) {
  const QuadrilateralSsp profile(makeCrossGradientProfile());

  context.check(profile.rangeSegmentCount() == 2U,
                "Q exposes one fewer range cell than range columns");
  context.check(profile.locateRangeSegment(350.0, 0U) == 1U,
                "an internal exact range node selects the right-hand cell");
  context.check(profile.locateRangeSegment(350.0, 1U) == 1U,
                "an internal exact range node retains the hinted right cell");
  context.check(profile.locateRangeSegment(800.0, 0U) == 1U,
                "the final exact range node selects the last cell");
  context.check(profile.locateRangeSegment(349.0, 1U) == 0U,
                "a query left of its hinted cell relocates to the left cell");
  context.check(profile.locateRangeSegment(349.0, 0U) == 0U,
                "a hinted interior query keeps its cell");

  const auto internalNode =
      profile.evaluate(Vec2{.range = 350.0, .depth = 50.0}, 0U);
  context.checkNear(internalNode.soundSpeed, 1530.0, 0.0,
                    "Q is continuous at an internal range node");
  context.checkNear(internalNode.soundSpeedGradient.range, 30.0 / 450.0,
                    1.0e-15, "internal range node uses the right-hand cell");
  context.checkNear(internalNode.soundSpeedGradient.depth, -0.2, 1.0e-15,
                    "internal range node preserves its column depth slope");
  context.checkNear(internalNode.soundSpeedHessian.rangeDepth, -0.2 / 450.0,
                    1.0e-18,
                    "internal range node uses the right-hand cross derivative");
  context.check(internalNode.rangeSegmentIndex == 1U,
                "an internal exact range node reports the right-hand cell");

  const auto finalNode =
      profile.evaluate(Vec2{.range = 800.0, .depth = 50.0}, 0U);
  context.checkNear(finalNode.soundSpeed, 1560.0, 0.0,
                    "Q accepts the final range node");
  context.checkNear(finalNode.soundSpeedGradient.range, 30.0 / 450.0, 1.0e-15,
                    "the final range node uses the last cell");
  context.check(finalNode.rangeSegmentIndex == 1U,
                "the final exact range node reports the last cell");

  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(profile.evaluateAtSegments(
            Vec2{.range = 350.0, .depth = 50.0}, 0U, 0U));
      },
      "an explicit left cell rejects its open upper range boundary");
}

void testExactDepthNodeOwnership(Context& context) {
  const QuadrilateralSsp profile(makeDepthLocatorProfile());

  context.check(profile.segmentCount() == 3U,
                "the depth-locator fixture has three depth cells");
  context.check(profile.locateSegment(200.0, 0U) == 2U,
                "a non-adjacent exact depth node selects the deeper cell");
  context.check(profile.locateSegment(200.0, 1U) == 1U,
                "an exact depth node retains an adjacent hinted cell");
  context.check(profile.locateSegment(200.0, 2U) == 2U,
                "an exact depth node retains the hinted right cell");
  context.check(profile.locateSegment(-5.0, 1U) == 0U,
                "a depth above the profile selects the first cell");
  context.check(profile.locateSegment(305.0, 0U) == 2U,
                "a depth below the profile selects the last cell");
}

void testOutsideRangeRejection(Context& context) {
  const QuadrilateralSsp profile(makeCrossGradientProfile());
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(
            profile.evaluate(Vec2{.range = -1.0, .depth = 50.0}, 0U));
      },
      "Q rejects a query before its range grid");
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(
            profile.evaluate(Vec2{.range = 800.0001, .depth = 50.0}, 0U));
      },
      "Q rejects a query after its range grid");
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(profile.locateRangeSegment(
            std::numeric_limits<double>::quiet_NaN(), 0U));
      },
      "Q rejects a non-finite range query");
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(profile.locateRangeSegment(100.0, 2U));
      },
      "Q rejects an out-of-range previous range hint");
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(profile.minimumRangeForSegment(2U));
      },
      "Q rejects an out-of-range minimum range segment index");
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(profile.maximumRangeForSegment(2U));
      },
      "Q rejects an out-of-range maximum range segment index");
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(makeCrossGradientProfile().quadrilateralRealSoundSpeedAt(
            Vec2{.range = 175.0, .depth = 101.0}));
      },
      "the launch helper rejects a depth outside the ENV profile");
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(makeCrossGradientProfile().quadrilateralRealSoundSpeedAt(
            Vec2{.range = 900.0, .depth = 50.0}));
      },
      "the launch helper rejects a range outside the grid");
}

void testBilinearValuesAndGradients(Context& context) {
  const QuadrilateralSsp profile(makeCrossGradientProfile());

  const auto interior =
      profile.evaluate(Vec2{.range = 175.0, .depth = 50.0}, 0U);
  context.checkNear(interior.soundSpeed, 1515.0, 0.0,
                    "Q interpolates the real sound speed bilinearly");
  context.checkNear(interior.soundSpeedGradient.range, 30.0 / 350.0, 1.0e-15,
                    "Q returns dc/dr");
  context.checkNear(interior.soundSpeedGradient.depth, -0.1, 1.0e-15,
                    "Q returns dc/dz");
  context.checkNear(interior.soundSpeedHessian.rangeDepth, -0.2 / 350.0,
                    1.0e-18, "Q returns the bilinear cross derivative");
  context.check(interior.soundSpeedHessian.rangeRange == 0.0 &&
                    interior.soundSpeedHessian.depthDepth == 0.0 &&
                    interior.imaginarySoundSpeed == 0.0,
                "Q geometry has zero pure second derivatives and no loss");

  const auto quarter = profile.evaluate(Vec2{.range = 87.5, .depth = 25.0}, 0U);
  context.checkNear(quarter.soundSpeed, 1508.75, 0.0,
                    "Q bilinear value at the quarter point of the first cell");
  context.checkNear(quarter.soundSpeedGradient.range, 0.1, 1.0e-15,
                    "Q dc/dr at the quarter point");
  context.checkNear(quarter.soundSpeedGradient.depth, -0.05, 1.0e-15,
                    "Q dc/dz at the quarter point");
  context.checkNear(quarter.soundSpeedHessian.rangeDepth, -0.2 / 350.0, 1.0e-18,
                    "Q cross derivative at the quarter point");

  constexpr double step = 1.0;
  const auto rangeMinus = profile.evaluate(
      Vec2{.range = 175.0 - step, .depth = 50.0}, 0U);
  const auto rangePlus = profile.evaluate(
      Vec2{.range = 175.0 + step, .depth = 50.0}, 0U);
  const auto depthMinus = profile.evaluate(
      Vec2{.range = 175.0, .depth = 50.0 - step}, 0U);
  const auto depthPlus = profile.evaluate(
      Vec2{.range = 175.0, .depth = 50.0 + step}, 0U);
  context.checkNear(
      (rangePlus.soundSpeedGradient.depth -
       rangeMinus.soundSpeedGradient.depth) /
          (2.0 * step),
      interior.soundSpeedHessian.rangeDepth, 2.0e-16,
      "Q cross derivative matches the range finite difference of dc/dz");
  context.checkNear(
      (depthPlus.soundSpeedGradient.range -
       depthMinus.soundSpeedGradient.range) /
          (2.0 * step),
      interior.soundSpeedHessian.rangeDepth, 2.0e-16,
      "Q cross derivative matches the depth finite difference of dc/dr");

  const auto lowerLeft =
      profile.evaluate(Vec2{.range = 0.0, .depth = 0.0}, 0U);
  const auto upperRight =
      profile.evaluate(Vec2{.range = 800.0, .depth = 100.0}, 0U);
  context.check(lowerLeft.soundSpeed == 1500.0 &&
                    upperRight.soundSpeed == 1540.0,
                "Q returns the tabulated sound speed exactly at grid nodes");
  const auto lastCellMid =
      profile.evaluate(Vec2{.range = 575.0, .depth = 50.0}, 0U);
  context.checkNear(lastCellMid.soundSpeed,
                    0.5 * 1530.0 + 0.5 * 1560.0, 0.0,
                    "Q interpolates the last range cell at its midpoint");
  context.check(lastCellMid.rangeSegmentIndex == 1U,
                "the last-cell midpoint reports range cell one");

  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(profile.evaluateAtSegment(
            Vec2{.range = 175.0, .depth = 101.0}, 0U));
      },
      "Q explicit-segment evaluation rejects a depth outside that segment");
  const auto relocated = profile.evaluateAtSegment(
      Vec2{.range = 575.0, .depth = 50.0}, 0U);
  context.check(relocated.rangeSegmentIndex == 1U &&
                    relocated.soundSpeed == lastCellMid.soundSpeed,
                "single-segment entry relocates the range cell from zero");
}

void testDensityInterpolation(Context& context) {
  const QuadrilateralSsp profile(makeCrossGradientProfile());
  const auto quarter =
      profile.evaluate(Vec2{.range = 87.5, .depth = 25.0}, 0U);
  const auto middle = profile.evaluate(Vec2{.range = 175.0, .depth = 50.0}, 0U);
  const auto otherCell =
      profile.evaluate(Vec2{.range = 575.0, .depth = 50.0}, 0U);
  context.checkNear(quarter.density, 1025.0, 0.0,
                    "Q density interpolates the ENV reference profile");
  context.checkNear(middle.density, 1050.0, 0.0,
                    "Q density at the half depth");
  context.check(otherCell.density == middle.density,
                "Q density is range independent");
}

void testEvaluatorDispatch(Context& context) {
  const SoundSpeedProfile profile = makeCrossGradientProfile();
  const QuadrilateralSsp concrete(profile);
  const GeometrySspEvaluator evaluator(profile);

  context.check(
      sspGradientContinuity(SspInterpolationKind::Quadrilateral) ==
          SspGradientContinuity::DiscontinuousAtNodes,
      "Q interpolation keeps a gradient jump at cell boundaries");
  context.check(evaluator.interpolationKind() ==
                    SspInterpolationKind::Quadrilateral &&
                    evaluator.gradientContinuity() ==
                        SspGradientContinuity::DiscontinuousAtNodes &&
                    evaluator.segmentCount() == 1U,
      "Q dispatches to the discontinuous-gradient geometry backend");
  context.check(evaluator.rangeSegmentCount() == 2U &&
                    evaluator.minimumRangeForSegment(0U) == 0.0 &&
                    evaluator.maximumRangeForSegment(0U) == 350.0 &&
                    evaluator.minimumRangeForSegment(1U) == 350.0 &&
                    evaluator.maximumRangeForSegment(1U) == 800.0,
                "Q exposes its range-cell intervals");
  context.check(evaluator.locateRangeSegment(349.0, 1U) == 0U &&
                    evaluator.locateRangeSegment(350.0, 0U) == 1U &&
                    evaluator.locateRangeSegment(350.0, 1U) == 1U &&
                    evaluator.locateRangeSegment(800.0, 0U) == 1U,
                "Q range locator preserves hints and right-open boundaries");

  for (const Vec2 position :
       std::array<Vec2, 4>{Vec2{.range = 0.0, .depth = 0.0},
                           Vec2{.range = 175.0, .depth = 50.0},
                           Vec2{.range = 350.0, .depth = 50.0},
                           Vec2{.range = 800.0, .depth = 100.0}}) {
    checkSameSample(context, concrete.evaluate(position, 0U),
                    evaluator.evaluate(position, 0U),
                    "geometry wrapper preserves the concrete Q sample");
  }
  checkSameSample(
      context, concrete.evaluateAtSegments(Vec2{.range = 350.0, .depth = 50.0},
                                           0U, 1U),
      evaluator.evaluateAtSegments(Vec2{.range = 350.0, .depth = 50.0}, 0U, 1U),
      "geometry wrapper preserves the explicit two-segment entry");

  const auto interior = evaluator.evaluate(Vec2{.range = 175.0, .depth = 50.0}, 0U);
  context.check(
      profile.quadrilateralRealSoundSpeedAt(
          Vec2{.range = 175.0, .depth = 50.0}) == interior.soundSpeed,
      "Q launch-source helper uses the geometry evaluator arithmetic");

  const std::vector<SoundSpeedPoint> referencePoints = profile.points();
  for (const SspInterpolationKind kind :
       {SspInterpolationKind::CLinear, SspInterpolationKind::Pchip,
        SspInterpolationKind::N2Linear, SspInterpolationKind::CubicSpline}) {
    const SoundSpeedProfile rangeIndependent(referencePoints, kind);
    const GeometrySspEvaluator other(rangeIndependent);
    const auto sample = other.evaluate(Vec2{.range = 175.0, .depth = 50.0}, 0U);
    context.check(sample.soundSpeed != interior.soundSpeed,
                  "Q results differ from the range-independent backends");
    context.check(
        other.rangeSegmentCount() == 1U &&
            other.minimumRangeForSegment(0U) ==
                -std::numeric_limits<double>::infinity() &&
            other.maximumRangeForSegment(0U) ==
                std::numeric_limits<double>::infinity() &&
            other.locateRangeSegment(25.0, 0U) == 0U &&
            other.evaluateAtSegments(Vec2{.range = 25.0, .depth = 50.0}, 0U,
                                     0U)
                .rangeSegmentIndex == 0U,
        "range-independent backends retain range segment zero");
    context.expectThrows<ValidationError>(
        [&] {
          static_cast<void>(other.evaluateAtSegments(
              Vec2{.range = 25.0, .depth = 50.0}, 0U, 1U));
        },
        "range-independent backends reject a nonzero range segment");
    context.expectThrows<ValidationError>(
        [&] {
          static_cast<void>(other.evaluate(
              Vec2{.range = 25.0, .depth = 50.0}, 0U, 1U));
        },
        "range-independent backends reject a nonzero previous range hint");
    context.expectThrows<ValidationError>(
        [&] {
          static_cast<void>(other.locateRangeSegment(
              std::numeric_limits<double>::quiet_NaN(), 0U));
        },
        "range-independent backends reject a non-finite range");
  }
}

void testFrequencyImaginaryAnchors(Context& context) {
  const SoundSpeedProfile profile = makeAttenuatingProfile();
  const GeometrySspEvaluator geometry(profile);

  const double frequency = 1000.0;
  const QuadrilateralFrequencySsp concrete(profile, frequency);
  const FrequencySspEvaluator evaluator(profile, frequency);
  context.check(
      evaluator.interpolationKind() == SspInterpolationKind::Quadrilateral &&
          evaluator.gradientContinuity() ==
              SspGradientContinuity::DiscontinuousAtNodes &&
          evaluator.frequency() == frequency &&
          evaluator.segmentCount() == 1U &&
          evaluator.rangeSegmentCount() == 2U &&
          evaluator.locateRangeSegment(350.0, 0U) == 1U &&
          evaluator.minimumRangeForSegment(0U) == 0.0 &&
          evaluator.maximumRangeForSegment(1U) == 800.0,
      "Q frequency evaluator exposes its interpolation and segment shape");
  context.check(!evaluator.isLossless() &&
                    !evaluator.uniformComplexSoundSpeed().has_value(),
                "nonuniform lossy Q does not expose a uniform shortcut");

  const Vec2 position{.range = 175.0, .depth = 25.0};
  const auto actual = evaluator.evaluateAtSegments(position, 0U, 0U);
  const auto concreteSample = concrete.evaluateAtSegments(position, 0U, 0U);
  const auto realSample = geometry.evaluateAtSegments(position, 0U, 0U);
  checkSameSample(context, concreteSample, actual,
                  "frequency wrapper preserves the concrete Q sample");
  context.check(actual.soundSpeed == realSample.soundSpeed &&
                    actual.soundSpeedGradient ==
                        realSample.soundSpeedGradient &&
                    actual.soundSpeedHessian == realSample.soundSpeedHessian &&
                    actual.density == realSample.density,
                "Q frequency real fields delegate exactly to Q geometry");

  // Closed-form anchor for dB-per-wavelength:
  // cImag = value * referenceSpeed / (kDecibelsPerNeper * 2 * pi).
  constexpr double kDecibelsPerNeper = 8.6858896;
  const double topImaginary =
      0.05 * 1400.0 / (kDecibelsPerNeper * 2.0 * std::acos(-1.0));
  const double bottomImaginary =
      0.10 * 1800.0 / (kDecibelsPerNeper * 2.0 * std::acos(-1.0));
  context.checkNear(
      actual.imaginarySoundSpeed,
      0.75 * topImaginary + 0.25 * bottomImaginary, 1.0e-9,
      "Q converts the ENV reference nodes before depth interpolation");

  const double convertedTop = convertAttenuation(
      profile.points()[0U].attenuation, frequency, 1400.0).imaginarySoundSpeed;
  const double convertedBottom =
      convertAttenuation(profile.points()[1U].attenuation, frequency, 1800.0)
          .imaginarySoundSpeed;
  context.checkNear(actual.imaginarySoundSpeed,
                    0.75 * convertedTop + 0.25 * convertedBottom, 1.0e-18,
                    "Q imaginary interpolation matches the node conversion");
  context.checkNear(convertedTop, topImaginary, 1.0e-12,
                    "dB-per-wavelength anchors agree with the converter");

  const double wrongTop = convertAttenuation(
      profile.points()[0U].attenuation, frequency, 1540.0).imaginarySoundSpeed;
  const double wrongBottom =
      convertAttenuation(profile.points()[1U].attenuation, frequency, 1520.0)
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
  const auto deeper = evaluator.evaluate(
      Vec2{.range = 500.0, .depth = 75.0}, 0U, 1U);
  context.checkNear(deeper.imaginarySoundSpeed,
                    0.25 * topImaginary + 0.75 * bottomImaginary, 1.0e-9,
                    "Q imaginary interpolation follows depth alone");

  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(
            evaluator.evaluate(Vec2{.range = -1.0, .depth = 25.0}, 0U, 0U));
      },
      "Q frequency evaluator rejects range below its grid");
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(evaluator.evaluate(
            Vec2{.range = 800.0001, .depth = 25.0}, 0U, 0U));
      },
      "Q frequency evaluator rejects range above its grid");

  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(QuadrilateralFrequencySsp(
            profile, std::numeric_limits<double>::quiet_NaN()));
      },
      "non-finite frequency is rejected");
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(QuadrilateralFrequencySsp(profile, -50.0));
      },
      "negative frequency is rejected");

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

void testTwoFrequencyIndependence(Context& context) {
  const SoundSpeedProfile profile = makeThorpProfile();
  const Vec2 position{.range = 175.0, .depth = 25.0};

  QuadrilateralFrequencySsp low(profile, 1000.0);
  const auto lowBefore = low.evaluateAtSegments(position, 0U, 0U);
  const auto lowAgain = low.evaluateAtSegments(position, 0U, 0U);
  context.check(lowBefore.imaginarySoundSpeed ==
                    lowAgain.imaginarySoundSpeed,
                "repeated Q frequency evaluation is bit-stable");

  QuadrilateralFrequencySsp high(profile, 2000.0);
  const auto highSample = high.evaluateAtSegments(position, 0U, 0U);
  const auto lowAfter = low.evaluateAtSegments(position, 0U, 0U);
  context.check(
      lowBefore.imaginarySoundSpeed != highSample.imaginarySoundSpeed,
      "Thorp-converted Q imaginary sound speed depends on frequency");
  context.check(
      lowBefore.imaginarySoundSpeed == lowAfter.imaginarySoundSpeed,
      "constructing a second frequency evaluator leaves the first unchanged");
  context.check(
      lowBefore.soundSpeed == highSample.soundSpeed &&
          lowBefore.soundSpeedGradient == highSample.soundSpeedGradient &&
          lowBefore.soundSpeedHessian == highSample.soundSpeedHessian,
      "Q real fields stay identical across frequencies");

  const double lowTop =
      convertAttenuation(profile.points()[0U].attenuation, 1000.0, 1400.0)
          .imaginarySoundSpeed;
  const double lowBottom =
      convertAttenuation(profile.points()[1U].attenuation, 1000.0, 1800.0)
          .imaginarySoundSpeed;
  context.checkNear(lowBefore.imaginarySoundSpeed,
                    0.75 * lowTop + 0.25 * lowBottom, 1.0e-18,
                    "low-frequency imaginary matches its node conversion");
  const double highTop =
      convertAttenuation(profile.points()[0U].attenuation, 2000.0, 1400.0)
          .imaginarySoundSpeed;
  const double highBottom =
      convertAttenuation(profile.points()[1U].attenuation, 2000.0, 1800.0)
          .imaginarySoundSpeed;
  context.checkNear(highSample.imaginarySoundSpeed,
                    0.75 * highTop + 0.25 * highBottom, 1.0e-18,
                    "high-frequency imaginary matches its node conversion");

  const FrequencySspEvaluator cLinear(
      SoundSpeedProfile(profile.points(), SspInterpolationKind::CLinear),
      1000.0);
  const auto cLinearSample = cLinear.evaluate(position, 0U);
  context.check(
      cLinearSample.soundSpeed != lowBefore.soundSpeed,
      "frequency dispatch does not confuse Q real speed with C-linear");
  context.check(
      cLinearSample.imaginarySoundSpeed == lowBefore.imaginarySoundSpeed,
      "Q imaginary sound speed matches the depth-only reference conversion");
}

class TempDirectory {
 public:
  TempDirectory() {
    path_ = std::filesystem::temp_directory_path() /
            ("rayreuse_q_ssp_test_" +
             std::to_string(static_cast<long long>(
                 std::chrono::system_clock::to_time_t(
                     std::chrono::system_clock::now()))));
    std::filesystem::create_directories(path_);
  }
  ~TempDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }
  TempDirectory(const TempDirectory&) = delete;
  TempDirectory& operator=(const TempDirectory&) = delete;

  [[nodiscard]] const std::filesystem::path& path() const noexcept {
    return path_;
  }

 private:
  std::filesystem::path path_;
};

void writeFile(const std::filesystem::path& path, const std::string& contents) {
  std::ofstream output(path);
  output << contents;
}

constexpr const char* kQuadrilateralEnv = R"ENV('A01 quadrilateral parser fixture'
1000.0
1
'QVW'
2  0.0  100.0
0.0    1500.0  0.0  1.0  0.0  0.0 /
100.0  1500.0  0.0  1.0  0.0  0.0 /
'R' 0.0
1
50.0 /
11
0.0  100.0 /
51
0.02  0.70 /
'CC'
101
-45.0  45.0 /
1.0  101.0  0.71
'MS' 1.0  0.5
3  5  'P')ENV";

constexpr const char* kCrossGradientSsp = R"SSP(3
0.0  0.35  0.80
1500.0  1540.0  1580.0
1500.0  1520.0  1540.0)SSP";

void testParserQuadrilateral(Context& context) {
  const TempDirectory directory;
  const std::filesystem::path environmentPath = directory.path() / "q_case.env";
  const std::filesystem::path sspPath = directory.path() / "q_case.ssp";
  writeFile(environmentPath, kQuadrilateralEnv);
  writeFile(sspPath, kCrossGradientSsp);

  const auto parsed = EnvironmentParser::parseFile(environmentPath);
  const SoundSpeedProfile& profile =
      parsed.simulationCase.environment().soundSpeedProfile();
  context.check(profile.interpolationKind() ==
                    SspInterpolationKind::Quadrilateral,
                "top option 'QVW' parses as quadrilateral SSP interpolation");
  const SharedQuadrilateralSspGrid& grid = profile.quadrilateralGrid();
  context.check(static_cast<bool>(grid) && grid->depthCount == 2U &&
                    grid->rangeCount == 3U &&
                    grid->rangesMeters ==
                        std::vector<double>({0.0, 350.0, 800.0}) &&
                    grid->speedsDepthMajor ==
                        std::vector<double>({1500.0, 1540.0, 1580.0, 1500.0,
                                             1520.0, 1540.0}),
                "the sibling .ssp grid is loaded with km-to-m conversion");
  const GeometrySspEvaluator evaluator(profile);
  context.checkNear(
      evaluator.evaluate(Vec2{.range = 175.0, .depth = 50.0}, 0U).soundSpeed,
      1515.0, 0.0,
      "the parsed grid reproduces the cross-gradient bilinear anchor");

  context.expectThrows<ValidationError>(
      [&] {
        std::istringstream input(kQuadrilateralEnv);
        static_cast<void>(EnvironmentParser::parse(input, "stream_q.env"));
      },
      "quadrilateral SSP through a stream parse requires parseFile");

  const std::filesystem::path lonelyPath =
      directory.path() / "lonely_q.env";
  writeFile(lonelyPath, kQuadrilateralEnv);
  context.expectThrows<BellhopError>(
      [&] {
        static_cast<void>(EnvironmentParser::parseFile(lonelyPath));
      },
      "a missing sibling .ssp file fails explicitly");

  const std::filesystem::path nonMonotonicPath =
      directory.path() / "nonmonotonic_q.env";
  const std::filesystem::path nonMonotonicSsp =
      directory.path() / "nonmonotonic_q.ssp";
  writeFile(nonMonotonicPath, kQuadrilateralEnv);
  writeFile(nonMonotonicSsp,
            "3\n0.0  0.35  0.35\n1500.0  1540.0  1580.0\n1500.0  1520.0  "
            "1540.0\n");
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(EnvironmentParser::parseFile(nonMonotonicPath));
      },
      "non-increasing .ssp ranges fail explicitly");

  const std::filesystem::path shortRowPath =
      directory.path() / "shortrow_q.env";
  const std::filesystem::path shortRowSsp =
      directory.path() / "shortrow_q.ssp";
  writeFile(shortRowPath, kQuadrilateralEnv);
  writeFile(shortRowSsp,
            "3\n0.0  0.35  0.80\n1500.0  1540.0  1580.0\n1500.0  1520.0\n");
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(EnvironmentParser::parseFile(shortRowPath));
      },
      "a short .ssp speed row fails explicitly");
}

}  // namespace

int main() {
  Context context;
  testGridValidation(context);
  testExactRangeNodeOwnership(context);
  testExactDepthNodeOwnership(context);
  testOutsideRangeRejection(context);
  testBilinearValuesAndGradients(context);
  testDensityInterpolation(context);
  testEvaluatorDispatch(context);
  testFrequencyImaginaryAnchors(context);
  testTwoFrequencyIndependence(context);
  testParserQuadrilateral(context);
  if (context.failureCount() != 0) {
    std::cerr << context.failureCount()
              << " quadrilateral SSP test assertion(s) failed\n";
    return 1;
  }
  std::cout << "All Bellhop RayReuse quadrilateral SSP tests passed\n";
  return 0;
}
