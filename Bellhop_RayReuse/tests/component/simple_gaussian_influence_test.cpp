#include <cmath>
#include <complex>
#include <cstddef>
#include <iostream>
#include <numbers>
#include <string>

#include "rayreuse/error.hpp"
#include "rayreuse/field/simple_gaussian_influence.hpp"
#include "support/test_harness.hpp"

namespace {

using rayreuse::FrequencyWorkspace;
using rayreuse::IntensityWorkspace;
using rayreuse::RayFrequencyPoint;
using rayreuse::RayFrequencyState;
using rayreuse::RayPath;
using rayreuse::RayState;
using rayreuse::ReceiverGrid;
using rayreuse::SimpleGaussianDiagnostic;
using rayreuse::SimpleGaussianDiagnosticRequest;
using rayreuse::SimpleGaussianInfluence;
using rayreuse::ValidationError;
using rayreuse::test::Context;

template <typename Influence>
concept HasIntensityAccumulator = requires(
    Influence& influence, IntensityWorkspace& workspace,
    const RayPath& path, const RayFrequencyState& frequencyState) {
  influence.accumulateIntensity(workspace, path, frequencyState, 0.1);
};

static_assert(!HasIntensityAccumulator<SimpleGaussianInfluence>);

RayPath makePath() {
  RayPath path;
  path.launchAngle = 0.2;
  path.points = {
      RayState{.position = {0.0, 0.0},
               .slowness = {0.001, 0.002},
               .dynamicP = {0.0, 0.0},
               .dynamicQ = {1.0, 0.0},
               .soundSpeed = 1500.0,
               .realTravelTime = 0.0},
      RayState{.position = {100.0, 20.0},
               .slowness = {0.003, 0.004},
               .dynamicP = {0.0, 0.0},
               .dynamicQ = {-3.0, 0.0},
               .soundSpeed = 1500.0,
               .realTravelTime = 0.1}};
  return path;
}

RayFrequencyState makeFrequencyState() {
  return RayFrequencyState{
      .frequency = 1.0,
      .points =
          {RayFrequencyPoint{.complexTravelTime = {0.0, 0.0},
                             .amplitude = 100.0,
                             .reflectionPhase = 9.0,
                             .active = true},
           RayFrequencyPoint{.complexTravelTime = {0.1, 0.002},
                             .amplitude = 2.0,
                             .reflectionPhase = 0.3,
                             .active = true}}};
}

void checkComplexNear(Context& context, std::complex<double> actual,
                      std::complex<double> expected, double tolerance,
                      const std::string& message) {
  context.checkNear(actual.real(), expected.real(), tolerance,
                    message + " real");
  context.checkNear(actual.imag(), expected.imag(), tolerance,
                    message + " imaginary");
}

SimpleGaussianDiagnostic runDiagnostic(Context& context,
                                       std::size_t rangeIndex) {
  const ReceiverGrid receivers({15.0, 25.0}, {50.0, 75.0});
  FrequencyWorkspace workspace(1.0, receivers);
  const SimpleGaussianInfluence influence(receivers, 10.0);
  const auto diagnostic = influence.accumulate(
      workspace, makePath(), makeFrequencyState(), 0.1,
      SimpleGaussianDiagnosticRequest{
          .receiverRangeIndex = rangeIndex, .receiverDepthIndex = 0U});
  context.check(diagnostic.has_value() && diagnostic->evaluated,
                "simple Gaussian diagnostic evaluates the selected cell");
  context.check(workspace.at(1U, rangeIndex) != std::complex<double>{},
                "simple Gaussian evaluates every rectilinear receiver depth");
  return diagnostic.value();
}

void testOriginFormulaAndLegacyStepLength(Context& context) {
  const SimpleGaussianDiagnostic diagnostic = runDiagnostic(context, 0U);
  const double weight = 0.5;
  const double beta = static_cast<double>(0.98F);
  const double gaussianA = -4.0 * std::log(beta) / (0.1 * 0.1);
  const double normalization =
      0.1 * std::sqrt(gaussianA / std::numbers::pi);
  const double legacyArcLength = 15.0;
  const double segmentLength = std::sqrt(100.0 * 100.0 + 20.0 * 20.0);
  const double deltaDepth = 5.0;
  const double closestPointDistance =
      std::abs(deltaDepth * 100.0) / segmentLength;
  const double offRayDistance = std::sqrt(
      deltaDepth * deltaDepth -
      closestPointDistance * closestPointDistance);
  const double effectiveDistance = legacyArcLength + offRayDistance;
  const double angularOffset =
      std::atan(closestPointDistance / effectiveDistance);
  const std::complex<double> delay =
      std::complex<double>{0.05, 0.001} + 0.003 * deltaDepth;
  const double causticPhase = 0.5 * std::numbers::pi;
  const std::complex<double> phase =
      (2.0 * std::numbers::pi) * delay - 0.3 - causticPhase;
  const std::complex<double> expected =
      (std::sqrt(std::cos(0.2)) * normalization * 2.0 /
       std::sqrt(effectiveDistance)) *
      std::exp(-gaussianA * angularOffset * angularOffset -
               std::complex<double>{0.0, 1.0} * phase);

  context.check(
      diagnostic.evaluationCount == 1U &&
          diagnostic.leftPointIndex == 0U &&
          diagnostic.rightPointIndex == 1U,
      "simple Gaussian diagnostic selects the first receiver chord");
  context.checkNear(diagnostic.interpolationWeight, weight, 0.0,
                    "simple Gaussian range interpolation weight");
  context.checkNear(diagnostic.qInterpolated, -1.0, 0.0,
                    "simple Gaussian interpolated real q");
  context.checkNear(diagnostic.beta, beta, 0.0,
                    "simple Gaussian beta preserves default-REAL rounding");
  context.checkNear(diagnostic.gaussianA, gaussianA, 0.0,
                    "simple Gaussian A uses beta and launch spacing");
  context.checkNear(diagnostic.normalization, normalization, 0.0,
                    "simple Gaussian CN normalization");
  context.checkNear(diagnostic.legacyArcLength, legacyArcLength, 0.0,
                    "SINT uses (iS-1+W) times configured deltas");
  context.check(
      std::abs(diagnostic.legacyArcLength - weight * segmentLength) > 30.0,
      "legacy SINT fixture differs from true geometric arc length");
  context.checkNear(diagnostic.closestPointDistance, closestPointDistance,
                    1.0e-15, "simple Gaussian CPA");
  context.checkNear(diagnostic.offRayDistance, offRayDistance, 1.0e-15,
                    "simple Gaussian DS");
  context.checkNear(diagnostic.effectiveDistance, effectiveDistance,
                    1.0e-15, "simple Gaussian SX1");
  context.checkNear(diagnostic.angularOffset, angularOffset, 1.0e-15,
                    "simple Gaussian theta");
  context.checkNear(diagnostic.causticPhase, causticPhase, 0.0,
                    "receiver-interpolated q crossing adds pi over two");
  context.checkNear(diagnostic.rightAmplitude, 2.0, 0.0,
                    "simple Gaussian uses the right endpoint amplitude");
  context.checkNear(diagnostic.rightReflectionPhase, 0.3, 0.0,
                    "simple Gaussian uses the right endpoint phase");
  checkComplexNear(context, diagnostic.delay, delay, 1.0e-18,
                   "simple Gaussian interpolated delay");
  checkComplexNear(context, diagnostic.pressureIncrement, expected, 1.0e-15,
                   "simple Gaussian Origin contribution");
}

void testInterpolatedCausticPhasePersists(Context& context) {
  const SimpleGaussianDiagnostic first = runDiagnostic(context, 0U);
  const SimpleGaussianDiagnostic second = runDiagnostic(context, 1U);
  context.checkNear(first.causticPhase, 0.5 * std::numbers::pi, 0.0,
                    "first receiver crosses q at interpolation");
  context.checkNear(second.qInterpolated, -2.0, 0.0,
                    "second receiver remains beyond the caustic");
  context.checkNear(second.causticPhase, first.causticPhase, 0.0,
                    "interpolated caustic phase persists to later receivers");
}

void testSafeSubsetContracts(Context& context) {
  const ReceiverGrid receivers({15.0, 25.0}, {50.0, 75.0});
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(SimpleGaussianInfluence(receivers, 0.0));
      },
      "simple Gaussian rejects non-positive configured deltas");
  context.expectThrows<ValidationError>(
      [&] {
        FrequencyWorkspace workspace(1.0, receivers);
        const SimpleGaussianInfluence influence(receivers, 10.0);
        static_cast<void>(influence.accumulate(
            workspace, makePath(), makeFrequencyState(), 0.0));
      },
      "simple Gaussian rejects non-positive launch spacing");
}

void testInactiveTerminalPointCompletesLastSegment(Context& context) {
  RayPath path = makePath();
  path.points.push_back(
      RayState{.position = {200.0, 40.0},
               .slowness = {0.003, 0.004},
               .dynamicP = {0.0, 0.0},
               .dynamicQ = {-4.0, 0.0},
               .soundSpeed = 1500.0,
               .realTravelTime = 0.2});
  RayFrequencyState state = makeFrequencyState();
  state.points.push_back(
      RayFrequencyPoint{.complexTravelTime = {0.2, 0.002},
                        .amplitude = 3.0,
                        .reflectionPhase = 0.4,
                        .active = false});
  const ReceiverGrid receivers({30.0}, {150.0});
  FrequencyWorkspace workspace(1.0, receivers);
  static_cast<void>(SimpleGaussianInfluence(receivers, 10.0)
                        .accumulate(workspace, path, state, 0.1));
  context.check(
      workspace.at(0U, 0U) != std::complex<double>{},
      "first inactive terminal point completes the final active segment");
}

}  // namespace

int main() {
  Context context;
  testOriginFormulaAndLegacyStepLength(context);
  testInterpolatedCausticPhasePersists(context);
  testSafeSubsetContracts(context);
  testInactiveTerminalPointCompletesLastSegment(context);
  if (context.failureCount() != 0) {
    std::cerr << context.failureCount()
              << " simple-Gaussian influence assertion(s) failed\n";
    return 1;
  }
  std::cout << "All simple-Gaussian influence tests passed\n";
  return 0;
}
