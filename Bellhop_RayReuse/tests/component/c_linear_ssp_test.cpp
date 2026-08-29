#include "rayreuse/acoustics/attenuation.hpp"
#include "rayreuse/model/c_linear_frequency_ssp.hpp"
#include "rayreuse/model/c_linear_ssp.hpp"

#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "rayreuse/error.hpp"
#include "rayreuse/model/environment.hpp"
#include "support/test_harness.hpp"

namespace {

using rayreuse::BiologicalAttenuationLayer;
using rayreuse::CLinearFrequencySsp;
using rayreuse::CLinearSsp;
using rayreuse::FrancoisGarrisonParameters;
using rayreuse::RawAttenuation;
using rayreuse::SoundSpeedPoint;
using rayreuse::SoundSpeedProfile;
using rayreuse::ValidationError;
using rayreuse::VolumeAttenuation;
using rayreuse::VolumeAttenuationModel;
using rayreuse::Vec2;
using rayreuse::test::Context;

SoundSpeedProfile makeConstantProfile() {
  return SoundSpeedProfile(
      {{.depth = 0.0, .soundSpeed = 1500.0, .density = 1000.0},
       {.depth = 1000.0, .soundSpeed = 1500.0, .density = 1050.0}});
}

SoundSpeedProfile makePiecewiseLinearProfile() {
  return SoundSpeedProfile(
      {{.depth = 0.0, .soundSpeed = 1480.0, .density = 1000.0},
       {.depth = 100.0, .soundSpeed = 1500.0, .density = 1020.0},
       {.depth = 300.0, .soundSpeed = 1460.0, .density = 1060.0},
       {.depth = 600.0, .soundSpeed = 1520.0, .density = 1120.0}});
}

void checkZeroHessian(Context& context,
                      const rayreuse::SoundSpeedSample& sample,
                      const char* description) {
  context.checkNear(sample.soundSpeedHessian.rangeRange, 0.0, 0.0,
                    std::string(description) + " range-range Hessian");
  context.checkNear(sample.soundSpeedHessian.rangeDepth, 0.0, 0.0,
                    std::string(description) + " range-depth Hessian");
  context.checkNear(sample.soundSpeedHessian.depthDepth, 0.0, 0.0,
                    std::string(description) + " depth-depth Hessian");
}

void testConstantProfile(Context& context) {
  const CLinearSsp ssp(makeConstantProfile());
  const auto sample = ssp.evaluate(Vec2{.range = 125.0, .depth = 500.0}, 0U);

  context.check(ssp.segmentCount() == 1U,
                "constant profile has one interpolation segment");
  context.checkNear(sample.soundSpeed, 1500.0, 0.0,
                    "constant profile preserves sound speed");
  context.checkNear(sample.imaginarySoundSpeed, 0.0, 0.0,
                    "M1 constant profile has zero imaginary sound speed");
  context.checkNear(sample.soundSpeedGradient.range, 0.0, 0.0,
                    "range-independent profile has zero range gradient");
  context.checkNear(sample.soundSpeedGradient.depth, 0.0, 0.0,
                    "constant profile has zero depth gradient");
  context.checkNear(sample.density, 1025.0, 1.0e-12,
                    "density is linearly interpolated");
  context.check(sample.segmentIndex == 0U,
                "constant profile returns its segment index");
  checkZeroHessian(context, sample, "constant profile");
}

void testPiecewiseLinearProfile(Context& context) {
  const CLinearSsp ssp(makePiecewiseLinearProfile());

  const auto first = ssp.evaluate(Vec2{.range = 25.0, .depth = 50.0}, 0U);
  context.checkNear(first.soundSpeed, 1490.0, 1.0e-12,
                    "first segment sound-speed interpolation");
  context.checkNear(first.imaginarySoundSpeed, 0.0, 0.0,
                    "M1 piecewise profile has zero imaginary sound speed");
  context.checkNear(first.soundSpeedGradient.depth, 0.2, 1.0e-15,
                    "first segment analytic derivative");
  context.checkNear(first.density, 1010.0, 1.0e-12,
                    "first segment density interpolation");
  checkZeroHessian(context, first, "first linear segment");

  const auto second = ssp.evaluate(Vec2{.range = 25.0, .depth = 200.0}, 0U);
  context.checkNear(second.soundSpeed, 1480.0, 1.0e-12,
                    "second segment sound-speed interpolation");
  context.checkNear(second.soundSpeedGradient.depth, -0.2, 1.0e-15,
                    "second segment analytic derivative");
  context.checkNear(second.density, 1040.0, 1.0e-12,
                    "second segment density interpolation");
  context.check(second.segmentIndex == 1U, "locator advances the segment hint");

  const auto third = ssp.evaluate(Vec2{.range = 25.0, .depth = 450.0}, 1U);
  context.checkNear(third.soundSpeed, 1490.0, 1.0e-12,
                    "third segment sound-speed interpolation");
  context.checkNear(third.soundSpeedGradient.depth, 0.2, 1.0e-15,
                    "third segment analytic derivative");
  context.checkNear(third.density, 1090.0, 1.0e-12,
                    "third segment density interpolation");
  context.check(third.segmentIndex == 2U,
                "locator advances into the final segment");
}

void testNodeArrivalSideSemantics(Context& context) {
  const CLinearSsp ssp(makePiecewiseLinearProfile());
  constexpr Vec2 firstNode{.range = 10.0, .depth = 100.0};
  constexpr Vec2 secondNode{.range = 10.0, .depth = 300.0};

  const auto firstNodeFromAbove = ssp.evaluate(firstNode, 0U);
  const auto firstNodeFromBelow = ssp.evaluate(firstNode, 1U);
  context.check(firstNodeFromAbove.segmentIndex == 0U,
                "node retains the shallower segment hint");
  context.checkNear(firstNodeFromAbove.soundSpeedGradient.depth, 0.2, 1.0e-15,
                    "node uses shallower-side derivative on downward arrival");
  context.check(firstNodeFromBelow.segmentIndex == 1U,
                "node retains the deeper segment hint");
  context.checkNear(firstNodeFromBelow.soundSpeedGradient.depth, -0.2, 1.0e-15,
                    "node uses deeper-side derivative on upward arrival");

  const auto secondNodeFromNonAdjacentHint = ssp.evaluate(secondNode, 0U);
  context.check(secondNodeFromNonAdjacentHint.segmentIndex == 1U,
                "strict node search chooses the segment to the left");
  context.checkNear(secondNodeFromNonAdjacentHint.soundSpeedGradient.depth,
                    -0.2, 1.0e-15,
                    "non-adjacent node search uses the left derivative");

  const auto secondNodeFromDeeperSide = ssp.evaluate(secondNode, 2U);
  context.check(secondNodeFromDeeperSide.segmentIndex == 2U,
                "node preserves an adjacent deeper segment hint");
  context.checkNear(secondNodeFromDeeperSide.soundSpeedGradient.depth, 0.2,
                    1.0e-15,
                    "deeper segment hint selects the right derivative");

  context.checkNear(firstNodeFromAbove.soundSpeed,
                    firstNodeFromBelow.soundSpeed, 0.0,
                    "sound speed is continuous across a profile node");
  context.checkNear(firstNodeFromAbove.density, firstNodeFromBelow.density, 0.0,
                    "density is continuous across a profile node");
}

void testVolumeAttenuationProjection(Context& context) {
  const SoundSpeedProfile profile(
      {{.depth = 0.0, .soundSpeed = 1400.0, .density = 1000.0},
       {.depth = 100.0, .soundSpeed = 1500.0, .density = 1000.0},
       {.depth = 200.0, .soundSpeed = 1600.0, .density = 1000.0}});
  const VolumeAttenuation thorp{.model = VolumeAttenuationModel::Thorp};
  const VolumeAttenuation fg{
      .model = VolumeAttenuationModel::FrancoisGarrison,
      .parameters = FrancoisGarrisonParameters{.temperatureCelsius = 10.0,
                                               .salinityPsu = 35.0,
                                               .pH = 8.0,
                                               .meanDepthMeters = 100.0}};
  const auto layers = std::make_shared<const rayreuse::BiologicalAttenuationLayers>(
      rayreuse::BiologicalAttenuationLayers{{
          .minimumDepth = 0.0, .maximumDepth = 100.0,
          .resonanceFrequency = 1000.0, .qualityFactor = 2.0,
          .attenuationCoefficientDecibelsPerKilometer = 10.0}});
  const VolumeAttenuation biological{
      .model = VolumeAttenuationModel::Biological, .parameters = layers};

  context.check(CLinearFrequencySsp(profile, 1000.0).isLossless(),
                "C explicit None path remains exactly lossless");
  context.check(!CLinearFrequencySsp(profile, 1000.0, thorp).isLossless(),
                "C explicit Thorp path is lossy");
  context.check(!CLinearFrequencySsp(profile, 1000.0, fg).isLossless(),
                "C explicit FG path is lossy");
  const CLinearFrequencySsp bio(profile, 1000.0, biological);
  const auto lower = bio.evaluate(Vec2{.range = 0.0, .depth = 0.0}, 0U);
  const auto upper = bio.evaluate(Vec2{.range = 0.0, .depth = 100.0}, 0U);
  const auto outside = bio.evaluate(Vec2{.range = 0.0, .depth = 200.0}, 1U);
  context.check(lower.imaginarySoundSpeed > 0.0 &&
                    upper.imaginarySoundSpeed > 0.0 &&
                    outside.imaginarySoundSpeed == 0.0,
                "C biological endpoints are inclusive and outside node is lossless");
  const auto interpolated =
      bio.evaluate(Vec2{.range = 0.0, .depth = 150.0}, 1U);
  context.check(interpolated.imaginarySoundSpeed > 0.0,
                "C biological loss is converted at nodes before interpolation");
  const auto lowFirst = CLinearFrequencySsp(profile, 500.0, biological)
                            .evaluate(Vec2{.range = 0.0, .depth = 50.0}, 0U);
  static_cast<void>(CLinearFrequencySsp(profile, 2000.0, biological)
                        .evaluate(Vec2{.range = 0.0, .depth = 50.0}, 0U));
  const auto lowSecond = CLinearFrequencySsp(profile, 500.0, biological)
                             .evaluate(Vec2{.range = 0.0, .depth = 50.0}, 0U);
  context.check(lowFirst.imaginarySoundSpeed ==
                    lowSecond.imaginarySoundSpeed,
                "C low/high/low construction is deterministic");

  RawAttenuation legacyThorp{};
  legacyThorp.volumeModel = VolumeAttenuationModel::Thorp;
  const SoundSpeedProfile legacy(
      {{.depth = 0.0, .soundSpeed = 1400.0, .density = 1000.0,
        .attenuation = legacyThorp},
       {.depth = 100.0, .soundSpeed = 1500.0, .density = 1000.0,
        .attenuation = legacyThorp}});
  const auto legacySample = CLinearFrequencySsp(legacy, 1000.0).evaluate(
      Vec2{.range = 0.0, .depth = 50.0}, 0U);
  const auto explicitSample = CLinearFrequencySsp(legacy, 1000.0, thorp).evaluate(
      Vec2{.range = 0.0, .depth = 50.0}, 0U);
  context.check(legacySample.imaginarySoundSpeed ==
                    explicitSample.imaginarySoundSpeed,
                "C legacy and matching explicit Thorp baselines are exact");
}

void testValidation(Context& context) {
  const CLinearSsp ssp(makePiecewiseLinearProfile());

  context.expectThrows<ValidationError>(
      [&ssp] {
        static_cast<void>(ssp.evaluate(
            Vec2{.range = 0.0,
                 .depth = std::numeric_limits<double>::quiet_NaN()},
            0U));
      },
      "non-finite query depth is rejected");
  context.expectThrows<ValidationError>(
      [&ssp] {
        static_cast<void>(
            ssp.evaluate(Vec2{.range = std::numeric_limits<double>::infinity(),
                              .depth = 50.0},
                         0U));
      },
      "non-finite query range is rejected");
  const auto above = ssp.evaluate(Vec2{.range = 0.0, .depth = -1.0}, 0U);
  context.check(above.segmentIndex == 0U,
                "query above the profile extrapolates the first segment");
  context.checkNear(above.soundSpeed, 1479.8, 1.0e-12,
                    "top boundary overshoot preserves C-linear slope");
  const auto below = ssp.evaluate(Vec2{.range = 0.0, .depth = 601.0}, 0U);
  context.check(below.segmentIndex == 2U,
                "query below the profile extrapolates the final segment");
  context.checkNear(below.soundSpeed, 1520.2, 1.0e-12,
                    "bottom boundary overshoot preserves C-linear slope");
  context.expectThrows<ValidationError>(
      [&ssp] {
        static_cast<void>(ssp.evaluate(Vec2{.range = 0.0, .depth = 50.0}, 3U));
      },
      "invalid previous segment hint is rejected");
  context.expectThrows<ValidationError>(
      [&ssp] {
        static_cast<void>(
            ssp.evaluateAtSegment(Vec2{.range = 0.0, .depth = 200.0}, 0U));
      },
      "explicit segment evaluation rejects extrapolation");

  const double tinyDepthInterval = std::numeric_limits<double>::denorm_min();
  const SoundSpeedProfile illConditionedProfile(
      {{.depth = 0.0, .soundSpeed = 1.0, .density = 1000.0},
       {.depth = tinyDepthInterval, .soundSpeed = 2.0, .density = 1000.0}});
  context.expectThrows<ValidationError>(
      [&illConditionedProfile] {
        static_cast<void>(CLinearSsp(illConditionedProfile));
      },
      "non-finite precomputed gradient is rejected");
}

}  // namespace

int main() {
  Context context;
  testConstantProfile(context);
  testPiecewiseLinearProfile(context);
  testNodeArrivalSideSemantics(context);
  testVolumeAttenuationProjection(context);
  testValidation(context);

  if (context.failureCount() != 0) {
    std::cerr << context.failureCount()
              << " C-linear SSP test assertion(s) failed\n";
    return 1;
  }

  std::cout << "All Bellhop RayReuse C-linear SSP tests passed\n";
  return 0;
}
