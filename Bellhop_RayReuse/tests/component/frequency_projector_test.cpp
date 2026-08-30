#include "rayreuse/field/frequency_projector.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <iostream>
#include <limits>
#include <memory>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

#include "rayreuse/acoustics/attenuation.hpp"
#include "rayreuse/error.hpp"
#include "rayreuse/model/c_linear_frequency_ssp.hpp"
#include "rayreuse/model/cubic_spline_frequency_ssp.hpp"
#include "rayreuse/ray/geometry_tracer.hpp"
#include "support/boundary_acoustics_fixture.hpp"
#include "support/munk_case_fixture.hpp"
#include "support/test_harness.hpp"

namespace {

using rayreuse::AcousticMaterial;
using rayreuse::AttenuationUnit;
using rayreuse::BiologicalAttenuationLayers;
using rayreuse::BoundaryKind;
using rayreuse::BoundaryModel;
using rayreuse::CLinearFrequencySsp;
using rayreuse::convertAttenuation;
using rayreuse::CubicSplineFrequencySsp;
using rayreuse::Environment;
using rayreuse::FrancoisGarrisonParameters;
using rayreuse::FrequencyProjector;
using rayreuse::FrequencyWorkspace;
using rayreuse::GeometryTracer;
using rayreuse::IntegratorSettings;
using rayreuse::RawAttenuation;
using rayreuse::RayFrequencyState;
using rayreuse::RayPath;
using rayreuse::RayState;
using rayreuse::ReceiverGrid;
using rayreuse::ReflectionBoundary;
using rayreuse::ReflectionEvent;
using rayreuse::SoundSpeedPoint;
using rayreuse::SoundSpeedProfile;
using rayreuse::Source;
using rayreuse::StepQuadrature;
using rayreuse::ValidationError;
using rayreuse::Vec2;
using rayreuse::VolumeAttenuation;
using rayreuse::VolumeAttenuationModel;
using rayreuse::test::Context;

constexpr double kSoundSpeed = 1500.0;
constexpr double kWaterDensity = 1000.0;
constexpr double kLegacyActiveThreshold = static_cast<double>(0.005F);

RawAttenuation thorpAttenuation() {
  return RawAttenuation{.value = 0.0,
                        .unit = AttenuationUnit::DecibelsPerWavelength,
                        .volumeModel = VolumeAttenuationModel::Thorp};
}

Environment makeConstantEnvironment(
    RawAttenuation waterAttenuation = {},
    BoundaryModel seabed = BoundaryModel::rigid(100.0)) {
  return Environment(SoundSpeedProfile({{.depth = 0.0,
                                         .soundSpeed = kSoundSpeed,
                                         .density = kWaterDensity,
                                         .attenuation = waterAttenuation},
                                        {.depth = 100.0,
                                         .soundSpeed = kSoundSpeed,
                                         .density = kWaterDensity,
                                         .attenuation = waterAttenuation}}),
                     BoundaryModel::vacuum(0.0), std::move(seabed));
}

RayState makeRayState(Vec2 position, Vec2 slowness,
                      double realTravelTime = 0.0) {
  return RayState{.position = position,
                  .slowness = slowness,
                  .dynamicP = {1.0, 0.0},
                  .dynamicQ = {0.0, 1.0},
                  .soundSpeed = kSoundSpeed,
                  .realTravelTime = realTravelTime};
}

RayPath makeSingleStepPath(double length, double depth) {
  RayPath path;
  path.points = {makeRayState({.range = 0.0, .depth = depth},
                              {.range = 1.0 / kSoundSpeed, .depth = 0.0}),
                 makeRayState({.range = length, .depth = depth},
                              {.range = 1.0 / kSoundSpeed, .depth = 0.0},
                              length / kSoundSpeed)};
  path.steps = {
      StepQuadrature{.stepLength = length,
                     .startWeight = 0.0,
                     .midpointWeight = length,
                     .midpoint = {.range = 0.5 * length, .depth = depth}}};
  return path;
}

ReflectionEvent makeReflectionEvent(std::size_t pointIndex,
                                    ReflectionBoundary boundary, Vec2 position,
                                    double tangentSlowness,
                                    double normalSlowness) {
  const Vec2 tangent{.range = 1.0, .depth = 0.0};
  const Vec2 normal{
      .range = 0.0,
      .depth = boundary == ReflectionBoundary::SeaSurface ? -1.0 : 1.0};
  const Vec2 incident = tangentSlowness * tangent + normalSlowness * normal;
  const Vec2 reflected = tangentSlowness * tangent - normalSlowness * normal;
  return ReflectionEvent{.rayPointIndex = pointIndex,
                         .boundary = boundary,
                         .boundarySegmentIndex = 0U,
                         .position = position,
                         .boundaryTangent = tangent,
                         .outwardNormal = normal,
                         .incidentSlowness = incident,
                         .reflectedSlowness = reflected,
                         .tangentSlowness = tangentSlowness,
                         .normalSlowness = normalSlowness};
}

void checkComplexNear(Context& context, std::complex<double> actual,
                      std::complex<double> expected, double tolerance,
                      const char* message) {
  context.checkNear(actual.real(), expected.real(), tolerance,
                    std::string(message) + " real");
  context.checkNear(actual.imag(), expected.imag(), tolerance,
                    std::string(message) + " imaginary");
}

void testLosslessAndThorpPropagation(Context& context) {
  const RayPath path = makeSingleStepPath(10000.0, 50.0);

  const Environment losslessEnvironment = makeConstantEnvironment();
  const CLinearFrequencySsp losslessProfile(
      losslessEnvironment.soundSpeedProfile(), 5000.0);
  context.check(losslessProfile.isLossless(),
                "zero-attenuation frequency SSP is lossless");
  context.check(losslessProfile.uniformComplexSoundSpeed().has_value(),
                "constant lossless frequency SSP exposes a uniform value");
  const RayFrequencyState lossless =
      FrequencyProjector(losslessEnvironment).project(path, 5000.0, 0.75);
  context.check(lossless.points.size() == path.points.size(),
                "lossless projection has one state per geometry point");
  checkComplexNear(context, lossless.points.back().complexTravelTime,
                   {10000.0 / kSoundSpeed, 0.0}, 1.0e-15,
                   "lossless projection reproduces real travel time");
  context.checkNear(lossless.points.back().amplitude, 0.75, 0.0,
                    "lossless propagation preserves source amplitude");
  context.checkNear(lossless.points.back().reflectionPhase, 0.0, 0.0,
                    "lossless direct ray has zero reflection phase");
  context.check(lossless.points.back().active,
                "ordinary direct ray remains active");

  const Environment thorpEnvironment =
      makeConstantEnvironment(thorpAttenuation());
  const CLinearFrequencySsp thorpProfile(thorpEnvironment.soundSpeedProfile(),
                                         5000.0);
  context.check(!thorpProfile.isLossless(),
                "Thorp frequency SSP remains lossy");
  context.check(thorpProfile.uniformComplexSoundSpeed().has_value(),
                "constant Thorp frequency SSP exposes a uniform value");
  const RayFrequencyState thorp =
      FrequencyProjector(thorpEnvironment).project(path, 5000.0, 1.0);
  // Independent constant-speed evaluation of the 5 kHz Fortran Thorp anchor.
  const std::complex<double> expected{6.6666666666370800,
                                      -1.4044361562126921e-5};
  checkComplexNear(context, thorp.points.back().complexTravelTime, expected,
                   2.0e-15, "5 kHz Thorp complex travel time");
  context.check(thorp.points.back().complexTravelTime.real() !=
                    path.points.back().realTravelTime,
                "lossy projection recomputes rather than copies tau real");
}

void testNodeConversionPrecedesInterpolation(Context& context) {
  SoundSpeedProfile profile({{.depth = 0.0,
                              .soundSpeed = 1400.0,
                              .density = 1000.0,
                              .attenuation = thorpAttenuation()},
                             {.depth = 1000.0,
                              .soundSpeed = 1800.0,
                              .density = 1200.0,
                              .attenuation = thorpAttenuation()}});
  const CLinearFrequencySsp frequencyProfile(profile, 5000.0);
  context.check(
      !frequencyProfile.uniformComplexSoundSpeed().has_value(),
      "nonuniform frequency SSP retains the general interpolation path");
  const auto sample =
      frequencyProfile.evaluateAtSegment({.range = 0.0, .depth = 250.0}, 0U);
  const double nodeFirst =
      0.75 *
          convertAttenuation(profile.points()[0U].attenuation, 5000.0, 1400.0)
              .imaginarySoundSpeed +
      0.25 *
          convertAttenuation(profile.points()[1U].attenuation, 5000.0, 1800.0)
              .imaginarySoundSpeed;
  const double convertAfterInterpolation =
      convertAttenuation(profile.points()[0U].attenuation, 5000.0, 1500.0)
          .imaginarySoundSpeed;
  context.checkNear(sample.soundSpeed, 1500.0, 0.0,
                    "frequency SSP preserves C-linear real speed");
  context.checkNear(sample.imaginarySoundSpeed, nodeFirst, 1.0e-18,
                    "frequency SSP interpolates converted node cimag");
  context.check(
      std::abs(sample.imaginarySoundSpeed - convertAfterInterpolation) > 1.0e-5,
      "node-first contract is distinguishable from query-point conversion");
  context.checkNear(sample.density, 1050.0, 0.0,
                    "frequency SSP preserves linear density");

  Environment environment(std::move(profile), BoundaryModel::vacuum(0.0),
                          BoundaryModel::rigid(1000.0));
  RayPath path;
  path.points = {makeRayState({.range = 0.0, .depth = 0.0},
                              {.range = 1.0 / 1400.0, .depth = 0.0}),
                 makeRayState({.range = 1000.0, .depth = 500.0},
                              {.range = 1.0 / 1500.0, .depth = 0.0})};
  path.steps = {StepQuadrature{.stepLength = 1000.0,
                               .startWeight = 400.0,
                               .midpointWeight = 600.0,
                               .midpoint = {.range = 500.0, .depth = 250.0}}};
  const RayFrequencyState projected =
      FrequencyProjector(environment).project(path, 5000.0, 1.0);
  checkComplexNear(
      context, projected.points.back().complexTravelTime,
      {6.8571428571135828e-1, -1.4156716454626132e-6}, 2.0e-16,
      "projector consumes node-first C-linear complex sound speed");
}

void testVacuumRigidPhaseIsUnwrapped(Context& context) {
  const Environment environment = makeConstantEnvironment();
  const GeometryTracer tracer(environment,
                              IntegratorSettings{.stepLength = 100.0,
                                                 .rangeLimit = 1000.0,
                                                 .depthLimit = 200.0,
                                                 .maximumRayPoints = 18U});
  const RayPath path =
      tracer.trace(Source{.depth = 50.0}, 0.5 * std::numbers::pi);
  const RayFrequencyState state =
      FrequencyProjector(environment).project(path, 250.0, 1.0);

  std::size_t vacuumCount = 0U;
  for (const ReflectionEvent& event : path.events) {
    if (event.boundary == ReflectionBoundary::SeaSurface) {
      ++vacuumCount;
    }
    const auto& reflected = state.points[event.rayPointIndex + 1U];
    context.checkNear(reflected.reflectionPhase,
                      static_cast<double>(vacuumCount) * std::numbers::pi,
                      1.0e-14,
                      "vacuum adds pi while rigid adds zero without wrapping");
    context.checkNear(reflected.amplitude, 1.0, 0.0,
                      "vacuum and rigid reflections preserve amplitude");
    context.check(reflected.active, "lossless reflected state remains active");
  }
  context.check(vacuumCount >= 2U,
                "phase test reaches multiple vacuum reflections");
  context.check(state.points.back().reflectionPhase > 2.0 * std::numbers::pi,
                "stored cumulative reflection phase exceeds one wrapped turn");
}

RayPath makeAcousticReflectionPath() {
  const auto& fixture = rayreuse::test::kHalfSpaceCoefficientFixtures[1U];
  const Vec2 position{.range = 25.0, .depth = 100.0};
  const ReflectionEvent event = makeReflectionEvent(
      0U, ReflectionBoundary::Seabed, position, fixture.tangentSlowness,
      fixture.outwardNormalSlowness);
  RayPath path;
  path.points = {makeRayState(position, event.incidentSlowness),
                 makeRayState(position, event.reflectedSlowness)};
  path.events = {event};
  return path;
}

RayPath makeRepeatedAcousticReflectionPath() {
  const auto& fixture = rayreuse::test::kHalfSpaceCoefficientFixtures[1U];
  const Vec2 bottom{.range = 0.0, .depth = 100.0};
  const Vec2 surface{.range = 100.0, .depth = 0.0};
  const ReflectionEvent bottom0 = makeReflectionEvent(
      0U, ReflectionBoundary::Seabed, bottom, fixture.tangentSlowness,
      fixture.outwardNormalSlowness);
  const ReflectionEvent surface0 = makeReflectionEvent(
      2U, ReflectionBoundary::SeaSurface, surface, fixture.tangentSlowness,
      fixture.outwardNormalSlowness);
  const ReflectionEvent bottom1 = makeReflectionEvent(
      4U, ReflectionBoundary::Seabed, bottom, fixture.tangentSlowness,
      fixture.outwardNormalSlowness);
  const ReflectionEvent surface1 = makeReflectionEvent(
      6U, ReflectionBoundary::SeaSurface, surface, fixture.tangentSlowness,
      fixture.outwardNormalSlowness);
  const ReflectionEvent bottom2 = makeReflectionEvent(
      8U, ReflectionBoundary::Seabed, bottom, fixture.tangentSlowness,
      fixture.outwardNormalSlowness);

  RayPath path;
  path.points = {makeRayState(bottom, bottom0.incidentSlowness),
                 makeRayState(bottom, bottom0.reflectedSlowness),
                 makeRayState(surface, surface0.incidentSlowness),
                 makeRayState(surface, surface0.reflectedSlowness),
                 makeRayState(bottom, bottom1.incidentSlowness),
                 makeRayState(bottom, bottom1.reflectedSlowness),
                 makeRayState(surface, surface1.incidentSlowness),
                 makeRayState(surface, surface1.reflectedSlowness),
                 makeRayState(bottom, bottom2.incidentSlowness),
                 makeRayState(bottom, bottom2.reflectedSlowness),
                 makeRayState(surface, surface1.incidentSlowness)};
  path.events = {bottom0, surface0, bottom1, surface1, bottom2};
  path.steps = {StepQuadrature{.stepLength = 100.0,
                               .startWeight = 0.0,
                               .midpointWeight = 100.0,
                               .midpoint = {.range = 50.0, .depth = 50.0}},
                StepQuadrature{.stepLength = 100.0,
                               .startWeight = 0.0,
                               .midpointWeight = 100.0,
                               .midpoint = {.range = 50.0, .depth = 50.0}},
                StepQuadrature{.stepLength = 100.0,
                               .startWeight = 0.0,
                               .midpointWeight = 100.0,
                               .midpoint = {.range = 50.0, .depth = 50.0}},
                StepQuadrature{.stepLength = 100.0,
                               .startWeight = 0.0,
                               .midpointWeight = 100.0,
                               .midpoint = {.range = 50.0, .depth = 50.0}},
                StepQuadrature{.stepLength = 100.0,
                               .startWeight = 0.0,
                               .midpointWeight = 100.0,
                               .midpoint = {.range = 50.0, .depth = 50.0}}};
  return path;
}

Environment makeAcousticBottomEnvironment() {
  const auto& fixture = rayreuse::test::kHalfSpaceCoefficientFixtures[1U];
  return makeConstantEnvironment(
      {},
      BoundaryModel::acousticHalfSpace(
          100.0, AcousticMaterial{
                     .compressionalSoundSpeed = fixture.compressionalSoundSpeed,
                     .shearSoundSpeed = 0.0,
                     .density = fixture.halfSpaceDensity,
                     .compressionalAttenuation = {
                         .value = fixture.attenuationDecibelsPerWavelength,
                         .unit = AttenuationUnit::DecibelsPerWavelength}}));
}

void testAcousticReflectionAndActiveCutoff(Context& context) {
  const auto& fixture = rayreuse::test::kHalfSpaceCoefficientFixtures[1U];
  const RayPath path = makeAcousticReflectionPath();
  const FrequencyProjector projector(makeAcousticBottomEnvironment());

  const RayFrequencyState ordinary =
      projector.project(path, fixture.frequencyHz, 1.0);
  context.checkNear(ordinary.points.back().amplitude,
                    std::abs(fixture.expectedRawCoefficient), 2.0e-15,
                    "projector applies acoustic coefficient magnitude");
  context.checkNear(ordinary.points.back().reflectionPhase,
                    std::arg(fixture.expectedRawCoefficient), 2.0e-15,
                    "projector applies acoustic coefficient phase");
  context.check(ordinary.points.back().active,
                "ordinary acoustic reflection remains active");

  const RayFrequencyState killed =
      projector.project(path, fixture.frequencyHz, 0.04);
  context.check(
      killed.points.front().active,
      "source point stays active before the first legacy cutoff check");
  context.checkNear(killed.points.back().amplitude,
                    0.04 * std::abs(fixture.expectedRawCoefficient), 2.0e-16,
                    "active cutoff does not overwrite the physical amplitude");
  context.check(!killed.points.back().active,
                "acoustic reflection below 0.005 becomes inactive");

  RayPath rigidPath = path;
  const FrequencyProjector rigidProjector(makeConstantEnvironment());
  const RayFrequencyState exact =
      rigidProjector.project(rigidPath, 250.0, kLegacyActiveThreshold);
  context.check(exact.points.back().active,
                "amplitude exactly at promoted threshold stays active");
  const RayFrequencyState below = rigidProjector.project(
      rigidPath, 250.0, std::nextafter(kLegacyActiveThreshold, 0.0));
  context.check(!below.points.back().active,
                "strictly smaller amplitude becomes inactive");

  const RayFrequencyState repeated = projector.project(
      makeRepeatedAcousticReflectionPath(), fixture.frequencyHz, 1.0);
  context.checkNear(
      repeated.points[1U].amplitude, 1.19835889068164225e-1, 2.0e-15,
      "first acoustic-bottom reflection remains above active cutoff");
  context.check(repeated.points[1U].active,
                "first acoustic-bottom post-point is active");
  context.checkNear(
      repeated.points[5U].amplitude, 1.43606403087573626e-2, 2.0e-15,
      "second acoustic-bottom reflection remains above active cutoff");
  context.check(repeated.points[5U].active,
                "second acoustic-bottom post-point is active");
  context.checkNear(
      repeated.points[9U].amplitude, 1.72092009898805486e-3, 3.0e-16,
      "third acoustic-bottom reflection matches Fortran cumulative amplitude");
  context.checkNear(
      repeated.points[9U].reflectionPhase, 6.39628196441146546, 2.0e-14,
      "third acoustic-bottom reflection matches unwrapped Fortran phase");
  context.check(!repeated.points[9U].active,
                "third acoustic-bottom post-point becomes inactive");
  context.check(!repeated.points[10U].active,
                "inactive state is sticky across the geometry suffix");
}

void testMunkReflectionOracle(Context& context) {
  const Environment environment = rayreuse::test::makeMunkEnvironment();
  const RayPath path =
      GeometryTracer(environment, rayreuse::test::makeMunkIntegratorSettings())
          .trace(Source{.depth = 1000.0},
                 rayreuse::test::kMunkExtremeLaunchAngle);
  const RayFrequencyState state =
      FrequencyProjector(environment).project(path, 50.0, 1.0);
  const auto eventIterator = std::find_if(
      path.events.begin(), path.events.end(), [](const ReflectionEvent& event) {
        return event.boundary == ReflectionBoundary::Seabed;
      });
  context.check(eventIterator != path.events.end(),
                "extreme Munk ray reaches the acoustic seabed");
  if (eventIterator == path.events.end()) {
    return;
  }

  const auto& fixture = rayreuse::test::kHalfSpaceCoefficientFixtures.back();
  context.checkNear(eventIterator->tangentSlowness, fixture.tangentSlowness,
                    2.0e-15,
                    "Munk event tangent slowness matches Fortran oracle");
  context.checkNear(eventIterator->normalSlowness,
                    fixture.outwardNormalSlowness, 2.0e-15,
                    "Munk event normal slowness matches Fortran oracle");
  const auto& reflected = state.points[eventIterator->rayPointIndex + 1U];
  context.checkNear(reflected.amplitude,
                    std::abs(fixture.expectedRawCoefficient), 2.0e-15,
                    "Munk projected bottom amplitude matches Fortran oracle");
  context.checkNear(reflected.reflectionPhase,
                    std::arg(fixture.expectedRawCoefficient), 2.0e-15,
                    "Munk projected bottom phase matches Fortran oracle");
  context.checkNear(
      reflected.complexTravelTime.imag(), 0.0, 0.0,
      "lossless Munk water column retains zero imaginary travel time");
}

void testProjectionDoesNotMutateGeometry(Context& context) {
  const Environment environment = makeConstantEnvironment(thorpAttenuation());
  const GeometryTracer tracer(environment,
                              IntegratorSettings{.stepLength = 10.0,
                                                 .rangeLimit = 35.0,
                                                 .depthLimit = 200.0,
                                                 .maximumRayPoints = 100U});
  const RayPath path = tracer.trace(Source{.depth = 50.0}, 0.0);
  const RayPath before = path;
  const FrequencyProjector projector(environment);
  RayFrequencyState low = projector.project(path, 50.0, 1.0);
  RayFrequencyState high = projector.project(path, 5000.0, 1.0);

  context.check(path.points.size() == before.points.size() &&
                    path.steps.size() == before.steps.size() &&
                    path.events.size() == before.events.size(),
                "projection preserves geometry container sizes");
  for (std::size_t index = 0U; index < path.points.size(); ++index) {
    context.check(
        path.points[index].position == before.points[index].position &&
            path.points[index].slowness == before.points[index].slowness &&
            path.points[index].dynamicP == before.points[index].dynamicP &&
            path.points[index].dynamicQ == before.points[index].dynamicQ &&
            path.points[index].soundSpeed == before.points[index].soundSpeed &&
            path.points[index].realTravelTime ==
                before.points[index].realTravelTime,
        "projection leaves every geometry point unchanged");
  }
  context.check(low.points.back().complexTravelTime !=
                    high.points.back().complexTravelTime,
                "two frequencies receive independent projected travel times");
  low.points.front().amplitude = 0.25;
  context.checkNear(high.points.front().amplitude, 1.0, 0.0,
                    "frequency states own independent point storage");

  const ReceiverGrid receivers({10.0, 20.0}, {100.0, 200.0});
  FrequencyWorkspace lowWorkspace(50.0, receivers);
  FrequencyWorkspace highWorkspace(5000.0, receivers);
  lowWorkspace.at(0U, 0U) = {1.0, -2.0};
  highWorkspace.at(0U, 0U) = {3.0, 4.0};
  lowWorkspace.clear();
  context.check(highWorkspace.at(0U, 0U) == std::complex<double>{3.0, 4.0},
                "clearing one frequency workspace does not alter another");
}

void testEnvironmentVolumeAttenuationProjection(Context& context) {
  const auto layers = std::make_shared<const BiologicalAttenuationLayers>(
      BiologicalAttenuationLayers{
          {.minimumDepth = 0.0,
           .maximumDepth = 100.0,
           .resonanceFrequency = 1000.0,
           .qualityFactor = 2.0,
           .attenuationCoefficientDecibelsPerKilometer = 100.0}});
  const VolumeAttenuation biological{
      .model = VolumeAttenuationModel::Biological, .parameters = layers};
  const VolumeAttenuation fg{
      .model = VolumeAttenuationModel::FrancoisGarrison,
      .parameters = FrancoisGarrisonParameters{.temperatureCelsius = 10.0,
                                               .salinityPsu = 35.0,
                                               .pH = 8.0,
                                               .meanDepthMeters = 50.0}};
  const AcousticMaterial fluid{.compressionalSoundSpeed = 1800.0,
                               .shearSoundSpeed = 0.0,
                               .density = 1800.0};
  const SoundSpeedProfile profile(
      {{.depth = 0.0, .soundSpeed = kSoundSpeed, .density = kWaterDensity},
       {.depth = 100.0, .soundSpeed = kSoundSpeed, .density = kWaterDensity}});
  const BoundaryModel bottom = BoundaryModel::acousticHalfSpace(100.0, fluid);
  const Environment losslessEnvironment(profile, BoundaryModel::vacuum(0.0),
                                        bottom);
  const Environment biologicalEnvironment(profile, BoundaryModel::vacuum(0.0),
                                          bottom, biological);
  const Environment fgEnvironment(profile, BoundaryModel::vacuum(0.0), bottom,
                                  fg);

  const double tangent = std::sin(std::numbers::pi / 6.0) / kSoundSpeed;
  const double normal = std::cos(std::numbers::pi / 6.0) / kSoundSpeed;
  const Vec2 start{.range = 0.0, .depth = 50.0};
  const Vec2 reflection{.range = 100.0, .depth = 100.0};
  const ReflectionEvent event = makeReflectionEvent(
      1U, ReflectionBoundary::Seabed, reflection, tangent, normal);
  RayPath path;
  path.points = {
      makeRayState(start, {.range = 1.0 / kSoundSpeed, .depth = 0.0}),
      makeRayState(reflection, event.incidentSlowness, 100.0 / kSoundSpeed),
      makeRayState(reflection, event.reflectedSlowness, 100.0 / kSoundSpeed)};
  path.steps = {StepQuadrature{.stepLength = 100.0,
                               .startWeight = 0.0,
                               .midpointWeight = 100.0,
                               .midpoint = {.range = 50.0, .depth = 75.0}}};
  path.events = {event};

  const auto lossless =
      FrequencyProjector(losslessEnvironment).project(path, 500.0, 1.0);
  const FrequencyProjector biologicalProjector(biologicalEnvironment);
  const auto biologicalLow = biologicalProjector.project(path, 500.0, 1.0);
  const auto biologicalHigh = biologicalProjector.project(path, 2000.0, 1.0);
  const auto biologicalRepeated = biologicalProjector.project(path, 500.0, 1.0);
  const auto fgState =
      FrequencyProjector(fgEnvironment).project(path, 500.0, 1.0);
  context.check(biologicalLow.points.back().complexTravelTime.imag() < 0.0 &&
                    fgState.points.back().complexTravelTime.imag() < 0.0,
                "projector applies biological and FG water-column loss");
  context.check(
      biologicalLow.points.back().amplitude != lossless.points.back().amplitude,
      "ordinary boundary reflection includes environment volume loss");
  context.check(biologicalHigh.points.back().complexTravelTime !=
                    biologicalLow.points.back().complexTravelTime,
                "volume attenuation remains frequency local");
  context.check(biologicalRepeated.points.back().complexTravelTime ==
                        biologicalLow.points.back().complexTravelTime &&
                    biologicalRepeated.points.back().amplitude ==
                        biologicalLow.points.back().amplitude &&
                    biologicalRepeated.points.back().reflectionPhase ==
                        biologicalLow.points.back().reflectionPhase,
                "500-2000-500 projection is deterministic");

  RayPath longPath = path;
  longPath.events.front().longMaterialOverride =
      rayreuse::FrozenBoundaryMaterial{
          .material = fluid,
          .attenuationEvaluationDepth =
              rayreuse::kLegacyLongBoundaryAttenuationDepth};
  const auto longState = biologicalProjector.project(longPath, 500.0, 1.0);
  context.checkNear(
      longState.points.back().amplitude, lossless.points.back().amplitude, 0.0,
      "long material legacy depth excludes biological boundary loss");

  const AcousticMaterial elastic{.compressionalSoundSpeed = 2000.0,
                                 .shearSoundSpeed = 1000.0,
                                 .density = 2000.0};
  const Environment elasticEnvironment(
      profile, BoundaryModel::vacuum(0.0),
      BoundaryModel::acousticHalfSpace(100.0, elastic), biological);
  const auto elasticState =
      FrequencyProjector(elasticEnvironment).project(path, 500.0, 1.0);
  const Environment elasticLosslessEnvironment(
      profile, BoundaryModel::vacuum(0.0),
      BoundaryModel::acousticHalfSpace(100.0, elastic));
  const auto elasticLossless =
      FrequencyProjector(elasticLosslessEnvironment).project(path, 500.0, 1.0);
  context.check(elasticState.points.back().amplitude !=
                    elasticLossless.points.back().amplitude,
                "elastic reflection applies volume loss to material modes");

  const BoundaryModel grain = BoundaryModel::grainSizeHalfSpace(100.0, 3.0);
  const auto grainLossless =
      FrequencyProjector(
          Environment(profile, BoundaryModel::vacuum(0.0), grain))
          .project(path, 500.0, 1.0);
  const auto grainBiological =
      FrequencyProjector(
          Environment(profile, BoundaryModel::vacuum(0.0), grain, biological))
          .project(path, 500.0, 1.0);
  context.check(grainBiological.points.back().amplitude ==
                        grainLossless.points.back().amplitude &&
                    grainBiological.points.back().reflectionPhase ==
                        grainLossless.points.back().reflectionPhase,
                "grain-size reflection ignores environment volume attenuation");
}

void testInvalidInputs(Context& context) {
  const FrequencyProjector projector(makeConstantEnvironment());
  const RayPath path = makeSingleStepPath(10.0, 50.0);
  context.expectThrows<ValidationError>(
      [&projector, &path] {
        static_cast<void>(projector.project(path, 0.0, 1.0));
      },
      "non-positive projection frequency is rejected");
  context.expectThrows<ValidationError>(
      [&projector, &path] {
        static_cast<void>(projector.project(path, 50.0, -1.0));
      },
      "negative source amplitude is rejected");

  RayPath malformed = path;
  malformed.steps.clear();
  context.expectThrows<ValidationError>(
      [&projector, &malformed] {
        static_cast<void>(projector.project(malformed, 50.0, 1.0));
      },
      "malformed path transition topology is rejected");
}

void testPchipFrequencyProjection(Context& context) {
  const SoundSpeedProfile profile(
      {{.depth = 0.0,
        .soundSpeed = 1500.0,
        .density = 1000.0,
        .attenuation = {.value = 0.1,
                        .unit = AttenuationUnit::DecibelsPerWavelength}},
       {.depth = 500.0,
        .soundSpeed = 1480.0,
        .density = 1100.0,
        .attenuation = {.value = 0.4,
                        .unit = AttenuationUnit::DecibelsPerWavelength}},
       {.depth = 1000.0,
        .soundSpeed = 1520.0,
        .density = 1200.0,
        .attenuation = {.value = 0.2,
                        .unit = AttenuationUnit::DecibelsPerWavelength}}},
      rayreuse::SspInterpolationKind::Pchip);
  const Environment environment(profile, BoundaryModel::vacuum(0.0),
                                BoundaryModel::rigid(1000.0));
  const FrequencyProjector projector(environment);

  RayPath path;
  path.points = {RayState{.position = {.range = 0.0, .depth = 250.0},
                          .slowness = {.range = 1.0 / 1483.75, .depth = 0.0},
                          .dynamicP = {1.0, 0.0},
                          .dynamicQ = {0.0, 1.0},
                          .soundSpeed = 1483.75,
                          .realTravelTime = 0.0},
                 RayState{.position = {.range = 100.0, .depth = 750.0},
                          .slowness = {.range = 1.0 / 1483.75, .depth = 0.0},
                          .dynamicP = {1.0, 0.0},
                          .dynamicQ = {0.0, 1.0},
                          .soundSpeed = 1483.75,
                          .realTravelTime = 100.0 / 1483.75}};
  path.steps = {StepQuadrature{.stepLength = 100.0,
                               .startWeight = 50.0,
                               .midpointWeight = 50.0,
                               .midpoint = {.range = 50.0, .depth = 500.0}}};

  const RayPath pathCopy = path;
  const RayFrequencyState state50 = projector.project(path, 50.0, 1.0);
  const RayFrequencyState state250 = projector.project(path, 250.0, 1.0);

  context.check(path.points.size() == pathCopy.points.size(),
                "PCHIP projection preserves ray point count");
  context.check(state50.frequency == 50.0 && state250.frequency == 250.0,
                "projected states retain distinct frequencies");
  context.check(
      state50.points.back().complexTravelTime.imag() < 0.0,
      "attenuating PCHIP projection produces negative imaginary travel time");
  context.check(
      state50.points.back().complexTravelTime !=
          state250.points.back().complexTravelTime,
      "different frequencies produce different complex acoustic states");
}

Environment makeN2AttenuatedEnvironment() {
  return Environment(
      SoundSpeedProfile({{.depth = 0.0,
                          .soundSpeed = 1500.0,
                          .density = 1000.0,
                          .attenuation = thorpAttenuation()},
                         {.depth = 500.0,
                          .soundSpeed = 1480.0,
                          .density = 1100.0,
                          .attenuation = thorpAttenuation()},
                         {.depth = 1000.0,
                          .soundSpeed = 1520.0,
                          .density = 1200.0,
                          .attenuation = thorpAttenuation()}},
                        rayreuse::SspInterpolationKind::N2Linear),
      BoundaryModel::vacuum(0.0), BoundaryModel::rigid(1000.0));
}

// One frozen N² trajectory, projected at 50 Hz and 250 Hz through the shared
// frequency evaluator: the two RayFrequencyState instances stay independent
// and stable, the imaginary travel time keeps the attenuation sign
// convention, and the input path is untouched field by field.
void testN2FrequencyProjection(Context& context) {
  const Environment environment = makeN2AttenuatedEnvironment();
  const RayPath path =
      GeometryTracer(environment, IntegratorSettings{.stepLength = 100.0,
                                                     .rangeLimit = 5000.0,
                                                     .depthLimit = 3000.0,
                                                     .maximumRayPoints = 40U})
          .trace(Source{.depth = 250.0}, 20.0 * std::numbers::pi / 180.0);
  context.check(path.points.size() >= 10U,
                "N2 attenuated projection uses a nontrivial frozen path");
  context.check(!path.events.empty(),
                "N2 attenuated frozen path includes at least one reflection");

  const RayPath before = path;
  const FrequencyProjector projector(environment);
  RayFrequencyState state50 = projector.project(path, 50.0, 1.0);
  const RayFrequencyState state250 = projector.project(path, 250.0, 1.0);
  const RayFrequencyState repeat50 = projector.project(path, 50.0, 1.0);

  context.check(state50.frequency == 50.0 && state250.frequency == 250.0,
                "N2 projected states retain their own frequencies");
  context.check(state50.points.size() == path.points.size() &&
                    state250.points.size() == path.points.size(),
                "N2 projection stores one state per geometry point");
  context.check(state50.points.back().complexTravelTime.imag() < 0.0,
                "attenuating N2 50 Hz projection has negative imaginary "
                "travel time");
  context.check(state250.points.back().complexTravelTime.imag() < 0.0,
                "attenuating N2 250 Hz projection has negative imaginary "
                "travel time");
  context.check(
      std::abs(state250.points.back().complexTravelTime.imag() -
               state50.points.back().complexTravelTime.imag()) > 1.0e-9,
      "Thorp volume attenuation separates the two N2 frequency states");
  context.check(repeat50.points.back().complexTravelTime ==
                    state50.points.back().complexTravelTime,
                "repeating an N2 projection at one frequency is bit-stable");

  state50.points.front().amplitude = 0.25;
  context.checkNear(state250.points.front().amplitude, 1.0, 0.0,
                    "mutating one N2 frequency state leaves the other "
                    "untouched");

  context.check(path.launchAngle == before.launchAngle &&
                    path.points.size() == before.points.size() &&
                    path.steps.size() == before.steps.size() &&
                    path.events.size() == before.events.size() &&
                    path.terminationReason == before.terminationReason,
                "N2 projection preserves the path container and bookkeeping");
  for (std::size_t index = 0U; index < path.points.size(); ++index) {
    context.check(
        path.points[index].position == before.points[index].position &&
            path.points[index].slowness == before.points[index].slowness &&
            path.points[index].dynamicP == before.points[index].dynamicP &&
            path.points[index].dynamicQ == before.points[index].dynamicQ &&
            path.points[index].soundSpeed == before.points[index].soundSpeed &&
            path.points[index].realTravelTime ==
                before.points[index].realTravelTime,
        "N2 projection leaves every geometry point unchanged");
  }
  for (std::size_t index = 0U; index < path.steps.size(); ++index) {
    context.check(
        path.steps[index].stepLength == before.steps[index].stepLength &&
            path.steps[index].startWeight == before.steps[index].startWeight &&
            path.steps[index].midpointWeight ==
                before.steps[index].midpointWeight &&
            path.steps[index].midpoint == before.steps[index].midpoint,
        "N2 projection leaves every step quadrature unchanged");
  }
  for (std::size_t index = 0U; index < path.events.size(); ++index) {
    const ReflectionEvent& event = path.events[index];
    const ReflectionEvent& previous = before.events[index];
    context.check(
        event.rayPointIndex == previous.rayPointIndex &&
            event.reflectedRayPointIndex == previous.reflectedRayPointIndex &&
            event.boundary == previous.boundary &&
            event.boundarySegmentIndex == previous.boundarySegmentIndex &&
            event.boundaryCurvature == previous.boundaryCurvature &&
            event.position == previous.position &&
            event.boundaryTangent == previous.boundaryTangent &&
            event.outwardNormal == previous.outwardNormal &&
            event.incidentSlowness == previous.incidentSlowness &&
            event.reflectedSlowness == previous.reflectedSlowness &&
            event.tangentSlowness == previous.tangentSlowness &&
            event.normalSlowness == previous.normalSlowness,
        "N2 projection leaves every reflection event unchanged");
  }
}

// A lossless N² water column must reuse the frozen real travel time exactly
// instead of re-integrating a complex slowness.
void testN2LosslessProjectionReusesFrozenTravelTime(Context& context) {
  const Environment environment(
      SoundSpeedProfile(
          {{.depth = 0.0, .soundSpeed = 1500.0, .density = 1000.0},
           {.depth = 500.0, .soundSpeed = 1480.0, .density = 1100.0},
           {.depth = 1000.0, .soundSpeed = 1520.0, .density = 1200.0}},
          rayreuse::SspInterpolationKind::N2Linear),
      BoundaryModel::vacuum(0.0), BoundaryModel::rigid(1000.0));
  const RayPath path =
      GeometryTracer(environment, IntegratorSettings{.stepLength = 100.0,
                                                     .rangeLimit = 5000.0,
                                                     .depthLimit = 3000.0,
                                                     .maximumRayPoints = 40U})
          .trace(Source{.depth = 250.0}, 20.0 * std::numbers::pi / 180.0);

  const RayFrequencyState state =
      FrequencyProjector(environment).project(path, 100.0, 1.0);
  context.check(state.points.size() == path.points.size(),
                "lossless N2 projection stores one state per geometry point");
  for (std::size_t index = 0U; index < path.points.size(); ++index) {
    context.check(
        state.points[index].complexTravelTime ==
            std::complex<double>{path.points[index].realTravelTime, 0.0},
        "lossless N2 projection copies the frozen real travel time");
  }
}

// End-to-end spline leg of testN2FrequencyProjection: a real cubic-spline
// RayPath produced by the generic GeometryTracer (node crossings plus an
// acoustic reflection) is projected at two frequencies through the shared
// FrequencyProjector. The frozen geometry must survive field by field, and
// the per-frequency spline states must stay independent, repeatable, and
// free of cross-frequency contamination.
void testSplineFrozenPathProjection(Context& context) {
  const auto thorp = []() {
    return RawAttenuation{.value = 0.0,
                          .unit = AttenuationUnit::DecibelsPerWavelength,
                          .volumeModel = VolumeAttenuationModel::Thorp};
  };
  const Environment environment(
      SoundSpeedProfile({{.depth = 0.0,
                          .soundSpeed = 1500.0,
                          .density = 1000.0,
                          .attenuation = thorp()},
                         {.depth = 500.0,
                          .soundSpeed = 1480.0,
                          .density = 1100.0,
                          .attenuation = thorp()},
                         {.depth = 1000.0,
                          .soundSpeed = 1520.0,
                          .density = 1200.0,
                          .attenuation = thorp()}},
                        rayreuse::SspInterpolationKind::CubicSpline),
      BoundaryModel::vacuum(0.0), BoundaryModel::rigid(1000.0));
  const RayPath path =
      GeometryTracer(environment, IntegratorSettings{.stepLength = 100.0,
                                                     .rangeLimit = 5000.0,
                                                     .depthLimit = 3000.0,
                                                     .maximumRayPoints = 40U})
          .trace(Source{.depth = 250.0}, 20.0 * std::numbers::pi / 180.0);
  context.check(path.points.size() >= 10U,
                "spline frozen projection uses a nontrivial traced path");
  context.check(!path.events.empty(),
                "spline frozen path includes at least one reflection");

  const RayPath before = path;
  const FrequencyProjector projector(environment);
  RayFrequencyState state50 = projector.project(path, 50.0, 1.0);
  const RayFrequencyState state250 = projector.project(path, 250.0, 1.0);
  const RayFrequencyState repeat50 = projector.project(path, 50.0, 1.0);

  context.check(state50.frequency == 50.0 && state250.frequency == 250.0,
                "spline projected states retain their own frequencies");
  context.check(state50.points.size() == path.points.size() &&
                    state250.points.size() == path.points.size(),
                "spline projection stores one state per geometry point");
  context.check(state50.points.back().complexTravelTime.imag() < 0.0,
                "attenuating spline 50 Hz projection has negative imaginary "
                "travel time");
  context.check(state250.points.back().complexTravelTime.imag() < 0.0,
                "attenuating spline 250 Hz projection has negative imaginary "
                "travel time");
  context.check(
      std::abs(state250.points.back().complexTravelTime.imag() -
               state50.points.back().complexTravelTime.imag()) > 1.0e-9,
      "Thorp volume attenuation separates the two spline frequency states");
  context.check(repeat50.points.back().complexTravelTime ==
                    state50.points.back().complexTravelTime,
                "repeating a spline projection at one frequency is bit-stable");

  state50.points.front().amplitude = 0.25;
  context.checkNear(state250.points.front().amplitude, 1.0, 0.0,
                    "mutating one spline frequency state leaves the other "
                    "untouched");

  context.check(path.launchAngle == before.launchAngle &&
                    path.points.size() == before.points.size() &&
                    path.steps.size() == before.steps.size() &&
                    path.events.size() == before.events.size() &&
                    path.terminationReason == before.terminationReason,
                "spline projection preserves the path container and "
                "bookkeeping");
  for (std::size_t index = 0U; index < path.points.size(); ++index) {
    context.check(
        path.points[index].position == before.points[index].position &&
            path.points[index].slowness == before.points[index].slowness &&
            path.points[index].dynamicP == before.points[index].dynamicP &&
            path.points[index].dynamicQ == before.points[index].dynamicQ &&
            path.points[index].soundSpeed == before.points[index].soundSpeed &&
            path.points[index].realTravelTime ==
                before.points[index].realTravelTime,
        "spline projection leaves every geometry point unchanged");
  }
  for (std::size_t index = 0U; index < path.steps.size(); ++index) {
    context.check(
        path.steps[index].stepLength == before.steps[index].stepLength &&
            path.steps[index].startWeight == before.steps[index].startWeight &&
            path.steps[index].midpointWeight ==
                before.steps[index].midpointWeight &&
            path.steps[index].midpoint == before.steps[index].midpoint,
        "spline projection leaves every step quadrature unchanged");
  }
  for (std::size_t index = 0U; index < path.events.size(); ++index) {
    const ReflectionEvent& event = path.events[index];
    const ReflectionEvent& previous = before.events[index];
    context.check(
        event.rayPointIndex == previous.rayPointIndex &&
            event.reflectedRayPointIndex == previous.reflectedRayPointIndex &&
            event.boundary == previous.boundary &&
            event.boundarySegmentIndex == previous.boundarySegmentIndex &&
            event.boundaryCurvature == previous.boundaryCurvature &&
            event.position == previous.position &&
            event.boundaryTangent == previous.boundaryTangent &&
            event.outwardNormal == previous.outwardNormal &&
            event.incidentSlowness == previous.incidentSlowness &&
            event.reflectedSlowness == previous.reflectedSlowness &&
            event.tangentSlowness == previous.tangentSlowness &&
            event.normalSlowness == previous.normalSlowness,
        "spline projection leaves every reflection event unchanged");
  }
}

// FP-2E G02 quadrilateral leg of the frozen-path projection contract (the
// testSplineFrozenPathProjection pattern): a real Q RayPath from the generic
// GeometryTracer — crossing the 350 m range node of the shared cross-gradient
// grid and reflecting off boundaries — is projected at two frequencies. Q real
// c(r,z) may only act during the geometry trace, so both projections must
// leave the frozen path unchanged field by field, keep the per-frequency
// states independent, and repeat bit-stably.
void testQuadrilateralFrozenPathProjection(Context& context) {
  const Environment environment(
      SoundSpeedProfile({{.depth = 0.0,
                          .soundSpeed = 1500.0,
                          .density = 1000.0,
                          .attenuation = thorpAttenuation()},
                         {.depth = 100.0,
                          .soundSpeed = 1500.0,
                          .density = 1100.0,
                          .attenuation = thorpAttenuation()}},
                        rayreuse::SspInterpolationKind::Quadrilateral,
                        std::make_shared<const rayreuse::QuadrilateralSspGrid>(
                            rayreuse::QuadrilateralSspGrid{
                                .rangesMeters = {0.0, 350.0, 800.0},
                                .speedsDepthMajor = {1500.0, 1540.0, 1580.0,
                                                     1500.0, 1520.0, 1540.0},
                                .depthCount = 2U,
                                .rangeCount = 3U})),
      BoundaryModel::vacuum(0.0), BoundaryModel::rigid(100.0));
  const RayPath path =
      GeometryTracer(environment, IntegratorSettings{.stepLength = 50.0,
                                                     .rangeLimit = 400.0,
                                                     .depthLimit = 2000.0,
                                                     .maximumRayPoints = 64U})
          .trace(Source{.depth = 50.0}, 20.0 * std::numbers::pi / 180.0);
  context.check(path.points.size() >= 10U,
                "quadrilateral frozen projection uses a nontrivial traced "
                "path");
  context.check(!path.events.empty(),
                "quadrilateral frozen path includes at least one reflection");
  context.check(
      path.terminationReason == rayreuse::RayTerminationReason::ExitedDomain,
      "quadrilateral frozen path exits the spatial box");
  bool crossedRangeNode = false;
  for (const RayState& point : path.points) {
    if (point.position.range > 350.0) {
      crossedRangeNode = true;
      break;
    }
  }
  context.check(crossedRangeNode,
                "quadrilateral frozen path crosses the 350 m range node");

  const RayPath before = path;
  const FrequencyProjector projector(environment);
  RayFrequencyState state1000 = projector.project(path, 1000.0, 1.0);
  const RayFrequencyState state2000 = projector.project(path, 2000.0, 1.0);
  const RayFrequencyState repeat1000 = projector.project(path, 1000.0, 1.0);

  context.check(state1000.frequency == 1000.0 && state2000.frequency == 2000.0,
                "quadrilateral projected states retain their own frequencies");
  context.check(state1000.points.size() == path.points.size() &&
                    state2000.points.size() == path.points.size(),
                "quadrilateral projection stores one state per geometry point");
  context.check(state1000.points.back().complexTravelTime.imag() < 0.0,
                "attenuating quadrilateral 1000 Hz projection has negative "
                "imaginary travel time");
  context.check(state2000.points.back().complexTravelTime.imag() < 0.0,
                "attenuating quadrilateral 2000 Hz projection has negative "
                "imaginary travel time");
  context.check(
      std::abs(state2000.points.back().complexTravelTime.imag() -
               state1000.points.back().complexTravelTime.imag()) > 1.0e-9,
      "Thorp reference attenuation separates the two quadrilateral frequency "
      "states");
  context.check(
      repeat1000.points.back().complexTravelTime ==
          state1000.points.back().complexTravelTime,
      "repeating a quadrilateral projection at one frequency is bit-stable");

  state1000.points.front().amplitude = 0.25;
  context.checkNear(state2000.points.front().amplitude, 1.0, 0.0,
                    "mutating one quadrilateral frequency state leaves the "
                    "other untouched");

  context.check(path.launchAngle == before.launchAngle &&
                    path.points.size() == before.points.size() &&
                    path.steps.size() == before.steps.size() &&
                    path.events.size() == before.events.size() &&
                    path.terminationReason == before.terminationReason,
                "quadrilateral projection preserves the path container and "
                "bookkeeping");
  for (std::size_t index = 0U; index < path.points.size(); ++index) {
    context.check(
        path.points[index].position == before.points[index].position &&
            path.points[index].slowness == before.points[index].slowness &&
            path.points[index].dynamicP == before.points[index].dynamicP &&
            path.points[index].dynamicQ == before.points[index].dynamicQ &&
            path.points[index].soundSpeed == before.points[index].soundSpeed &&
            path.points[index].realTravelTime ==
                before.points[index].realTravelTime,
        "quadrilateral projection leaves every geometry point unchanged");
  }
  for (std::size_t index = 0U; index < path.steps.size(); ++index) {
    context.check(
        path.steps[index].stepLength == before.steps[index].stepLength &&
            path.steps[index].startWeight == before.steps[index].startWeight &&
            path.steps[index].midpointWeight ==
                before.steps[index].midpointWeight &&
            path.steps[index].midpoint == before.steps[index].midpoint,
        "quadrilateral projection leaves every step quadrature unchanged");
  }
  for (std::size_t index = 0U; index < path.events.size(); ++index) {
    const ReflectionEvent& event = path.events[index];
    const ReflectionEvent& previous = before.events[index];
    context.check(
        event.rayPointIndex == previous.rayPointIndex &&
            event.reflectedRayPointIndex == previous.reflectedRayPointIndex &&
            event.boundary == previous.boundary &&
            event.boundarySegmentIndex == previous.boundarySegmentIndex &&
            event.boundaryCurvature == previous.boundaryCurvature &&
            event.position == previous.position &&
            event.boundaryTangent == previous.boundaryTangent &&
            event.outwardNormal == previous.outwardNormal &&
            event.incidentSlowness == previous.incidentSlowness &&
            event.reflectedSlowness == previous.reflectedSlowness &&
            event.tangentSlowness == previous.tangentSlowness &&
            event.normalSlowness == previous.normalSlowness,
        "quadrilateral projection leaves every reflection event unchanged");
  }
}

// FrequencyProjector consumes FrequencySspEvaluator, whose variant gains the
// spline backend only in G01, so the projector-level frozen-path check for
// spline (the testN2FrequencyProjection pattern, field by field) lands with
// G02 end-to-end validation. This test pins the evaluator-level contract the
// projector relies on today: two per-frequency spline evaluators over one
// frozen trajectory are fully independent, repeatable, and free of
// cross-frequency contamination. The full traced-path frozen check is
// testSplineFrozenPathProjection above.
void testSplineFrequencyStateIndependence(Context& context) {
  const auto thorp = []() {
    return RawAttenuation{.value = 0.0,
                          .unit = AttenuationUnit::DecibelsPerWavelength,
                          .volumeModel = VolumeAttenuationModel::Thorp};
  };
  const SoundSpeedProfile profile({{.depth = 0.0,
                                    .soundSpeed = 1500.0,
                                    .density = 1000.0,
                                    .attenuation = thorp()},
                                   {.depth = 500.0,
                                    .soundSpeed = 1480.0,
                                    .density = 1100.0,
                                    .attenuation = thorp()},
                                   {.depth = 1000.0,
                                    .soundSpeed = 1520.0,
                                    .density = 1200.0,
                                    .attenuation = thorp()}});

  const CubicSplineFrequencySsp low(profile, 50.0);
  const CubicSplineFrequencySsp high(profile, 250.0);
  context.check(!low.isLossless() && !high.isLossless(),
                "Thorp spline evaluators are lossy at both frequencies");
  context.check(!low.uniformComplexSoundSpeed().has_value(),
                "Thorp spline evaluators keep the general path");

  // A frozen deterministic stand-in for the traced trajectory: arrival-side
  // segment hints plus query depths, projected at both frequencies.
  const std::vector<std::pair<std::size_t, double>> trajectory{
      {0U, 60.0}, {0U, 180.0}, {0U, 300.0}, {1U, 620.0}, {1U, 940.0}};

  for (const auto& [segment, depth] : trajectory) {
    const Vec2 position{.range = 0.0, .depth = depth};
    const auto lowState = low.evaluateAtSegment(position, segment);
    const auto highState = high.evaluateAtSegment(position, segment);
    const auto repeatedLow = low.evaluateAtSegment(position, segment);

    context.check(
        lowState.soundSpeed == highState.soundSpeed &&
            lowState.soundSpeedGradient == highState.soundSpeedGradient &&
            lowState.soundSpeedHessian == highState.soundSpeedHessian &&
            lowState.density == highState.density &&
            lowState.segmentIndex == highState.segmentIndex,
        "spline real observables are bit-identical across frequencies");
    context.check(lowState.imaginarySoundSpeed != highState.imaginarySoundSpeed,
                  "Thorp spline imaginary states separate 50 Hz from 250 Hz");
    context.check(
        repeatedLow.soundSpeed == lowState.soundSpeed &&
            repeatedLow.imaginarySoundSpeed == lowState.imaginarySoundSpeed &&
            repeatedLow.soundSpeedGradient == lowState.soundSpeedGradient &&
            repeatedLow.soundSpeedHessian == lowState.soundSpeedHessian,
        "repeating a spline projection at one frequency is bit-stable");
  }

  // The imaginary travel-time sign convention on a representative projected
  // step: Thorp attenuation yields a negative imaginary contribution.
  const auto attenuated =
      low.evaluateAtSegment(Vec2{.range = 0.0, .depth = 620.0}, 1U);
  context.check(attenuated.imaginarySoundSpeed > 0.0,
                "converted Thorp node attenuation keeps the positive "
                "imaginary sound speed convention");
}

}  // namespace

int main() {
  Context context;
  testLosslessAndThorpPropagation(context);
  testNodeConversionPrecedesInterpolation(context);
  testVacuumRigidPhaseIsUnwrapped(context);
  testAcousticReflectionAndActiveCutoff(context);
  testMunkReflectionOracle(context);
  testProjectionDoesNotMutateGeometry(context);
  testEnvironmentVolumeAttenuationProjection(context);
  testPchipFrequencyProjection(context);
  testN2FrequencyProjection(context);
  testN2LosslessProjectionReusesFrozenTravelTime(context);
  testSplineFrozenPathProjection(context);
  testQuadrilateralFrozenPathProjection(context);
  testSplineFrequencyStateIndependence(context);
  testInvalidInputs(context);

  if (context.failureCount() != 0) {
    std::cerr << context.failureCount()
              << " frequency-projector assertion(s) failed\n";
    return 1;
  }
  std::cout << "All Bellhop RayReuse frequency-projector tests passed\n";
  return 0;
}
