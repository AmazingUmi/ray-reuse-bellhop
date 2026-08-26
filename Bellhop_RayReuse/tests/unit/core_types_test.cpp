#include <cmath>
#include <complex>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <utility>
#include <vector>

#include "rayreuse/cache/ray_path_cache.hpp"
#include "rayreuse/error.hpp"
#include "rayreuse/field/frequency_workspace.hpp"
#include "rayreuse/model/environment.hpp"
#include "rayreuse/model/simulation_case.hpp"
#include "rayreuse/numerics/vec2.hpp"
#include "support/test_harness.hpp"

namespace {

using rayreuse::BeamFamily;
using rayreuse::BeamWidthMode;
using rayreuse::BoundaryCurvatureMode;
using rayreuse::BoundaryModel;
using rayreuse::Environment;
using rayreuse::FieldComponent;
using rayreuse::FrequencyGrid;
using rayreuse::IntegratorSettings;
using rayreuse::LaunchFan;
using rayreuse::RayFrequencyPoint;
using rayreuse::RayFrequencyState;
using rayreuse::RayPath;
using rayreuse::RayPathCache;
using rayreuse::RayState;
using rayreuse::RayTerminationReason;
using rayreuse::ReceiverGrid;
using rayreuse::SimulationCase;
using rayreuse::SimulationRunMode;
using rayreuse::SoundSpeedPoint;
using rayreuse::SoundSpeedProfile;
using rayreuse::Source;
using rayreuse::SourceBeamPattern;
using rayreuse::SourceBeamPatternSample;
using rayreuse::StepQuadrature;
using rayreuse::ValidationError;
using rayreuse::Vec2;
using rayreuse::test::Context;

Environment makeEnvironment() {
  std::vector<SoundSpeedPoint> points{
      {.depth = 0.0, .soundSpeed = 1500.0, .density = 1000.0},
      {.depth = 1000.0, .soundSpeed = 1500.0, .density = 1000.0},
  };
  return Environment(SoundSpeedProfile(std::move(points)),
                     BoundaryModel::vacuum(0.0), BoundaryModel::rigid(1000.0));
}

SimulationCase makeSimulationCase(std::vector<double> frequencies) {
  return SimulationCase(
      makeEnvironment(), Source{.depth = 500.0, .amplitude = 1.0},
      ReceiverGrid({400.0, 500.0, 600.0}, {100.0, 1000.0, 5000.0}),
      FrequencyGrid(std::move(frequencies)),
      LaunchFan{.minimumAngle = -0.1,
                .maximumAngle = 0.1,
                .explicitLaunchAngleCount = 301U},
      IntegratorSettings{.stepLength = 10.0,
                         .rangeLimit = 5100.0,
                         .depthLimit = 1100.0,
                         .maximumRayPoints = 10000U});
}

RayPath makeRayPath() {
  RayPath path;
  path.launchAngle = 0.0;
  path.points = {
      RayState{.position = {0.0, 500.0},
               .slowness = {1.0 / 1500.0, 0.0},
               .dynamicP = {1.0, 0.0},
               .dynamicQ = {0.0, 1.0},
               .soundSpeed = 1500.0,
               .realTravelTime = 0.0},
      RayState{.position = {10.0, 500.0},
               .slowness = {1.0 / 1500.0, 0.0},
               .dynamicP = {1.0, 0.0},
               .dynamicQ = {10.0 * 1500.0, 1.0},
               .soundSpeed = 1500.0,
               .realTravelTime = 10.0 / 1500.0},
  };
  path.steps = {
      StepQuadrature{.stepLength = 10.0,
                     .startWeight = 0.0,
                     .midpointWeight = 10.0,
                     .midpoint = {5.0, 500.0}},
  };
  path.terminationReason = RayTerminationReason::ExitedDomain;
  return path;
}

RayPath makeReflectedRayPath() {
  constexpr double kSlownessComponent = 1.0 / (1500.0 * 1.4142135623730950488);
  RayPath path;
  path.launchAngle = -0.78539816339744830962;
  path.points = {
      RayState{.position = {0.0, 10.0},
               .slowness = {kSlownessComponent, -kSlownessComponent},
               .dynamicP = {1.0, 0.0},
               .dynamicQ = {0.0, 1.0},
               .soundSpeed = 1500.0,
               .realTravelTime = 0.0},
      RayState{.position = {10.0, 0.0},
               .slowness = {kSlownessComponent, -kSlownessComponent},
               .dynamicP = {1.0, 0.0},
               .dynamicQ = {15000.0, 1.0},
               .soundSpeed = 1500.0,
               .realTravelTime = 10.0 / 1500.0},
      RayState{.position = {10.0, 0.0},
               .slowness = {kSlownessComponent, kSlownessComponent},
               .dynamicP = {1.0, 0.0},
               .dynamicQ = {15000.0, 1.0},
               .soundSpeed = 1500.0,
               .realTravelTime = 10.0 / 1500.0},
      RayState{.position = {20.0, 10.0},
               .slowness = {kSlownessComponent, kSlownessComponent},
               .dynamicP = {1.0, 0.0},
               .dynamicQ = {30000.0, 1.0},
               .soundSpeed = 1500.0,
               .realTravelTime = 20.0 / 1500.0},
  };
  path.steps = {
      StepQuadrature{.stepLength = 10.0,
                     .startWeight = 0.0,
                     .midpointWeight = 10.0,
                     .midpoint = {5.0, 5.0}},
      StepQuadrature{.stepLength = 10.0,
                     .startWeight = 0.0,
                     .midpointWeight = 10.0,
                     .midpoint = {15.0, 5.0}},
  };
  path.events = {
      {.rayPointIndex = 1U,
       .reflectedRayPointIndex = 2U,
       .boundary = rayreuse::ReflectionBoundary::SeaSurface,
       .boundarySegmentIndex = 0U,
       .position = {10.0, 0.0},
       .boundaryTangent = {1.0, 0.0},
       .outwardNormal = {0.0, -1.0},
       .incidentSlowness = {kSlownessComponent, -kSlownessComponent},
       .reflectedSlowness = {kSlownessComponent, kSlownessComponent},
       .tangentSlowness = kSlownessComponent,
       .normalSlowness = kSlownessComponent},
  };
  path.terminationReason = RayTerminationReason::ExitedDomain;
  return path;
}

void testVec2(Context& context) {
  const Vec2 value{3.0, 4.0};
  context.checkNear(rayreuse::norm(value), 5.0, 1.0e-15,
                    "Vec2 norm uses range/depth components");
  context.check(rayreuse::dot(value, Vec2{1.0, 2.0}) == 11.0,
                "Vec2 dot product");
  context.check(value + Vec2{2.0, -1.0} == Vec2{5.0, 3.0}, "Vec2 addition");
}

void testSimulationCase(Context& context) {
  const SimulationCase simulation = makeSimulationCase({50.0});
  context.check(simulation.runMode() == SimulationRunMode::Coherent &&
                    simulation.beamFamily() == BeamFamily::CervenyGaussian &&
                    !simulation.sourceBeamPattern().isDirectional(),
                "SimulationCase defaults to coherent Cerveny metadata and "
                "an omnidirectional source beam pattern");
  context.check(simulation.frequencies().size() == 1U,
                "single-frequency RayReuse case retains its frequency");
  context.check(simulation.frequencies().designFrequency() == 50.0,
                "single frequency is the design frequency");
  context.check(simulation.environment().waterDepth() == 1000.0,
                "environment reports water depth in metres");
  context.check(simulation.receivers().rangeCount() == 3U,
                "receiver range count");
  context.check(simulation.launchFanPlan().launchAngleCount == 300U,
                "SimulationCase enforces D-02 instead of accepting 301");
  context.check(simulation.launchFanPlan().launchAngles.size() ==
                    simulation.launchFanPlan().launchAngleCount,
                "SimulationCase stores the complete derived launch fan");

  context.expectThrows<ValidationError>(
      [] { static_cast<void>(makeSimulationCase({})); },
      "empty frequency list is rejected");
  const SimulationCase broadband = makeSimulationCase({25.0, 50.0, 100.0});
  const SimulationCase highestFrequencyOnly = makeSimulationCase({100.0});
  context.check(
      broadband.frequencies().size() == 3U &&
          broadband.frequencies().designFrequency() == 100.0 &&
          broadband.launchFanPlan().designFrequency == 100.0 &&
          broadband.launchFanPlan().launchAngles ==
              highestFrequencyOnly.launchFanPlan().launchAngles,
      "multiple frequencies are accepted and fmax plans the shared fan");
  context.expectThrows<ValidationError>(
      [] { static_cast<void>(makeSimulationCase({0.0})); },
      "non-positive frequencies are rejected");
  context.expectThrows<ValidationError>(
      [] { static_cast<void>(makeSimulationCase({50.0, 50.0})); },
      "duplicate frequencies are rejected");
  context.expectThrows<ValidationError>(
      [] { static_cast<void>(makeSimulationCase({100.0, 50.0})); },
      "descending frequencies are rejected");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(makeSimulationCase(
            {50.0, std::numeric_limits<double>::infinity()}));
      },
      "infinite frequencies are rejected");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(makeSimulationCase(
            {50.0, std::numeric_limits<double>::quiet_NaN()}));
      },
      "NaN frequencies are rejected");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(
            ReceiverGrid(std::vector<double>{}, std::vector<double>{1.0}));
      },
      "empty receiver depth grid is rejected");
}

void testSimulationProductMetadata(Context& context) {
  const SourceBeamPattern pattern = SourceBeamPattern::directional(
      {{.angleDegrees = -30.0, .powerDecibels = -6.0},
       {.angleDegrees = 0.0, .powerDecibels = 0.0},
       {.angleDegrees = 20.0, .powerDecibels = -12.0}});
  const double degreesToRadians = std::numbers::pi / 180.0;
  const double minusSixDb = std::pow(10.0, -6.0 / 20.0);
  const double minusTwelveDb = std::pow(10.0, -12.0 / 20.0);
  context.check(pattern.isDirectional() && pattern.size() == 3U &&
                    pattern.minimumAngleDegrees() == -30.0 &&
                    pattern.maximumAngleDegrees() == 20.0,
                "directional source beam pattern retains its range metadata");
  context.checkNear(pattern.amplitudeForLaunchAngle(-30.0 * degreesToRadians),
                    minusSixDb, 2.0e-15,
                    "source beam pattern converts amplitude dB at a knot");
  context.checkNear(
      pattern.amplitudeForLaunchAngle(-15.0 * degreesToRadians),
      0.5 * (minusSixDb + 1.0), 2.0e-15,
      "source beam pattern interpolates converted linear amplitudes");
  context.checkNear(
      pattern.amplitudeForLaunchAngle(30.0 * degreesToRadians),
      1.5 * minusTwelveDb - 0.5, 2.0e-15,
      "source beam pattern uses the last segment for extrapolation");

  const SourceBeamPattern omni = SourceBeamPattern::omnidirectional();
  context.check(
      !omni.isDirectional() && omni.size() == 2U &&
          omni.minimumAngleDegrees() == -180.0 &&
          omni.maximumAngleDegrees() == 180.0,
      "omnidirectional source beam pattern has default range metadata");
  context.checkNear(omni.amplitudeForLaunchAngle(2.0), 1.0, 0.0,
                    "omnidirectional source beam pattern is unity");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(SourceBeamPattern::directional(
            {{.angleDegrees = 0.0, .powerDecibels = 0.0}}));
      },
      "source beam pattern rejects a single sample");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(SourceBeamPattern::directional(
            {{.angleDegrees = 0.0, .powerDecibels = 0.0},
             {.angleDegrees = 0.0, .powerDecibels = -3.0}}));
      },
      "source beam pattern rejects duplicate angles");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(SourceBeamPattern::directional(
            {{.angleDegrees = 0.0, .powerDecibels = 0.0},
             {.angleDegrees = 1.0, .powerDecibels = 10000.0}}));
      },
      "source beam pattern rejects amplitude-conversion overflow");

  const auto makeMetadataCase =
      [&](SimulationRunMode runMode, BeamFamily beamFamily,
          FieldComponent fieldComponent = FieldComponent::Pressure,
          BoundaryCurvatureMode curvatureMode = BoundaryCurvatureMode::Standard,
          BeamWidthMode beamWidthMode = BeamWidthMode::MinimumWidth) {
        return SimulationCase(
            makeEnvironment(), Source{.depth = 500.0, .amplitude = 1.0},
            ReceiverGrid({400.0, 500.0, 600.0}, {100.0, 1000.0, 5000.0}),
            FrequencyGrid({50.0}),
            LaunchFan{.minimumAngle = -0.1,
                      .maximumAngle = 0.1,
                      .explicitLaunchAngleCount = 301U},
            IntegratorSettings{.stepLength = 10.0,
                               .rangeLimit = 5100.0,
                               .depthLimit = 1100.0,
                               .maximumRayPoints = 10000U},
            pattern, runMode, beamFamily, fieldComponent, curvatureMode,
            beamWidthMode);
      };
  const SimulationCase arrivals = makeMetadataCase(
      SimulationRunMode::AsciiArrivals, BeamFamily::GeometricHat);
  context.check(arrivals.runMode() == SimulationRunMode::AsciiArrivals &&
                    arrivals.beamFamily() == BeamFamily::GeometricHat &&
                    arrivals.sourceBeamPattern().isDirectional(),
                "SimulationCase preserves product metadata and beam pattern");
  const SimulationCase incoherent = makeMetadataCase(
      SimulationRunMode::Incoherent, BeamFamily::CervenyGaussian);
  const SimulationCase semiCoherent = makeMetadataCase(
      SimulationRunMode::SemiCoherent, BeamFamily::CervenyGaussian);
  context.check(incoherent.runMode() == SimulationRunMode::Incoherent &&
                    semiCoherent.runMode() == SimulationRunMode::SemiCoherent &&
                    rayreuse::isTransmissionLossMode(incoherent.runMode()) &&
                    rayreuse::isTransmissionLossMode(semiCoherent.runMode()) &&
                    rayreuse::fieldAccumulationKind(incoherent.runMode()) ==
                        rayreuse::FieldAccumulationKind::Intensity &&
                    rayreuse::fieldAccumulationKind(semiCoherent.runMode()) ==
                        rayreuse::FieldAccumulationKind::Intensity &&
                    !rayreuse::usesLloydMirror(incoherent.runMode()) &&
                    rayreuse::usesLloydMirror(semiCoherent.runMode()),
                "SimulationCase preserves I/S mode and accumulation metadata");
  for (const FieldComponent component :
       {FieldComponent::Pressure, FieldComponent::Vertical,
        FieldComponent::Horizontal}) {
    const SimulationCase cartesianCerveny = makeMetadataCase(
        SimulationRunMode::Coherent, BeamFamily::CervenyGaussian, component);
    context.check(cartesianCerveny.fieldComponent() == component,
                  "SimulationCase preserves Cartesian Cerveny component");
  }
  for (const BoundaryCurvatureMode curvatureMode :
       {BoundaryCurvatureMode::Double, BoundaryCurvatureMode::Standard,
        BoundaryCurvatureMode::Zero}) {
    const SimulationCase cartesianCerveny = makeMetadataCase(
        SimulationRunMode::Coherent, BeamFamily::CervenyGaussian,
        FieldComponent::Pressure, curvatureMode);
    context.check(cartesianCerveny.curvatureMode() == curvatureMode,
                  "SimulationCase preserves Cartesian Cerveny curvature");
  }
  for (const BeamWidthMode widthMode :
       {BeamWidthMode::SpaceFilling, BeamWidthMode::MinimumWidth,
        BeamWidthMode::Wkb}) {
    const SimulationCase cartesianCerveny = makeMetadataCase(
        SimulationRunMode::Coherent, BeamFamily::CervenyGaussian,
        FieldComponent::Pressure, BoundaryCurvatureMode::Standard, widthMode);
    context.check(cartesianCerveny.beamWidthMode() == widthMode,
                  "SimulationCase preserves Cartesian Cerveny beam width");
  }
  context.expectThrows<ValidationError>(
      [&makeMetadataCase] {
        static_cast<void>(makeMetadataCase(SimulationRunMode::Coherent,
                                           BeamFamily::GeometricHat,
                                           FieldComponent::Vertical));
      },
      "non-Cerveny families reject non-pressure components");
  context.expectThrows<ValidationError>(
      [&makeMetadataCase] {
        static_cast<void>(makeMetadataCase(SimulationRunMode::RayTrace,
                                           BeamFamily::CervenyGaussian,
                                           FieldComponent::Horizontal));
      },
      "non-TL products reject non-pressure components");
  context.expectThrows<ValidationError>(
      [&makeMetadataCase] {
        static_cast<void>(makeMetadataCase(
            SimulationRunMode::Coherent, BeamFamily::GeometricGaussian,
            FieldComponent::Pressure, BoundaryCurvatureMode::Double));
      },
      "non-Cerveny families reject non-standard curvature");
  context.expectThrows<ValidationError>(
      [&makeMetadataCase] {
        static_cast<void>(makeMetadataCase(
            SimulationRunMode::RayTrace, BeamFamily::CervenyGaussian,
            FieldComponent::Pressure, BoundaryCurvatureMode::Zero));
      },
      "non-TL products reject non-standard curvature");
  context.expectThrows<ValidationError>(
      [&makeMetadataCase] {
        static_cast<void>(makeMetadataCase(
            SimulationRunMode::Coherent, BeamFamily::GeometricHat,
            FieldComponent::Pressure, BoundaryCurvatureMode::Standard,
            BeamWidthMode::SpaceFilling));
      },
      "non-Cerveny families reject non-minimum beam width");
  context.expectThrows<ValidationError>(
      [&makeMetadataCase] {
        static_cast<void>(makeMetadataCase(
            SimulationRunMode::RayTrace, BeamFamily::CervenyGaussian,
            FieldComponent::Pressure, BoundaryCurvatureMode::Standard,
            BeamWidthMode::Wkb));
      },
      "non-TL products reject non-minimum beam width");
  const SimulationCase simpleGaussian =
      makeMetadataCase(SimulationRunMode::Coherent, BeamFamily::SimpleGaussian);
  context.check(simpleGaussian.beamFamily() == BeamFamily::SimpleGaussian,
                "SimulationCase accepts coherent Simple Gaussian TL");
  context.expectThrows<ValidationError>(
      [&makeMetadataCase] {
        static_cast<void>(makeMetadataCase(SimulationRunMode::Incoherent,
                                           BeamFamily::SimpleGaussian));
      },
      "SimulationCase rejects unsupported Simple Gaussian intensity mode");
  context.expectThrows<ValidationError>(
      [&makeMetadataCase] {
        static_cast<void>(makeMetadataCase(static_cast<SimulationRunMode>(999),
                                           BeamFamily::CervenyGaussian));
      },
      "SimulationCase rejects invalid run mode enum values");
  context.expectThrows<ValidationError>(
      [&makeMetadataCase] {
        static_cast<void>(makeMetadataCase(SimulationRunMode::Coherent,
                                           static_cast<BeamFamily>(999)));
      },
      "SimulationCase rejects invalid beam family enum values");
  context.expectThrows<ValidationError>(
      [&makeMetadataCase] {
        static_cast<void>(makeMetadataCase(SimulationRunMode::Coherent,
                                           BeamFamily::CervenyGaussian,
                                           static_cast<FieldComponent>(999)));
      },
      "SimulationCase rejects invalid field component enum values");
  context.expectThrows<ValidationError>(
      [&makeMetadataCase] {
        static_cast<void>(makeMetadataCase(
            SimulationRunMode::Coherent, BeamFamily::CervenyGaussian,
            FieldComponent::Pressure, static_cast<BoundaryCurvatureMode>(999)));
      },
      "SimulationCase rejects invalid curvature mode enum values");
  context.expectThrows<ValidationError>(
      [&makeMetadataCase] {
        static_cast<void>(makeMetadataCase(
            SimulationRunMode::Coherent, BeamFamily::CervenyGaussian,
            FieldComponent::Pressure, BoundaryCurvatureMode::Standard,
            static_cast<BeamWidthMode>(999)));
      },
      "SimulationCase rejects invalid beam width enum values");
}

void testRayPathCache(Context& context) {
  RayPathCache cache;
  cache.reserve(1U);
  cache.append(makeRayPath());
  context.expectThrows<std::logic_error>(
      [&cache] { static_cast<void>(cache.at(0U)); },
      "unfrozen RayPathCache cannot be consumed");
  context.expectThrows<std::logic_error>(
      [&cache] { static_cast<void>(cache.contentFingerprint()); },
      "unfrozen RayPathCache cannot be fingerprinted");
  cache.freeze();

  context.check(cache.frozen(), "RayPathCache reports frozen state");
  context.check(cache.size() == 1U, "RayPathCache retains appended path");
  context.check(cache.at(0U).points.size() == 2U,
                "RayPathCache owns complete point state");
  context.check(cache.memoryFootprintBytes() >= sizeof(RayPathCache),
                "RayPathCache reports a non-trivial memory footprint");
  const std::uint64_t fingerprint = cache.contentFingerprint();
  context.check(cache.contentFingerprint() == fingerprint,
                "frozen RayPathCache fingerprint remains stable");

  RayPathCache sameContentCache;
  sameContentCache.reserve(32U);
  sameContentCache.append(makeRayPath());
  sameContentCache.freeze();
  context.check(sameContentCache.contentFingerprint() == fingerprint,
                "equal cache content has an equal fingerprint regardless of "
                "capacity");

  RayPath changedLaunchAnglePath = makeRayPath();
  changedLaunchAnglePath.launchAngle = 0.01;
  RayPathCache changedLaunchAngleCache;
  changedLaunchAngleCache.append(std::move(changedLaunchAnglePath));
  changedLaunchAngleCache.freeze();
  context.check(changedLaunchAngleCache.contentFingerprint() != fingerprint,
                "ray launch angle affects the fingerprint");

  RayPath changedStatePath = makeRayPath();
  changedStatePath.points.back().dynamicP[1] = 0.125;
  RayPathCache changedStateCache;
  changedStateCache.append(std::move(changedStatePath));
  changedStateCache.freeze();
  context.check(changedStateCache.contentFingerprint() != fingerprint,
                "RayState semantic fields affect the fingerprint");

  RayPath changedStepPath = makeRayPath();
  changedStepPath.steps.front().midpoint.range = 5.25;
  RayPathCache changedStepCache;
  changedStepCache.append(std::move(changedStepPath));
  changedStepCache.freeze();
  context.check(changedStepCache.contentFingerprint() != fingerprint,
                "StepQuadrature semantic fields affect the fingerprint");

  RayPath changedTerminationPath = makeRayPath();
  changedTerminationPath.terminationReason = RayTerminationReason::PointLimit;
  RayPathCache changedTerminationCache;
  changedTerminationCache.append(std::move(changedTerminationPath));
  changedTerminationCache.freeze();
  context.check(changedTerminationCache.contentFingerprint() != fingerprint,
                "ray termination reason affects the fingerprint");

  RayPathCache reflectedCache;
  reflectedCache.append(makeReflectedRayPath());
  reflectedCache.freeze();
  context.check(reflectedCache.at(0U).points.size() == 4U &&
                    reflectedCache.at(0U).steps.size() == 2U &&
                    reflectedCache.at(0U).events.size() == 1U,
                "double-point reflection mapping is accepted");

  RayPath changedEventPath = makeReflectedRayPath();
  changedEventPath.events.front().boundarySegmentIndex = 1U;
  RayPathCache changedEventCache;
  changedEventCache.append(std::move(changedEventPath));
  changedEventCache.freeze();
  context.check(changedEventCache.contentFingerprint() !=
                    reflectedCache.contentFingerprint(),
                "ReflectionEvent semantic fields affect the fingerprint");

  RayPath longMaterialPath = makeReflectedRayPath();
  longMaterialPath.events.front().longMaterialOverride =
      rayreuse::FrozenBoundaryMaterial{
          .material = {.compressionalSoundSpeed = 1800.0,
                       .shearSoundSpeed = 600.0,
                       .density = 1.8,
                       .compressionalAttenuation = {.value = 0.2},
                       .shearAttenuation = {.value = 0.4}},
          .attenuationEvaluationDepth =
              rayreuse::kLegacyLongBoundaryAttenuationDepth};
  RayPathCache longMaterialCache;
  longMaterialCache.append(longMaterialPath);
  longMaterialCache.freeze();

  longMaterialPath.events.front().longMaterialOverride->material.density = 1.9;
  RayPathCache changedLongMaterialCache;
  changedLongMaterialCache.append(std::move(longMaterialPath));
  changedLongMaterialCache.freeze();
  context.check(changedLongMaterialCache.contentFingerprint() !=
                    longMaterialCache.contentFingerprint(),
                "frozen long-boundary raw material affects the fingerprint");

  RayPathCache orderedCache;
  orderedCache.append(makeRayPath());
  orderedCache.append(makeReflectedRayPath());
  orderedCache.freeze();
  RayPathCache reversedCache;
  reversedCache.append(makeReflectedRayPath());
  reversedCache.append(makeRayPath());
  reversedCache.freeze();
  context.check(
      orderedCache.contentFingerprint() != reversedCache.contentFingerprint(),
      "cache path order affects the fingerprint");
  context.check(orderedCache.contentFingerprint() != fingerprint,
                "cache container sizes affect the fingerprint");

  context.expectThrows<std::logic_error>(
      [&cache] { cache.append(makeRayPath()); },
      "frozen RayPathCache rejects mutation");

  const double cachedTravelTime = cache.at(0U).points.back().realTravelTime;
  RayFrequencyState frequencyState{
      .frequency = 50.0,
      .points = std::vector<RayFrequencyPoint>(
          cache.at(0U).points.size(),
          RayFrequencyPoint{.complexTravelTime = {1.0, 2.0},
                            .amplitude = 0.5,
                            .reflectionPhase = 0.25,
                            .active = true})};
  frequencyState.points.back().amplitude = 0.25;
  context.check(cache.at(0U).points.back().realTravelTime == cachedTravelTime,
                "frequency-state mutation cannot change cached geometry");

  RayPath invalidEventPath = makeRayPath();
  invalidEventPath.events.push_back(
      {.rayPointIndex = 99U,
       .boundary = rayreuse::ReflectionBoundary::SeaSurface,
       .boundarySegmentIndex = 0U,
       .position = {0.0, 0.0},
       .boundaryTangent = {1.0, 0.0},
       .outwardNormal = {0.0, -1.0},
       .incidentSlowness = {1.0 / 1500.0, -1.0 / 1500.0},
       .reflectedSlowness = {1.0 / 1500.0, 1.0 / 1500.0},
       .tangentSlowness = 1.0 / 1500.0,
       .normalSlowness = 1.0 / 1500.0});
  RayPathCache invalidEventCache;
  invalidEventCache.append(std::move(invalidEventPath));
  context.expectThrows<ValidationError>(
      [&invalidEventCache] { invalidEventCache.freeze(); },
      "reflection events must reference an existing ray point");

  RayPath invalidQuadraturePath = makeRayPath();
  invalidQuadraturePath.steps.front().midpointWeight = 9.0;
  RayPathCache invalidQuadratureCache;
  invalidQuadratureCache.append(std::move(invalidQuadraturePath));
  context.expectThrows<ValidationError>(
      [&invalidQuadratureCache] { invalidQuadratureCache.freeze(); },
      "quadrature weights must reproduce the actual step length");
}

void testFrequencyWorkspace(Context& context) {
  const ReceiverGrid receivers({10.0, 20.0}, {100.0, 200.0, 300.0});
  rayreuse::FrequencyWorkspace workspace(50.0, receivers);
  context.check(workspace.pressure().size() == 6U,
                "workspace allocates depth-major pressure slice");
  workspace.at(1U, 2U) = std::complex<double>{3.0, -4.0};
  context.check(workspace.at(1U, 2U) == std::complex<double>{3.0, -4.0},
                "workspace range is contiguous inside each depth");
  context.check(workspace.pressure()[5U] == std::complex<double>{3.0, -4.0},
                "workspace pressure span exposes depth-major linear storage");
  workspace.pressure()[3U] = std::complex<double>{-2.0, 1.0};
  context.check(workspace.at(1U, 0U) == std::complex<double>{-2.0, 1.0},
                "workspace checked and linear access share the same storage");
  workspace.clear();
  context.check(workspace.at(1U, 2U) == std::complex<double>{},
                "workspace clear resets pressure");
  context.expectThrows<std::out_of_range>(
      [&workspace] { static_cast<void>(workspace.at(2U, 0U)); },
      "workspace rejects invalid depth index");
  context.expectThrows<ValidationError>(
      [&receivers] {
        static_cast<void>(rayreuse::FrequencyWorkspace(0.0, receivers));
      },
      "workspace rejects non-positive frequency");

  rayreuse::IntensityWorkspace intensity(50.0, receivers);
  intensity.add(1U, 2U, 3.0);
  intensity.add(1U, 2U, 4.0);
  context.check(
      intensity.intensity().size() == 6U && intensity.at(1U, 2U) == 7.0,
      "intensity workspace accumulates depth-major non-negative "
      "power");
  intensity.clear();
  context.check(intensity.at(1U, 2U) == 0.0,
                "intensity workspace clear resets power");
  context.expectThrows<ValidationError>(
      [&intensity] { intensity.add(0U, 0U, -1.0); },
      "intensity workspace rejects negative contributions");
  context.expectThrows<std::out_of_range>(
      [&intensity] { static_cast<void>(intensity.at(2U, 0U)); },
      "intensity workspace rejects an invalid depth index");
}

}  // namespace

int main() {
  Context context;
  testVec2(context);
  testSimulationCase(context);
  testSimulationProductMetadata(context);
  testRayPathCache(context);
  testFrequencyWorkspace(context);

  if (context.failureCount() != 0) {
    std::cerr << context.failureCount() << " test assertion(s) failed\n";
    return 1;
  }
  std::cout << "All Bellhop RayReuse core type tests passed\n";
  return 0;
}
