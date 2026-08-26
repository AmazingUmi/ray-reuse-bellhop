#include "rayreuse/field/geometric_gaussian_influence.hpp"

#include <cmath>
#include <complex>
#include <cstddef>
#include <iostream>
#include <numbers>
#include <string>
#include <vector>

#include "support/test_harness.hpp"

namespace {

using rayreuse::FrequencyWorkspace;
using rayreuse::GeometricGaussianDiagnostic;
using rayreuse::GeometricGaussianDiagnosticRequest;
using rayreuse::GeometricGaussianInfluence;
using rayreuse::GeometricGaussianWidthBranch;
using rayreuse::IntensityWorkspace;
using rayreuse::RayFrequencyPoint;
using rayreuse::RayFrequencyState;
using rayreuse::RayPath;
using rayreuse::RayState;
using rayreuse::ReceiverGrid;
using rayreuse::test::Context;

constexpr double kSoundSpeed = 1500.0;
constexpr double kFrequency = 50.0;
constexpr double kDalpha = 0.1;

RayPath makeHorizontalPath(const std::vector<double>& ranges,
                           const std::vector<double>& q,
                           double launchAngle = 0.0) {
  RayPath path;
  path.launchAngle = launchAngle;
  for (std::size_t index = 0U; index < ranges.size(); ++index) {
    path.points.push_back(
        RayState{.position = {.range = ranges[index], .depth = 500.0},
                 .slowness = {.range = 1.0 / kSoundSpeed, .depth = 0.0},
                 .dynamicP = {},
                 .dynamicQ = {q[index], 0.0},
                 .soundSpeed = kSoundSpeed,
                 .realTravelTime = 0.0});
  }
  return path;
}

RayFrequencyState makeFrequencyState(std::size_t pointCount,
                                     double rightRealDelay,
                                     double imaginaryDelay = 0.0) {
  RayFrequencyState result{.frequency = kFrequency, .points = {}};
  for (std::size_t index = 0U; index < pointCount; ++index) {
    const double weight =
        static_cast<double>(index) / static_cast<double>(pointCount - 1U);
    result.points.push_back(
        RayFrequencyPoint{.complexTravelTime =
                              {weight * rightRealDelay, imaginaryDelay},
                          .amplitude = 1.0,
                          .reflectionPhase = 0.0,
                          .active = true});
  }
  return result;
}

void checkComplexNear(Context& context, std::complex<double> actual,
                      std::complex<double> expected, double tolerance,
                      const std::string& message) {
  context.checkNear(actual.real(), expected.real(), tolerance,
                    message + " real");
  context.checkNear(actual.imag(), expected.imag(), tolerance,
                    message + " imaginary");
}

GeometricGaussianDiagnostic runWidthDiagnostic(
    Context& context, double leftQ, double rightQ, double rightRealDelay,
    double receiverDepth = 500.0) {
  const ReceiverGrid receivers({receiverDepth}, {100.0, 300.0});
  const RayPath path =
      makeHorizontalPath({0.0, 200.0}, {leftQ, rightQ});
  const RayFrequencyState state =
      makeFrequencyState(2U, rightRealDelay);
  FrequencyWorkspace workspace(kFrequency, receivers);
  const auto diagnostic = GeometricGaussianInfluence(receivers).accumulate(
      workspace, path, state, kDalpha,
      GeometricGaussianDiagnosticRequest{.receiverRangeIndex = 0U,
                                         .receiverDepthIndex = 0U});
  context.check(diagnostic.has_value() && diagnostic->evaluated,
                "Cartesian B evaluates the width-branch receiver");
  return diagnostic.value();
}

void testSigmaBranchesAndOriginAnchor(Context& context) {
  const GeometricGaussianDiagnostic geometric =
      runWidthDiagnostic(context, 200.0, 400.0, 0.001);
  context.check(geometric.widthBranch ==
                    GeometricGaussianWidthBranch::Geometric,
                "Cartesian B selects the geometric width branch");
  context.checkNear(geometric.geometricSigma, 0.02, 1.0e-18,
                    "Cartesian B geometric sigma");
  context.checkNear(geometric.nearFieldSigma,
                    0.010000000149011612, 0.0,
                    "Cartesian B preserves Origin REAL4 0.2 promotion");
  context.checkNear(geometric.wavelengthSigma,
                    94.24777960769379, 2.0e-14,
                    "Cartesian B pi-lambda candidate");
  context.checkNear(geometric.sigma1, 0.02, 1.0e-18,
                    "Cartesian B geometric sigma1");
  context.checkNear(geometric.gaussianWeight, 1.0, 0.0,
                    "Cartesian B centerline Gaussian weight");
  checkComplexNear(context, geometric.pressureIncrement,
                   {0.8810792938493487, -0.13954925083786673},
                   2.0e-15, "Cartesian B coherent F2CPP anchor");

  const GeometricGaussianDiagnostic offset =
      runWidthDiagnostic(context, 200.0, 400.0, 0.001, 500.01);
  context.checkNear(offset.normalOffset, 0.01, 1.0e-14,
                    "Cartesian B normal offset");
  context.checkNear(offset.gaussianWeight, std::exp(-0.125), 5.0e-13,
                    "Cartesian B Gaussian kernel");

  const GeometricGaussianDiagnostic nearField =
      runWidthDiagnostic(context, 100.0, 200.0, 0.002);
  context.check(nearField.widthBranch ==
                    GeometricGaussianWidthBranch::NearField,
                "Cartesian B selects the near-field branch");
  context.checkNear(nearField.nearFieldSigma,
                    0.020000000298023225, 0.0,
                    "Cartesian B near-field sigma");
  context.checkNear(nearField.sigma1,
                    0.020000000298023225, 0.0,
                    "Cartesian B near-field sigma1");
  context.checkNear(nearField.gaussianWeight,
                    0.70710677591819149, 0.0,
                    "Cartesian B widening weight");

  const GeometricGaussianDiagnostic wavelength =
      runWidthDiagnostic(context, 100.0, 200.0, 20.0);
  context.check(wavelength.widthBranch ==
                    GeometricGaussianWidthBranch::WavelengthCap,
                "Cartesian B selects the wavelength-cap branch");
  context.checkNear(wavelength.nearFieldSigma,
                    200.00000298023224, 0.0,
                    "Cartesian B uncapped near-field candidate");
  context.checkNear(wavelength.sigma1, wavelength.wavelengthSigma, 0.0,
                    "Cartesian B caps width at pi lambda");
}

void testIntensityUsesAttenuationAndGaussianOnce(Context& context) {
  const ReceiverGrid receivers({500.0}, {100.0, 300.0});
  const RayPath path =
      makeHorizontalPath({0.0, 200.0}, {100.0, 200.0});
  const RayFrequencyState state =
      makeFrequencyState(2U, 0.002, -1.0e-4);
  IntensityWorkspace workspace(kFrequency, receivers);
  const auto diagnostic =
      GeometricGaussianInfluence(receivers).accumulateIntensity(
          workspace, path, state, kDalpha,
          GeometricGaussianDiagnosticRequest{0U, 0U});
  context.check(diagnostic.has_value() && diagnostic->evaluated,
                "Cartesian IB evaluates the selected receiver");
  context.checkNear(diagnostic->intensityIncrement,
                    1.324577993883975, 0.0,
                    "Cartesian IB uses sqrt(2pi)*power*W");
  context.checkNear(workspace.at(0U, 0U),
                    diagnostic->intensityIncrement, 0.0,
                    "Cartesian IB stores the per-ray intensity increment");
  context.check(
      std::abs(diagnostic->intensityIncrement - 0.9366181026208776) > 0.3,
      "Cartesian IB fixture distinguishes W from W squared");
}

void testCausticAndActivePrefix(Context& context) {
  {
    const ReceiverGrid receivers({500.0}, {150.0, 300.0});
    const RayPath path =
        makeHorizontalPath({0.0, 200.0}, {1.0, -1.0});
    const RayFrequencyState state = makeFrequencyState(2U, 0.002);
    FrequencyWorkspace workspace(kFrequency, receivers);
    const auto diagnostic = GeometricGaussianInfluence(receivers).accumulate(
        workspace, path, state, kDalpha,
        GeometricGaussianDiagnosticRequest{0U, 0U});
    context.check(diagnostic.has_value() && diagnostic->evaluated,
                  "Cartesian B evaluates across a real-q caustic");
    context.checkNear(diagnostic->qInterpolated, -0.5, 0.0,
                      "Cartesian B caustic fixture q");
    context.checkNear(diagnostic->causticPhase, std::numbers::pi / 2.0, 0.0,
                      "Cartesian B adds receiver-side caustic phase");
  }

  {
    const ReceiverGrid receivers({500.0}, {100.0, 300.0, 500.0});
    const RayPath path = makeHorizontalPath(
        {0.0, 200.0, 400.0, 600.0}, {1.0, 100.0, 200.0, 300.0});
    RayFrequencyState state = makeFrequencyState(4U, 0.006);
    state.points[2U].active = false;
    state.points[3U].active = false;
    FrequencyWorkspace workspace(kFrequency, receivers);
    static_cast<void>(GeometricGaussianInfluence(receivers).accumulate(
        workspace, path, state, kDalpha));
    context.check(std::abs(workspace.at(0U, 1U)) > 0.0 &&
                      workspace.at(0U, 2U) == std::complex<double>{},
                  "Cartesian B includes inactive terminal and excludes its "
                  "suffix");
  }
}

}  // namespace

int main() {
  Context context;
  testSigmaBranchesAndOriginAnchor(context);
  testIntensityUsesAttenuationAndGaussianOnce(context);
  testCausticAndActivePrefix(context);
  if (context.failureCount() != 0) {
    std::cerr << context.failureCount()
              << " geometric Gaussian influence assertion(s) failed\n";
    return 1;
  }
  std::cout << "All geometric Gaussian influence tests passed\n";
  return 0;
}
