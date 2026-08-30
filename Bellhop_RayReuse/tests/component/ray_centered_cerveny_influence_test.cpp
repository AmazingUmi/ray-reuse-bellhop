#include "rayreuse/field/ray_centered_cerveny_influence.hpp"

#include <cmath>
#include <complex>
#include <cstddef>
#include <iostream>
#include <limits>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

#include "rayreuse/error.hpp"
#include "support/test_harness.hpp"

namespace {

using rayreuse::BeamWidthMode;
using rayreuse::BoundaryModel;
using rayreuse::CartesianCervenySettings;
using rayreuse::cervenyHermiteTaper;
using rayreuse::Environment;
using rayreuse::FieldComponent;
using rayreuse::FrequencyWorkspace;
using rayreuse::IntensityWorkspace;
using rayreuse::RayCenteredCervenyDiagnostic;
using rayreuse::RayCenteredCervenyDiagnosticRequest;
using rayreuse::RayCenteredCervenyInfluence;
using rayreuse::RayFrequencyPoint;
using rayreuse::RayFrequencyState;
using rayreuse::RayPath;
using rayreuse::RayState;
using rayreuse::ReceiverGrid;
using rayreuse::SimulationRunMode;
using rayreuse::SoundSpeedPoint;
using rayreuse::SoundSpeedProfile;
using rayreuse::ValidationError;
using rayreuse::test::Context;

constexpr double kSoundSpeed = 1500.0;
constexpr std::complex<double> kEpsilon{0.0, 1.0};

Environment makeEnvironment() {
  return Environment(
      SoundSpeedProfile(
          {SoundSpeedPoint{
               .depth = 0.0, .soundSpeed = kSoundSpeed, .density = 1000.0},
           SoundSpeedPoint{
               .depth = 1000.0, .soundSpeed = kSoundSpeed, .density = 1000.0}}),
      BoundaryModel::vacuum(0.0), BoundaryModel::rigid(1000.0));
}

RayPath makeSlantedPath(std::size_t pointCount, double dynamicP) {
  const std::vector<rayreuse::Vec2> positions{
      {0.0, 50.0}, {80.0, 110.0}, {240.0, 230.0}, {400.0, 350.0}};
  RayPath path;
  path.launchAngle = 0.0;
  path.points.reserve(pointCount);
  for (std::size_t index = 0U; index < pointCount; ++index) {
    path.points.push_back(
        RayState{.position = positions[index],
                 .slowness = {0.8 / kSoundSpeed, 0.6 / kSoundSpeed},
                 .dynamicP = {dynamicP, 0.0},
                 .dynamicQ = {0.0, 1.0},
                 .soundSpeed = kSoundSpeed,
                 .realTravelTime = 0.0});
  }
  return path;
}

RayFrequencyState makeFrequencyState(std::size_t pointCount, double frequency) {
  return RayFrequencyState{
      .frequency = frequency,
      .points = std::vector<RayFrequencyPoint>(
          pointCount, RayFrequencyPoint{.complexTravelTime = {},
                                        .amplitude = 1.0,
                                        .reflectionPhase = 0.0,
                                        .active = true})};
}

void checkComplexNear(Context& context, std::complex<double> actual,
                      std::complex<double> expected, double tolerance,
                      const std::string& message) {
  context.checkNear(actual.real(), expected.real(), tolerance,
                    message + " real");
  context.checkNear(actual.imag(), expected.imag(), tolerance,
                    message + " imaginary");
}

RayCenteredCervenyDiagnostic runComponentDiagnostic(Context& context,
                                                    FieldComponent component) {
  const Environment environment = makeEnvironment();
  const ReceiverGrid receivers({150.0}, {100.0, 200.0});
  const RayPath path = makeSlantedPath(3U, 1.0e-6);
  const RayFrequencyState frequencyState = makeFrequencyState(3U, 1.0);
  FrequencyWorkspace workspace(1.0, receivers);
  const RayCenteredCervenyInfluence influence(
      environment, receivers,
      CartesianCervenySettings{.imageCount = 1U, .beamWindow = 5},
      BeamWidthMode::MinimumWidth, SimulationRunMode::Coherent, component);
  const auto diagnostic = influence.accumulate(
      workspace, path, frequencyState, kEpsilon,
      RayCenteredCervenyDiagnosticRequest{.receiverRangeIndex = 1U,
                                          .receiverDepthIndex = 0U});
  context.check(diagnostic.has_value() && diagnostic->evaluated,
                "component diagnostic evaluates the selected receiver");
  return diagnostic.value();
}

void testPressureAndVelocityComponents(Context& context) {
  const RayCenteredCervenyDiagnostic pressure =
      runComponentDiagnostic(context, FieldComponent::Pressure);
  context.check(pressure.evaluationCount == 1U &&
                    pressure.leftPointIndex == 1U &&
                    pressure.rightPointIndex == 2U,
                "pressure diagnostic selects the expected ray chord");
  context.checkNear(pressure.interpolationWeight, 0.6, 1.0e-15,
                    "pressure diagnostic interpolation weight");
  context.checkNear(pressure.normalOffset, 40.0, 1.0e-13,
                    "pressure diagnostic normal coordinate");
  checkComplexNear(context, pressure.qInterpolated, {0.0, 1.0}, 0.0,
                   "pressure diagnostic q");
  checkComplexNear(context, pressure.gammaInterpolated, {0.0, -1.0e-6}, 1.0e-21,
                   "pressure diagnostic gamma=p/q");
  const double attenuation = std::exp(-std::numbers::pi * 1.0e-6 * 40.0 * 40.0);
  const std::complex<double> expectedPressure =
      std::sqrt(std::complex<double>{0.0, -kSoundSpeed}) * attenuation;
  checkComplexNear(context, pressure.pressureContribution, expectedPressure,
                   1.0e-13, "pressure diagnostic contribution");

  const RayCenteredCervenyDiagnostic vertical =
      runComponentDiagnostic(context, FieldComponent::Vertical);
  const RayCenteredCervenyDiagnostic horizontal =
      runComponentDiagnostic(context, FieldComponent::Horizontal);
  const double omega = 2.0 * std::numbers::pi;
  const std::complex<double> normalDerivative =
      std::complex<double>{0.0, -1.0} * omega * pressure.gammaInterpolated *
      pressure.normalOffset * pressure.pressureContribution;
  const std::complex<double> alongDerivative = std::complex<double>{0.0, -1.0} *
                                               omega / kSoundSpeed *
                                               pressure.pressureContribution;
  const std::complex<double> expectedVertical =
      kSoundSpeed * (std::conj(normalDerivative) * (0.8 / kSoundSpeed) +
                     std::conj(alongDerivative) * (0.6 / kSoundSpeed));
  const std::complex<double> expectedHorizontal =
      kSoundSpeed * (-normalDerivative * (0.6 / kSoundSpeed) +
                     alongDerivative * (0.8 / kSoundSpeed));
  checkComplexNear(context, vertical.pressureContribution, expectedVertical,
                   1.0e-13,
                   "V conjugates both derivatives like Fortran DOT_PRODUCT");
  checkComplexNear(context, horizontal.pressureContribution, expectedHorizontal,
                   1.0e-13,
                   "H preserves the handwritten non-conjugated expression");
}

std::vector<double> imageRanges() {
  std::vector<double> ranges;
  for (std::size_t index = 0U; index <= 10U; ++index) {
    ranges.push_back(100.0 * static_cast<double>(index));
  }
  return ranges;
}

std::complex<double> runCoherentImages(std::size_t imageCount) {
  const Environment environment = makeEnvironment();
  const ReceiverGrid receivers({100.0}, imageRanges());
  const RayPath path = makeSlantedPath(4U, 0.0);
  const RayFrequencyState frequencyState = makeFrequencyState(4U, 1.0);
  FrequencyWorkspace workspace(1.0, receivers);
  const RayCenteredCervenyInfluence influence(
      environment, receivers,
      CartesianCervenySettings{.imageCount = imageCount, .beamWindow = 5});
  static_cast<void>(
      influence.accumulate(workspace, path, frequencyState, kEpsilon));
  return workspace.at(0U, 1U);
}

double runIntensityImages(std::size_t imageCount, SimulationRunMode runMode) {
  const Environment environment = makeEnvironment();
  const ReceiverGrid receivers({100.0}, imageRanges());
  const RayPath path = makeSlantedPath(4U, 0.0);
  const RayFrequencyState frequencyState = makeFrequencyState(4U, 1.0);
  IntensityWorkspace workspace(1.0, receivers);
  const RayCenteredCervenyInfluence influence(
      environment, receivers,
      CartesianCervenySettings{.imageCount = imageCount, .beamWindow = 5},
      BeamWidthMode::MinimumWidth, runMode);
  static_cast<void>(
      influence.accumulateIntensity(workspace, path, frequencyState, kEpsilon));
  return workspace.at(0U, 1U);
}

void testLegacyImageNormalFlipsAndPerImagePower(Context& context) {
  const std::complex<double> oneImage = runCoherentImages(1U);
  const std::complex<double> twoImages = runCoherentImages(2U);
  const std::complex<double> threeImages = runCoherentImages(3U);
  const std::complex<double> base =
      std::sqrt(std::complex<double>{0.0, -kSoundSpeed});
  checkComplexNear(context, oneImage, base, 1.0e-13,
                   "true image reaches the selected receiver");
  checkComplexNear(context, twoImages - oneImage, -base, 1.0e-13,
                   "surface rnV flips on every step");
  checkComplexNear(context, threeImages - twoImages, base, 1.0e-13,
                   "bottom inherits and continues vector-wide flips");

  const double basePower = std::abs(base) * std::abs(base);
  const double incoherentOne =
      runIntensityImages(1U, SimulationRunMode::Incoherent);
  const double incoherentTwo =
      runIntensityImages(2U, SimulationRunMode::Incoherent);
  const double semiCoherentTwo =
      runIntensityImages(2U, SimulationRunMode::SemiCoherent);
  context.checkNear(incoherentOne, basePower, 1.0e-12,
                    "I accumulates true-image abs squared");
  context.checkNear(incoherentTwo, 2.0 * basePower, 2.0e-12,
                    "I forms abs squared separately for every image");
  context.checkNear(semiCoherentTwo, incoherentTwo, 0.0,
                    "S uses the same per-image power law as I");
  context.checkNear(std::abs(twoImages) * std::abs(twoImages), 0.0, 1.0e-24,
                    "coherent surface polarity cancels the true image");
}

RayPath makeNearHorizontalSkipPath() {
  RayPath path = makeSlantedPath(4U, 0.0);
  path.points[2U].slowness = {0.0, 1.0 / kSoundSpeed};
  return path;
}

std::complex<double> runNearHorizontalImages(std::size_t imageCount) {
  const Environment environment = makeEnvironment();
  const ReceiverGrid receivers({100.0}, imageRanges());
  const RayPath path = makeNearHorizontalSkipPath();
  const RayFrequencyState frequencyState = makeFrequencyState(4U, 1.0);
  FrequencyWorkspace workspace(1.0, receivers);
  const RayCenteredCervenyInfluence influence(
      environment, receivers,
      CartesianCervenySettings{.imageCount = imageCount, .beamWindow = 5});
  static_cast<void>(
      influence.accumulate(workspace, path, frequencyState, kEpsilon));
  return workspace.at(0U, 3U);
}

void testNearHorizontalStepDoesNotFlipImageNormals(Context& context) {
  const std::complex<double> trueImage = runNearHorizontalImages(1U);
  const std::complex<double> withSurface = runNearHorizontalImages(2U);
  context.check(trueImage != std::complex<double>{},
                "near-horizontal fixture retains a true-image baseline");
  checkComplexNear(context, withSurface - trueImage, {}, 1.0e-13,
                   "zn near zero cycles before the surface rnV vector flip");
}

RayPath makeWkbCrossingPath() {
  RayPath path;
  path.launchAngle = 0.0;
  for (const auto& [range, dynamicQ] : std::vector<std::pair<double, double>>{
           {0.0, 1.0}, {100.0, 1.0}, {300.0, -1.0}}) {
    path.points.push_back(RayState{.position = {range, 50.0},
                                   .slowness = {1.0 / kSoundSpeed, 0.0},
                                   .dynamicP = {0.0, 0.0},
                                   .dynamicQ = {dynamicQ, 0.0},
                                   .soundSpeed = kSoundSpeed,
                                   .realTravelTime = 0.0});
  }
  return path;
}

RayCenteredCervenyDiagnostic runWkbCrossingDiagnostic(Context& context,
                                                      double receiverRange) {
  const Environment environment = makeEnvironment();
  const ReceiverGrid receivers({50.0}, {0.0, receiverRange});
  const RayPath path = makeWkbCrossingPath();
  const RayFrequencyState frequencyState = makeFrequencyState(3U, 1.0);
  FrequencyWorkspace workspace(1.0, receivers);
  const RayCenteredCervenyInfluence influence(
      environment, receivers,
      CartesianCervenySettings{.imageCount = 1U, .beamWindow = 5},
      BeamWidthMode::Wkb);
  const auto diagnostic = influence.accumulate(
      workspace, path, frequencyState, {1.0, 0.0},
      RayCenteredCervenyDiagnosticRequest{.receiverRangeIndex = 1U,
                                          .receiverDepthIndex = 0U});
  context.check(diagnostic.has_value() && diagnostic->evaluated,
                "WKB crossing diagnostic evaluates the receiver chord");
  return diagnostic.value();
}

void testWkbReceiverInterpolationUpdatesKmah(Context& context) {
  const RayCenteredCervenyDiagnostic before =
      runWkbCrossingDiagnostic(context, 150.0);
  const RayCenteredCervenyDiagnostic after =
      runWkbCrossingDiagnostic(context, 250.0);
  context.checkNear(before.interpolationWeight, 0.25, 0.0,
                    "WKB pre-crossing receiver interpolation weight");
  context.checkNear(after.interpolationWeight, 0.75, 0.0,
                    "WKB post-crossing receiver interpolation weight");
  context.check(before.kmahFinal == 1 && after.kmahFinal == -1,
                "WKB KMAH flips at the receiver-interpolated q zero");

  const Environment environment = makeEnvironment();
  const ReceiverGrid receivers({50.0}, {0.0, 250.0});
  const RayPath path = makeWkbCrossingPath();
  const RayFrequencyState frequencyState = makeFrequencyState(3U, 1.0);
  context.expectThrows<ValidationError>(
      [&] {
        FrequencyWorkspace workspace(1.0, receivers);
        const RayCenteredCervenyInfluence minimumWidth(
            environment, receivers, {}, BeamWidthMode::MinimumWidth);
        static_cast<void>(minimumWidth.accumulate(workspace, path,
                                                  frequencyState, {1.0, 0.0}));
      },
      "F/M ray-centered epsilon remains positive imaginary only");
  context.expectThrows<ValidationError>(
      [&] {
        FrequencyWorkspace workspace(1.0, receivers);
        const RayCenteredCervenyInfluence wkb(environment, receivers, {},
                                              BeamWidthMode::Wkb);
        static_cast<void>(
            wkb.accumulate(workspace, path, frequencyState, {0.0, 1.0}));
      },
      "WKB ray-centered epsilon remains real only");
}

void testHermiteAppliedOnceToIntensity(Context& context) {
  const Environment environment = makeEnvironment();
  const ReceiverGrid receivers({150.0}, {100.0, 200.0});
  const RayPath path = makeSlantedPath(3U, 0.0);
  const RayFrequencyState frequencyState = makeFrequencyState(3U, 1500.0);
  IntensityWorkspace workspace(1500.0, receivers);
  const RayCenteredCervenyInfluence influence(
      environment, receivers,
      CartesianCervenySettings{.imageCount = 1U, .beamWindow = 5},
      BeamWidthMode::MinimumWidth, SimulationRunMode::Incoherent);
  const auto diagnostic = influence.accumulateIntensity(
      workspace, path, frequencyState, kEpsilon,
      RayCenteredCervenyDiagnosticRequest{.receiverRangeIndex = 1U,
                                          .receiverDepthIndex = 0U});
  context.check(diagnostic.has_value() && diagnostic->evaluated,
                "Hermite intensity diagnostic is evaluated");
  const double expectedTaper = cervenyHermiteTaper(40.0, 30.0, 60.0);
  const double magnitude = std::abs(diagnostic->pressureContribution);
  const double expected = expectedTaper * magnitude * magnitude;
  context.checkNear(diagnostic->hermiteTaper, expectedTaper, 1.0e-15,
                    "Hermite fixture has a nontrivial taper");
  context.checkNear(diagnostic->intensityIncrement, expected, 1.0e-12,
                    "intensity multiplies abs squared by Hermite once");
  context.checkNear(workspace.at(0U, 1U), expected, 1.0e-12,
                    "workspace receives the once-tapered intensity");
}

void testModeAndGridContracts(Context& context) {
  const Environment environment = makeEnvironment();
  const ReceiverGrid receivers({150.0}, {100.0, 200.0});
  const RayPath path = makeSlantedPath(3U, 0.0);
  const RayFrequencyState frequencyState = makeFrequencyState(3U, 1.0);
  const RayCenteredCervenyInfluence coherent(environment, receivers);
  const RayCenteredCervenyInfluence incoherent(environment, receivers, {},
                                               BeamWidthMode::MinimumWidth,
                                               SimulationRunMode::Incoherent);
  context.expectThrows<ValidationError>(
      [&] {
        IntensityWorkspace workspace(1.0, receivers);
        static_cast<void>(coherent.accumulateIntensity(
            workspace, path, frequencyState, kEpsilon));
      },
      "coherent ray-centered influence rejects intensity workspace");
  context.expectThrows<ValidationError>(
      [&] {
        FrequencyWorkspace workspace(1.0, receivers);
        static_cast<void>(
            incoherent.accumulate(workspace, path, frequencyState, kEpsilon));
      },
      "incoherent ray-centered influence rejects pressure workspace");
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(RayCenteredCervenyInfluence(
            environment, ReceiverGrid({100.0}, {100.0})));
      },
      "ray-centered influence requires at least two ranges");
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(RayCenteredCervenyInfluence(
            environment, ReceiverGrid({100.0}, {100.0, 210.0, 300.0})));
      },
      "ray-centered influence requires equally spaced ranges");
}

}  // namespace

int main() {
  Context context;
  testPressureAndVelocityComponents(context);
  testLegacyImageNormalFlipsAndPerImagePower(context);
  testNearHorizontalStepDoesNotFlipImageNormals(context);
  testWkbReceiverInterpolationUpdatesKmah(context);
  testHermiteAppliedOnceToIntensity(context);
  testModeAndGridContracts(context);
  if (context.failureCount() != 0) {
    std::cerr << context.failureCount()
              << " ray-centered Cerveny assertion(s) failed\n";
    return 1;
  }
  std::cout << "All ray-centered Cerveny influence tests passed\n";
  return 0;
}
