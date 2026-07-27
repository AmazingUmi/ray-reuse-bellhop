#include <cmath>
#include <complex>
#include <iostream>
#include <limits>
#include <numbers>
#include <string>

#include "bellhop/acoustics/attenuation.hpp"
#include "bellhop/acoustics/boundary_acoustics.hpp"
#include "bellhop/error.hpp"
#include "support/boundary_acoustics_fixture.hpp"
#include "support/test_harness.hpp"

namespace {

using bellhop::AcousticMaterial;
using bellhop::AttenuationUnit;
using bellhop::BoundaryAcousticsResult;
using bellhop::BoundaryModel;
using bellhop::ValidationError;
using bellhop::classifyBoundaryCoefficient;
using bellhop::convertAttenuation;
using bellhop::evaluateBoundaryAcoustics;
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

  const BoundaryModel elastic = BoundaryModel::acousticHalfSpace(
      100.0,
      AcousticMaterial{
          .compressionalSoundSpeed = 1600.0,
          .shearSoundSpeed = 500.0,
          .density = 1800.0});
  context.expectThrows<ValidationError>(
      [&elastic] {
        static_cast<void>(evaluateBoundaryAcoustics(
            elastic, 50.0, 1000.0, 0.0, 1.0 / 1500.0));
      },
      "elastic half-space is rejected rather than approximated as fluid");
}

}  // namespace

int main() {
  Context context;
  testVacuumAndRigid(context);
  testFluidHalfSpaceOracles(context);
  testCoefficientApplicationSemantics(context);
  testInvalidAndUnsupportedInputs(context);

  if (context.failureCount() != 0) {
    std::cerr << context.failureCount() << " test assertion(s) failed\n";
    return 1;
  }
  std::cout << "All Bellhop F2CPP boundary acoustics tests passed\n";
  return 0;
}
