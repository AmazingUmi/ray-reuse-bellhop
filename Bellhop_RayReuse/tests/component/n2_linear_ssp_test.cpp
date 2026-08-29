#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <utility>

#include "rayreuse/error.hpp"
#include "rayreuse/model/n2_linear_frequency_ssp.hpp"
#include "rayreuse/model/n2_linear_ssp.hpp"
#include "support/test_harness.hpp"

namespace {

using rayreuse::AttenuationUnit;
using rayreuse::FrancoisGarrisonParameters;
using rayreuse::N2LinearFrequencySsp;
using rayreuse::N2LinearSsp;
using rayreuse::SoundSpeedProfile;
using rayreuse::SspInterpolationKind;
using rayreuse::ValidationError;
using rayreuse::VolumeAttenuation;
using rayreuse::VolumeAttenuationModel;
using rayreuse::Vec2;
using rayreuse::test::Context;

SoundSpeedProfile makeProfile() {
  return SoundSpeedProfile(
      {{.depth = 0.0, .soundSpeed = 1500.0, .density = 1000.0},
       {.depth = 100.0, .soundSpeed = 1600.0, .density = 1100.0},
       {.depth = 200.0, .soundSpeed = 1400.0, .density = 1300.0}},
      SspInterpolationKind::N2Linear);
}

void checkRealOracle(Context& context, const N2LinearSsp& profile,
                     double depth, std::size_t segment,
                     double expectedSoundSpeed, double expectedGradient,
                     double expectedCurvature, double expectedDensity,
                     const char* description) {
  const auto sample = profile.evaluateAtSegment(
      Vec2{.range = 0.0, .depth = depth}, segment);
  context.checkNear(sample.soundSpeed, expectedSoundSpeed, 3.0e-12,
                    std::string(description) + " sound speed");
  context.checkNear(sample.soundSpeedGradient.depth, expectedGradient,
                    3.0e-15, std::string(description) + " gradient");
  context.checkNear(sample.soundSpeedHessian.depthDepth,
                    expectedCurvature, 3.0e-18,
                    std::string(description) + " curvature");
  context.checkNear(sample.density, expectedDensity, 2.0e-12,
                    std::string(description) + " density");
}

void testRealFortranOracle(Context& context) {
  const N2LinearSsp profile(makeProfile());
  // Values are frozen from the Bellhop Fortran SSP oracle (NVW profile).
  checkRealOracle(context, profile, 50.0, 0U,
                  1547.5821125259863, 0.99740219310406519,
                  0.00192844914675006224, 1050.0, "first midpoint");
  checkRealOracle(context, profile, 100.0, 0U,
                  1600.0, 1.10222222222222155,
                  0.00227792592592592326, 1100.0, "node from left");
  checkRealOracle(context, profile, 100.0, 1U,
                  1600.0, -2.44897959183673342,
                  0.0112453144523115226, 1100.0, "node from right");
  checkRealOracle(context, profile, 150.0, 1U,
                  1490.02583573253605, -1.97791040141486962,
                  0.00787663434191761790, 1200.00000000000023,
                  "second midpoint");
}

void testNodeAndExtrapolationSemantics(Context& context) {
  const N2LinearSsp profile(makeProfile());
  const Vec2 node{.range = 0.0, .depth = 100.0};
  const auto left = profile.evaluate(node, 0U);
  const auto right = profile.evaluate(node, 1U);
  context.check(left.segmentIndex == 0U && right.segmentIndex == 1U,
                "exact N2 node retains the arrival-side segment");
  context.checkNear(left.soundSpeed, right.soundSpeed, 0.0,
                    "N2 sound speed is continuous at a node");
  context.check(std::abs(left.soundSpeedGradient.depth -
                         right.soundSpeedGradient.depth) > 1.0,
                "N2 gradient jumps at a node");
  context.check(profile.evaluate(node, 0U).segmentIndex == 0U,
                "left hint is retained at a shared node");
  context.check(profile.evaluate(node, 1U).segmentIndex == 1U,
                "right hint is retained at a shared node");

  const auto above = profile.evaluate(
      Vec2{.range = 0.0, .depth = -20.0}, 1U);
  const auto below = profile.evaluate(
      Vec2{.range = 0.0, .depth = 220.0}, 0U);
  context.check(above.segmentIndex == 0U && below.segmentIndex == 1U,
                "automatic N2 range extrapolation uses the edge segment");
}

void testFrequencyComplexOracle(Context& context) {
  const SoundSpeedProfile source(
      {{.depth = 0.0,
        .soundSpeed = 1500.0,
        .density = 1000.0,
        .attenuation =
            {.value = 0.1,
             .unit = AttenuationUnit::DecibelsPerWavelength}},
       {.depth = 500.0,
        .soundSpeed = 1480.0,
        .density = 1100.0,
        .attenuation =
            {.value = 0.4,
             .unit = AttenuationUnit::DecibelsPerWavelength}},
       {.depth = 1000.0,
        .soundSpeed = 1520.0,
        .density = 1200.0,
        .attenuation =
            {.value = 0.2,
             .unit = AttenuationUnit::DecibelsPerWavelength}}},
      SspInterpolationKind::N2Linear);
  const N2LinearFrequencySsp profile(source, 50.0);
  const auto sample = profile.evaluateAtSegment(
      Vec2{.range = 0.0, .depth = 250.0}, 0U);
  context.checkNear(sample.soundSpeed, 1489.91621090979174, 4.0e-12,
                    "complex N2 interpolation matches Fortran real part");
  context.checkNear(sample.imaginarySoundSpeed, 6.87988934309839983,
                    4.0e-14,
                    "complex N2 interpolation matches Fortran imaginary part");
  context.checkNear(sample.soundSpeedGradient.depth,
                    -0.0397683421458134914, 4.0e-16,
                    "complex N2 gradient follows Fortran's real slope path");
  context.checkNear(sample.soundSpeedHessian.depthDepth,
                    3.18444961960798548e-6, 4.0e-19,
                    "complex N2 curvature matches Fortran");
  context.check(!profile.isLossless(),
                "attenuating N2 profile is not lossless");
  context.check(!profile.uniformComplexSoundSpeed().has_value(),
                "nonuniform N2 profile has no uniform fast path");
}

void testVolumePaths(Context& context) {
  const SoundSpeedProfile source(
      {{.depth = 0.0, .soundSpeed = 1400.0, .density = 1000.0},
       {.depth = 100.0, .soundSpeed = 1500.0, .density = 1000.0},
       {.depth = 200.0, .soundSpeed = 1600.0, .density = 1000.0}},
      SspInterpolationKind::N2Linear);
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
  const VolumeAttenuation bioModel{
      .model = VolumeAttenuationModel::Biological, .parameters = layers};
  context.check(N2LinearFrequencySsp(source, 1000.0).isLossless(),
                "N None path is exactly lossless");
  context.check(!N2LinearFrequencySsp(source, 1000.0, thorp).isLossless(),
                "N Thorp path is lossy");
  context.check(!N2LinearFrequencySsp(source, 1000.0, fg).isLossless(),
                "N FG path is lossy");
  auto legacyPoints = source.points();
  for (auto& point : legacyPoints) {
    point.attenuation.volumeModel = VolumeAttenuationModel::Thorp;
  }
  const SoundSpeedProfile legacyThorp(std::move(legacyPoints),
                                      SspInterpolationKind::N2Linear);
  context.check(
      N2LinearFrequencySsp(source, 1000.0, thorp)
              .evaluate(Vec2{.range = 0.0, .depth = 50.0}, 0U)
              .imaginarySoundSpeed ==
          N2LinearFrequencySsp(legacyThorp, 1000.0)
              .evaluate(Vec2{.range = 0.0, .depth = 50.0}, 0U)
              .imaginarySoundSpeed,
      "N explicit and legacy Thorp baselines are exact");
  const N2LinearFrequencySsp bio(source, 1000.0, bioModel);
  context.check(bio.evaluate(Vec2{.range = 0.0, .depth = 0.0}, 0U)
                        .imaginarySoundSpeed > 0.0 &&
                    bio.evaluate(Vec2{.range = 0.0, .depth = 100.0}, 0U)
                        .imaginarySoundSpeed > 0.0 &&
                    bio.evaluate(Vec2{.range = 0.0, .depth = 200.0}, 1U)
                            .imaginarySoundSpeed == 0.0,
                "N biological endpoints are inclusive and outside node is lossless");
  context.check(bio.evaluate(Vec2{.range = 0.0, .depth = 150.0}, 1U)
                        .imaginarySoundSpeed > 0.0,
                "N biological loss is node-first, not query-depth converted");
  const auto low = N2LinearFrequencySsp(source, 500.0, bioModel).evaluate(
      Vec2{.range = 0.0, .depth = 50.0}, 0U);
  static_cast<void>(N2LinearFrequencySsp(source, 2000.0, bioModel).evaluate(
      Vec2{.range = 0.0, .depth = 50.0}, 0U));
  const auto repeated = N2LinearFrequencySsp(source, 500.0, bioModel).evaluate(
      Vec2{.range = 0.0, .depth = 50.0}, 0U);
  context.check(low.soundSpeed == repeated.soundSpeed &&
                    low.imaginarySoundSpeed == repeated.imaginarySoundSpeed,
                "N low/high/low evaluation is deterministic");
}

void testValidation(Context& context) {
  const N2LinearSsp profile(makeProfile());
  context.expectThrows<ValidationError>(
      [&profile] {
        static_cast<void>(profile.evaluate(
            Vec2{.range = 0.0,
                 .depth = std::numeric_limits<double>::quiet_NaN()},
            0U));
      },
      "N2 rejects a non-finite query");
  context.expectThrows<ValidationError>(
      [&profile] {
        static_cast<void>(profile.evaluateAtSegment(
            Vec2{.range = 0.0, .depth = 150.0}, 0U));
      },
      "N2 explicit segment rejects a depth outside the segment");
  // The first segment's N2 grows toward the surface, so extrapolating far
  // enough above the profile drives the interpolated N2 non-positive.
  const N2LinearSsp decreasing(
      SoundSpeedProfile(
          {{.depth = 0.0, .soundSpeed = 1600.0, .density = 1000.0},
           {.depth = 100.0, .soundSpeed = 1500.0, .density = 1100.0},
           {.depth = 200.0, .soundSpeed = 1400.0, .density = 1300.0}},
          SspInterpolationKind::N2Linear));
  context.expectThrows<ValidationError>(
      [&decreasing] {
        static_cast<void>(decreasing.evaluate(
            Vec2{.range = 0.0, .depth = -730.0}, 1U));
      },
      "N2 edge extrapolation fails when the extrapolated N2 is non-positive");
}

}  // namespace

int main() {
  Context context;
  testRealFortranOracle(context);
  testNodeAndExtrapolationSemantics(context);
  testFrequencyComplexOracle(context);
  testVolumePaths(context);
  testValidation(context);
  if (context.failureCount() != 0) {
    std::cerr << context.failureCount()
              << " N2-linear SSP test assertion(s) failed\n";
    return 1;
  }
  std::cout << "All Bellhop RayReuse N2-linear SSP tests passed\n";
  return 0;
}
