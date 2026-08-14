#include <cmath>
#include <complex>
#include <cstddef>
#include <iostream>
#include <limits>
#include <numbers>
#include <string>
#include <vector>

#include "bellhop/error.hpp"
#include "bellhop/field/geometric_hat_influence.hpp"
#include "support/test_harness.hpp"

namespace {

using bellhop::CervenyCoordinateSystem;
using bellhop::ArrivalWorkspace;
using bellhop::FrequencyWorkspace;
using bellhop::FieldAccumulationKind;
using bellhop::GeometricHatDiagnosticRequest;
using bellhop::GeometricHatInfluence;
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
using bellhop::fieldAccumulationKind;
using bellhop::isArrivalMode;
using bellhop::isTransmissionLossMode;
using bellhop::usesLloydMirror;
using bellhop::test::Context;

constexpr double kSoundSpeed = 1500.0;
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

RayFrequencyState makeFrequencyState(const std::vector<double>& ranges,
                                     double frequency,
                                     double imaginaryDelay = 0.0) {
  RayFrequencyState result;
  result.frequency = frequency;
  for (double range : ranges) {
    result.points.push_back(
        RayFrequencyPoint{
            .complexTravelTime = {range / kSoundSpeed, imaginaryDelay},
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

void checkPathUnchanged(Context& context, const RayPath& actual,
                        const RayPath& expected,
                        const std::string& message) {
  context.check(actual.points.size() == expected.points.size(),
                message + " point count");
  for (std::size_t index = 0U; index < actual.points.size(); ++index) {
    const RayState& lhs = actual.points[index];
    const RayState& rhs = expected.points[index];
    context.check(lhs.position.range == rhs.position.range &&
                      lhs.position.depth == rhs.position.depth &&
                      lhs.slowness.range == rhs.slowness.range &&
                      lhs.slowness.depth == rhs.slowness.depth &&
                      lhs.dynamicP == rhs.dynamicP &&
                      lhs.dynamicQ == rhs.dynamicQ &&
                      lhs.soundSpeed == rhs.soundSpeed &&
                      lhs.realTravelTime == rhs.realTravelTime,
                  message + " point " + std::to_string(index));
  }
}

void checkFrequencyStateUnchanged(Context& context,
                                  const RayFrequencyState& actual,
                                  const RayFrequencyState& expected,
                                  const std::string& message) {
  context.check(actual.frequency == expected.frequency &&
                    actual.points.size() == expected.points.size(),
                message + " metadata");
  for (std::size_t index = 0U; index < actual.points.size(); ++index) {
    const RayFrequencyPoint& lhs = actual.points[index];
    const RayFrequencyPoint& rhs = expected.points[index];
    context.check(lhs.complexTravelTime == rhs.complexTravelTime &&
                      lhs.amplitude == rhs.amplitude &&
                      lhs.reflectionPhase == rhs.reflectionPhase &&
                      lhs.active == rhs.active,
                  message + " point " + std::to_string(index));
  }
}

void testCartesianOriginAnchorAndWalkers(Context& context) {
  const std::vector<double> rayRanges{0.0, 200.0, 400.0, 600.0};
  const ReceiverGrid receivers({500.0}, {100.0, 300.0, 500.0});
  const RayPath path =
      makeHorizontalPath(rayRanges, {0.0, 100.0, 200.0, 300.0});
  const RayFrequencyState frequencyState =
      makeFrequencyState(rayRanges, 50.0);
  FrequencyWorkspace workspace(50.0, receivers);
  const GeometricHatInfluence influence(
      receivers, CervenyCoordinateSystem::Cartesian);
  const auto diagnostic = influence.accumulate(
      workspace, path, frequencyState, kDalpha,
      GeometricHatDiagnosticRequest{.receiverRangeIndex = 0U,
                                    .receiverDepthIndex = 0U});

  context.check(diagnostic.has_value() && diagnostic->evaluated,
                "G evaluates the selected receiver");
  context.check(diagnostic->evaluationCount == 1U &&
                    diagnostic->leftPointIndex == 0U &&
                    diagnostic->rightPointIndex == 1U,
                "G selects the Origin half-open segment");
  context.checkNear(diagnostic->interpolationWeight, 0.5, 0.0,
                    "G segment interpolation weight");
  context.checkNear(diagnostic->qInterpolated, 50.0, 0.0,
                    "G interpolated real q");
  context.checkNear(diagnostic->hatWeight, 1.0, 0.0,
                    "G centerline hat weight");
  context.checkNear(diagnostic->amplitudeConstant, std::sqrt(30.0),
                    1.0e-15, "G Origin amplitude constant");
  checkComplexNear(context, workspace.at(0U, 0U),
                   {-2.7386127875258324, -4.743416490252568},
                   2.0e-14, "G 50 Hz Origin numerical anchor");

  // Origin's ray-centered index walker intentionally does not visit the
  // first receiver when both initial projected normals quantize to index 1.
  FrequencyWorkspace rayCenteredWorkspace(50.0, receivers);
  const GeometricHatInfluence rayCentered(
      receivers, CervenyCoordinateSystem::RayCentered);
  static_cast<void>(rayCentered.accumulate(
      rayCenteredWorkspace, path, frequencyState, kDalpha));
  context.check(rayCenteredWorkspace.at(0U, 0U) ==
                    std::complex<double>{},
                "g preserves the Origin initial same-index skip");

  const ReceiverGrid irregular({500.0, 500.0001, 500.0002},
                               {100.0, 300.0, 500.0},
                               ReceiverGridLayout::Irregular);
  FrequencyWorkspace irregularWorkspace(50.0, irregular);
  const GeometricHatInfluence irregularCartesian(
      irregular, CervenyCoordinateSystem::Cartesian);
  static_cast<void>(irregularCartesian.accumulate(
      irregularWorkspace, path, frequencyState, kDalpha));
  checkComplexNear(context, irregularWorkspace.at(0U, 0U),
                   workspace.at(0U, 0U), 0.0,
                   "G accepts irregular depths with one receiver per range");
}

void testIntensityWeightAndSourceGeometry(Context& context) {
  const std::vector<double> rayRanges{0.0, 200.0};
  const double beamRadius = 50.0 / (kSoundSpeed / kDalpha);
  const ReceiverGrid receivers({500.0 + beamRadius / 2.0},
                               {100.0, 300.0});
  RayPath path = makeHorizontalPath(
      rayRanges, {0.0, 100.0}, std::numbers::pi / 3.0);
  const RayFrequencyState frequencyState =
      makeFrequencyState(rayRanges, 10.0, -1.0e-4);
  IntensityWorkspace incoherentWorkspace(10.0, receivers);
  const GeometricHatInfluence incoherent(
      receivers, CervenyCoordinateSystem::Cartesian,
      SourceGeometry::Line,
      SimulationRunMode::IncoherentTransmissionLoss);
  const auto diagnostic = incoherent.accumulateIntensity(
      incoherentWorkspace, path, frequencyState, kDalpha,
      GeometricHatDiagnosticRequest{.receiverRangeIndex = 0U,
                                    .receiverDepthIndex = 0U});
  context.check(diagnostic.has_value() && diagnostic->evaluated,
                "I evaluates the offset hat receiver");
  context.checkNear(diagnostic->hatWeight, 0.5, 3.0e-12,
                    "I uses a nontrivial hat weight");
  context.checkNear(diagnostic->intensityIncrement,
                    14.81268384785484, 2.0e-11,
                    "I applies complex-tau attenuation and W once");
  context.checkNear(incoherentWorkspace.at(0U, 0U),
                    diagnostic->intensityIncrement, 0.0,
                    "I stores the Origin intensity increment");
  context.check(
      std::abs(diagnostic->intensityIncrement -
               14.81268384785484 * diagnostic->hatWeight) > 1.0,
      "I fixture distinguishes W from W squared");

  const ReceiverGrid centerReceivers({500.0}, {100.0, 300.0});
  FrequencyWorkspace pointWorkspace(10.0, centerReceivers);
  FrequencyWorkspace lineWorkspace(10.0, centerReceivers);
  const GeometricHatInfluence point(
      centerReceivers, CervenyCoordinateSystem::Cartesian,
      SourceGeometry::Point);
  const GeometricHatInfluence line(
      centerReceivers, CervenyCoordinateSystem::Cartesian,
      SourceGeometry::Line);
  const auto pointDiagnostic = point.accumulate(
      pointWorkspace, path, frequencyState, kDalpha,
      GeometricHatDiagnosticRequest{0U, 0U});
  const auto lineDiagnostic = line.accumulate(
      lineWorkspace, path, frequencyState, kDalpha,
      GeometricHatDiagnosticRequest{0U, 0U});
  context.checkNear(pointDiagnostic->amplitudeConstant /
                        lineDiagnostic->amplitudeConstant,
                    std::sqrt(0.5), 1.0e-15,
                    "point G applies sqrt(abs(cos(alpha)))");

  IntensityWorkspace semiWorkspace(10.0, receivers);
  const GeometricHatInfluence semi(
      receivers, CervenyCoordinateSystem::Cartesian,
      SourceGeometry::Line,
      SimulationRunMode::SemiCoherentTransmissionLoss);
  static_cast<void>(semi.accumulateIntensity(
      semiWorkspace, path, frequencyState, kDalpha));
  context.checkNear(semiWorkspace.at(0U, 0U),
                    incoherentWorkspace.at(0U, 0U), 0.0,
                    "S uses the same per-ray intensity law as I");
}

void testRayCenteredReverseCausticAndCache(Context& context) {
  {
    const std::vector<double> rayRanges{600.0, 400.0, 200.0};
    const ReceiverGrid receivers({500.0}, {100.0, 300.0, 500.0});
    const RayPath path = makeHorizontalPath(
        rayRanges, {300.0, 200.0, 100.0}, 0.0, true);
    const RayFrequencyState frequencyState =
        makeFrequencyState(rayRanges, 1.0);
    const RayPath originalPath = path;
    const RayFrequencyState originalFrequencyState = frequencyState;
    FrequencyWorkspace workspace(1.0, receivers);
    const GeometricHatInfluence influence(
        receivers, CervenyCoordinateSystem::RayCentered);
    static_cast<void>(influence.accumulate(
        workspace, path, frequencyState, kDalpha));
    context.check(std::abs(workspace.at(0U, 2U)) > 0.0 &&
                      std::abs(workspace.at(0U, 1U)) > 0.0,
                  "g walks receiver indices backwards in range");
    checkPathUnchanged(context, path, originalPath,
                       "g local amplitude prescaling leaves RayPath frozen");
    checkFrequencyStateUnchanged(
        context, frequencyState, originalFrequencyState,
        "g leaves projected per-frequency cache frozen");

    FrequencyWorkspace cartesianWorkspace(1.0, receivers);
    const GeometricHatInfluence cartesian(
        receivers, CervenyCoordinateSystem::Cartesian);
    static_cast<void>(cartesian.accumulate(
        cartesianWorkspace, path, frequencyState, kDalpha));
    context.check(std::abs(cartesianWorkspace.at(0U, 2U)) > 0.0 &&
                      std::abs(cartesianWorkspace.at(0U, 1U)) > 0.0,
                  "G safely starts a left-going ray beyond all receivers");
  }

  {
    const std::vector<double> rayRanges{0.0, 200.0, 400.0};
    const ReceiverGrid receivers({500.0}, {100.0, 250.0, 400.0});
    const RayPath path =
        makeHorizontalPath(rayRanges, {1.0, 1.0, -1.0});
    RayFrequencyState frequencyState =
        makeFrequencyState(rayRanges, 1.0);
    for (RayFrequencyPoint& point : frequencyState.points) {
      point.complexTravelTime = {};
    }
    FrequencyWorkspace workspace(1.0, receivers);
    const GeometricHatInfluence influence(
        receivers, CervenyCoordinateSystem::RayCentered);
    const auto diagnostic = influence.accumulate(
        workspace, path, frequencyState, kDalpha,
        GeometricHatDiagnosticRequest{.receiverRangeIndex = 2U,
                                      .receiverDepthIndex = 0U});
    context.check(diagnostic.has_value() && diagnostic->evaluated,
                  "g evaluates the caustic endpoint");
    context.checkNear(diagnostic->qInterpolated, -1.0, 0.0,
                      "g interpolates negative real q");
    context.checkNear(diagnostic->causticPhase,
                      std::numbers::pi / 2.0, 0.0,
                      "g applies the receiver-side real-q caustic phase");
    context.checkNear(diagnostic->pressureIncrement.real(), 0.0,
                      3.0e-15,
                      "g caustic anchor has zero real contribution");
    context.check(diagnostic->pressureIncrement.imag() > 0.0,
                  "g caustic anchor rotates contribution by plus pi/2");
  }

  {
    const std::vector<double> rayRanges{0.0, 200.0, 400.0};
    const ReceiverGrid receivers({500.0}, {100.0, 300.0, 500.0});
    const RayPath path =
        makeHorizontalPath(rayRanges, {1.0, -1.0, -2.0});
    RayFrequencyState frequencyState =
        makeFrequencyState(rayRanges, 1.0);
    for (RayFrequencyPoint& point : frequencyState.points) {
      point.complexTravelTime = {};
    }
    FrequencyWorkspace workspace(1.0, receivers);
    const GeometricHatInfluence influence(
        receivers, CervenyCoordinateSystem::RayCentered);
    const auto diagnostic = influence.accumulate(
        workspace, path, frequencyState, kDalpha,
        GeometricHatDiagnosticRequest{.receiverRangeIndex = 1U,
                                      .receiverDepthIndex = 0U});
    context.check(diagnostic.has_value() && diagnostic->evaluated,
                  "g evaluates after a left-endpoint q crossing");
    context.checkNear(diagnostic->causticPhase,
                      std::numbers::pi / 2.0, 0.0,
                      "g persists the segment-left real-q caustic phase");
  }
}

void testDuplicateActivePrefixAndContracts(Context& context) {
  const ReceiverGrid receivers({500.0}, {100.0, 300.0, 500.0});
  RayPath duplicatePath = makeHorizontalPath(
      {0.0, 200.0, 200.0, 400.0}, {1.0, 100.0, 100.0, 200.0});
  RayFrequencyState duplicateFrequency =
      makeFrequencyState({0.0, 200.0, 200.0, 400.0}, 1.0);
  duplicateFrequency.points[2U].reflectionPhase = 0.25;
  FrequencyWorkspace duplicateWorkspace(1.0, receivers);
  const GeometricHatInfluence cartesian(
      receivers, CervenyCoordinateSystem::Cartesian);
  const auto duplicateDiagnostic = cartesian.accumulate(
      duplicateWorkspace, duplicatePath, duplicateFrequency, kDalpha,
      GeometricHatDiagnosticRequest{.receiverRangeIndex = 1U,
                                    .receiverDepthIndex = 0U});
  context.check(duplicateDiagnostic.has_value() &&
                    duplicateDiagnostic->evaluationCount == 1U &&
                    duplicateDiagnostic->leftPointIndex == 2U,
                "G skips the incident/reflected duplicate and resumes once");
  context.checkNear(duplicateDiagnostic->causticPhase, 0.25, 0.0,
                    "G takes phase from the reflected duplicate point");

  RayPath prefixPath = makeHorizontalPath(
      {0.0, 200.0, 400.0, 600.0}, {1.0, 100.0, 200.0, 300.0});
  RayFrequencyState prefixFrequency =
      makeFrequencyState({0.0, 200.0, 400.0, 600.0}, 1.0);
  prefixFrequency.points[2U].active = false;
  prefixFrequency.points[3U].active = false;
  FrequencyWorkspace prefixWorkspace(1.0, receivers);
  static_cast<void>(cartesian.accumulate(
      prefixWorkspace, prefixPath, prefixFrequency, kDalpha));
  context.check(std::abs(prefixWorkspace.at(0U, 1U)) > 0.0 &&
                    prefixWorkspace.at(0U, 2U) ==
                        std::complex<double>{},
                "G includes the inactive terminal and rejects its suffix");

  const GeometricHatInfluence incoherent(
      receivers, CervenyCoordinateSystem::Cartesian,
      SourceGeometry::Point,
      SimulationRunMode::IncoherentTransmissionLoss);
  context.expectThrows<ValidationError>(
      [&] {
        IntensityWorkspace workspace(1.0, receivers);
        static_cast<void>(cartesian.accumulateIntensity(
            workspace, prefixPath, prefixFrequency, kDalpha));
      },
      "C cannot write an intensity workspace");
  context.expectThrows<ValidationError>(
      [&] {
        FrequencyWorkspace workspace(1.0, receivers);
        static_cast<void>(incoherent.accumulate(
            workspace, prefixPath, prefixFrequency, kDalpha));
      },
      "I cannot write a complex pressure workspace");
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(GeometricHatInfluence(
            receivers, CervenyCoordinateSystem::Cartesian,
            SourceGeometry::Point, SimulationRunMode::RayTrace));
      },
      "ray-trace mode cannot construct a field influence");
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(GeometricHatInfluence(
            ReceiverGrid({500.0, 510.0}, {100.0, 300.0},
                         ReceiverGridLayout::Irregular),
            CervenyCoordinateSystem::RayCentered));
      },
      "g rejects irregular receiver grids");
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(GeometricHatInfluence(
            ReceiverGrid({500.0}, {100.0, 300.0, 550.0}),
            CervenyCoordinateSystem::RayCentered));
      },
      "g rejects nonuniform receiver ranges");
  context.expectThrows<ValidationError>(
      [&] {
        RayFrequencyState invalidFrequency = prefixFrequency;
        invalidFrequency.points[1U].complexTravelTime =
            {0.0, std::numeric_limits<double>::quiet_NaN()};
        FrequencyWorkspace workspace(1.0, receivers);
        static_cast<void>(cartesian.accumulate(
            workspace, prefixPath, invalidFrequency, kDalpha));
      },
      "G rejects non-finite per-frequency state before accumulation");
}

void testArrivalSinkOriginFieldsAndMerge(Context& context) {
  for (const SimulationRunMode mode :
       {SimulationRunMode::AsciiArrivals,
        SimulationRunMode::BinaryArrivals}) {
    context.check(isArrivalMode(mode) && !isTransmissionLossMode(mode) &&
                      fieldAccumulationKind(mode) ==
                          FieldAccumulationKind::None &&
                      !usesLloydMirror(mode),
                  "arrival run modes are distinct non-field products");
  }
  const std::vector<double> rayRanges{0.0, 200.0, 400.0};
  const ReceiverGrid receivers({500.0}, {100.0, 300.0});
  const RayPath path = makeHorizontalPath(
      rayRanges, {0.0, 100.0, 200.0}, std::numbers::pi / 3.0);
  const RayFrequencyState frequencyState =
      makeFrequencyState(rayRanges, 50.0);
  ArrivalWorkspace workspace(50.0, receivers);
  const GeometricHatInfluence influence(
      receivers, CervenyCoordinateSystem::Cartesian,
      SourceGeometry::Point, SimulationRunMode::AsciiArrivals);
  const auto diagnostic = influence.accumulateArrivals(
      workspace, path, frequencyState, kDalpha,
      GeometricHatDiagnosticRequest{0U, 0U});

  context.check(diagnostic.has_value() && diagnostic->evaluated,
                "A shares the accepted G receiver traversal");
  context.check(workspace.arrivalCountAt(0U, 0U) == 1U,
                "A stores the direct arrival in the selected cell");
  const bellhop::Arrival direct = workspace.arrivalsAt(0U, 0U).front();
  context.checkNear(direct.amplitude,
                    static_cast<float>(std::sqrt(15.0)), 0.0,
                    "A stores the Origin G point-source amplitude");
  context.checkNear(direct.phaseRadians, 0.0, 0.0,
                    "A stores unwrapped phase radians");
  context.checkNear(direct.delaySeconds.real(),
                    static_cast<float>(100.0 / kSoundSpeed), 0.0,
                    "A stores interpolated complex delay");
  context.checkNear(direct.sourceDeclinationDegrees, 60.0F, 4.0e-6,
                    "A stores launch angle in degrees");
  context.checkNear(direct.receiverDeclinationDegrees, 0.0F, 0.0,
                    "A stores Cartesian segment declination");
  context.check(direct.topBounceCount == 0 &&
                    direct.bottomBounceCount == 0,
                "A direct arrival has zero bounces");

  static_cast<void>(influence.accumulateArrivals(
      workspace, path, frequencyState, kDalpha));
  context.check(workspace.arrivalCountAt(0U, 0U) == 1U &&
                    workspace.mergeCount() >= 1U,
                "adjacent equivalent bracketing contributions reach AddArr merge");

  const ReceiverGrid irregular(
      {500.0, 500.001}, {100.0, 300.0},
      ReceiverGridLayout::Irregular);
  ArrivalWorkspace irregularWorkspace(50.0, irregular);
  const GeometricHatInfluence irregularInfluence(
      irregular, CervenyCoordinateSystem::Cartesian,
      SourceGeometry::Point, SimulationRunMode::BinaryArrivals);
  static_cast<void>(irregularInfluence.accumulateArrivals(
      irregularWorkspace, path, frequencyState, kDalpha));
  context.check(irregularWorkspace.arrivalCountAt(0U, 0U) == 1U &&
                    irregularWorkspace.arrivalCountAt(0U, 1U) == 1U,
                "a supports Origin irregular Cartesian receiver cells");
}

void testArrivalSinkBouncesCausticAndContracts(Context& context) {
  const ReceiverGrid receivers({500.0}, {100.0, 300.0});
  RayPath reflected = makeHorizontalPath(
      {0.0, 200.0, 200.0, 400.0}, {1.0, 100.0, 100.0, 200.0});
  reflected.events.push_back(
      ReflectionEvent{.rayPointIndex = 1U,
                      .reflectedRayPointIndex = 2U,
                      .boundary = ReflectionBoundary::SeaSurface,
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
  RayFrequencyState reflectedFrequency =
      makeFrequencyState({0.0, 200.0, 200.0, 400.0}, 1.0);
  reflectedFrequency.points[2U].reflectionPhase = 0.25;
  ArrivalWorkspace reflectedWorkspace(1.0, receivers);
  const GeometricHatInfluence cartesian(
      receivers, CervenyCoordinateSystem::Cartesian,
      SourceGeometry::Line, SimulationRunMode::AsciiArrivals);
  static_cast<void>(cartesian.accumulateArrivals(
      reflectedWorkspace, reflected, reflectedFrequency, kDalpha));
  const bellhop::Arrival reflectedArrival =
      reflectedWorkspace.arrivalsAt(0U, 1U).front();
  context.check(reflectedArrival.topBounceCount == 1 &&
                    reflectedArrival.bottomBounceCount == 0,
                "A uses the reflected endpoint prefix bounce count");
  context.checkNear(reflectedArrival.phaseRadians, 0.25F, 0.0,
                    "A preserves reflection phase after duplicate point");

  RayPath caustic = makeHorizontalPath(
      {0.0, 200.0, 400.0}, {1.0, 1.0, -1.0});
  RayFrequencyState causticFrequency =
      makeFrequencyState({0.0, 200.0, 400.0}, 1.0);
  for (RayFrequencyPoint& point : causticFrequency.points) {
    point.complexTravelTime = {};
  }
  const ReceiverGrid rayCenteredReceivers(
      {500.0}, {100.0, 250.0, 400.0});
  ArrivalWorkspace causticWorkspace(1.0, rayCenteredReceivers);
  const GeometricHatInfluence rayCentered(
      rayCenteredReceivers, CervenyCoordinateSystem::RayCentered,
      SourceGeometry::Point, SimulationRunMode::BinaryArrivals);
  static_cast<void>(rayCentered.accumulateArrivals(
      causticWorkspace, caustic, causticFrequency, kDalpha));
  context.checkNear(
      causticWorkspace.arrivalsAt(0U, 2U).front().phaseRadians,
      static_cast<float>(std::numbers::pi / 2.0), 0.0,
      "a stores ray-centered caustic phase without wrapping");

  context.expectThrows<ValidationError>(
      [&] {
        ArrivalWorkspace wrongFrequency(2.0, receivers);
        static_cast<void>(cartesian.accumulateArrivals(
            wrongFrequency, reflected, reflectedFrequency, kDalpha));
      },
      "arrival sink rejects a frequency-mismatched workspace");
  const GeometricHatInfluence coherent(
      receivers, CervenyCoordinateSystem::Cartesian);
  context.expectThrows<ValidationError>(
      [&] {
        ArrivalWorkspace arrivals(1.0, receivers);
        static_cast<void>(coherent.accumulateArrivals(
            arrivals, reflected, reflectedFrequency, kDalpha));
      },
      "TL influence cannot write an arrival workspace");
}

}  // namespace

int main() {
  Context context;
  testCartesianOriginAnchorAndWalkers(context);
  testIntensityWeightAndSourceGeometry(context);
  testRayCenteredReverseCausticAndCache(context);
  testDuplicateActivePrefixAndContracts(context);
  testArrivalSinkOriginFieldsAndMerge(context);
  testArrivalSinkBouncesCausticAndContracts(context);
  if (context.failureCount() != 0) {
    std::cerr << context.failureCount()
              << " geometric hat assertion(s) failed\n";
    return 1;
  }
  std::cout << "All geometric hat influence tests passed\n";
  return 0;
}
