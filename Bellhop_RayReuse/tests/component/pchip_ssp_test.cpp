#include "rayreuse/model/pchip_ssp.hpp"

#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "rayreuse/error.hpp"
#include "rayreuse/model/pchip_frequency_ssp.hpp"
#include "support/munk_case_fixture.hpp"
#include "support/test_harness.hpp"

namespace {

using rayreuse::AttenuationUnit;
using rayreuse::FrancoisGarrisonParameters;
using rayreuse::PchipFrequencySsp;
using rayreuse::PchipSsp;
using rayreuse::SoundSpeedPoint;
using rayreuse::SoundSpeedProfile;
using rayreuse::SspInterpolationKind;
using rayreuse::ValidationError;
using rayreuse::Vec2;
using rayreuse::VolumeAttenuation;
using rayreuse::VolumeAttenuationModel;
using rayreuse::test::Context;

SoundSpeedProfile makeMunkPchipProfile() {
  const auto environment =
      rayreuse::test::makeMunkEnvironment(SspInterpolationKind::Pchip);
  return SoundSpeedProfile(environment.soundSpeedProfile().points(),
                           SspInterpolationKind::Pchip);
}

void checkSample(Context& context, const PchipSsp& profile, double depth,
                 std::size_t segment, double expectedSoundSpeed,
                 double expectedGradient, double expectedCurvature,
                 const char* description) {
  const auto sample =
      profile.evaluateAtSegment(Vec2{.range = 0.0, .depth = depth}, segment);
  context.checkNear(sample.soundSpeed, expectedSoundSpeed, 2.0e-12,
                    std::string(description) + " sound speed");
  context.checkNear(sample.soundSpeedGradient.depth, expectedGradient, 2.0e-15,
                    std::string(description) + " gradient");
  context.checkNear(sample.soundSpeedHessian.depthDepth, expectedCurvature,
                    2.0e-18, std::string(description) + " curvature");
  context.checkNear(sample.density, 1000.0, 0.0,
                    std::string(description) + " density");
}

void testTwoPointProfile(Context& context) {
  const PchipSsp profile(SoundSpeedProfile(
      {{.depth = 0.0, .soundSpeed = 1500.0, .density = 1000.0},
       {.depth = 100.0, .soundSpeed = 1520.0, .density = 1100.0}},
      SspInterpolationKind::Pchip));
  const auto sample = profile.evaluate(Vec2{.range = 30.0, .depth = 25.0}, 0U);
  context.checkNear(sample.soundSpeed, 1505.0, 0.0,
                    "two-point PCHIP is exactly linear");
  context.checkNear(sample.soundSpeedGradient.depth, 0.2, 0.0,
                    "two-point PCHIP has the secant derivative");
  context.checkNear(sample.soundSpeedHessian.depthDepth, 0.0, 0.0,
                    "two-point PCHIP has zero curvature");
  context.checkNear(sample.density, 1025.0, 0.0,
                    "PCHIP density remains linear");
}

void testMunkFortranOracle(Context& context) {
  const PchipSsp profile(makeMunkPchipProfile());
  checkSample(context, profile, 0.0, 0U, 1.54851999999999998e3,
              -1.06470000000001619e-1, 1.51778687423298930e-4,
              "Munk surface node");
  checkSample(context, profile, 100.0, 0U, 1.53863544671855811e3,
              -9.11855328144180050e-2, 1.53910656288373468e-4,
              "Munk first-segment midpoint");
  checkSample(context, profile, 225.0, 1U, 1.52844523412126159e3,
              -7.19467007783713003e-2, 1.43250811963000049e-4,
              "Munk short-segment midpoint");
  checkSample(context, profile, 1500.0, 8U, 1.50041253509381886e3,
              5.17535093819098020e-3, 3.14929812361940256e-5,
              "Munk minimum-adjacent midpoint");
}

void testPeakPlateauAndExtrapolation(Context& context) {
  const PchipSsp peak(SoundSpeedProfile(
      {{.depth = 0.0, .soundSpeed = 1500.0, .density = 1000.0},
       {.depth = 100.0, .soundSpeed = 1510.0, .density = 1010.0},
       {.depth = 200.0, .soundSpeed = 1500.0, .density = 1020.0}},
      SspInterpolationKind::Pchip));
  const auto midpoint = peak.evaluate(Vec2{.range = 0.0, .depth = 50.0}, 0U);
  context.checkNear(midpoint.soundSpeed, 1507.5, 1.0e-12,
                    "three-point PCHIP peak midpoint");
  context.checkNear(midpoint.soundSpeedGradient.depth, 0.1, 1.0e-15,
                    "three-point PCHIP midpoint derivative");
  context.checkNear(midpoint.soundSpeedHessian.depthDepth, -0.002, 1.0e-17,
                    "three-point PCHIP midpoint curvature");
  const auto peakNode = peak.evaluate(Vec2{.range = 0.0, .depth = 100.0}, 1U);
  context.checkNear(peakNode.soundSpeedGradient.depth, 0.0, 0.0,
                    "PCHIP forces a zero derivative at a peak");
  const auto extrapolated =
      peak.evaluate(Vec2{.range = 0.0, .depth = -10.0}, 1U);
  context.check(extrapolated.segmentIndex == 0U,
                "PCHIP extrapolates with the first segment");
  context.checkNear(extrapolated.soundSpeed, 1497.9, 1.0e-12,
                    "PCHIP uses the full cubic for extrapolation");

  const PchipSsp plateau(SoundSpeedProfile(
      {{.depth = 0.0, .soundSpeed = 1500.0, .density = 1000.0},
       {.depth = 100.0, .soundSpeed = 1510.0, .density = 1000.0},
       {.depth = 200.0, .soundSpeed = 1510.0, .density = 1000.0},
       {.depth = 300.0, .soundSpeed = 1520.0, .density = 1000.0}},
      SspInterpolationKind::Pchip));
  context.checkNear(plateau.evaluate(Vec2{.range = 0.0, .depth = 100.0}, 0U)
                        .soundSpeedGradient.depth,
                    0.0, 1.0e-15, "PCHIP plateau entry derivative is zero");
  context.checkNear(plateau.evaluate(Vec2{.range = 0.0, .depth = 200.0}, 2U)
                        .soundSpeedGradient.depth,
                    0.0, 1.0e-15, "PCHIP plateau exit derivative is zero");
}

void testNodeSideSemantics(Context& context) {
  const PchipSsp profile(makeMunkPchipProfile());
  const Vec2 node{.range = 0.0, .depth = 1400.0};
  const auto left = profile.evaluate(node, 7U);
  const auto right = profile.evaluate(node, 8U);
  context.check(left.segmentIndex == 7U && right.segmentIndex == 8U,
                "exact PCHIP node retains the arrival-side segment");
  context.checkNear(left.soundSpeed, right.soundSpeed, 0.0,
                    "PCHIP sound speed is continuous at nodes");
  context.checkNear(left.soundSpeedGradient.depth,
                    right.soundSpeedGradient.depth, 1.0e-18,
                    "PCHIP first derivative is continuous at nodes");
  context.checkNear(left.soundSpeedHessian.depthDepth, 0.0, 0.0,
                    "left arrival side retains left curvature");
  context.checkNear(right.soundSpeedHessian.depthDepth, 7.20140375276255709e-5,
                    2.0e-18, "right arrival side retains right curvature");
  const auto nonAdjacent = profile.evaluate(node, 0U);
  context.check(nonAdjacent.segmentIndex == 7U,
                "nonadjacent PCHIP hint selects the segment to the left");
  context.check(
      profile.evaluate(Vec2{.range = 0.0, .depth = 0.0}, 8U).segmentIndex == 0U,
      "nonadjacent PCHIP hint selects the first segment at the top node");
}

void testFrequencyComplexOracle(Context& context) {
  const SoundSpeedProfile source(
      {{.depth = 0.0,
        .soundSpeed = 1500.0,
        .density = 1000.0,
        .attenuation = {.value = 0.1,
                        .unit = AttenuationUnit::DecibelsPerWavelength}},
       {.depth = 500.0,
        .soundSpeed = 1480.0,
        .density = 1100.0,
        .attenuation = {.value = 0.4,
                        .unit = AttenuationUnit::DecibelsPerWavelength}},
       {.depth = 1000.0,
        .soundSpeed = 1520.0,
        .density = 1200.0,
        .attenuation = {.value = 0.2,
                        .unit = AttenuationUnit::DecibelsPerWavelength}}},
      SspInterpolationKind::Pchip);
  const PchipFrequencySsp profile(source, 50.0);
  const auto firstQuarter =
      profile.evaluateAtSegment(Vec2{.range = 0.0, .depth = 250.0}, 0U);
  context.checkNear(firstQuarter.soundSpeed, 1483.75, 2.0e-12,
                    "complex PCHIP preserves real oracle value");
  context.checkNear(firstQuarter.imaginarySoundSpeed, 8.64634968092250666,
                    2.0e-14,
                    "complex PCHIP matches converted-node Fortran oracle");
  const auto secondQuarter =
      profile.evaluateAtSegment(Vec2{.range = 0.0, .depth = 750.0}, 1U);
  context.checkNear(secondQuarter.imaginarySoundSpeed, 9.70452545644202935,
                    2.0e-14,
                    "complex PCHIP independently limits imaginary values");
  context.check(!profile.isLossless(),
                "attenuating PCHIP profile is not lossless");
  context.check(!profile.uniformComplexSoundSpeed().has_value(),
                "nonuniform PCHIP has no uniform complex fast path");
}

void testVolumePaths(Context& context) {
  const SoundSpeedProfile source(
      {{.depth = 0.0, .soundSpeed = 1400.0, .density = 1000.0},
       {.depth = 100.0, .soundSpeed = 1500.0, .density = 1000.0},
       {.depth = 200.0, .soundSpeed = 1600.0, .density = 1000.0}},
      SspInterpolationKind::Pchip);
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
  context.check(PchipFrequencySsp(source, 1000.0).isLossless(),
                "P None path is exactly lossless");
  context.check(!PchipFrequencySsp(source, 1000.0, thorp).isLossless(),
                "P Thorp path is lossy");
  context.check(!PchipFrequencySsp(source, 1000.0, fg).isLossless(),
                "P FG path is lossy");
  auto legacyPoints = source.points();
  for (auto& point : legacyPoints) {
    point.attenuation.volumeModel = VolumeAttenuationModel::Thorp;
  }
  const SoundSpeedProfile legacyThorp(std::move(legacyPoints),
                                      SspInterpolationKind::Pchip);
  context.check(PchipFrequencySsp(source, 1000.0, thorp)
                        .evaluate(Vec2{.range = 0.0, .depth = 50.0}, 0U)
                        .imaginarySoundSpeed ==
                    PchipFrequencySsp(legacyThorp, 1000.0)
                        .evaluate(Vec2{.range = 0.0, .depth = 50.0}, 0U)
                        .imaginarySoundSpeed,
                "P explicit and legacy Thorp baselines are exact");
  const PchipFrequencySsp bio(source, 1000.0, biological);
  context.check(
      bio.evaluate(Vec2{.range = 0.0, .depth = 0.0}, 0U).imaginarySoundSpeed >
              0.0 &&
          bio.evaluate(Vec2{.range = 0.0, .depth = 100.0}, 0U)
                  .imaginarySoundSpeed > 0.0 &&
          bio.evaluate(Vec2{.range = 0.0, .depth = 200.0}, 1U)
                  .imaginarySoundSpeed == 0.0,
      "P biological endpoints are inclusive and outside node is lossless");
  context.check(
      bio.evaluate(Vec2{.range = 0.0, .depth = 150.0}, 1U).imaginarySoundSpeed >
          0.0,
      "P biological loss is node-first, not query-depth converted");
  const auto low = PchipFrequencySsp(source, 500.0, biological)
                       .evaluate(Vec2{.range = 0.0, .depth = 50.0}, 0U);
  static_cast<void>(PchipFrequencySsp(source, 2000.0, biological)
                        .evaluate(Vec2{.range = 0.0, .depth = 50.0}, 0U));
  const auto repeated = PchipFrequencySsp(source, 500.0, biological)
                            .evaluate(Vec2{.range = 0.0, .depth = 50.0}, 0U);
  context.check(low.imaginarySoundSpeed == repeated.imaginarySoundSpeed,
                "P low/high/low evaluation is deterministic");
}

void testValidation(Context& context) {
  const PchipSsp profile(makeMunkPchipProfile());
  context.expectThrows<ValidationError>(
      [&profile] {
        static_cast<void>(profile.evaluate(
            Vec2{.range = 0.0,
                 .depth = std::numeric_limits<double>::quiet_NaN()},
            0U));
      },
      "PCHIP rejects a non-finite query");
  context.expectThrows<ValidationError>(
      [&profile] {
        static_cast<void>(
            profile.evaluateAtSegment(Vec2{.range = 0.0, .depth = 225.0}, 0U));
      },
      "PCHIP explicit segment rejects a depth outside the segment");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(PchipSsp(SoundSpeedProfile(
            {{.depth = 0.0, .soundSpeed = 1500.0, .density = 1000.0},
             {.depth = std::numeric_limits<double>::denorm_min(),
              .soundSpeed = 1501.0,
              .density = 1000.0},
             {.depth = 1.0, .soundSpeed = 1502.0, .density = 1000.0}},
            SspInterpolationKind::Pchip)));
      },
      "PCHIP rejects a segment whose coefficient arithmetic overflows");
}

}  // namespace

int main() {
  Context context;
  testTwoPointProfile(context);
  testMunkFortranOracle(context);
  testPeakPlateauAndExtrapolation(context);
  testNodeSideSemantics(context);
  testFrequencyComplexOracle(context);
  testVolumePaths(context);
  testValidation(context);
  if (context.failureCount() != 0) {
    std::cerr << context.failureCount()
              << " PCHIP SSP test assertion(s) failed\n";
    return 1;
  }
  std::cout << "All Bellhop RayReuse PCHIP SSP tests passed\n";
  return 0;
}
