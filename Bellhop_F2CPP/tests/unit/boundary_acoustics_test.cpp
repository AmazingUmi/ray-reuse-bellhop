#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <limits>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

#include "bellhop/acoustics/attenuation.hpp"
#include "bellhop/acoustics/boundary_acoustics.hpp"
#include "bellhop/error.hpp"
#include "support/boundary_acoustics_fixture.hpp"
#include "support/test_harness.hpp"

namespace {

using bellhop::AcousticMaterial;
using bellhop::AttenuationUnit;
using bellhop::BiologicalAttenuationLayer;
using bellhop::BiologicalAttenuationLayers;
using bellhop::BoundaryAcousticsResult;
using bellhop::BoundaryGeometry;
using bellhop::BoundaryOrientation;
using bellhop::BoundaryModel;
using bellhop::GrainSizeMaterial;
using bellhop::TabulatedReflectionPoint;
using bellhop::TabulatedReflectionTable;
using bellhop::VolumeAttenuation;
using bellhop::VolumeAttenuationModel;
using bellhop::Vec2;
using bellhop::ValidationError;
using bellhop::classifyBoundaryCoefficient;
using bellhop::convertAttenuation;
using bellhop::evaluateAcousticHalfSpaceAcoustics;
using bellhop::evaluateBoundaryAcoustics;
using bellhop::evaluateFluidHalfSpaceAcoustics;
using bellhop::evaluateGrainSizeHalfSpaceAcoustics;
using bellhop::evaluateTabulatedReflectionAcoustics;
using bellhop::test::Context;

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
      BoundaryModel::vacuum(0.0), 250.0, 1000.0,
      1.0 / 1500.0, 1.0 / 1500.0);
  context.check(vacuum.rawCoefficient ==
                    std::complex<double>{-1.0, 0.0},
                "vacuum raw coefficient is -1");
  context.check(vacuum.amplitudeMultiplier == 1.0,
                "vacuum preserves amplitude");
  context.check(vacuum.phaseIncrement ==
                    std::numbers::pi_v<double>,
                "vacuum adds positive pi");
  context.check(!vacuum.coefficientSuppressed,
                "vacuum is never small-coefficient suppressed");

  const BoundaryAcousticsResult rigid = evaluateBoundaryAcoustics(
      BoundaryModel::rigid(100.0), 250.0, 1000.0,
      1.0 / 1500.0, 1.0 / 1500.0);
  context.check(rigid.rawCoefficient ==
                    std::complex<double>{1.0, 0.0},
                "rigid raw coefficient is +1");
  context.check(rigid.amplitudeMultiplier == 1.0 &&
                    rigid.phaseIncrement == 0.0,
                "rigid preserves amplitude and phase");
}

void testFluidHalfSpaceOracles(Context& context) {
  for (const auto& fixture :
       bellhop::test::kHalfSpaceCoefficientFixtures) {
    const BoundaryModel boundary = BoundaryModel::acousticHalfSpace(
        100.0,
        AcousticMaterial{
            .compressionalSoundSpeed =
                fixture.compressionalSoundSpeed,
            .shearSoundSpeed = 0.0,
            .density = fixture.halfSpaceDensity,
            .compressionalAttenuation =
                {.value =
                     fixture.attenuationDecibelsPerWavelength,
                 .unit =
                     AttenuationUnit::DecibelsPerWavelength}});
    const BoundaryAcousticsResult result =
        evaluateBoundaryAcoustics(
            boundary, fixture.frequencyHz, fixture.waterDensity,
            fixture.tangentSlowness,
            fixture.outwardNormalSlowness);
    const auto converted = convertAttenuation(
        boundary.material()->compressionalAttenuation,
        fixture.frequencyHz, fixture.compressionalSoundSpeed);
    context.checkNear(
        converted.imaginarySoundSpeed,
        fixture.compressionalImaginarySoundSpeed, 2.0e-14,
        std::string(fixture.name) +
            " attenuation-to-complex-speed integration");
    checkComplexNear(context, result.rawCoefficient,
                     fixture.expectedRawCoefficient, 2.0e-15,
                     std::string(fixture.name));
    context.check(
        result.coefficientSuppressed == fixture.expectedSuppressed,
        std::string(fixture.name) + " suppression decision");
    const double expectedAmplitude =
        fixture.expectedSuppressed
            ? 0.0
            : std::abs(fixture.expectedRawCoefficient);
    context.checkNear(
        result.amplitudeMultiplier, expectedAmplitude, 2.0e-15,
        std::string(fixture.name) + " amplitude multiplier");
  }
}

void testElasticHalfSpaceOracles(Context& context) {
  struct ElasticFixture {
    const char* name;
    double incidenceAngleDegrees;
    double compressionalAttenuation;
    double shearAttenuation;
    std::complex<double> expected;
  };
  const std::array fixtures{
      ElasticFixture{"lossless normal", 0.0, 0.0, 0.0,
                     {4.54545454545454530e-1, 0.0}},
      ElasticFixture{"lossless 30 degree", 30.0, 0.0, 0.0,
                     {4.04439753970198490e-1, 0.0}},
      ElasticFixture{"lossless 60 degree", 60.0, 0.0, 0.0,
                     {-1.33833243674634672e-1,
                      1.68586165662701098e-1}},
      ElasticFixture{"lossy 30 degree", 30.0, 0.5, 1.0,
                     {4.04463539890146495e-1,
                      3.04094195441093063e-3}},
  };
  for (const ElasticFixture& fixture : fixtures) {
    const double angle =
        fixture.incidenceAngleDegrees * std::numbers::pi / 180.0;
    const AcousticMaterial material{
        .compressionalSoundSpeed = 2000.0,
        .shearSoundSpeed = 1000.0,
        .density = 2000.0,
        .compressionalAttenuation = {
            .value = fixture.compressionalAttenuation,
            .unit = AttenuationUnit::DecibelsPerWavelength},
        .shearAttenuation = {
            .value = fixture.shearAttenuation,
            .unit = AttenuationUnit::DecibelsPerWavelength}};
    const BoundaryAcousticsResult result =
        evaluateAcousticHalfSpaceAcoustics(
            material, 100.0, VolumeAttenuation{}, 1000.0, 1000.0,
            std::sin(angle) / 1500.0,
            std::cos(angle) / 1500.0);
    checkComplexNear(context, result.rawCoefficient, fixture.expected,
                     3.0e-15, fixture.name);
    context.checkNear(result.amplitudeMultiplier,
                      std::abs(fixture.expected), 3.0e-15,
                      std::string(fixture.name) + " magnitude");
  }

  const AcousticMaterial fanMaterial{
      .compressionalSoundSpeed = 2000.0,
      .shearSoundSpeed = 1000.0,
      .density = 2000.0,
      .compressionalAttenuation = {
          .value = 0.5,
          .unit = AttenuationUnit::DecibelsPerWavelength},
      .shearAttenuation = {
          .value = 1.0,
          .unit = AttenuationUnit::DecibelsPerWavelength}};
  const BoundaryAcousticsResult fanResult =
      evaluateAcousticHalfSpaceAcoustics(
          fanMaterial, 100.0, VolumeAttenuation{}, 1000.0, 1000.0,
          4.41123891731239656e-4, 4.99854135311822716e-4);
  checkComplexNear(
      context, fanResult.rawCoefficient,
      {3.90674596711252209e-1, 4.96522118361658518e-3}, 3.0e-15,
      "400-ray fan near-critical Origin event");
}

void testGrainSizeHalfSpaceOracles(Context& context) {
  const GrainSizeMaterial grain =
      *BoundaryModel::grainSizeHalfSpace(100.0, 3.0)
           .grainSizeMaterial();
  struct Fixture {
    double angleDegrees;
    std::complex<double> expected;
  };
  const std::array fixtures{
      Fixture{0.0, {1.82607622727862906e-1,
                    8.19608151292109191e-3}},
      Fixture{30.0, {1.96328379103933526e-1,
                     1.15078565713302005e-2}},
      Fixture{60.0, {3.37988225520059138e-1,
                     5.90745040880302011e-2}},
  };
  for (const Fixture& fixture : fixtures) {
    const double angle = fixture.angleDegrees * std::numbers::pi / 180.0;
    const auto result = evaluateGrainSizeHalfSpaceAcoustics(
        grain, 1000.0, 1500.0, 1000.0,
        std::sin(angle) / 1500.0, std::cos(angle) / 1500.0);
    checkComplexNear(context, result.rawCoefficient, fixture.expected,
                     3.0e-15, "grain-size gfortran coefficient");
    const auto doubledFrequency = evaluateGrainSizeHalfSpaceAcoustics(
        grain, 2000.0, 1500.0, 1000.0,
        std::sin(angle) / 1500.0, std::cos(angle) / 1500.0);
    checkComplexNear(context, doubledFrequency.rawCoefficient,
                     fixture.expected, 3.0e-15,
                     "grain-size coefficient is frequency invariant");
  }
}

void testTabulatedReflectionOracles(Context& context) {
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
          .phaseRadians = 250.0 * std::numbers::pi / 180.0},
      TabulatedReflectionPoint{
          .angleDegrees = 80.0,
          .magnitude = 0.4,
          .phaseRadians = 290.0 * std::numbers::pi / 180.0}};
  struct Fixture {
    double grazingAngleDegrees;
    double expectedMagnitude;
    double expectedPhase;
    std::complex<double> expectedRaw;
  };
  const std::array fixtures{
      Fixture{5.0, 0.0, 0.0, {0.0, 0.0}},
      Fixture{10.0, 0.2, 2.96705972839036036,
              {-1.9696155060244161e-1, 3.47296355333860593e-2}},
      Fixture{20.0, 0.4, std::numbers::pi,
              {-4.0e-1, 4.89858719658941308e-17}},
      Fixture{30.0, 0.6, 3.31612557878922631,
              {-5.90884651807324746e-1, -1.04188906600158282e-1}},
      Fixture{45.0, 0.75, 3.83972435438752502,
              {-5.74533332339233538e-1, -4.82090707264904439e-1}},
      Fixture{70.0, 0.65, 4.71238898038468967,
              {-1.19403062916866932e-16, -6.5e-1}},
      Fixture{80.0, 0.4, 5.06145483078355607,
              {1.36808057330267602e-1, -3.75877048314363360e-1}},
      Fixture{85.0, 0.0, 0.0, {0.0, 0.0}}};
  for (const Fixture& fixture : fixtures) {
    const double angle =
        fixture.grazingAngleDegrees * std::numbers::pi / 180.0;
    const BoundaryAcousticsResult result =
        evaluateTabulatedReflectionAcoustics(
            table, std::cos(angle) / 1500.0,
            std::sin(angle) / 1500.0);
    context.checkNear(result.amplitudeMultiplier,
                      fixture.expectedMagnitude, 2.0e-15,
                      "tabulated magnitude matches gfortran");
    context.checkNear(result.phaseIncrement, fixture.expectedPhase,
                      3.0e-15,
                      "tabulated unwrapped phase matches gfortran");
    checkComplexNear(context, result.rawCoefficient,
                     fixture.expectedRaw, 3.0e-15,
                     "tabulated raw coefficient matches gfortran");
    context.check(!result.coefficientSuppressed,
                  "tabulated coefficients never use acoustic suppression");
  }

  const double angle = 30.0 * std::numbers::pi / 180.0;
  const auto rightGoing = evaluateTabulatedReflectionAcoustics(
      table, std::cos(angle) / 1500.0, std::sin(angle) / 1500.0);
  const auto leftGoing = evaluateTabulatedReflectionAcoustics(
      table, -std::cos(angle) / 1500.0, std::sin(angle) / 1500.0);
  checkComplexNear(context, leftGoing.rawCoefficient,
                   rightGoing.rawCoefficient, 5.0e-16,
                   "tabulated angle folds left- and right-going rays");

  const TabulatedReflectionTable explicitPhaseAtZero{
      {.angleDegrees = 0.0, .magnitude = 1.0, .phaseRadians = 0.0},
      {.angleDegrees = 45.0, .magnitude = 0.0, .phaseRadians = 4.0},
      {.angleDegrees = 90.0, .magnitude = 1.0, .phaseRadians = 8.0}};
  const auto zero = evaluateTabulatedReflectionAcoustics(
      explicitPhaseAtZero, 1.0 / 1500.0, 1.0 / 1500.0);
  context.checkNear(zero.amplitudeMultiplier, 0.0, 2.0e-15,
                    "zero tabulated magnitude stays zero");
  context.checkNear(zero.phaseIncrement, 4.0, 3.0e-15,
                    "zero magnitude retains its explicit table phase");

  const TabulatedReflectionTable tinyTable{
      {.angleDegrees = 0.0, .magnitude = 1.0e-8,
       .phaseRadians = 0.25},
      {.angleDegrees = 90.0, .magnitude = 1.0e-8,
       .phaseRadians = 0.25}};
  const auto tiny = evaluateTabulatedReflectionAcoustics(
      tinyTable, 1.0 / 1500.0, 1.0 / 1500.0);
  context.checkNear(tiny.amplitudeMultiplier, 1.0e-8, 0.0,
                    "tiny tabulated magnitude is not coefficient-suppressed");
  context.check(!tiny.coefficientSuppressed,
                "tabulated branch bypasses the A/G 1e-5 cutoff");

  const TabulatedReflectionTable decisionTable{
      {.angleDegrees = 10.0, .magnitude = 0.2, .phaseRadians = 1.0},
      {.angleDegrees = 80.0, .magnitude = 0.8, .phaseRadians = 2.0}};
  const auto evaluateDecisionAngle = [&](double degrees) {
    const double radians = degrees * std::numbers::pi / 180.0;
    return evaluateTabulatedReflectionAcoustics(
        decisionTable, std::cos(radians) / 1500.0,
        std::sin(radians) / 1500.0);
  };
  const auto justBelowInside = evaluateDecisionAngle(9.9999999);
  context.checkNear(justBelowInside.amplitudeMultiplier,
                    1.9999999914285718e-1, 3.0e-16,
                    "REAL4 decision keeps the near-lower query inside");
  context.checkNear(justBelowInside.phaseIncrement,
                    9.9999999857142863e-1, 3.0e-16,
                    "near-lower alpha still uses the binary64 query");
  const auto belowOutside = evaluateDecisionAngle(9.9999990);
  context.checkNear(belowOutside.amplitudeMultiplier, 0.0, 0.0,
                    "REAL4 decision rejects a lower query beyond half ULP");
  const auto justAboveInside = evaluateDecisionAngle(80.0000001);
  context.checkNear(justAboveInside.amplitudeMultiplier,
                    8.0000000085714285e-1, 3.0e-16,
                    "REAL4 decision keeps the near-upper query inside");
  context.checkNear(justAboveInside.phaseIncrement,
                    2.0000000014285710, 5.0e-16,
                    "near-upper alpha still uses the binary64 query");
  const auto aboveOutside = evaluateDecisionAngle(80.0000100);
  context.checkNear(aboveOutside.amplitudeMultiplier, 0.0, 0.0,
                    "REAL4 decision rejects an upper query beyond half ULP");

  const TabulatedReflectionTable bracketDecisionTable{
      {.angleDegrees = 10.0, .magnitude = 0.2, .phaseRadians = 1.0},
      {.angleDegrees = 30.0, .magnitude = 0.6, .phaseRadians = 2.0},
      {.angleDegrees = 80.0, .magnitude = 0.8, .phaseRadians = 4.0}};
  constexpr double kNearInteriorDegrees = 29.9999999;
  const double nearInteriorRadians =
      kNearInteriorDegrees * std::numbers::pi / 180.0;
  const auto nearInterior = evaluateTabulatedReflectionAcoustics(
      bracketDecisionTable,
      std::cos(nearInteriorRadians) / 1500.0,
      std::sin(nearInteriorRadians) / 1500.0);
  const double rightBracketWeight =
      (kNearInteriorDegrees - 30.0) / (80.0 - 30.0);
  context.checkNear(
      nearInterior.amplitudeMultiplier,
      (1.0 - rightBracketWeight) * 0.6 + rightBracketWeight * 0.8,
      3.0e-15,
      "REAL4-rounded interior knot selects the right bracket");
  context.checkNear(
      nearInterior.phaseIncrement,
      (1.0 - rightBracketWeight) * 2.0 + rightBracketWeight * 4.0,
      3.0e-15,
      "binary64 alpha extrapolates within the selected right bracket");
}

void testHalfSpaceUsesMaterialRecordDepth(Context& context) {
  const BoundaryModel boundary = BoundaryModel::acousticHalfSpace(
      100.0,
      AcousticMaterial{
          .compressionalSoundSpeed = 1600.0,
          .shearSoundSpeed = 0.0,
          .density = 1800.0,
          .compressionalAttenuation = {
              .value = 0.0,
              .unit = AttenuationUnit::DecibelsPerWavelength},
      });
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
                          0.25}}),
  };
  const BoundaryAcousticsResult lossless = evaluateBoundaryAcoustics(
      boundary, 1000.0, 1000.0, 1.0 / 1500.0, 1.0 / 1500.0);
  const BoundaryAcousticsResult lossy = evaluateBoundaryAcoustics(
      boundary, biological, 1000.0, 1000.0, 1.0 / 1500.0,
      1.0 / 1500.0);
  context.check(
      lossy.rawCoefficient != lossless.rawCoefficient,
      "flat acoustic half-space biological loss uses its ENV record depth");
}

void testLongFormatSegmentMaterialAndDepth(Context& context) {
  const auto material = [](double soundSpeed, double density) {
    return AcousticMaterial{
        .compressionalSoundSpeed = soundSpeed,
        .shearSoundSpeed = 0.0,
        .density = density,
        .compressionalAttenuation = {
            .value = 0.0,
            .unit = AttenuationUnit::DecibelsPerWavelength}};
  };
  const auto longMaterials =
      std::make_shared<const std::vector<AcousticMaterial>>(
          std::vector<AcousticMaterial>{
              material(1600.0, 1500.0), material(1800.0, 2000.0),
              material(2000.0, 2500.0)});
  const BoundaryModel boundary = BoundaryModel::acousticHalfSpace(
      BoundaryGeometry::piecewiseLinear(
          {Vec2{.range = -1000.0, .depth = 100.0},
           Vec2{.range = 2000.0, .depth = 100.0},
           Vec2{.range = 4000.0, .depth = 100.0}},
          100.0, BoundaryOrientation::Lower),
      material(1550.0, 1400.0), longMaterials);
  const BoundaryAcousticsResult first = evaluateBoundaryAcoustics(
      boundary, 1U, VolumeAttenuation{}, 1000.0, 1000.0,
      1.0 / 1500.0, 1.0 / 1500.0);
  const BoundaryAcousticsResult second = evaluateBoundaryAcoustics(
      boundary, 2U, VolumeAttenuation{}, 1000.0, 1000.0,
      1.0 / 1500.0, 1.0 / 1500.0);
  context.check(first.rawCoefficient != second.rawCoefficient,
                "LL segment switch changes the acoustic coefficient");
  const BoundaryAcousticsResult direct = evaluateFluidHalfSpaceAcoustics(
      (*longMaterials)[1U], 1.0e20, VolumeAttenuation{}, 1000.0,
      1000.0, 1.0 / 1500.0, 1.0 / 1500.0);
  checkComplexNear(context, second.rawCoefficient,
                   direct.rawCoefficient, 0.0,
                   "LL segment coefficient uses its frozen left-node material");

  auto mutableElastic = std::vector<AcousticMaterial>(*longMaterials);
  mutableElastic[1U].shearSoundSpeed = 900.0;
  mutableElastic[1U].shearAttenuation = {
      .value = 0.05,
      .unit = AttenuationUnit::DecibelsPerWavelength};
  const auto elasticMaterials =
      std::make_shared<const std::vector<AcousticMaterial>>(
          std::move(mutableElastic));
  const BoundaryModel elasticBoundary = BoundaryModel::acousticHalfSpace(
      boundary.geometry(), *boundary.material(), elasticMaterials);
  const BoundaryAcousticsResult elastic = evaluateBoundaryAcoustics(
      elasticBoundary, 2U, VolumeAttenuation{}, 1000.0, 1000.0,
      1.0 / 1500.0, 1.0 / 1500.0);
  const BoundaryAcousticsResult directElastic =
      evaluateAcousticHalfSpaceAcoustics(
          (*elasticMaterials)[1U], 1.0e20, VolumeAttenuation{}, 1000.0,
          1000.0, 1.0 / 1500.0, 1.0 / 1500.0);
  checkComplexNear(context, elastic.rawCoefficient,
                   directElastic.rawCoefficient, 0.0,
                   "elastic LL segment evaluates both P and S material");
  context.check(elastic.rawCoefficient != second.rawCoefficient,
                "elastic LL shear properties affect reflection");

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
                          0.25}}),
  };
  const BoundaryAcousticsResult excluded = evaluateBoundaryAcoustics(
      boundary, 2U, biological, 1000.0, 1000.0,
      1.0 / 1500.0, 1.0 / 1500.0);
  checkComplexNear(
      context, excluded.rawCoefficient, second.rawCoefficient, 0.0,
      "LL 1D20 conversion excludes a biological layer at physical depth");
}

void testCoefficientApplicationSemantics(Context& context) {
  for (const auto& fixture :
       bellhop::test::kRawCoefficientApplicationFixtures) {
    const BoundaryAcousticsResult result =
        classifyBoundaryCoefficient(
            fixture.rawCoefficient, fixture.acousticHalfSpace);
    const double outgoingAmplitude =
        fixture.incomingAmplitude * result.amplitudeMultiplier;
    const double outgoingPhase =
        fixture.incomingUnwrappedPhase + result.phaseIncrement;
    context.checkNear(
        outgoingAmplitude, fixture.expectedAmplitude, 1.0e-15,
        std::string(fixture.name) + " outgoing amplitude");
    context.checkNear(
        outgoingPhase, fixture.expectedUnwrappedPhase, 1.0e-15,
        std::string(fixture.name) + " unwrapped phase");
    context.checkNear(
        std::atan2(std::sin(outgoingPhase), std::cos(outgoingPhase)),
        fixture.expectedWrappedPhase, 1.0e-15,
        std::string(fixture.name) + " wrapped comparison phase");
    context.check(
        result.coefficientSuppressed == fixture.expectedSuppressed,
        std::string(fixture.name) + " suppression decision");
    checkComplexNear(context, result.rawCoefficient,
                     fixture.rawCoefficient, 0.0,
                     std::string(fixture.name) +
                         " retains raw coefficient");
  }
}

void testInvalidAndUnsupportedInputs(Context& context) {
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(evaluateBoundaryAcoustics(
            BoundaryModel::rigid(100.0), 0.0, 1000.0, 0.0,
            1.0 / 1500.0));
      },
      "non-positive frequency is rejected");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(evaluateBoundaryAcoustics(
            BoundaryModel::rigid(100.0), 50.0, 1000.0, 0.0,
            0.0));
      },
      "non-positive outward normal slowness is rejected");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(classifyBoundaryCoefficient(
            {std::numeric_limits<double>::quiet_NaN(), 0.0},
            true));
      },
      "non-finite raw coefficient is rejected");

  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(evaluateAcousticHalfSpaceAcoustics(
            AcousticMaterial{
                .compressionalSoundSpeed = 1600.0,
                .shearSoundSpeed = 0.0,
                .density = 1800.0,
                .shearAttenuation = {.value = 0.1}},
            100.0, VolumeAttenuation{}, 50.0, 1000.0, 0.0,
            1.0 / 1500.0));
      },
      "zero shear speed with nonzero shear loss is rejected");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(evaluateAcousticHalfSpaceAcoustics(
            AcousticMaterial{
                .compressionalSoundSpeed = 1600.0,
                .shearSoundSpeed = -100.0,
                .density = 1800.0},
            100.0, VolumeAttenuation{}, 50.0, 1000.0, 0.0,
            1.0 / 1500.0));
      },
      "public half-space evaluator rejects negative shear speed");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(evaluateAcousticHalfSpaceAcoustics(
            AcousticMaterial{
                .compressionalSoundSpeed = 1600.0,
                .shearSoundSpeed = 500.0,
                .density = -1800.0},
            100.0, VolumeAttenuation{}, 50.0, 1000.0, 0.0,
            1.0 / 1500.0));
      },
      "public half-space evaluator rejects negative density");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(evaluateAcousticHalfSpaceAcoustics(
            AcousticMaterial{
                .compressionalSoundSpeed = 1600.0,
                .shearSoundSpeed = 0.0,
                .density = 1800.0,
                .shearAttenuation = {
                    .unit = static_cast<AttenuationUnit>(999)}},
            100.0, VolumeAttenuation{}, 50.0, 1000.0, 0.0,
            1.0 / 1500.0));
      },
      "public half-space evaluator validates unused shear attenuation");
}

}  // namespace

int main() {
  Context context;
  testVacuumAndRigid(context);
  testFluidHalfSpaceOracles(context);
  testElasticHalfSpaceOracles(context);
  testGrainSizeHalfSpaceOracles(context);
  testTabulatedReflectionOracles(context);
  testHalfSpaceUsesMaterialRecordDepth(context);
  testLongFormatSegmentMaterialAndDepth(context);
  testCoefficientApplicationSemantics(context);
  testInvalidAndUnsupportedInputs(context);

  if (context.failureCount() != 0) {
    std::cerr << context.failureCount() << " test assertion(s) failed\n";
    return 1;
  }
  std::cout << "All Bellhop F2CPP boundary acoustics tests passed\n";
  return 0;
}
