#include <cmath>
#include <complex>
#include <cstddef>
#include <iostream>
#include <limits>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

#include "bellhop/error.hpp"
#include "bellhop/field/geometric_gaussian_influence.hpp"
#include "support/test_harness.hpp"

namespace {

using bellhop::ArrivalWorkspace;
using bellhop::FrequencyWorkspace;
using bellhop::GeometricGaussianDiagnostic;
using bellhop::GeometricGaussianDiagnosticRequest;
using bellhop::GeometricGaussianInfluence;
using bellhop::GeometricGaussianWidthBranch;
using bellhop::IntensityWorkspace;
using bellhop::RayFrequencyPoint;
using bellhop::RayFrequencyState;
using bellhop::RayPath;
using bellhop::RayState;
using bellhop::ReceiverGrid;
using bellhop::ReceiverGridLayout;
using bellhop::ReflectionBoundary;
using bellhop::ReflectionEvent;
using bellhop::SimulationRunMode;
using bellhop::SourceGeometry;
using bellhop::ValidationError;
using bellhop::test::Context;

constexpr double kSoundSpeed = 1500.0;
constexpr double kFrequency = 50.0;
constexpr double kDalpha = 0.1;

RayPath makeHorizontalPath(const std::vector<double>& ranges,
                           const std::vector<double>& q,
                           double launchAngle = 0.0,
                           bool leftGoing = false) {
  RayPath path;
  path.launchAngle = launchAngle;
  for (std::size_t index = 0U; index < ranges.size(); ++index) {
    path.points.push_back(
        RayState{.position = {.range = ranges[index], .depth = 500.0},
                 .slowness = {.range = leftGoing ? -1.0 / kSoundSpeed
                                                  : 1.0 / kSoundSpeed,
                              .depth = 0.0},
                 .dynamicP = {},
                 .dynamicQ = {q[index], 0.0},
                 .soundSpeed = kSoundSpeed,
                 .realTravelTime = 0.0});
  }
  return path;
}

RayFrequencyState makeFrequencyState(
    std::size_t pointCount, double rightRealDelay,
    double imaginaryDelay = 0.0) {
  RayFrequencyState result{
      .frequency = kFrequency,
      .points = {},
  };
  for (std::size_t index = 0U; index < pointCount; ++index) {
    const double weight = pointCount == 1U
                              ? 0.0
                              : static_cast<double>(index) /
                                    static_cast<double>(pointCount - 1U);
    result.points.push_back(
        RayFrequencyPoint{
            .complexTravelTime =
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
    Context& context, double leftQ, double rightQ,
    double rightRealDelay, double receiverDepth = 500.0) {
  const ReceiverGrid receivers({receiverDepth}, {100.0, 300.0});
  const RayPath path =
      makeHorizontalPath({0.0, 200.0}, {leftQ, rightQ});
  const RayFrequencyState frequencyState =
      makeFrequencyState(2U, rightRealDelay);
  FrequencyWorkspace workspace(kFrequency, receivers);
  const GeometricGaussianInfluence influence(
      receivers, SourceGeometry::Line);
  const auto diagnostic = influence.accumulate(
      workspace, path, frequencyState, kDalpha,
      GeometricGaussianDiagnosticRequest{.receiverRangeIndex = 0U,
                                         .receiverDepthIndex = 0U});
  context.check(diagnostic.has_value() && diagnostic->evaluated,
                "B evaluates width-branch receiver");
  return diagnostic.value();
}

void testSigmaBranchesAndOriginAnchor(Context& context) {
  const GeometricGaussianDiagnostic geometric =
      runWidthDiagnostic(context, 200.0, 400.0, 0.001);
  context.check(geometric.widthBranch ==
                    GeometricGaussianWidthBranch::Geometric,
                "B selects the geometric sigma branch");
  context.checkNear(geometric.geometricSigma, 0.02, 1.0e-18,
                    "B geometric sigma");
  context.checkNear(geometric.nearFieldSigma,
                    0.010000000149011612, 0.0,
                    "B promotes the REAL4 0.2 literal before multiplying");
  context.checkNear(geometric.wavelengthSigma,
                    94.24777960769379, 2.0e-14,
                    "B pi-lambda candidate");
  context.checkNear(geometric.sigma1, 0.02, 1.0e-18,
                    "B geometric sigma1");
  context.checkNear(geometric.gaussianWeight, 1.0, 0.0,
                    "B centerline geometric weight");
  checkComplexNear(context, geometric.pressureIncrement,
                   {0.8810792938493487, -0.13954925083786673},
                   2.0e-15, "B coherent Origin numerical anchor");
  const GeometricGaussianDiagnostic offset =
      runWidthDiagnostic(context, 200.0, 400.0, 0.001, 500.01);
  context.checkNear(offset.normalOffset, 0.01, 1.0e-14,
                    "B resolves Cartesian normal offset");
  context.checkNear(offset.gaussianWeight, std::exp(-0.125),
                    5.0e-13,
                    "B applies exp(-0.5*(n/sigma1)^2)");

  const GeometricGaussianDiagnostic nearField =
      runWidthDiagnostic(context, 100.0, 200.0, 0.002);
  context.check(nearField.widthBranch ==
                    GeometricGaussianWidthBranch::NearField,
                "B selects the 0.2*f*tau branch");
  context.checkNear(nearField.geometricSigma, 0.01, 1.0e-18,
                    "B near-field fixture geometric sigma");
  context.checkNear(nearField.nearFieldSigma,
                    0.020000000298023225, 0.0,
                    "B near-field sigma");
  context.checkNear(nearField.sigma1,
                    0.020000000298023225, 0.0,
                    "B near-field sigma1");
  context.checkNear(nearField.gaussianWeight,
                    0.70710677591819149, 0.0,
                    "B uses sqrt(sigma/sigma1) widening weight");

  const GeometricGaussianDiagnostic wavelength =
      runWidthDiagnostic(context, 100.0, 200.0, 20.0);
  context.check(wavelength.widthBranch ==
                    GeometricGaussianWidthBranch::WavelengthCap,
                "B selects the pi-lambda cap branch");
  context.checkNear(wavelength.nearFieldSigma,
                    200.00000298023224, 0.0,
                    "B uncapped near-field candidate");
  context.checkNear(wavelength.sigma1, wavelength.wavelengthSigma, 0.0,
                    "B caps widening at pi lambda");
  context.checkNear(
      wavelength.gaussianWeight,
      std::sqrt(wavelength.geometricSigma / wavelength.sigma1),
      0.0, "B wavelength branch keeps sqrt sigma ratio");
}

void testIntensityAndSourceGeometry(Context& context) {
  const ReceiverGrid receivers({500.0}, {100.0, 300.0});
  RayPath path = makeHorizontalPath(
      {0.0, 200.0}, {100.0, 200.0}, std::numbers::pi / 3.0);
  const RayFrequencyState frequencyState =
      makeFrequencyState(2U, 0.002, -1.0e-4);
  IntensityWorkspace incoherentWorkspace(kFrequency, receivers);
  const GeometricGaussianInfluence incoherent(
      receivers, SourceGeometry::Line,
      SimulationRunMode::IncoherentTransmissionLoss);
  const auto diagnostic = incoherent.accumulateIntensity(
      incoherentWorkspace, path, frequencyState, kDalpha,
      GeometricGaussianDiagnosticRequest{0U, 0U});
  context.check(diagnostic.has_value() && diagnostic->evaluated,
                "IB evaluates the selected receiver");
  context.checkNear(diagnostic->intensityIncrement,
                    1.324577993883975, 0.0,
                    "IB uses sqrt(2pi)*const^2*exp(2w Im tau)*W");
  context.checkNear(incoherentWorkspace.at(0U, 0U),
                    diagnostic->intensityIncrement, 0.0,
                    "IB stores the Origin intensity increment");
  context.check(
      std::abs(diagnostic->intensityIncrement - 0.9366181026208776) >
          0.3,
      "IB fixture distinguishes W from W squared");

  IntensityWorkspace semiWorkspace(kFrequency, receivers);
  const GeometricGaussianInfluence semi(
      receivers, SourceGeometry::Line,
      SimulationRunMode::SemiCoherentTransmissionLoss);
  static_cast<void>(semi.accumulateIntensity(
      semiWorkspace, path, frequencyState, kDalpha));
  context.checkNear(semiWorkspace.at(0U, 0U),
                    incoherentWorkspace.at(0U, 0U), 0.0,
                    "SB uses the same per-ray intensity law as IB");

  FrequencyWorkspace pointWorkspace(kFrequency, receivers);
  FrequencyWorkspace lineWorkspace(kFrequency, receivers);
  const GeometricGaussianInfluence point(receivers,
                                         SourceGeometry::Point);
  const GeometricGaussianInfluence line(receivers,
                                        SourceGeometry::Line);
  const auto pointDiagnostic = point.accumulate(
      pointWorkspace, path, frequencyState, kDalpha,
      GeometricGaussianDiagnosticRequest{0U, 0U});
  const auto lineDiagnostic = line.accumulate(
      lineWorkspace, path, frequencyState, kDalpha,
      GeometricGaussianDiagnosticRequest{0U, 0U});
  context.checkNear(pointDiagnostic->amplitudeConstant /
                        lineDiagnostic->amplitudeConstant,
                    std::sqrt(0.5), 1.0e-15,
                    "point B adds sqrt(abs(cos(alpha))) to common 2pi ratio");
}

void testGridWalkerCausticAndFrozenInput(Context& context) {
  const ReceiverGrid receivers({500.0}, {150.0, 300.0});
  const RayPath path =
      makeHorizontalPath({0.0, 200.0}, {1.0, -1.0});
  RayFrequencyState frequencyState = makeFrequencyState(2U, 0.002);
  FrequencyWorkspace workspace(kFrequency, receivers);
  const GeometricGaussianInfluence influence(
      receivers, SourceGeometry::Line);
  const auto diagnostic = influence.accumulate(
      workspace, path, frequencyState, kDalpha,
      GeometricGaussianDiagnosticRequest{0U, 0U});
  context.check(diagnostic.has_value() && diagnostic->evaluated,
                "B evaluates across a real-q caustic");
  context.checkNear(diagnostic->qInterpolated, -0.5, 0.0,
                    "B caustic fixture has negative interpolated q");
  context.checkNear(diagnostic->causticPhase,
                    std::numbers::pi / 2.0, 0.0,
                    "B adds the receiver-side caustic phase");
  context.check(path.points[1U].dynamicQ[0U] == -1.0 &&
                    frequencyState.points[1U].amplitude == 1.0,
                "B leaves geometry and frequency caches frozen");

  const ReceiverGrid irregular({500.0, 500.001}, {150.0, 300.0},
                               ReceiverGridLayout::Irregular);
  FrequencyWorkspace irregularWorkspace(kFrequency, irregular);
  const GeometricGaussianInfluence irregularInfluence(
      irregular, SourceGeometry::Line);
  static_cast<void>(irregularInfluence.accumulate(
      irregularWorkspace, path, frequencyState, kDalpha));
  checkComplexNear(context, irregularWorkspace.at(0U, 0U),
                   workspace.at(0U, 0U), 0.0,
                   "B supports one irregular depth per receiver range");

  const ReceiverGrid reverseReceivers({500.0},
                                      {100.0, 300.0, 500.0});
  const RayPath reversePath = makeHorizontalPath(
      {600.0, 400.0, 200.0}, {300.0, 200.0, 100.0}, 0.0, true);
  const RayFrequencyState reverseFrequency =
      makeFrequencyState(3U, 0.003);
  FrequencyWorkspace reverseWorkspace(kFrequency, reverseReceivers);
  const GeometricGaussianInfluence reverse(
      reverseReceivers, SourceGeometry::Line);
  static_cast<void>(reverse.accumulate(
      reverseWorkspace, reversePath, reverseFrequency, kDalpha));
  context.check(std::abs(reverseWorkspace.at(0U, 2U)) > 0.0 &&
                    std::abs(reverseWorkspace.at(0U, 1U)) > 0.0,
                "B walks left-going receiver segments safely");
}

void testPrefixDuplicateAndContracts(Context& context) {
  const ReceiverGrid receivers({500.0}, {100.0, 300.0, 500.0});
  RayPath path = makeHorizontalPath(
      {0.0, 200.0, 200.0, 400.0, 600.0},
      {1.0, 100.0, 100.0, 200.0, 300.0});
  RayFrequencyState frequencyState = makeFrequencyState(5U, 0.005);
  frequencyState.points[2U].reflectionPhase = 0.25;
  frequencyState.points[3U].active = false;
  frequencyState.points[4U].active = false;
  FrequencyWorkspace workspace(kFrequency, receivers);
  const GeometricGaussianInfluence coherent(receivers);
  const auto diagnostic = coherent.accumulate(
      workspace, path, frequencyState, kDalpha,
      GeometricGaussianDiagnosticRequest{1U, 0U});
  context.check(diagnostic.has_value() &&
                    diagnostic->evaluationCount == 1U &&
                    diagnostic->leftPointIndex == 2U,
                "B skips reflection duplicates and resumes once");
  context.checkNear(diagnostic->causticPhase, 0.25, 0.0,
                    "B takes reflection phase from the duplicate right state");
  context.check(workspace.at(0U, 2U) == std::complex<double>{},
                "B includes the inactive terminal but not its suffix");

  path.events.push_back(
      ReflectionEvent{.rayPointIndex = 1U,
                      .reflectedRayPointIndex = 2U,
                      .boundary = ReflectionBoundary::Seabed,
                      .boundarySegmentIndex = 0U,
                      .boundaryCurvature = 0.0,
                      .position = {},
                      .boundaryTangent = {},
                      .outwardNormal = {},
                      .incidentSlowness = {},
                      .reflectedSlowness = {},
                      .tangentSlowness = 0.0,
                      .normalSlowness = 0.0,
                      .longMaterialOverride = std::nullopt});
  ArrivalWorkspace arrivalWorkspace(kFrequency, receivers);
  const GeometricGaussianInfluence arrivals(
      receivers, SourceGeometry::Point,
      SimulationRunMode::AsciiArrivals);
  static_cast<void>(arrivals.accumulateArrivals(
      arrivalWorkspace, path, frequencyState, kDalpha));
  context.check(
      !arrivalWorkspace.arrivalsAt(0U, 1U).empty() &&
          arrivalWorkspace.arrivalsAt(0U, 1U).front().bottomBounceCount == 1,
      "B arrivals use reflected endpoint prefix bounce counts");

  const GeometricGaussianInfluence incoherent(
      receivers, SourceGeometry::Point,
      SimulationRunMode::IncoherentTransmissionLoss);
  context.expectThrows<ValidationError>(
      [&] {
        IntensityWorkspace wrong(kFrequency, receivers);
        static_cast<void>(coherent.accumulateIntensity(
            wrong, path, frequencyState, kDalpha));
      },
      "CB cannot write intensity");
  context.expectThrows<ValidationError>(
      [&] {
        FrequencyWorkspace wrong(kFrequency, receivers);
        static_cast<void>(incoherent.accumulate(
            wrong, path, frequencyState, kDalpha));
      },
      "IB cannot write complex pressure");
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(GeometricGaussianInfluence(
            receivers, SourceGeometry::Point,
            SimulationRunMode::RayTrace));
      },
      "ray-trace mode cannot construct B influence");
  context.expectThrows<ValidationError>(
      [&] {
        RayFrequencyState invalid = frequencyState;
        invalid.points[1U].complexTravelTime =
            {0.0, std::numeric_limits<double>::quiet_NaN()};
        FrequencyWorkspace target(kFrequency, receivers);
        static_cast<void>(coherent.accumulate(
            target, path, invalid, kDalpha));
      },
      "B rejects non-finite frequency state before accumulation");
}

void testArrivalSinkWidthBranchesAndFields(Context& context) {
  const auto runArrival = [&](double leftQ, double rightQ,
                              double rightDelay) {
    const ReceiverGrid receivers({500.0}, {100.0, 300.0});
    const RayPath path = makeHorizontalPath(
        {0.0, 200.0}, {leftQ, rightQ}, std::numbers::pi / 3.0);
    const RayFrequencyState frequencyState =
        makeFrequencyState(2U, rightDelay, -1.0e-5);
    ArrivalWorkspace workspace(kFrequency, receivers);
    const GeometricGaussianInfluence influence(
        receivers, SourceGeometry::Point,
        SimulationRunMode::AsciiArrivals);
    const auto diagnostic = influence.accumulateArrivals(
        workspace, path, frequencyState, kDalpha,
        GeometricGaussianDiagnosticRequest{0U, 0U});
    context.check(diagnostic.has_value() && diagnostic->evaluated &&
                      workspace.arrivalCountAt(0U, 0U) == 1U,
                  "B arrival sink shares the accepted receiver traversal");
    const bellhop::Arrival arrival = workspace.arrivalsAt(0U, 0U).front();
    context.checkNear(
        arrival.amplitude,
        static_cast<float>(diagnostic->amplitudeConstant *
                           diagnostic->gaussianWeight),
        0.0, "B arrival stores the family-specific amplitude and weight");
    context.checkNear(arrival.phaseRadians,
                      static_cast<float>(diagnostic->causticPhase), 0.0,
                      "B arrival stores unwrapped phase");
    context.checkNear(arrival.delaySeconds.real(),
                      static_cast<float>(diagnostic->delay.real()), 0.0,
                      "B arrival stores interpolated delay");
    context.checkNear(arrival.sourceDeclinationDegrees, 60.0F, 4.0e-6,
                      "B arrival stores source declination");
    context.checkNear(arrival.receiverDeclinationDegrees, 0.0F, 0.0,
                      "B arrival stores segment declination");
    return diagnostic->widthBranch;
  };

  context.check(runArrival(200.0, 400.0, 0.001) ==
                    GeometricGaussianWidthBranch::Geometric,
                "B arrivals preserve the geometric-width branch");
  context.check(runArrival(100.0, 200.0, 0.002) ==
                    GeometricGaussianWidthBranch::NearField,
                "B arrivals preserve the near-field branch");
  context.check(runArrival(100.0, 200.0, 20.0) ==
                    GeometricGaussianWidthBranch::WavelengthCap,
                "B arrivals preserve the wavelength-cap branch");

  const ReceiverGrid irregular(
      {500.0, 500.0001}, {100.0, 300.0},
      ReceiverGridLayout::Irregular);
  const RayPath path = makeHorizontalPath(
      {0.0, 200.0, 400.0}, {1.0, -1.0, -2.0});
  RayFrequencyState frequencyState = makeFrequencyState(3U, 0.003);
  ArrivalWorkspace workspace(kFrequency, irregular);
  const GeometricGaussianInfluence influence(
      irregular, SourceGeometry::Line,
      SimulationRunMode::BinaryArrivals);
  static_cast<void>(influence.accumulateArrivals(
      workspace, path, frequencyState, kDalpha));
  context.check(workspace.candidateCount() > 0U,
                "B arrivals retain irregular Cartesian support and caustics");
}

}  // namespace

int main() {
  Context context;
  testSigmaBranchesAndOriginAnchor(context);
  testIntensityAndSourceGeometry(context);
  testGridWalkerCausticAndFrozenInput(context);
  testPrefixDuplicateAndContracts(context);
  testArrivalSinkWidthBranchesAndFields(context);
  if (context.failureCount() != 0) {
    std::cerr << context.failureCount()
              << " geometric Gaussian assertion(s) failed\n";
    return 1;
  }
  std::cout << "All geometric Gaussian influence tests passed\n";
  return 0;
}
