#include "rayreuse/field/geometric_hat_influence.hpp"

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
using rayreuse::GeometricHatDiagnosticRequest;
using rayreuse::GeometricHatInfluence;
using rayreuse::IntensityWorkspace;
using rayreuse::RayFrequencyPoint;
using rayreuse::RayFrequencyState;
using rayreuse::RayPath;
using rayreuse::RayState;
using rayreuse::ReceiverGrid;
using rayreuse::test::Context;

constexpr double kSoundSpeed = 1500.0;
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

RayFrequencyState makeFrequencyState(const std::vector<double>& ranges,
                                     double frequency,
                                     double imaginaryDelay = 0.0) {
  RayFrequencyState state;
  state.frequency = frequency;
  for (const double range : ranges) {
    state.points.push_back(
        RayFrequencyPoint{.complexTravelTime =
                              {range / kSoundSpeed, imaginaryDelay},
                          .amplitude = 1.0,
                          .reflectionPhase = 0.0,
                          .active = true});
  }
  return state;
}

void checkComplexNear(Context& context, std::complex<double> actual,
                      std::complex<double> expected, double tolerance,
                      const std::string& message) {
  context.checkNear(actual.real(), expected.real(), tolerance,
                    message + " real");
  context.checkNear(actual.imag(), expected.imag(), tolerance,
                    message + " imaginary");
}

void testCartesianOriginAnchor(Context& context) {
  const std::vector<double> ranges{0.0, 200.0, 400.0, 600.0};
  const ReceiverGrid receivers({500.0}, {100.0, 300.0, 500.0});
  const RayPath path =
      makeHorizontalPath(ranges, {0.0, 100.0, 200.0, 300.0});
  const RayFrequencyState state = makeFrequencyState(ranges, 50.0);
  FrequencyWorkspace workspace(50.0, receivers);
  const auto diagnostic = GeometricHatInfluence(receivers).accumulate(
      workspace, path, state, kDalpha,
      GeometricHatDiagnosticRequest{.receiverRangeIndex = 0U,
                                    .receiverDepthIndex = 0U});

  context.check(diagnostic.has_value() && diagnostic->evaluated &&
                    diagnostic->evaluationCount == 1U &&
                    diagnostic->leftPointIndex == 0U &&
                    diagnostic->rightPointIndex == 1U,
                "Cartesian G uses the Origin half-open receiver segment");
  context.checkNear(diagnostic->interpolationWeight, 0.5, 0.0,
                    "Cartesian G interpolation weight");
  context.checkNear(diagnostic->qInterpolated, 50.0, 0.0,
                    "Cartesian G interpolated real q");
  context.checkNear(diagnostic->hatWeight, 1.0, 0.0,
                    "Cartesian G centerline hat weight");
  context.checkNear(diagnostic->amplitudeConstant, std::sqrt(30.0), 1.0e-15,
                    "Cartesian G amplitude constant");
  checkComplexNear(context, workspace.at(0U, 0U),
                   {-2.7386127875258324, -4.743416490252568}, 2.0e-14,
                   "Cartesian G 50 Hz F2CPP anchor");
}

void testIntensityUsesAttenuationAndHatOnce(Context& context) {
  const std::vector<double> ranges{0.0, 200.0};
  const double beamRadius = 50.0 / (kSoundSpeed / kDalpha);
  const ReceiverGrid receivers({500.0 + beamRadius / 2.0}, {100.0, 300.0});
  const RayPath path = makeHorizontalPath(ranges, {0.0, 100.0});
  const RayFrequencyState state =
      makeFrequencyState(ranges, 10.0, -1.0e-4);
  IntensityWorkspace workspace(10.0, receivers);
  const auto diagnostic = GeometricHatInfluence(receivers).accumulateIntensity(
      workspace, path, state, kDalpha,
      GeometricHatDiagnosticRequest{.receiverRangeIndex = 0U,
                                    .receiverDepthIndex = 0U});

  context.check(diagnostic.has_value() && diagnostic->evaluated,
                "Cartesian G intensity evaluates the offset receiver");
  context.checkNear(diagnostic->hatWeight, 0.5, 3.0e-12,
                    "Cartesian G intensity uses nontrivial W");
  context.checkNear(diagnostic->intensityIncrement, 14.81268384785484,
                    2.0e-11,
                    "Cartesian G intensity applies attenuation and W once");
  context.checkNear(workspace.at(0U, 0U),
                    diagnostic->intensityIncrement, 0.0,
                    "Cartesian G stores the per-ray intensity increment");
  context.check(
      std::abs(diagnostic->intensityIncrement -
               14.81268384785484 * diagnostic->hatWeight) > 1.0,
      "Cartesian G fixture distinguishes W from W squared");
}

void testCausticAndActivePrefix(Context& context) {
  {
    const std::vector<double> ranges{0.0, 200.0, 400.0};
    const ReceiverGrid receivers({500.0}, {350.0});
    const RayPath path = makeHorizontalPath(ranges, {1.0, 1.0, -1.0});
    RayFrequencyState state = makeFrequencyState(ranges, 1.0);
    for (RayFrequencyPoint& point : state.points) {
      point.complexTravelTime = {};
    }
    FrequencyWorkspace workspace(1.0, receivers);
    const auto diagnostic = GeometricHatInfluence(receivers).accumulate(
        workspace, path, state, kDalpha,
        GeometricHatDiagnosticRequest{.receiverRangeIndex = 0U,
                                      .receiverDepthIndex = 0U});
    context.check(diagnostic.has_value() && diagnostic->evaluated,
                  "Cartesian G evaluates after a receiver-side q crossing");
    context.checkNear(diagnostic->causticPhase, std::numbers::pi / 2.0, 0.0,
                      "Cartesian G adds the q-zero caustic phase");
    context.checkNear(diagnostic->pressureIncrement.real(), 0.0, 4.0e-15,
                      "Cartesian G caustic anchor has zero real part");
    context.check(diagnostic->pressureIncrement.imag() > 0.0,
                  "Cartesian G caustic rotates pressure by plus pi/2");
  }

  {
    const std::vector<double> ranges{0.0, 200.0, 400.0, 600.0};
    const ReceiverGrid receivers({500.0}, {100.0, 300.0, 500.0});
    const RayPath path =
        makeHorizontalPath(ranges, {1.0, 100.0, 200.0, 300.0});
    RayFrequencyState state = makeFrequencyState(ranges, 1.0);
    state.points[2U].active = false;
    state.points[3U].active = false;
    FrequencyWorkspace workspace(1.0, receivers);
    static_cast<void>(GeometricHatInfluence(receivers).accumulate(
        workspace, path, state, kDalpha));
    context.check(std::abs(workspace.at(0U, 1U)) > 0.0 &&
                      workspace.at(0U, 2U) == std::complex<double>{},
                  "Cartesian G includes the inactive terminal and excludes "
                  "its suffix");
  }
}

}  // namespace

int main() {
  Context context;
  testCartesianOriginAnchor(context);
  testIntensityUsesAttenuationAndHatOnce(context);
  testCausticAndActivePrefix(context);
  if (context.failureCount() != 0) {
    std::cerr << context.failureCount()
              << " geometric hat influence assertion(s) failed\n";
    return 1;
  }
  std::cout << "All geometric hat influence tests passed\n";
  return 0;
}
