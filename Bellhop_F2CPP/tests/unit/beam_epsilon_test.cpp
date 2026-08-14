#include <cmath>
#include <complex>
#include <iostream>
#include <limits>

#include "bellhop/error.hpp"
#include "bellhop/field/beam_epsilon.hpp"
#include "support/test_harness.hpp"

namespace {

using bellhop::BeamEpsilon;
using bellhop::BeamWidthMode;
using bellhop::ValidationError;
using bellhop::pickBeamEpsilon;
using bellhop::pickMinimumWidthEpsilon;
using bellhop::test::Context;

void testInfluenceOracleAnchors(Context& context) {
  const BeamEpsilon direct = pickMinimumWidthEpsilon(
      50.0, 1500.0, 2500.0, 1.0);
  context.checkNear(
      direct.value.real(), 0.0, 0.0,
      "direct minimum-width epsilon is purely imaginary");
  context.checkNear(
      direct.value.imag(), 3.75000000000000047e6, 0.0,
      "direct epsilon matches Influence oracle schema v1");
  context.checkNear(
      direct.halfWidthMeters, 1.5450968080927584e2, 2.0e-14,
      "direct half width preserves PickEpsilon operation order");

  const BeamEpsilon munk = pickMinimumWidthEpsilon(
      50.0, 1501.38000000000011, 25000.0, 1.0);
  context.checkNear(
      munk.value.real(), 0.0, 0.0,
      "Munk minimum-width epsilon is purely imaginary");
  context.checkNear(
      munk.value.imag(), 3.75344999999999925e7, 0.0,
      "Munk epsilon matches both Influence oracle samples");
  context.checkNear(
      munk.halfWidthMeters, 4.8882721738801513e2, 6.0e-14,
      "Munk half width matches the source-speed calculation");
}

void testMultiplierAndFrequencySemantics(Context& context) {
  const BeamEpsilon doubled = pickMinimumWidthEpsilon(
      250.0, 1500.0, 1000.0, 2.0);
  context.checkNear(
      doubled.value.imag(), 3.00000000000000047e6, 0.0,
      "epsilon multiplier is applied after optimum selection");

  const BeamEpsilon low = pickMinimumWidthEpsilon(
      50.0, 1500.0, 5000.0, 1.0);
  const BeamEpsilon high = pickMinimumWidthEpsilon(
      5000.0, 1500.0, 5000.0, 1.0);
  context.check(
      high.halfWidthMeters < low.halfWidthMeters,
      "minimum half width shrinks with increasing frequency");
  context.checkNear(
      high.value.imag(), 7.50000000000000093e6, 0.0,
      "5 kHz standard-case epsilon preserves binary64 operation order");
}

void testSpaceFillingAndWkbSemantics(Context& context) {
  const BeamEpsilon filling = pickBeamEpsilon(
      BeamWidthMode::SpaceFilling, 250.0, 1500.0, 0.0,
      0.25, 0.01, 0.0, 1.0);
  context.checkNear(filling.halfWidthMeters,
                    190.9859317102744, 3.0e-13,
                    "space-filling half width uses angular spacing");
  context.checkNear(filling.value.real(), 0.0, 0.0,
                    "space-filling epsilon is imaginary");
  context.checkNear(filling.value.imag(),
                    2.8647889756541163e7, 5.0e-8,
                    "space-filling epsilon preserves PickEpsilon order");

  const BeamEpsilon constantWkb = pickBeamEpsilon(
      BeamWidthMode::Wkb, 250.0, 1500.0, 0.0,
      0.25, 0.01, 0.0, 2.0);
  context.check(constantWkb.halfWidthMeters ==
                    std::numeric_limits<double>::max(),
                "WKB reports the legacy HUGE half width");
  context.checkNear(constantWkb.value.real(), 2.0e10, 0.0,
                    "zero-gradient WKB epsilon is the legacy real constant");
  context.checkNear(constantWkb.value.imag(), 0.0, 0.0,
                    "WKB epsilon remains real");

  const BeamEpsilon gradientWkb = pickBeamEpsilon(
      BeamWidthMode::Wkb, 250.0, 1500.0, 0.75,
      0.3, 0.01, 0.0, 1.0);
  context.checkNear(gradientWkb.value.real(),
                    -8.9016334871922247e5, 2.0e-9,
                    "WKB preserves the legacy cos(alpha squared) formula");
  context.checkNear(gradientWkb.value.imag(), 0.0, 0.0,
                    "gradient WKB epsilon remains real");
}

void testInvalidInputs(Context& context) {
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(
            pickMinimumWidthEpsilon(0.0, 1500.0, 1000.0, 1.0));
      },
      "zero frequency is rejected");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(
            pickMinimumWidthEpsilon(50.0, 0.0, 1000.0, 1.0));
      },
      "zero source sound speed is rejected");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(
            pickMinimumWidthEpsilon(50.0, 1500.0, 0.0, 1.0));
      },
      "zero loop range is rejected");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(
            pickMinimumWidthEpsilon(50.0, 1500.0, 1000.0, 0.0));
      },
      "zero epsilon multiplier is rejected");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(pickMinimumWidthEpsilon(
            std::numeric_limits<double>::quiet_NaN(), 1500.0,
            1000.0, 1.0));
      },
      "non-finite input is rejected");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(pickMinimumWidthEpsilon(
            1.0, std::numeric_limits<double>::max(),
            std::numeric_limits<double>::max(), 1.0));
      },
      "overflowing minimum-width intermediate is rejected");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(pickMinimumWidthEpsilon(
            50.0, 0.25, 1.0,
            std::numeric_limits<double>::denorm_min()));
      },
      "epsilon multiplier underflowing the result to zero is rejected");
}

}  // namespace

int main() {
  Context context;
  testInfluenceOracleAnchors(context);
  testMultiplierAndFrequencySemantics(context);
  testSpaceFillingAndWkbSemantics(context);
  testInvalidInputs(context);

  if (context.failureCount() != 0) {
    std::cerr << context.failureCount()
              << " beam-epsilon assertion(s) failed\n";
    return 1;
  }
  std::cout << "All Bellhop F2CPP beam-epsilon tests passed\n";
  return 0;
}
