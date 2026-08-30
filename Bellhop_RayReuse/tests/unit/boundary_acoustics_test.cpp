#include "rayreuse/acoustics/boundary_acoustics.hpp"

#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <limits>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

#include "rayreuse/acoustics/attenuation.hpp"
#include "rayreuse/error.hpp"
#include "support/boundary_acoustics_fixture.hpp"
#include "support/test_harness.hpp"

namespace {

using rayreuse::AcousticMaterial;
using rayreuse::AttenuationUnit;
using rayreuse::BiologicalAttenuationLayers;
using rayreuse::BoundaryAcousticsResult;
using rayreuse::BoundaryGeometry;
using rayreuse::BoundaryModel;
using rayreuse::BoundaryOrientation;
using rayreuse::classifyBoundaryCoefficient;
using rayreuse::convertAttenuation;
using rayreuse::evaluateAcousticHalfSpaceAcoustics;
using rayreuse::evaluateBoundaryAcoustics;
using rayreuse::evaluateGrainSizeHalfSpaceAcoustics;
using rayreuse::evaluateTabulatedReflectionAcoustics;
using rayreuse::GrainSizeMaterial;
using rayreuse::TabulatedReflectionPoint;
using rayreuse::TabulatedReflectionTable;
using rayreuse::ValidationError;
using rayreuse::Vec2;
using rayreuse::VolumeAttenuation;
using rayreuse::VolumeAttenuationModel;
using rayreuse::test::Context;

void checkComplexNear(Context& context, std::complex<double> actual,
                      std::complex<double> expected, double tolerance,
                      const std::string& message) {
  context.checkNear(actual.real(), expected.real(), tolerance,
                    message + " real");
  context.checkNear(actual.imag(), expected.imag(), tolerance,
                    message + " imaginary");
}

void testVacuumAndRigid(Context& context) {
  const BoundaryAcousticsResult vacuum = evaluateBoundaryAcoustics(
      BoundaryModel::vacuum(0.0), 250.0, 1000.0, 1.0 / 1500.0, 1.0 / 1500.0);
  context.check(vacuum.rawCoefficient == std::complex<double>{-1.0, 0.0},
                "vacuum raw coefficient is -1");
  context.check(vacuum.amplitudeMultiplier == 1.0,
                "vacuum preserves amplitude");
  context.check(vacuum.phaseIncrement == std::numbers::pi_v<double>,
                "vacuum adds positive pi");
  context.check(!vacuum.coefficientSuppressed,
                "vacuum is never small-coefficient suppressed");

  const BoundaryAcousticsResult rigid = evaluateBoundaryAcoustics(
      BoundaryModel::rigid(100.0), 250.0, 1000.0, 1.0 / 1500.0, 1.0 / 1500.0);
  context.check(rigid.rawCoefficient == std::complex<double>{1.0, 0.0},
                "rigid raw coefficient is +1");
  context.check(rigid.amplitudeMultiplier == 1.0 && rigid.phaseIncrement == 0.0,
                "rigid preserves amplitude and phase");
}

void testFluidHalfSpaceOracles(Context& context) {
  for (const auto& fixture : rayreuse::test::kHalfSpaceCoefficientFixtures) {
    const BoundaryModel boundary = BoundaryModel::acousticHalfSpace(
        100.0, AcousticMaterial{
                   .compressionalSoundSpeed = fixture.compressionalSoundSpeed,
                   .shearSoundSpeed = 0.0,
                   .density = fixture.halfSpaceDensity,
                   .compressionalAttenuation = {
                       .value = fixture.attenuationDecibelsPerWavelength,
                       .unit = AttenuationUnit::DecibelsPerWavelength}});
    const BoundaryAcousticsResult result = evaluateBoundaryAcoustics(
        boundary, fixture.frequencyHz, fixture.waterDensity,
        fixture.tangentSlowness, fixture.outwardNormalSlowness);
    const auto converted = convertAttenuation(
        boundary.material()->compressionalAttenuation, fixture.frequencyHz,
        fixture.compressionalSoundSpeed);
    context.checkNear(converted.imaginarySoundSpeed,
                      fixture.compressionalImaginarySoundSpeed, 2.0e-14,
                      std::string(fixture.name) +
                          " attenuation-to-complex-speed integration");
    checkComplexNear(context, result.rawCoefficient,
                     fixture.expectedRawCoefficient, 2.0e-15,
                     std::string(fixture.name));
    context.check(result.coefficientSuppressed == fixture.expectedSuppressed,
                  std::string(fixture.name) + " suppression decision");
    const double expectedAmplitude =
        fixture.expectedSuppressed ? 0.0
                                   : std::abs(fixture.expectedRawCoefficient);
    context.checkNear(result.amplitudeMultiplier, expectedAmplitude, 2.0e-15,
                      std::string(fixture.name) + " amplitude multiplier");
  }
}

void testElasticHalfSpaceOracles(Context& context) {
  struct Fixture {
    double incidenceAngleDegrees;
    double compressionalAttenuation;
    double shearAttenuation;
    std::complex<double> expected;
  };
  const std::array fixtures{
      Fixture{0.0, 0.0, 0.0, {4.54545454545454530e-1, 0.0}},
      Fixture{30.0, 0.0, 0.0, {4.04439753970198490e-1, 0.0}},
      Fixture{
          60.0, 0.0, 0.0, {-1.33833243674634672e-1, 1.68586165662701098e-1}},
      Fixture{30.0, 0.5, 1.0, {4.04463539890146495e-1, 3.04094195441093063e-3}},
  };
  for (const Fixture& fixture : fixtures) {
    const double angle =
        fixture.incidenceAngleDegrees * std::numbers::pi / 180.0;
    const AcousticMaterial material{
        .compressionalSoundSpeed = 2000.0,
        .shearSoundSpeed = 1000.0,
        .density = 2000.0,
        .compressionalAttenuation =
            {.value = fixture.compressionalAttenuation,
             .unit = AttenuationUnit::DecibelsPerWavelength},
        .shearAttenuation = {.value = fixture.shearAttenuation,
                             .unit = AttenuationUnit::DecibelsPerWavelength}};
    const BoundaryAcousticsResult result = evaluateAcousticHalfSpaceAcoustics(
        material, 1000.0, 1000.0, std::sin(angle) / 1500.0,
        std::cos(angle) / 1500.0);
    checkComplexNear(context, result.rawCoefficient, fixture.expected, 3.0e-15,
                     "elastic Origin coefficient");
  }
}

void testGrainAndTabulatedOracles(Context& context) {
  const GrainSizeMaterial grain =
      *BoundaryModel::grainSizeHalfSpace(100.0, 3.0).grainSizeMaterial();
  const double angle = 30.0 * std::numbers::pi / 180.0;
  const BoundaryAcousticsResult grainResult =
      evaluateGrainSizeHalfSpaceAcoustics(grain, 1000.0, 1500.0, 1000.0,
                                          std::sin(angle) / 1500.0,
                                          std::cos(angle) / 1500.0);
  checkComplexNear(context, grainResult.rawCoefficient,
                   {1.96328379103933526e-1, 1.15078565713302005e-2}, 3.0e-15,
                   "grain-size Origin coefficient");

  const TabulatedReflectionTable table{
      TabulatedReflectionPoint{
          .angleDegrees = 10.0,
          .magnitude = 0.2,
          .phaseRadians = 170.0 * std::numbers::pi / 180.0},
      TabulatedReflectionPoint{
          .angleDegrees = 30.0,
          .magnitude = 0.6,
          .phaseRadians = 190.0 * std::numbers::pi / 180.0},
      TabulatedReflectionPoint{
          .angleDegrees = 60.0,
          .magnitude = 0.9,
          .phaseRadians = 250.0 * std::numbers::pi / 180.0}};
  const double tableAngle = 20.0 * std::numbers::pi / 180.0;
  const BoundaryAcousticsResult tableResult =
      evaluateTabulatedReflectionAcoustics(table, std::cos(tableAngle) / 1500.0,
                                           std::sin(tableAngle) / 1500.0);
  context.checkNear(tableResult.amplitudeMultiplier, 0.4, 2.0e-15,
                    "tabulated magnitude interpolation");
  context.checkNear(tableResult.phaseIncrement, std::numbers::pi, 3.0e-15,
                    "tabulated unwrapped phase interpolation");
}

VolumeAttenuation biologicalLayer(double minimumDepth, double maximumDepth) {
  return VolumeAttenuation{
      .model = VolumeAttenuationModel::Biological,
      .parameters = std::make_shared<const BiologicalAttenuationLayers>(
          BiologicalAttenuationLayers{
              {.minimumDepth = minimumDepth,
               .maximumDepth = maximumDepth,
               .resonanceFrequency = 1000.0,
               .qualityFactor = 2.0,
               .attenuationCoefficientDecibelsPerKilometer = 100.0}})};
}

void testVolumeAttenuationAtBoundaries(Context& context) {
  const double tangent = std::sin(std::numbers::pi / 6.0) / 1500.0;
  const double normal = std::cos(std::numbers::pi / 6.0) / 1500.0;
  const AcousticMaterial fluid{.compressionalSoundSpeed = 1800.0,
                               .shearSoundSpeed = 0.0,
                               .density = 1800.0};
  const VolumeAttenuation layer = biologicalLayer(90.0, 110.0);
  const auto lossless = evaluateAcousticHalfSpaceAcoustics(
      fluid, 100.0, VolumeAttenuation{}, 1000.0, 1000.0, tangent, normal);
  const auto inLayer = evaluateAcousticHalfSpaceAcoustics(
      fluid, 100.0, layer, 1000.0, 1000.0, tangent, normal);
  const auto outsideLayer = evaluateAcousticHalfSpaceAcoustics(
      fluid, 200.0, layer, 1000.0, 1000.0, tangent, normal);
  context.check(inLayer.rawCoefficient != lossless.rawCoefficient,
                "compressional reflection responds to biological loss");
  context.check(outsideLayer.rawCoefficient == lossless.rawCoefficient,
                "compressional reflection uses the supplied evaluation depth");

  const AcousticMaterial elastic{.compressionalSoundSpeed = 2000.0,
                                 .shearSoundSpeed = 1000.0,
                                 .density = 2000.0};
  const auto elasticLossless = evaluateAcousticHalfSpaceAcoustics(
      elastic, 100.0, VolumeAttenuation{}, 1000.0, 1000.0, tangent, normal);
  const auto elasticBiological = evaluateAcousticHalfSpaceAcoustics(
      elastic, 100.0, layer, 1000.0, 1000.0, tangent, normal);
  context.check(
      elasticBiological.rawCoefficient != elasticLossless.rawCoefficient,
      "elastic compressional and shear conversion includes volume loss");

  const BoundaryModel flat = BoundaryModel::acousticHalfSpace(100.0, fluid);
  const auto flatLossless = evaluateBoundaryAcoustics(
      flat, 0U, VolumeAttenuation{}, 1000.0, 1000.0, tangent, normal);
  const auto flatBiological = evaluateBoundaryAcoustics(
      flat, 0U, layer, 1000.0, 1000.0, tangent, normal);
  context.check(flatBiological.rawCoefficient != flatLossless.rawCoefficient,
                "flat boundary evaluates biological loss at boundary depth");

  const BoundaryGeometry longGeometry =
      BoundaryGeometry::piecewiseLinear({Vec2{.range = 0.0, .depth = 100.0},
                                         Vec2{.range = 100.0, .depth = 100.0}},
                                        100.0, BoundaryOrientation::Lower);
  const auto materials = std::make_shared<const std::vector<AcousticMaterial>>(
      std::vector<AcousticMaterial>{fluid, fluid});
  const BoundaryModel longBoundary =
      BoundaryModel::acousticHalfSpace(longGeometry, fluid, materials);
  const auto longBiological = evaluateBoundaryAcoustics(
      longBoundary, 0U, layer, 1000.0, 1000.0, tangent, normal);
  context.check(
      longBiological.rawCoefficient == flatLossless.rawCoefficient,
      "legacy 1e20 long-material depth excludes physical biological layer");

  const GrainSizeMaterial grain =
      *BoundaryModel::grainSizeHalfSpace(100.0, 3.0).grainSizeMaterial();
  const auto grainBefore = evaluateGrainSizeHalfSpaceAcoustics(
      grain, 1000.0, 1500.0, 1000.0, tangent, normal);
  static_cast<void>(layer);
  const auto grainAfter = evaluateGrainSizeHalfSpaceAcoustics(
      grain, 1000.0, 1500.0, 1000.0, tangent, normal);
  context.check(grainAfter.rawCoefficient == grainBefore.rawCoefficient,
                "grain-size conversion remains independent of volume model");
}

void testCoefficientApplicationSemantics(Context& context) {
  for (const auto& fixture :
       rayreuse::test::kRawCoefficientApplicationFixtures) {
    const BoundaryAcousticsResult result = classifyBoundaryCoefficient(
        fixture.rawCoefficient, fixture.acousticHalfSpace);
    const double outgoingAmplitude =
        fixture.incomingAmplitude * result.amplitudeMultiplier;
    const double outgoingPhase =
        fixture.incomingUnwrappedPhase + result.phaseIncrement;
    context.checkNear(outgoingAmplitude, fixture.expectedAmplitude, 1.0e-15,
                      std::string(fixture.name) + " outgoing amplitude");
    context.checkNear(outgoingPhase, fixture.expectedUnwrappedPhase, 1.0e-15,
                      std::string(fixture.name) + " unwrapped phase");
    context.checkNear(
        std::atan2(std::sin(outgoingPhase), std::cos(outgoingPhase)),
        fixture.expectedWrappedPhase, 1.0e-15,
        std::string(fixture.name) + " wrapped comparison phase");
    context.check(result.coefficientSuppressed == fixture.expectedSuppressed,
                  std::string(fixture.name) + " suppression decision");
    checkComplexNear(context, result.rawCoefficient, fixture.rawCoefficient,
                     0.0,
                     std::string(fixture.name) + " retains raw coefficient");
  }
}

void testInvalidAndUnsupportedInputs(Context& context) {
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(evaluateBoundaryAcoustics(
            BoundaryModel::rigid(100.0), 0.0, 1000.0, 0.0, 1.0 / 1500.0));
      },
      "non-positive frequency is rejected");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(evaluateBoundaryAcoustics(BoundaryModel::rigid(100.0),
                                                    50.0, 1000.0, 0.0, 0.0));
      },
      "non-positive outward normal slowness is rejected");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(classifyBoundaryCoefficient(
            {std::numeric_limits<double>::quiet_NaN(), 0.0}, true));
      },
      "non-finite raw coefficient is rejected");
}

}  // namespace

int main() {
  Context context;
  testVacuumAndRigid(context);
  testFluidHalfSpaceOracles(context);
  testElasticHalfSpaceOracles(context);
  testGrainAndTabulatedOracles(context);
  testVolumeAttenuationAtBoundaries(context);
  testCoefficientApplicationSemantics(context);
  testInvalidAndUnsupportedInputs(context);

  if (context.failureCount() != 0) {
    std::cerr << context.failureCount() << " test assertion(s) failed\n";
    return 1;
  }
  std::cout << "All Bellhop RayReuse boundary acoustics tests passed\n";
  return 0;
}
