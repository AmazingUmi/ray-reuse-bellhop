#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <iostream>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "bellhop/acoustics/attenuation.hpp"
#include "bellhop/acoustics/boundary_acoustics.hpp"
#include "bellhop/cache/ray_path_cache.hpp"
#include "bellhop/error.hpp"
#include "bellhop/field/frequency_projector.hpp"
#include "bellhop/model/c_linear_frequency_ssp.hpp"
#include "bellhop/ray/geometry_tracer.hpp"
#include "support/boundary_acoustics_fixture.hpp"
#include "support/munk_case_fixture.hpp"
#include "support/test_harness.hpp"

namespace {

using bellhop::AcousticMaterial;
using bellhop::AttenuationUnit;
using bellhop::BiologicalAttenuationLayer;
using bellhop::BiologicalAttenuationLayers;
using bellhop::BoundaryAcousticsResult;
using bellhop::BoundaryGeometry;
using bellhop::BoundaryKind;
using bellhop::BoundaryModel;
using bellhop::BoundaryOrientation;
using bellhop::CLinearFrequencySsp;
using bellhop::Environment;
using bellhop::FrequencyProjector;
using bellhop::FrozenBoundaryMaterial;
using bellhop::FrequencyWorkspace;
using bellhop::GeometryTracer;
using bellhop::IntegratorSettings;
using bellhop::RayFrequencyState;
using bellhop::RayPath;
using bellhop::RayPathCache;
using bellhop::RayState;
using bellhop::RawAttenuation;
using bellhop::ReflectionBoundary;
using bellhop::ReflectionEvent;
using bellhop::ReceiverGrid;
using bellhop::QuadrilateralSspGrid;
using bellhop::SoundSpeedPoint;
using bellhop::SoundSpeedProfile;
using bellhop::Source;
using bellhop::SspInterpolationKind;
using bellhop::StepQuadrature;
using bellhop::TabulatedReflectionTable;
using bellhop::ValidationError;
using bellhop::Vec2;
using bellhop::VolumeAttenuation;
using bellhop::VolumeAttenuationModel;
using bellhop::convertAttenuation;
using bellhop::evaluateAcousticHalfSpaceAcoustics;
using bellhop::evaluateFluidHalfSpaceAcoustics;
using bellhop::evaluateGrainSizeHalfSpaceAcoustics;
using bellhop::evaluateTabulatedReflectionAcoustics;
using bellhop::test::Context;

constexpr double kSoundSpeed = 1500.0;
constexpr double kWaterDensity = 1000.0;
constexpr double kLegacyActiveThreshold =
    static_cast<double>(0.005F);

VolumeAttenuation thorpVolumeAttenuation() {
  return VolumeAttenuation{
      .model = VolumeAttenuationModel::Thorp};
}

VolumeAttenuation biologicalVolumeAttenuation() {
  return VolumeAttenuation{
      .model = VolumeAttenuationModel::Biological,
      .parameters =
          std::make_shared<const BiologicalAttenuationLayers>(
              BiologicalAttenuationLayers{
                  BiologicalAttenuationLayer{
                      .minimumDepth = 0.0,
                      .maximumDepth = 100.0,
                      .resonanceFrequency = 5000.0,
                      .qualityFactor = 10.0,
                      .attenuationCoefficientDecibelsPerKilometer =
                          0.25}}),
  };
}

Environment makeConstantEnvironment(
    RawAttenuation waterAttenuation = {},
    BoundaryModel seabed = BoundaryModel::rigid(100.0),
    VolumeAttenuation volumeAttenuation = {}) {
  return Environment(
      SoundSpeedProfile(
          {{.depth = 0.0,
            .soundSpeed = kSoundSpeed,
            .density = kWaterDensity,
            .attenuation = waterAttenuation},
           {.depth = 100.0,
            .soundSpeed = kSoundSpeed,
            .density = kWaterDensity,
            .attenuation = waterAttenuation}}),
      BoundaryModel::vacuum(0.0), std::move(seabed),
      std::move(volumeAttenuation));
}

RayState makeRayState(Vec2 position, Vec2 slowness,
                      double realTravelTime = 0.0) {
  return RayState{
      .position = position,
      .slowness = slowness,
      .dynamicP = {1.0, 0.0},
      .dynamicQ = {0.0, 1.0},
      .soundSpeed = kSoundSpeed,
      .realTravelTime = realTravelTime};
}

RayPath makeSingleStepPath(double length, double depth) {
  RayPath path;
  path.points = {
      makeRayState({.range = 0.0, .depth = depth},
                   {.range = 1.0 / kSoundSpeed, .depth = 0.0}),
      makeRayState({.range = length, .depth = depth},
                   {.range = 1.0 / kSoundSpeed, .depth = 0.0},
                   length / kSoundSpeed)};
  path.steps = {
      StepQuadrature{
          .stepLength = length,
          .startWeight = 0.0,
          .midpointWeight = length,
          .midpoint = {.range = 0.5 * length, .depth = depth}}};
  return path;
}

ReflectionEvent makeReflectionEvent(
    std::size_t pointIndex, ReflectionBoundary boundary,
    Vec2 position, double tangentSlowness,
    double normalSlowness) {
  const Vec2 tangent{.range = 1.0, .depth = 0.0};
  const Vec2 normal{
      .range = 0.0,
      .depth =
          boundary == ReflectionBoundary::SeaSurface ? -1.0 : 1.0};
  const Vec2 incident =
      tangentSlowness * tangent + normalSlowness * normal;
  const Vec2 reflected =
      tangentSlowness * tangent - normalSlowness * normal;
  return ReflectionEvent{
      .rayPointIndex = pointIndex,
      .reflectedRayPointIndex = pointIndex + 1U,
      .boundary = boundary,
      .boundarySegmentIndex = 0U,
      .boundaryCurvature = 0.0,
      .position = position,
      .boundaryTangent = tangent,
      .outwardNormal = normal,
      .incidentSlowness = incident,
      .reflectedSlowness = reflected,
      .tangentSlowness = tangentSlowness,
      .normalSlowness = normalSlowness,
      .longMaterialOverride = std::nullopt};
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

  const Environment losslessEnvironment =
      makeConstantEnvironment();
  const CLinearFrequencySsp losslessProfile(
      losslessEnvironment.soundSpeedProfile(), 5000.0);
  context.check(losslessProfile.isLossless(),
                "zero-attenuation frequency SSP is lossless");
  context.check(
      losslessProfile.uniformComplexSoundSpeed().has_value(),
      "constant lossless frequency SSP exposes a uniform value");
  const RayFrequencyState lossless =
      FrequencyProjector(losslessEnvironment)
          .project(path, 5000.0, 0.75);
  context.check(lossless.points.size() == path.points.size(),
                "lossless projection has one state per geometry point");
  checkComplexNear(
      context, lossless.points.back().complexTravelTime,
      {10000.0 / kSoundSpeed, 0.0}, 1.0e-15,
      "lossless projection reproduces real travel time");
  context.checkNear(lossless.points.back().amplitude, 0.75, 0.0,
                    "lossless propagation preserves source amplitude");
  context.checkNear(lossless.points.back().reflectionPhase, 0.0, 0.0,
                    "lossless direct ray has zero reflection phase");
  context.check(lossless.points.back().active,
                "ordinary direct ray remains active");

  const Environment thorpEnvironment =
      makeConstantEnvironment(
          {}, BoundaryModel::rigid(100.0),
          thorpVolumeAttenuation());
  const CLinearFrequencySsp thorpProfile(
      thorpEnvironment.soundSpeedProfile(), 5000.0,
      thorpEnvironment.volumeAttenuation());
  context.check(!thorpProfile.isLossless(),
                "Thorp frequency SSP remains lossy");
  context.check(
      thorpProfile.uniformComplexSoundSpeed().has_value(),
      "constant Thorp frequency SSP exposes a uniform value");
  const RayFrequencyState thorp =
      FrequencyProjector(thorpEnvironment)
          .project(path, 5000.0, 1.0);
  // Independent constant-speed evaluation of the 5 kHz Fortran Thorp anchor.
  const std::complex<double> expected{
      6.6666666666370800, -1.4044361562126921e-5};
  checkComplexNear(context, thorp.points.back().complexTravelTime,
                   expected, 2.0e-15,
                   "5 kHz Thorp complex travel time");
  context.check(
      thorp.points.back().complexTravelTime.real() !=
          path.points.back().realTravelTime,
      "lossy projection recomputes rather than copies tau real");
}

void testNodeConversionPrecedesInterpolation(Context& context) {
  SoundSpeedProfile profile(
      {{.depth = 0.0,
        .soundSpeed = 1400.0,
        .density = 1000.0,
        .attenuation = {}},
       {.depth = 1000.0,
        .soundSpeed = 1800.0,
        .density = 1200.0,
        .attenuation = {}}});
  const CLinearFrequencySsp frequencyProfile(
      profile, 5000.0, thorpVolumeAttenuation());
  context.check(
      !frequencyProfile.uniformComplexSoundSpeed().has_value(),
      "nonuniform frequency SSP retains the general interpolation path");
  const auto sample = frequencyProfile.evaluateAtSegment(
      {.range = 0.0, .depth = 250.0}, 0U);
  const double nodeFirst =
      0.75 *
          convertAttenuation(
              profile.points()[0U].attenuation,
              thorpVolumeAttenuation(), 5000.0, 1400.0, 0.0)
              .imaginarySoundSpeed +
      0.25 *
          convertAttenuation(
              profile.points()[1U].attenuation,
              thorpVolumeAttenuation(), 5000.0, 1800.0, 1000.0)
              .imaginarySoundSpeed;
  const double convertAfterInterpolation =
      convertAttenuation(
          profile.points()[0U].attenuation,
          thorpVolumeAttenuation(), 5000.0, 1500.0, 250.0)
          .imaginarySoundSpeed;
  context.checkNear(sample.soundSpeed, 1500.0, 0.0,
                    "frequency SSP preserves C-linear real speed");
  context.checkNear(sample.imaginarySoundSpeed, nodeFirst, 1.0e-18,
                    "frequency SSP interpolates converted node cimag");
  context.check(
      std::abs(sample.imaginarySoundSpeed -
               convertAfterInterpolation) > 1.0e-5,
      "node-first contract is distinguishable from query-point conversion");
  context.checkNear(sample.density, 1050.0, 0.0,
                    "frequency SSP preserves linear density");

  Environment environment(
      std::move(profile), BoundaryModel::vacuum(0.0),
      BoundaryModel::rigid(1000.0), thorpVolumeAttenuation());
  RayPath path;
  path.points = {
      makeRayState({.range = 0.0, .depth = 0.0},
                   {.range = 1.0 / 1400.0, .depth = 0.0}),
      makeRayState({.range = 1000.0, .depth = 500.0},
                   {.range = 1.0 / 1500.0, .depth = 0.0})};
  path.steps = {
      StepQuadrature{
          .stepLength = 1000.0,
          .startWeight = 400.0,
          .midpointWeight = 600.0,
          .midpoint = {.range = 500.0, .depth = 250.0}}};
  const RayFrequencyState projected =
      FrequencyProjector(environment).project(path, 5000.0, 1.0);
  checkComplexNear(
      context, projected.points.back().complexTravelTime,
      {6.8571428571135828e-1, -1.4156716454626132e-6},
      2.0e-16,
      "projector consumes node-first C-linear complex sound speed");
}

void testQuadrilateralFrequencyProjection(Context& context) {
  const auto grid = std::make_shared<const QuadrilateralSspGrid>(
      QuadrilateralSspGrid{
          .rangesMeters = {0.0, 350.0, 750.0},
          .speedsDepthMajor = {1500.0, 1540.0, 1580.0,
                               1500.0, 1520.0, 1540.0},
          .depthCount = 2U,
          .rangeCount = 3U});
  const SoundSpeedProfile profile(
      {{.depth = 0.0,
        .soundSpeed = 1400.0,
        .density = 1000.0,
        .attenuation = {
            .value = 0.05,
            .unit = AttenuationUnit::DecibelsPerWavelength}},
       {.depth = 100.0,
        .soundSpeed = 1800.0,
        .density = 1200.0,
        .attenuation = {
            .value = 0.10,
            .unit = AttenuationUnit::DecibelsPerWavelength}}},
      SspInterpolationKind::Quadrilateral, grid);
  const VolumeAttenuation thorp = thorpVolumeAttenuation();
  const Environment environment(
      profile, BoundaryModel::vacuum(0.0),
      BoundaryModel::rigid(100.0), thorp);
  RayPathCache cache;
  cache.append(makeSingleStepPath(350.0, 50.0));
  cache.freeze();
  const RayPath& path = cache.at(0U);
  const RayPath before = path;
  const FrequencyProjector projector(environment);

  std::array<std::complex<double>, 2U> projected{};
  std::size_t frequencyIndex = 0U;
  for (const double frequency : {1000.0, 5000.0}) {
    const double topImaginary =
        convertAttenuation(profile.points()[0U].attenuation, thorp,
                           frequency, 1400.0, 0.0)
            .imaginarySoundSpeed;
    const double bottomImaginary =
        convertAttenuation(profile.points()[1U].attenuation, thorp,
                           frequency, 1800.0, 100.0)
            .imaginarySoundSpeed;
    const double midpointImaginary =
        0.5 * topImaginary + 0.5 * bottomImaginary;
    const std::complex<double> expected =
        350.0 / std::complex<double>{1515.0, midpointImaginary};
    projected[frequencyIndex] =
        projector.project(path, frequency, 1.0)
            .points.back()
            .complexTravelTime;
    checkComplexNear(context, projected[frequencyIndex], expected, 2.0e-16,
                     "projector consumes Q real matrix and ENV-node loss");
    ++frequencyIndex;
  }
  context.check(projected[0U] != projected[1U],
                "Q projection reconstructs attenuation per frequency");
  context.check(
      cache.frozen() && path.points.size() == before.points.size() &&
          path.steps.size() == before.steps.size() &&
          path.events.size() == before.events.size() &&
          path.points.front().position == before.points.front().position &&
          path.points.back().position == before.points.back().position &&
          path.steps.front().midpoint == before.steps.front().midpoint &&
          path.terminationReason == before.terminationReason &&
          path.terminationDetail == before.terminationDetail,
      "two-frequency Q projection preserves the frozen geometry cache");
}

void testAttenuationUnitFrequencyProjection(Context& context) {
  struct UnitFixture {
    AttenuationUnit unit;
    double value;
  };
  constexpr std::array fixtures{
      UnitFixture{AttenuationUnit::NepersPerMeter, 1.0e-5},
      UnitFixture{AttenuationUnit::DecibelsPerMeterKilohertz,
                  8.6858896e-5},
      UnitFixture{AttenuationUnit::DecibelsPerMeter, 8.6858896e-5},
      UnitFixture{AttenuationUnit::DecibelsPerWavelength,
                  1.30288344e-4},
      UnitFixture{AttenuationUnit::QualityFactor,
                  209439.5102393195},
      UnitFixture{AttenuationUnit::LossParameter,
                  2.3873241463784303e-6},
  };
  const RayPath path = makeSingleStepPath(1000.0, 50.0);
  std::array<std::complex<double>, fixtures.size()> atOneKilohertz{};
  std::array<std::complex<double>, fixtures.size()> atTwoKilohertz{};
  for (std::size_t index = 0U; index < fixtures.size(); ++index) {
    const RawAttenuation raw{
        .value = fixtures[index].value,
        .unit = fixtures[index].unit,
    };
    const FrequencyProjector projector(makeConstantEnvironment(raw));
    atOneKilohertz[index] =
        projector.project(path, 1000.0, 1.0)
            .points.back()
            .complexTravelTime;
    atTwoKilohertz[index] =
        projector.project(path, 2000.0, 1.0)
            .points.back()
            .complexTravelTime;
    checkComplexNear(context, atOneKilohertz[index], atOneKilohertz[0U],
                     2.0e-18,
                     "six units agree at their 1 kHz reference anchor");
  }

  checkComplexNear(context, atTwoKilohertz[2U], atTwoKilohertz[0U],
                   2.0e-18, "N and M remain frequency independent");
  for (const std::size_t index : {1U, 3U, 4U, 5U}) {
    checkComplexNear(context, atTwoKilohertz[index], atTwoKilohertz[1U],
                     2.0e-18,
                     "F, W, Q, and L retain linear frequency scaling");
  }
  context.check(
      std::abs(atTwoKilohertz[0U] - atTwoKilohertz[1U]) > 1.0e-8,
      "2 kHz projection distinguishes frequency-independent and linear units");
}

void testVacuumRigidPhaseIsUnwrapped(Context& context) {
  const Environment environment = makeConstantEnvironment();
  const GeometryTracer tracer(
      environment,
      IntegratorSettings{
          .stepLength = 100.0,
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
    const auto& reflected =
        state.points[event.rayPointIndex + 1U];
    context.checkNear(
        reflected.reflectionPhase,
        static_cast<double>(vacuumCount) * std::numbers::pi,
        1.0e-14,
        "vacuum adds pi while rigid adds zero without wrapping");
    context.checkNear(reflected.amplitude, 1.0, 0.0,
                      "vacuum and rigid reflections preserve amplitude");
    context.check(reflected.active,
                  "lossless reflected state remains active");
  }
  context.check(vacuumCount >= 2U,
                "phase test reaches multiple vacuum reflections");
  context.check(
      state.points.back().reflectionPhase >
          2.0 * std::numbers::pi,
      "stored cumulative reflection phase exceeds one wrapped turn");
}

RayPath makeAcousticReflectionPath() {
  const auto& fixture =
      bellhop::test::kHalfSpaceCoefficientFixtures[1U];
  const Vec2 position{.range = 25.0, .depth = 100.0};
  const ReflectionEvent event = makeReflectionEvent(
      0U, ReflectionBoundary::Seabed, position,
      fixture.tangentSlowness, fixture.outwardNormalSlowness);
  RayPath path;
  path.points = {
      makeRayState(position, event.incidentSlowness),
      makeRayState(position, event.reflectedSlowness)};
  path.events = {event};
  return path;
}

RayPath makeRepeatedAcousticReflectionPath() {
  const auto& fixture =
      bellhop::test::kHalfSpaceCoefficientFixtures[1U];
  const Vec2 bottom{.range = 0.0, .depth = 100.0};
  const Vec2 surface{.range = 100.0, .depth = 0.0};
  const ReflectionEvent bottom0 = makeReflectionEvent(
      0U, ReflectionBoundary::Seabed, bottom,
      fixture.tangentSlowness, fixture.outwardNormalSlowness);
  const ReflectionEvent surface0 = makeReflectionEvent(
      2U, ReflectionBoundary::SeaSurface, surface,
      fixture.tangentSlowness, fixture.outwardNormalSlowness);
  const ReflectionEvent bottom1 = makeReflectionEvent(
      4U, ReflectionBoundary::Seabed, bottom,
      fixture.tangentSlowness, fixture.outwardNormalSlowness);
  const ReflectionEvent surface1 = makeReflectionEvent(
      6U, ReflectionBoundary::SeaSurface, surface,
      fixture.tangentSlowness, fixture.outwardNormalSlowness);
  const ReflectionEvent bottom2 = makeReflectionEvent(
      8U, ReflectionBoundary::Seabed, bottom,
      fixture.tangentSlowness, fixture.outwardNormalSlowness);

  RayPath path;
  path.points = {
      makeRayState(bottom, bottom0.incidentSlowness),
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
  path.steps = {
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
                     .midpoint = {.range = 50.0, .depth = 50.0}},
      StepQuadrature{.stepLength = 100.0,
                     .startWeight = 0.0,
                     .midpointWeight = 100.0,
                     .midpoint = {.range = 50.0, .depth = 50.0}}};
  return path;
}

Environment makeAcousticBottomEnvironment() {
  const auto& fixture =
      bellhop::test::kHalfSpaceCoefficientFixtures[1U];
  return makeConstantEnvironment(
      {},
      BoundaryModel::acousticHalfSpace(
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
                       AttenuationUnit::DecibelsPerWavelength}}));
}

Environment makeElasticBottomEnvironment() {
  return makeConstantEnvironment(
      {},
      BoundaryModel::acousticHalfSpace(
          100.0,
          AcousticMaterial{
              .compressionalSoundSpeed = 2000.0,
              .shearSoundSpeed = 1000.0,
              .density = 2000.0,
              .compressionalAttenuation = {
                  .value = 0.5,
                  .unit = AttenuationUnit::DecibelsPerWavelength},
              .shearAttenuation = {
                  .value = 1.0,
                  .unit = AttenuationUnit::DecibelsPerWavelength}}),
      thorpVolumeAttenuation());
}

void testElasticEnvironmentProjection(Context& context) {
  const Environment environment = makeElasticBottomEnvironment();
  RayPathCache cache;
  cache.append(makeAcousticReflectionPath());
  cache.freeze();
  const RayPath& path = cache.at(0U);
  const RayPath before = path;
  const ReflectionEvent& event = path.events.front();
  context.check(!event.longMaterialOverride.has_value(),
                "ordinary elastic bottom remains an Environment material");

  const FrequencyProjector projector(environment);
  std::array<RayFrequencyState, 2U> projected{
      projector.project(path, 1000.0, 1.0),
      projector.project(path, 2000.0, 1.0)};
  for (std::size_t index = 0U; index < projected.size(); ++index) {
    const double frequency = index == 0U ? 1000.0 : 2000.0;
    const auto expected = evaluateAcousticHalfSpaceAcoustics(
        *environment.seabed().material(), environment.seabed().depth(),
        environment.volumeAttenuation(), frequency, kWaterDensity,
        event.tangentSlowness, event.normalSlowness);
    context.checkNear(projected[index].points.back().amplitude,
                      expected.amplitudeMultiplier, 2.0e-15,
                      "projector applies ordinary elastic amplitude");
    context.checkNear(projected[index].points.back().reflectionPhase,
                      expected.phaseIncrement, 2.0e-15,
                      "projector applies ordinary elastic phase");
  }
  context.check(
      projected[0U].points.back().amplitude !=
              projected[1U].points.back().amplitude ||
          projected[0U].points.back().reflectionPhase !=
              projected[1U].points.back().reflectionPhase,
      "elastic P/S attenuation is evaluated independently per frequency");
  context.check(
      path.points.size() == before.points.size() && path.steps.empty() &&
          path.events.size() == before.events.size() &&
          path.points.front().position == before.points.front().position &&
          path.points.back().slowness == before.points.back().slowness &&
          path.events.front().incidentSlowness ==
              before.events.front().incidentSlowness &&
          path.events.front().reflectedSlowness ==
              before.events.front().reflectedSlowness &&
          !path.events.front().longMaterialOverride.has_value() &&
          !before.events.front().longMaterialOverride.has_value() &&
          path.terminationReason == before.terminationReason,
      "two-frequency elastic projection preserves the frozen path");
}

void testGrainSizeEnvironmentProjection(Context& context) {
  const Environment environment = makeConstantEnvironment(
      {}, BoundaryModel::grainSizeHalfSpace(100.0, 3.0),
      thorpVolumeAttenuation());
  RayPathCache cache;
  cache.append(makeAcousticReflectionPath());
  cache.freeze();
  const RayPath& path = cache.at(0U);
  const RayPath before = path;
  const ReflectionEvent& event = path.events.front();
  const auto& grain = *environment.seabed().grainSizeMaterial();
  const FrequencyProjector projector(environment);
  const auto first = projector.project(path, 1000.0, 1.0);
  const auto second = projector.project(path, 2000.0, 1.0);
  const auto expected = evaluateGrainSizeHalfSpaceAcoustics(
      grain, 1000.0, kSoundSpeed, kWaterDensity,
      event.tangentSlowness, event.normalSlowness);
  context.checkNear(first.points.back().amplitude,
                    expected.amplitudeMultiplier, 2.0e-15,
                    "projector applies grain-size amplitude");
  context.checkNear(first.points.back().reflectionPhase,
                    expected.phaseIncrement, 2.0e-15,
                    "projector applies grain-size phase");
  context.check(first.points.back().amplitude ==
                    second.points.back().amplitude &&
                first.points.back().reflectionPhase ==
                    second.points.back().reflectionPhase,
                "G sediment ignores ENV Thorp and stays frequency invariant");
  context.check(path.points.size() == before.points.size() &&
                    path.steps.size() == before.steps.size() &&
                    path.events.size() == before.events.size() &&
                    path.points.front().position ==
                        before.points.front().position &&
                    path.points.back().slowness ==
                        before.points.back().slowness &&
                    path.events.front().incidentSlowness ==
                        before.events.front().incidentSlowness &&
                    path.events.front().reflectedSlowness ==
                        before.events.front().reflectedSlowness &&
                    path.terminationReason == before.terminationReason &&
                    cache.frozen(),
                "two-frequency G projection preserves the frozen cache");

  const Environment varyingEnvironment(
      SoundSpeedProfile({
          {.depth = 0.0, .soundSpeed = 1400.0, .density = kWaterDensity},
          {.depth = 100.0, .soundSpeed = 1500.0, .density = kWaterDensity}}),
      BoundaryModel::vacuum(0.0),
      BoundaryModel::grainSizeHalfSpace(100.0, 3.0));
  const auto varying = FrequencyProjector(varyingEnvironment)
                           .project(path, 1000.0, 1.0);
  const auto localExpected = evaluateGrainSizeHalfSpaceAcoustics(
      *varyingEnvironment.seabed().grainSizeMaterial(), 1000.0, 1500.0,
      kWaterDensity, event.tangentSlowness, event.normalSlowness);
  const auto wrongSurfaceValue = evaluateGrainSizeHalfSpaceAcoustics(
      *varyingEnvironment.seabed().grainSizeMaterial(), 1000.0, 1400.0,
      kWaterDensity, event.tangentSlowness, event.normalSlowness);
  context.checkNear(varying.points.back().amplitude,
                    localExpected.amplitudeMultiplier, 2.0e-15,
                    "G projector uses water sound speed at the event");
  context.check(
      varying.points.back().amplitude !=
          wrongSurfaceValue.amplitudeMultiplier,
      "G projector does not freeze the source-depth water sound speed");
}

void testTabulatedReflectionProjection(Context& context) {
  const auto table = std::make_shared<const TabulatedReflectionTable>(
      TabulatedReflectionTable{
          {.angleDegrees = 0.0, .magnitude = 0.2,
           .phaseRadians = 0.0},
          {.angleDegrees = 45.0, .magnitude = 0.5,
           .phaseRadians = 4.0},
          {.angleDegrees = 90.0, .magnitude = 0.8,
           .phaseRadians = 7.0}});
  const Environment environment = makeConstantEnvironment(
      {}, BoundaryModel::tabulatedReflection(
              bellhop::BoundaryGeometry::flat(
                  100.0, bellhop::BoundaryOrientation::Lower),
              table),
      thorpVolumeAttenuation());
  RayPathCache cache;
  cache.append(makeAcousticReflectionPath());
  cache.freeze();
  const RayPath& path = cache.at(0U);
  const RayPath before = path;
  const ReflectionEvent& event = path.events.front();
  const auto expected = evaluateTabulatedReflectionAcoustics(
      *table, event.tangentSlowness, event.normalSlowness);
  const FrequencyProjector projector(environment);
  const auto first = projector.project(path, 1000.0, 1.0);
  const auto second = projector.project(path, 2000.0, 1.0);
  for (const RayFrequencyState* state : {&first, &second}) {
    context.checkNear(state->points.back().amplitude,
                      expected.amplitudeMultiplier, 2.0e-15,
                      "projector applies tabulated magnitude");
    context.checkNear(state->points.back().reflectionPhase,
                      expected.phaseIncrement, 3.0e-15,
                      "projector applies explicit tabulated phase");
  }
  context.check(first.points.back().amplitude ==
                    second.points.back().amplitude &&
                first.points.back().reflectionPhase ==
                    second.points.back().reflectionPhase,
                "tabulated coefficients are frequency independent");
  context.check(path.points.size() == before.points.size() &&
                    path.steps.size() == before.steps.size() &&
                    path.events.size() == before.events.size() &&
                    path.points.front().position ==
                        before.points.front().position &&
                    path.points.back().slowness ==
                        before.points.back().slowness &&
                    path.events.front().incidentSlowness ==
                        before.events.front().incidentSlowness &&
                    path.events.front().reflectedSlowness ==
                        before.events.front().reflectedSlowness &&
                    path.terminationReason == before.terminationReason &&
                    cache.frozen(),
                "two-frequency table projection preserves frozen geometry");

  const auto zeroEndpointTable =
      std::make_shared<const TabulatedReflectionTable>(
          TabulatedReflectionTable{
              {.angleDegrees = 10.0, .magnitude = 0.0,
               .phaseRadians = 1.0},
              {.angleDegrees = 80.0, .magnitude = 1.0,
               .phaseRadians = 2.0}});
  const Environment zeroEndpointEnvironment = makeConstantEnvironment(
      {}, BoundaryModel::tabulatedReflection(
              bellhop::BoundaryGeometry::flat(
                  100.0, bellhop::BoundaryOrientation::Lower),
              zeroEndpointTable));
  constexpr double kNearEndpointDegrees = 9.9999999;
  const double nearEndpointRadians =
      kNearEndpointDegrees * std::numbers::pi / 180.0;
  const double tangentSlowness =
      std::cos(nearEndpointRadians) / kSoundSpeed;
  const double normalSlowness =
      std::sin(nearEndpointRadians) / kSoundSpeed;
  const Vec2 position{.range = 25.0, .depth = 100.0};
  const ReflectionEvent nearEndpointEvent = makeReflectionEvent(
      0U, ReflectionBoundary::Seabed, position,
      tangentSlowness, normalSlowness);
  RayPath nearEndpointPath;
  nearEndpointPath.points = {
      makeRayState(position, nearEndpointEvent.incidentSlowness),
      makeRayState(position, nearEndpointEvent.reflectedSlowness)};
  nearEndpointPath.events = {nearEndpointEvent};
  const RayFrequencyState nearEndpoint =
      FrequencyProjector(zeroEndpointEnvironment)
          .project(nearEndpointPath, 1000.0, 1.0);
  context.checkNear(nearEndpoint.points.back().amplitude,
                    -1.4285714233383277e-9, 3.0e-16,
                    "REAL4 endpoint decision retains tiny negative amplitude");
  context.checkNear(nearEndpoint.points.back().reflectionPhase,
                    9.9999999857142863e-1, 3.0e-16,
                    "REAL4 endpoint decision retains extrapolated phase");
  context.check(!nearEndpoint.points.back().active,
                "tiny negative tabulated amplitude terminates the ray");
}

void testTopTabulatedReflectionProjection(Context& context) {
  const auto table = std::make_shared<const TabulatedReflectionTable>(
      TabulatedReflectionTable{
          {.angleDegrees = 0.0, .magnitude = 0.25, .phaseRadians = 0.0},
          {.angleDegrees = 90.0,
           .magnitude = 0.75,
           .phaseRadians = std::numbers::pi}});
  const Environment environment(
      SoundSpeedProfile(
          {{.depth = 0.0,
            .soundSpeed = kSoundSpeed,
            .density = kWaterDensity},
           {.depth = 100.0,
            .soundSpeed = kSoundSpeed,
            .density = kWaterDensity}}),
      BoundaryModel::tabulatedReflection(
          BoundaryGeometry::flat(0.0, BoundaryOrientation::Upper), table),
      BoundaryModel::vacuum(
          BoundaryGeometry::flat(100.0, BoundaryOrientation::Lower)));
  const double tangentSlowness = 0.5 / kSoundSpeed;
  const double normalSlowness =
      std::sqrt(0.75) / kSoundSpeed;
  const Vec2 position{.range = 25.0, .depth = 0.0};
  const ReflectionEvent event = makeReflectionEvent(
      0U, ReflectionBoundary::SeaSurface, position,
      tangentSlowness, normalSlowness);
  RayPath path;
  path.points = {
      makeRayState(position, event.incidentSlowness),
      makeRayState(position, event.reflectedSlowness)};
  path.events = {event};
  const RayFrequencyState projected =
      FrequencyProjector(environment).project(path, 1000.0, 1.0);
  const BoundaryAcousticsResult expected =
      evaluateTabulatedReflectionAcoustics(
          *table, tangentSlowness, normalSlowness);
  context.checkNear(projected.points.back().amplitude,
                    expected.amplitudeMultiplier, 2.0e-15,
                    "top table reflection amplitude uses the shared projector");
  context.checkNear(projected.points.back().reflectionPhase,
                    expected.phaseIncrement, 2.0e-15,
                    "top table reflection phase uses the shared projector");
}

void testAcousticReflectionAndActiveCutoff(Context& context) {
  const auto& fixture =
      bellhop::test::kHalfSpaceCoefficientFixtures[1U];
  const RayPath path = makeAcousticReflectionPath();
  const FrequencyProjector projector(makeAcousticBottomEnvironment());

  const RayFrequencyState ordinary =
      projector.project(path, fixture.frequencyHz, 1.0);
  context.checkNear(
      ordinary.points.back().amplitude,
      std::abs(fixture.expectedRawCoefficient), 2.0e-15,
      "projector applies acoustic coefficient magnitude");
  context.checkNear(
      ordinary.points.back().reflectionPhase,
      std::arg(fixture.expectedRawCoefficient), 2.0e-15,
      "projector applies acoustic coefficient phase");
  context.check(ordinary.points.back().active,
                "ordinary acoustic reflection remains active");

  const RayFrequencyState killed =
      projector.project(path, fixture.frequencyHz, 0.04);
  context.check(
      killed.points.front().active,
      "source point stays active before the first legacy cutoff check");
  context.checkNear(
      killed.points.back().amplitude,
      0.04 * std::abs(fixture.expectedRawCoefficient), 2.0e-16,
      "active cutoff does not overwrite the physical amplitude");
  context.check(!killed.points.back().active,
                "acoustic reflection below 0.005 becomes inactive");

  RayPath rigidPath = path;
  const FrequencyProjector rigidProjector(makeConstantEnvironment());
  const RayFrequencyState exact =
      rigidProjector.project(rigidPath, 250.0,
                             kLegacyActiveThreshold);
  context.check(exact.points.back().active,
                "amplitude exactly at promoted threshold stays active");
  const RayFrequencyState below = rigidProjector.project(
      rigidPath, 250.0,
      std::nextafter(kLegacyActiveThreshold, 0.0));
  context.check(!below.points.back().active,
                "strictly smaller amplitude becomes inactive");

  const RayFrequencyState repeated = projector.project(
      makeRepeatedAcousticReflectionPath(), fixture.frequencyHz, 1.0);
  context.checkNear(
      repeated.points[1U].amplitude,
      1.19835889068164225e-1, 2.0e-15,
      "first acoustic-bottom reflection remains above active cutoff");
  context.check(repeated.points[1U].active,
                "first acoustic-bottom post-point is active");
  context.checkNear(
      repeated.points[5U].amplitude,
      1.43606403087573626e-2, 2.0e-15,
      "second acoustic-bottom reflection remains above active cutoff");
  context.check(repeated.points[5U].active,
                "second acoustic-bottom post-point is active");
  context.checkNear(
      repeated.points[9U].amplitude,
      1.72092009898805486e-3, 3.0e-16,
      "third acoustic-bottom reflection matches Fortran cumulative amplitude");
  context.checkNear(
      repeated.points[9U].reflectionPhase,
      6.39628196441146546, 2.0e-14,
      "third acoustic-bottom reflection matches unwrapped Fortran phase");
  context.check(!repeated.points[9U].active,
                "third acoustic-bottom post-point becomes inactive");
  context.check(!repeated.points[10U].active,
                "inactive state is sticky across the geometry suffix");
}

void testFrozenLongMaterialOverridesEnvironmentFallback(Context& context) {
  RayPath path = makeAcousticReflectionPath();
  const AcousticMaterial longMaterial{
      .compressionalSoundSpeed = 1800.0,
      .shearSoundSpeed = 0.0,
      .density = 2000.0,
      .compressionalAttenuation = {
          .value = 0.20,
          .unit = AttenuationUnit::DecibelsPerWavelength},
  };
  path.events.front().longMaterialOverride = FrozenBoundaryMaterial{
      .material = longMaterial,
      .attenuationEvaluationDepth = 1.0e20};
  const RayPath before = path;
  const Environment environment = makeAcousticBottomEnvironment();
  const FrequencyProjector projector(environment);
  for (const double frequency : {1000.0, 2000.0}) {
    const RayFrequencyState state = projector.project(path, frequency, 1.0);
    const ReflectionEvent& event = path.events.front();
    const auto expected = evaluateFluidHalfSpaceAcoustics(
        longMaterial, 1.0e20, environment.volumeAttenuation(), frequency,
        kWaterDensity, event.tangentSlowness, event.normalSlowness);
    context.checkNear(state.points.back().amplitude,
                      expected.amplitudeMultiplier, 2.0e-15,
                      "projector uses the frozen LL material amplitude");
    context.checkNear(state.points.back().reflectionPhase,
                      expected.phaseIncrement, 2.0e-15,
                      "projector uses the frozen LL material phase");
  }
  const RayFrequencyState fallback = FrequencyProjector(environment).project(
      makeAcousticReflectionPath(), 1000.0, 1.0);
  const RayFrequencyState overridden = projector.project(path, 1000.0, 1.0);
  context.check(
      overridden.points.back().amplitude != fallback.points.back().amplitude,
      "frozen LL material is observably distinct from the ENV fallback");
  context.check(
      path.events.front().longMaterialOverride->material
              .compressionalAttenuation.value ==
          before.events.front().longMaterialOverride->material
              .compressionalAttenuation.value &&
          path.events.front().longMaterialOverride->attenuationEvaluationDepth ==
              before.events.front()
                  .longMaterialOverride->attenuationEvaluationDepth,
      "multi-frequency projection does not mutate frozen LL material");

  RayPath elasticLongOverride = path;
  auto& elasticMaterial = elasticLongOverride.events.front()
                              .longMaterialOverride->material;
  elasticMaterial.shearSoundSpeed = 900.0;
  elasticMaterial.shearAttenuation = {
      .value = 0.05,
      .unit = AttenuationUnit::DecibelsPerWavelength};
  const RayFrequencyState elasticState =
      projector.project(elasticLongOverride, 1000.0, 1.0);
  const ReflectionEvent& elasticEvent = elasticLongOverride.events.front();
  const auto expectedElastic = evaluateAcousticHalfSpaceAcoustics(
      elasticMaterial, 1.0e20, environment.volumeAttenuation(), 1000.0,
      kWaterDensity, elasticEvent.tangentSlowness,
      elasticEvent.normalSlowness);
  context.checkNear(elasticState.points.back().amplitude,
                    expectedElastic.amplitudeMultiplier, 2.0e-15,
                    "projector evaluates frozen elastic LL amplitude");
  context.checkNear(elasticState.points.back().reflectionPhase,
                    expectedElastic.phaseIncrement, 2.0e-15,
                    "projector evaluates frozen elastic LL phase");

  const RayFrequencyState rigid =
      FrequencyProjector(makeConstantEnvironment())
          .project(path, 1000.0, 1.0);
  context.checkNear(
      rigid.points.back().amplitude, 1.0, 0.0,
      "rigid boundary condition takes priority over a frozen LL material");
  context.checkNear(
      rigid.points.back().reflectionPhase, 0.0, 0.0,
      "rigid boundary ignores the frozen LL material phase");
}

void testMunkReflectionOracle(Context& context) {
  const Environment environment = bellhop::test::makeMunkEnvironment();
  const RayPath path =
      GeometryTracer(environment,
                     bellhop::test::makeMunkIntegratorSettings())
          .trace(Source{.depth = 1000.0},
                 bellhop::test::kMunkExtremeLaunchAngle);
  const RayFrequencyState state =
      FrequencyProjector(environment).project(path, 50.0, 1.0);
  const auto eventIterator = std::find_if(
      path.events.begin(), path.events.end(),
      [](const ReflectionEvent& event) {
        return event.boundary == ReflectionBoundary::Seabed;
      });
  context.check(eventIterator != path.events.end(),
                "extreme Munk ray reaches the acoustic seabed");
  if (eventIterator == path.events.end()) {
    return;
  }

  const auto& fixture =
      bellhop::test::kHalfSpaceCoefficientFixtures.back();
  context.checkNear(eventIterator->tangentSlowness,
                    fixture.tangentSlowness, 2.0e-15,
                    "Munk event tangent slowness matches Fortran oracle");
  context.checkNear(eventIterator->normalSlowness,
                    fixture.outwardNormalSlowness, 2.0e-15,
                    "Munk event normal slowness matches Fortran oracle");
  const auto& reflected =
      state.points[eventIterator->rayPointIndex + 1U];
  context.checkNear(reflected.amplitude,
                    std::abs(fixture.expectedRawCoefficient),
                    2.0e-15,
                    "Munk projected bottom amplitude matches Fortran oracle");
  context.checkNear(reflected.reflectionPhase,
                    std::arg(fixture.expectedRawCoefficient),
                    2.0e-15,
                    "Munk projected bottom phase matches Fortran oracle");
  context.checkNear(
      reflected.complexTravelTime.imag(), 0.0, 0.0,
      "lossless Munk water column retains zero imaginary travel time");
}

void testProjectionDoesNotMutateGeometry(Context& context) {
  const Environment environment =
      makeConstantEnvironment(
          {}, BoundaryModel::rigid(100.0),
          biologicalVolumeAttenuation());
  const GeometryTracer tracer(
      environment,
      IntegratorSettings{
          .stepLength = 10.0,
          .rangeLimit = 35.0,
          .depthLimit = 200.0,
          .maximumRayPoints = 100U});
  RayPathCache cache;
  cache.append(tracer.trace(Source{.depth = 50.0}, 0.0));
  cache.freeze();
  const RayPath& path = cache.at(0U);
  const RayPath before = path;
  const FrequencyProjector projector(environment);
  RayFrequencyState low =
      projector.project(path, 50.0, 1.0);
  RayFrequencyState high =
      projector.project(path, 5000.0, 1.0);

  context.check(path.points.size() == before.points.size() &&
                    path.steps.size() == before.steps.size() &&
                    path.events.size() == before.events.size(),
                "projection preserves geometry container sizes");
  for (std::size_t index = 0U; index < path.points.size(); ++index) {
    context.check(
        path.points[index].position == before.points[index].position &&
            path.points[index].slowness ==
                before.points[index].slowness &&
            path.points[index].dynamicP ==
                before.points[index].dynamicP &&
            path.points[index].dynamicQ ==
                before.points[index].dynamicQ &&
            path.points[index].soundSpeed ==
                before.points[index].soundSpeed &&
            path.points[index].realTravelTime ==
                before.points[index].realTravelTime,
        "projection leaves every geometry point unchanged");
  }
  for (std::size_t index = 0U; index < path.steps.size(); ++index) {
    context.check(
        path.steps[index].stepLength == before.steps[index].stepLength &&
            path.steps[index].startWeight == before.steps[index].startWeight &&
            path.steps[index].midpointWeight ==
                before.steps[index].midpointWeight &&
            path.steps[index].midpoint == before.steps[index].midpoint,
        "projection leaves every frozen quadrature step unchanged");
  }
  context.check(path.events.empty() == before.events.empty() &&
                    path.terminationReason == before.terminationReason &&
                    path.terminationDetail == before.terminationDetail &&
                    cache.frozen(),
                "projection preserves frozen events and termination state");
  context.check(
      low.points.back().complexTravelTime !=
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
  context.check(
      highWorkspace.at(0U, 0U) == std::complex<double>{3.0, 4.0},
      "clearing one frequency workspace does not alter another");
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

}  // namespace

int main() {
  Context context;
  testLosslessAndThorpPropagation(context);
  testNodeConversionPrecedesInterpolation(context);
  testQuadrilateralFrequencyProjection(context);
  testAttenuationUnitFrequencyProjection(context);
  testVacuumRigidPhaseIsUnwrapped(context);
  testAcousticReflectionAndActiveCutoff(context);
  testElasticEnvironmentProjection(context);
  testGrainSizeEnvironmentProjection(context);
  testTabulatedReflectionProjection(context);
  testTopTabulatedReflectionProjection(context);
  testFrozenLongMaterialOverridesEnvironmentFallback(context);
  testMunkReflectionOracle(context);
  testProjectionDoesNotMutateGeometry(context);
  testInvalidInputs(context);

  if (context.failureCount() != 0) {
    std::cerr << context.failureCount()
              << " frequency-projector assertion(s) failed\n";
    return 1;
  }
  std::cout << "All Bellhop F2CPP frequency-projector tests passed\n";
  return 0;
}
