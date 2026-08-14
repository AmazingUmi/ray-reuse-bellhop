#include <array>
#include <complex>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <numbers>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "bellhop/cache/ray_path_cache.hpp"
#include "bellhop/error.hpp"
#include "bellhop/field/frequency_workspace.hpp"
#include "bellhop/model/environment.hpp"
#include "bellhop/model/simulation_case.hpp"
#include "bellhop/numerics/vec2.hpp"
#include "support/test_harness.hpp"

namespace {

using bellhop::AcousticMaterial;
using bellhop::AttenuationUnit;
using bellhop::BoundaryGeometry;
using bellhop::BoundaryModel;
using bellhop::BoundaryOrientation;
using bellhop::BiologicalAttenuationLayer;
using bellhop::BiologicalAttenuationLayers;
using bellhop::Environment;
using bellhop::FrequencyGrid;
using bellhop::FrozenBoundaryMaterial;
using bellhop::GrainSizeMaterial;
using bellhop::IntegratorSettings;
using bellhop::LaunchFan;
using bellhop::RayFrequencyPoint;
using bellhop::QuadrilateralSspGrid;
using bellhop::RayFrequencyState;
using bellhop::RayPath;
using bellhop::RayPathCache;
using bellhop::RayState;
using bellhop::RayTerminationReason;
using bellhop::ReceiverGrid;
using bellhop::ReceiverGridLayout;
using bellhop::SimulationCase;
using bellhop::SoundSpeedPoint;
using bellhop::SoundSpeedProfile;
using bellhop::Source;
using bellhop::StepQuadrature;
using bellhop::TabulatedReflectionTable;
using bellhop::ValidationError;
using bellhop::Vec2;
using bellhop::VolumeAttenuation;
using bellhop::VolumeAttenuationModel;
using bellhop::SharedBiologicalAttenuationLayers;
using bellhop::test::Context;

Environment makeEnvironment() {
  std::vector<SoundSpeedPoint> points{
      {.depth = 0.0, .soundSpeed = 1500.0, .density = 1000.0},
      {.depth = 1000.0, .soundSpeed = 1500.0, .density = 1000.0},
  };
  return Environment(SoundSpeedProfile(std::move(points)),
                     BoundaryModel::vacuum(0.0),
                     BoundaryModel::rigid(1000.0));
}

void testEnvironmentVolumeAttenuation(Context& context) {
  const auto layers =
      std::make_shared<const BiologicalAttenuationLayers>(
          BiologicalAttenuationLayers{
              BiologicalAttenuationLayer{
                  .minimumDepth = 100.0,
                  .maximumDepth = 200.0,
                  .resonanceFrequency = 1000.0,
                  .qualityFactor = 10.0,
                  .attenuationCoefficientDecibelsPerKilometer = 0.25}});
  const Environment biological(
      SoundSpeedProfile({
          {.depth = 0.0, .soundSpeed = 1500.0, .density = 1000.0},
          {.depth = 1000.0, .soundSpeed = 1500.0, .density = 1000.0}}),
      BoundaryModel::vacuum(0.0), BoundaryModel::rigid(1000.0),
      VolumeAttenuation{
          .model = VolumeAttenuationModel::Biological,
          .parameters = layers});
  const Environment copied = biological;
  const auto& copiedLayers =
      std::get<SharedBiologicalAttenuationLayers>(
          copied.volumeAttenuation().parameters);
  context.check(copiedLayers.get() == layers.get(),
                "Environment copies share immutable biological layer storage");

  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(Environment(
            SoundSpeedProfile({
                {.depth = 0.0,
                 .soundSpeed = 1500.0,
                 .density = 1000.0},
                {.depth = 1000.0,
                 .soundSpeed = 1500.0,
                 .density = 1000.0}}),
            BoundaryModel::vacuum(0.0),
            BoundaryModel::rigid(1000.0),
            VolumeAttenuation{
                .model = static_cast<VolumeAttenuationModel>(999)}));
      },
      "Environment rejects an unknown volume attenuation model");

  const AcousticMaterial fluidMaterial{
      .compressionalSoundSpeed = 1600.0,
      .shearSoundSpeed = 0.0,
      .density = 1500.0};
  const auto longMaterials =
      std::make_shared<const std::vector<AcousticMaterial>>(
          std::vector<AcousticMaterial>{fluidMaterial, fluidMaterial});
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(BoundaryModel::acousticHalfSpace(
            BoundaryGeometry::curvilinear(
                {{.range = 0.0, .depth = 1000.0},
                 {.range = 1000.0, .depth = 1000.0}},
                1000.0, BoundaryOrientation::Lower),
            fluidMaterial, longMaterials));
      },
      "curvilinear long-format boundary materials remain unsupported");

  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(BoundaryModel::acousticHalfSpace(
            BoundaryGeometry::piecewiseLinear(
                {{.range = 0.0, .depth = 1000.0},
                 {.range = 1000.0, .depth = 1000.0}},
                1000.0, BoundaryOrientation::Lower),
            fluidMaterial,
            std::make_shared<const std::vector<AcousticMaterial>>(
                std::vector<AcousticMaterial>{fluidMaterial})));
      },
      "long-format material count must match the boundary node count");

  AcousticMaterial elasticMaterial = fluidMaterial;
  elasticMaterial.shearSoundSpeed = 100.0;
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(BoundaryModel::acousticHalfSpace(
            BoundaryGeometry::piecewiseLinear(
                {{.range = 0.0, .depth = 1000.0},
                 {.range = 1000.0, .depth = 1000.0}},
                1000.0, BoundaryOrientation::Lower),
            fluidMaterial,
            std::make_shared<const std::vector<AcousticMaterial>>(
                std::vector<AcousticMaterial>{elasticMaterial,
                                              elasticMaterial})));
      },
      "elastic long-format boundary materials remain unsupported");

  const BoundaryModel grain = BoundaryModel::grainSizeHalfSpace(1000.0, 2.6);
  context.check(grain.grainSizeMaterial().has_value(),
                "grain-size half-space owns its derived coefficients");
  if (grain.grainSizeMaterial().has_value()) {
    const GrainSizeMaterial& properties = *grain.grainSizeMaterial();
    context.checkNear(properties.soundSpeedRatio,
                      1.10143906552903359, 0.0,
                      "grain-size sound-speed ratio follows Origin branch");
    context.checkNear(properties.densityRatio,
                      1.42501035840809331, 0.0,
                      "grain-size density ratio follows Origin branch");
    context.checkNear(properties.attenuationCoefficient,
                      0.521499992907047294, 0.0,
                      "grain-size attenuation follows Origin branch");
  }

  struct GrainAnchor {
    double meanGrainSize;
    double soundSpeedRatio;
    double densityRatio;
    double attenuationCoefficient;
  };
  const std::array grainAnchors{
      GrainAnchor{-1.0, 1.33696096250787377, 2.49226699490100145,
                  0.455599993467330933},
      GrainAnchor{0.0, 1.27779996395111084, 2.31389999389648438,
                  0.455599993467330933},
      GrainAnchor{1.0, 1.22412577527575195, 2.15107646770775318,
                  0.480099992826581001},
      GrainAnchor{2.6, 1.10143906552903359, 1.42501035840809331,
                  0.521499992907047294},
      GrainAnchor{4.5, 1.01786019126302563, 1.19483112567104399,
                  0.757144819945096970},
      GrainAnchor{5.3, 0.989022205943008936, 1.14957354817725732,
                  0.314587988108397387},
      GrainAnchor{6.0, 0.987305558286607265, 1.14871618198230863,
                  0.139699976891279221},
      GrainAnchor{9.5, 0.978792158653959632, 1.14417563204187900,
                  0.0601000003516674042},
  };
  for (const GrainAnchor& anchor : grainAnchors) {
    const auto derived = *BoundaryModel::grainSizeHalfSpace(
                              1000.0, anchor.meanGrainSize)
                              .grainSizeMaterial();
    context.checkNear(derived.soundSpeedRatio, anchor.soundSpeedRatio, 0.0,
                      "grain-size speed breakpoint matches gfortran");
    context.checkNear(derived.densityRatio, anchor.densityRatio, 0.0,
                      "grain-size density breakpoint matches gfortran");
    context.checkNear(derived.attenuationCoefficient,
                      anchor.attenuationCoefficient, 0.0,
                      "grain-size attenuation breakpoint matches gfortran");
  }
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(BoundaryModel::grainSizeHalfSpace(
            BoundaryGeometry::flat(0.0, BoundaryOrientation::Upper), 1.0));
      },
      "grain-size half-space rejects upper-boundary geometry");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(BoundaryModel::grainSizeHalfSpace(
            1000.0, std::numeric_limits<double>::quiet_NaN()));
      },
      "grain-size half-space rejects non-finite mean grain size");

  const auto reflectionTable =
      std::make_shared<const TabulatedReflectionTable>(
          TabulatedReflectionTable{
              {.angleDegrees = 0.0, .magnitude = 1.0, .phaseRadians = 0.0},
              {.angleDegrees = 90.0, .magnitude = 0.0,
               .phaseRadians = std::numbers::pi}});
  const BoundaryModel tabulated = BoundaryModel::tabulatedReflection(
      BoundaryGeometry::flat(1000.0, BoundaryOrientation::Lower),
      reflectionTable);
  context.check(tabulated.reflectionTable().get() == reflectionTable.get(),
                "tabulated reflection shares immutable table storage");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(BoundaryModel::tabulatedReflection(
            BoundaryGeometry::flat(1000.0, BoundaryOrientation::Lower),
            std::make_shared<const TabulatedReflectionTable>(
                TabulatedReflectionTable{
                    {.angleDegrees = 0.0, .magnitude = 1.0},
                    {.angleDegrees = 0.0, .magnitude = 0.5}})));
      },
      "tabulated reflection rejects duplicate angles");
}

SimulationCase makeSimulationCase(
    std::vector<double> frequencies,
    bellhop::FieldComponent component = bellhop::FieldComponent::Pressure,
    bellhop::SourceGeometry sourceGeometry =
        bellhop::SourceGeometry::Point,
    bellhop::SimulationRunMode runMode =
        bellhop::SimulationRunMode::CoherentTransmissionLoss,
    bellhop::CervenyCoordinateSystem coordinateSystem =
        bellhop::CervenyCoordinateSystem::Cartesian,
    bellhop::BeamFamily beamFamily =
        bellhop::BeamFamily::CervenyGaussian) {
  return SimulationCase(
      makeEnvironment(), Source{.depth = 500.0, .amplitude = 1.0},
      ReceiverGrid({400.0, 500.0, 600.0}, {100.0, 1000.0, 5000.0}),
      FrequencyGrid(std::move(frequencies)),
      LaunchFan{
          .minimumAngle = -0.1,
          .maximumAngle = 0.1,
          .explicitLaunchAngleCount = 301U},
      IntegratorSettings{.stepLength = 10.0,
                         .rangeLimit = 5100.0,
                         .depthLimit = 1100.0,
                         .maximumRayPoints = 10000U},
      bellhop::SourceBeamPattern::omnidirectional(),
      runMode, component, sourceGeometry, coordinateSystem, beamFamily);
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
  constexpr double kSlownessComponent =
      1.0 / (1500.0 * 1.4142135623730950488);
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
       .boundary = bellhop::ReflectionBoundary::SeaSurface,
       .boundarySegmentIndex = 0U,
       .boundaryCurvature = 0.0,
       .position = {10.0, 0.0},
       .boundaryTangent = {1.0, 0.0},
       .outwardNormal = {0.0, -1.0},
       .incidentSlowness = {kSlownessComponent, -kSlownessComponent},
       .reflectedSlowness = {kSlownessComponent, kSlownessComponent},
       .tangentSlowness = kSlownessComponent,
       .normalSlowness = kSlownessComponent,
       .longMaterialOverride = std::nullopt},
  };
  path.terminationReason = RayTerminationReason::ExitedDomain;
  return path;
}

RayPath makeLegacyFrameReflectedRayPath() {
  constexpr double component =
      1.0 / (1500.0 * 1.4142135623730950488);
  constexpr Vec2 tangent{0.8, 0.0};
  constexpr Vec2 normal{0.0, -0.8};
  const Vec2 incident{component, -component};
  const double normalSlowness = bellhop::dot(incident, normal);
  const Vec2 reflected{
      .range = std::fma(-2.0 * normalSlowness, normal.range,
                        incident.range),
      .depth = std::fma(-2.0 * normalSlowness, normal.depth,
                        incident.depth)};

  RayPath path;
  path.launchAngle = -0.78539816339744830962;
  path.points = {
      RayState{.position = {10.0, 0.0},
               .slowness = incident,
               .dynamicP = {1.0, 0.0},
               .dynamicQ = {15000.0, 1.0},
               .soundSpeed = 1500.0,
               .realTravelTime = 10.0 / 1500.0},
      RayState{.position = {10.0, 0.0},
               .slowness = reflected,
               .dynamicP = {1.0, 0.0},
               .dynamicQ = {15000.0, 1.0},
               .soundSpeed = 1500.0,
               .realTravelTime = 10.0 / 1500.0},
  };
  path.events = {
      {.rayPointIndex = 0U,
       .reflectedRayPointIndex = 1U,
       .boundary = bellhop::ReflectionBoundary::SeaSurface,
       .boundarySegmentIndex = 3U,
       .boundaryCurvature = 1.0e-3,
       .position = {10.0, 0.0},
       .boundaryTangent = tangent,
       .outwardNormal = normal,
       .incidentSlowness = incident,
       .reflectedSlowness = reflected,
       .tangentSlowness = bellhop::dot(incident, tangent),
       .normalSlowness = normalSlowness,
       .longMaterialOverride = std::nullopt},
  };
  path.terminationReason = RayTerminationReason::PointLimit;
  return path;
}

void testVec2(Context& context) {
  const Vec2 value{3.0, 4.0};
  context.checkNear(bellhop::norm(value), 5.0, 1.0e-15,
                    "Vec2 norm uses range/depth components");
  context.check(bellhop::dot(value, Vec2{1.0, 2.0}) == 11.0,
                "Vec2 dot product");
  context.check(value + Vec2{2.0, -1.0} == Vec2{5.0, 3.0},
                "Vec2 addition");
}

void testSimulationCase(Context& context) {
  const SimulationCase simulation = makeSimulationCase({50.0});
  context.check(simulation.frequencies().size() == 1U,
                "F2CPP case contains exactly one frequency");
  context.check(
      simulation.fieldComponent() == bellhop::FieldComponent::Pressure &&
          makeSimulationCase({50.0}, bellhop::FieldComponent::Horizontal)
                  .fieldComponent() == bellhop::FieldComponent::Horizontal,
      "SimulationCase preserves the selected field component");
  context.check(
      simulation.sourceGeometry() == bellhop::SourceGeometry::Point &&
          makeSimulationCase(
              {50.0}, bellhop::FieldComponent::Pressure,
              bellhop::SourceGeometry::Line)
                  .sourceGeometry() == bellhop::SourceGeometry::Line,
      "SimulationCase preserves the selected source geometry");
  context.check(
      simulation.cervenyCoordinateSystem() ==
              bellhop::CervenyCoordinateSystem::Cartesian &&
          makeSimulationCase(
              {50.0}, bellhop::FieldComponent::Pressure,
              bellhop::SourceGeometry::Point,
              bellhop::SimulationRunMode::CoherentTransmissionLoss,
              bellhop::CervenyCoordinateSystem::RayCentered)
                  .cervenyCoordinateSystem() ==
              bellhop::CervenyCoordinateSystem::RayCentered,
      "SimulationCase defaults to Cartesian and preserves ray-centered beams");
  context.check(
      simulation.beamFamily() == bellhop::BeamFamily::CervenyGaussian &&
          makeSimulationCase(
              {50.0}, bellhop::FieldComponent::Pressure,
              bellhop::SourceGeometry::Point,
              bellhop::SimulationRunMode::CoherentTransmissionLoss,
              bellhop::CervenyCoordinateSystem::RayCentered,
              bellhop::BeamFamily::GeometricHat)
                  .beamFamily() == bellhop::BeamFamily::GeometricHat &&
          makeSimulationCase(
              {50.0}, bellhop::FieldComponent::Pressure,
              bellhop::SourceGeometry::Line,
              bellhop::SimulationRunMode::IncoherentTransmissionLoss,
              bellhop::CervenyCoordinateSystem::Cartesian,
              bellhop::BeamFamily::GeometricGaussian)
                  .beamFamily() ==
              bellhop::BeamFamily::GeometricGaussian &&
          makeSimulationCase(
              {50.0}, bellhop::FieldComponent::Pressure,
              bellhop::SourceGeometry::Point,
              bellhop::SimulationRunMode::CoherentTransmissionLoss,
              bellhop::CervenyCoordinateSystem::Cartesian,
              bellhop::BeamFamily::SimpleGaussian)
                  .beamFamily() == bellhop::BeamFamily::SimpleGaussian,
      "SimulationCase preserves every supported beam family");
  const std::array transmissionLossModes{
      bellhop::SimulationRunMode::CoherentTransmissionLoss,
      bellhop::SimulationRunMode::IncoherentTransmissionLoss,
      bellhop::SimulationRunMode::SemiCoherentTransmissionLoss};
  for (const bellhop::SimulationRunMode mode : transmissionLossModes) {
    context.check(bellhop::isTransmissionLossMode(mode),
                  "all three field modes are transmission-loss modes");
  }
  context.check(
      !bellhop::isTransmissionLossMode(
          bellhop::SimulationRunMode::RayTrace) &&
          bellhop::fieldAccumulationKind(
              bellhop::SimulationRunMode::RayTrace) ==
              bellhop::FieldAccumulationKind::None &&
          bellhop::fieldAccumulationKind(
              bellhop::SimulationRunMode::CoherentTransmissionLoss) ==
              bellhop::FieldAccumulationKind::ComplexPressure &&
          bellhop::fieldAccumulationKind(
              bellhop::SimulationRunMode::IncoherentTransmissionLoss) ==
              bellhop::FieldAccumulationKind::Intensity &&
          bellhop::fieldAccumulationKind(
              bellhop::SimulationRunMode::SemiCoherentTransmissionLoss) ==
              bellhop::FieldAccumulationKind::Intensity,
      "run modes select an explicit field accumulation domain");
  context.check(
      !bellhop::usesLloydMirror(
          bellhop::SimulationRunMode::CoherentTransmissionLoss) &&
          !bellhop::usesLloydMirror(
              bellhop::SimulationRunMode::IncoherentTransmissionLoss) &&
          bellhop::usesLloydMirror(
              bellhop::SimulationRunMode::SemiCoherentTransmissionLoss) &&
          !bellhop::usesLloydMirror(
              bellhop::SimulationRunMode::RayTrace),
      "only semi-coherent TL uses the Lloyd mirror");
  context.check(
      makeSimulationCase(
          {50.0}, bellhop::FieldComponent::Pressure,
          bellhop::SourceGeometry::Point,
          bellhop::SimulationRunMode::IncoherentTransmissionLoss)
              .runMode() ==
          bellhop::SimulationRunMode::IncoherentTransmissionLoss &&
          makeSimulationCase(
              {50.0}, bellhop::FieldComponent::Pressure,
              bellhop::SourceGeometry::Point,
              bellhop::SimulationRunMode::SemiCoherentTransmissionLoss)
                  .runMode() ==
              bellhop::SimulationRunMode::SemiCoherentTransmissionLoss,
      "SimulationCase preserves incoherent and semi-coherent modes");
  context.check(simulation.frequencies().designFrequency() == 50.0,
                "single frequency is the design frequency");
  context.check(simulation.environment().waterDepth() == 1000.0,
                "environment reports water depth in metres");
  context.check(simulation.receivers().rangeCount() == 3U,
                "receiver range count");
  context.check(simulation.launchFanPlan().launchAngleCount == 300U,
                "SimulationCase enforces D-02 instead of accepting 301");
  context.check(
      simulation.launchFanPlan().launchAngles.size() ==
          simulation.launchFanPlan().launchAngleCount,
      "SimulationCase stores the complete derived launch fan");

  context.expectThrows<ValidationError>(
      [] { static_cast<void>(makeSimulationCase({})); },
      "empty frequency list is rejected");
  context.expectThrows<ValidationError>(
      [] { static_cast<void>(makeSimulationCase({50.0, 100.0})); },
      "multiple frequencies are rejected by F2CPP");
  context.expectThrows<ValidationError>(
      [] { static_cast<void>(makeSimulationCase({0.0})); },
      "non-positive frequencies are rejected");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(makeSimulationCase(
            {50.0}, static_cast<bellhop::FieldComponent>(999)));
      },
      "unknown field components are rejected at the model boundary");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(makeSimulationCase(
            {50.0}, bellhop::FieldComponent::Pressure,
            static_cast<bellhop::SourceGeometry>(999)));
      },
      "unknown source geometry is rejected at the model boundary");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(makeSimulationCase(
            {50.0}, bellhop::FieldComponent::Pressure,
            bellhop::SourceGeometry::Point,
            static_cast<bellhop::SimulationRunMode>(999)));
      },
      "unknown run modes are rejected at the model boundary");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(makeSimulationCase(
            {50.0}, bellhop::FieldComponent::Pressure,
            bellhop::SourceGeometry::Point,
            bellhop::SimulationRunMode::CoherentTransmissionLoss,
            static_cast<bellhop::CervenyCoordinateSystem>(999)));
      },
      "unknown Cerveny coordinate systems are rejected at the model boundary");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(makeSimulationCase(
            {50.0}, bellhop::FieldComponent::Pressure,
            bellhop::SourceGeometry::Point,
            bellhop::SimulationRunMode::CoherentTransmissionLoss,
            bellhop::CervenyCoordinateSystem::Cartesian,
            static_cast<bellhop::BeamFamily>(999)));
      },
      "unknown beam families are rejected at the model boundary");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(makeSimulationCase(
            {50.0}, bellhop::FieldComponent::Pressure,
            bellhop::SourceGeometry::Point,
            bellhop::SimulationRunMode::CoherentTransmissionLoss,
            bellhop::CervenyCoordinateSystem::RayCentered,
            bellhop::BeamFamily::GeometricGaussian));
      },
      "geometric Gaussian rejects ray-centered coordinates");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(makeSimulationCase(
            {50.0}, bellhop::FieldComponent::Vertical,
            bellhop::SourceGeometry::Point,
            bellhop::SimulationRunMode::CoherentTransmissionLoss,
            bellhop::CervenyCoordinateSystem::Cartesian,
            bellhop::BeamFamily::GeometricHat));
      },
      "non-Cerveny families reject non-pressure field components");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(makeSimulationCase(
            {50.0}, bellhop::FieldComponent::Pressure,
            bellhop::SourceGeometry::Line,
            bellhop::SimulationRunMode::CoherentTransmissionLoss,
            bellhop::CervenyCoordinateSystem::Cartesian,
            bellhop::BeamFamily::SimpleGaussian));
      },
      "simple Gaussian rejects line sources");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(makeSimulationCase(
            {50.0}, bellhop::FieldComponent::Pressure,
            bellhop::SourceGeometry::Point,
            bellhop::SimulationRunMode::IncoherentTransmissionLoss,
            bellhop::CervenyCoordinateSystem::Cartesian,
            bellhop::BeamFamily::SimpleGaussian));
      },
      "simple Gaussian rejects non-coherent field modes");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(SimulationCase(
            makeEnvironment(), Source{.depth = 500.0, .amplitude = 1.0},
            ReceiverGrid(
                {400.0, 600.0}, {100.0, 1000.0},
                bellhop::ReceiverGridLayout::Irregular),
            FrequencyGrid({50.0}),
            LaunchFan{.minimumAngle = -0.1,
                      .maximumAngle = 0.1,
                      .explicitLaunchAngleCount = 300U},
            IntegratorSettings{.stepLength = 10.0,
                               .rangeLimit = 5100.0,
                               .depthLimit = 1100.0,
                               .maximumRayPoints = 10000U},
            bellhop::SourceBeamPattern::omnidirectional(),
            bellhop::SimulationRunMode::CoherentTransmissionLoss,
            bellhop::FieldComponent::Pressure,
            bellhop::SourceGeometry::Point,
            bellhop::CervenyCoordinateSystem::RayCentered,
            bellhop::BeamFamily::GeometricHat));
      },
      "ray-centered geometric hat rejects irregular receivers at the model boundary");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(bellhop::isTransmissionLossMode(
            static_cast<bellhop::SimulationRunMode>(999)));
      },
      "transmission-loss helper rejects unknown enum values");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(bellhop::fieldAccumulationKind(
            static_cast<bellhop::SimulationRunMode>(999)));
      },
      "accumulation helper rejects unknown enum values");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(bellhop::usesLloydMirror(
            static_cast<bellhop::SimulationRunMode>(999)));
      },
      "Lloyd-mirror helper rejects unknown enum values");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(
            ReceiverGrid(std::vector<double>{}, std::vector<double>{1.0}));
      },
      "empty receiver depth grid is rejected");
}

void testSourceBeamPattern(Context& context) {
  const bellhop::SourceBeamPattern pattern =
      bellhop::SourceBeamPattern::directional(
          {{.angleDegrees = -30.0, .powerDecibels = -6.0},
           {.angleDegrees = 0.0, .powerDecibels = 0.0},
           {.angleDegrees = 20.0, .powerDecibels = -12.0}});
  const double degreesToRadians = std::numbers::pi / 180.0;
  const double minusSixDb = std::pow(10.0, -6.0 / 20.0);
  const double minusTwelveDb = std::pow(10.0, -12.0 / 20.0);
  context.check(pattern.isDirectional() && pattern.size() == 3U,
                "directional source beam pattern retains its samples");
  context.checkNear(
      pattern.amplitudeForLaunchAngle(-30.0 * degreesToRadians),
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
  const bellhop::SourceBeamPattern omni =
      bellhop::SourceBeamPattern::omnidirectional();
  context.check(!omni.isDirectional(),
                "default source beam pattern is omnidirectional");
  context.checkNear(omni.amplitudeForLaunchAngle(2.0), 1.0, 0.0,
                    "omnidirectional source beam pattern is unity");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(bellhop::SourceBeamPattern::directional(
            {{.angleDegrees = 0.0, .powerDecibels = 0.0}}));
      },
      "source beam pattern rejects a single sample");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(bellhop::SourceBeamPattern::directional(
            {{.angleDegrees = 0.0, .powerDecibels = 0.0},
             {.angleDegrees = 0.0, .powerDecibels = -3.0}}));
      },
      "source beam pattern rejects duplicate angles");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(bellhop::SourceBeamPattern::directional(
            {{.angleDegrees = 0.0, .powerDecibels = 0.0},
             {.angleDegrees = 1.0, .powerDecibels = 10000.0}}));
      },
      "source beam pattern rejects amplitude-conversion overflow");
}

void testQuadrilateralGrid(Context& context) {
  const auto grid = std::make_shared<const QuadrilateralSspGrid>(
      QuadrilateralSspGrid{.rangesMeters = {0.0, 5000.0},
                           .speedsDepthMajor = {1400.0, 1450.0,
                                                1700.0, 1750.0},
                           .depthCount = 2U,
                           .rangeCount = 2U});
  const SoundSpeedProfile profile(
      {{.depth = 0.0, .soundSpeed = 1000.0, .density = 1000.0},
       {.depth = 1000.0, .soundSpeed = 1000.0, .density = 1000.0}},
      bellhop::SspInterpolationKind::Quadrilateral, grid);
  const SoundSpeedProfile copied = profile;
  context.check(copied.quadrilateralGrid().get() == grid.get(),
                "Q profile copies share immutable grid storage");
  context.checkNear(profile.quadrilateralRealSoundSpeedAt(
                        {.range = 0.0, .depth = 0.0}),
                    1400.0, 0.0, "Q query accepts its first grid node");
  context.checkNear(profile.quadrilateralRealSoundSpeedAt(
                        {.range = 5000.0, .depth = 1000.0}),
                    1750.0, 0.0, "Q query accepts its last grid node");
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(profile.quadrilateralRealSoundSpeedAt(
            {.range = -1.0, .depth = 500.0}));
      },
      "Q query rejects a range outside the grid");
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(profile.quadrilateralRealSoundSpeedAt(
            {.range = 0.0, .depth = 1001.0}));
      },
      "Q query rejects a depth outside the grid");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(SoundSpeedProfile(
            {{.depth = 0.0, .soundSpeed = 1500.0, .density = 1000.0},
             {.depth = 1000.0, .soundSpeed = 1500.0, .density = 1000.0}},
            bellhop::SspInterpolationKind::Quadrilateral));
      },
      "Q profile requires a grid");
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(SoundSpeedProfile(
            {{.depth = 0.0, .soundSpeed = 1500.0, .density = 1000.0},
             {.depth = 1000.0, .soundSpeed = 1500.0, .density = 1000.0}},
            bellhop::SspInterpolationKind::CLinear, grid));
      },
      "non-Q profile rejects a grid");

  const Environment environment(
      profile, BoundaryModel::vacuum(0.0), BoundaryModel::rigid(1000.0));
  const LaunchFan automaticFan{
      .minimumAngle = -std::numbers::pi / 4.0,
      .maximumAngle = std::numbers::pi / 4.0,
      .explicitLaunchAngleCount = std::nullopt};
  const SimulationCase simulation(
      environment, Source{.depth = 500.0, .amplitude = 1.0},
      ReceiverGrid({500.0}, {100.0, 5000.0}), FrequencyGrid({1000.0}),
      automaticFan,
      IntegratorSettings{.stepLength = 10.0, .rangeLimit = 5100.0,
                         .depthLimit = 1100.0, .maximumRayPoints = 10000U});
  const std::size_t expected = bellhop::LaunchFanPlanner::plan(
      {.frequencies = {1000.0}, .sourceSoundSpeed = 1550.0,
       .waterDepth = 1000.0, .maximumRange = 5000.0,
       .minimumLaunchAngle = -std::numbers::pi / 4.0,
       .maximumLaunchAngle = std::numbers::pi / 4.0,
       .explicitLaunchAngleCount = std::nullopt})
                                   .launchAngleCount;
  context.check(simulation.launchFanPlan().launchAngleCount == expected,
                "Q automatic launch planning uses the grid source sound speed");
}

void testRayPathCache(Context& context) {
  RayPathCache cache;
  cache.reserve(1U);
  cache.append(makeRayPath());
  context.expectThrows<std::logic_error>(
      [&cache] { static_cast<void>(cache.at(0U)); },
      "unfrozen RayPathCache cannot be consumed");
  cache.freeze();

  context.check(cache.frozen(), "RayPathCache reports frozen state");
  context.check(cache.size() == 1U, "RayPathCache retains appended path");
  context.check(cache.at(0U).points.size() == 2U,
                "RayPathCache owns complete point state");
  context.check(cache.memoryFootprintBytes() >= sizeof(RayPathCache),
                "RayPathCache reports a non-trivial memory footprint");

  RayPathCache reflectedCache;
  reflectedCache.append(makeReflectedRayPath());
  reflectedCache.freeze();
  context.check(reflectedCache.at(0U).points.size() == 4U &&
                    reflectedCache.at(0U).steps.size() == 2U &&
                    reflectedCache.at(0U).events.size() == 1U,
                "double-point reflection mapping is accepted");

  RayPathCache legacyFrameCache;
  legacyFrameCache.append(makeLegacyFrameReflectedRayPath());
  legacyFrameCache.freeze();
  context.check(
      legacyFrameCache.at(0U).events.front().boundaryTangent ==
          Vec2{0.8, 0.0},
      "RayPathCache accepts an unnormalized legacy curvilinear frame");

  const FrozenBoundaryMaterial validFrozenMaterial{
      .material = AcousticMaterial{
          .compressionalSoundSpeed = 1600.0,
          .shearSoundSpeed = 0.0,
          .density = 1500.0,
          .compressionalAttenuation = {
              .value = 0.1,
              .unit = AttenuationUnit::DecibelsPerWavelength}},
      .attenuationEvaluationDepth =
          bellhop::kLegacyLongBoundaryAttenuationDepth};
  RayPath frozenMaterialPath = makeReflectedRayPath();
  frozenMaterialPath.events.front().longMaterialOverride =
      validFrozenMaterial;
  RayPathCache frozenMaterialCache;
  frozenMaterialCache.append(std::move(frozenMaterialPath));
  frozenMaterialCache.freeze();

  RayPath invalidFrozenDepth = makeReflectedRayPath();
  invalidFrozenDepth.events.front().longMaterialOverride =
      validFrozenMaterial;
  invalidFrozenDepth.events.front()
      .longMaterialOverride->attenuationEvaluationDepth = 1000.0;
  RayPathCache invalidFrozenDepthCache;
  invalidFrozenDepthCache.append(std::move(invalidFrozenDepth));
  context.expectThrows<ValidationError>(
      [&invalidFrozenDepthCache] { invalidFrozenDepthCache.freeze(); },
      "frozen LL material requires the exact legacy 1e20 depth");

  RayPath invalidFrozenUnit = makeReflectedRayPath();
  invalidFrozenUnit.events.front().longMaterialOverride =
      validFrozenMaterial;
  invalidFrozenUnit.events.front()
      .longMaterialOverride->material.compressionalAttenuation.unit =
      static_cast<AttenuationUnit>(999);
  RayPathCache invalidFrozenUnitCache;
  invalidFrozenUnitCache.append(std::move(invalidFrozenUnit));
  context.expectThrows<ValidationError>(
      [&invalidFrozenUnitCache] { invalidFrozenUnitCache.freeze(); },
      "frozen LL material rejects an invalid attenuation unit");

  RayPath invalidFrozenShear = makeReflectedRayPath();
  invalidFrozenShear.events.front().longMaterialOverride =
      validFrozenMaterial;
  invalidFrozenShear.events.front()
      .longMaterialOverride->material.shearAttenuation.value = 0.1;
  RayPathCache invalidFrozenShearCache;
  invalidFrozenShearCache.append(std::move(invalidFrozenShear));
  context.expectThrows<ValidationError>(
      [&invalidFrozenShearCache] { invalidFrozenShearCache.freeze(); },
      "frozen LL material rejects shear attenuation");

  RayPath invalidLegacyMirror = makeLegacyFrameReflectedRayPath();
  invalidLegacyMirror.points[1U].slowness.depth += 1.0e-6;
  invalidLegacyMirror.events.front().reflectedSlowness =
      invalidLegacyMirror.points[1U].slowness;
  RayPathCache invalidLegacyMirrorCache;
  invalidLegacyMirrorCache.append(std::move(invalidLegacyMirror));
  context.expectThrows<ValidationError>(
      [&invalidLegacyMirrorCache] { invalidLegacyMirrorCache.freeze(); },
      "legacy frame still requires the exact Reflect2D mirror formula");

  RayPath invalidUnitFrameNorm = makeReflectedRayPath();
  invalidUnitFrameNorm.points[1U].slowness =
      1.001 * invalidUnitFrameNorm.points[1U].slowness;
  invalidUnitFrameNorm.points[2U].slowness =
      1.001 * invalidUnitFrameNorm.points[2U].slowness;
  auto& invalidUnitEvent = invalidUnitFrameNorm.events.front();
  invalidUnitEvent.incidentSlowness =
      invalidUnitFrameNorm.points[1U].slowness;
  invalidUnitEvent.reflectedSlowness =
      invalidUnitFrameNorm.points[2U].slowness;
  invalidUnitEvent.tangentSlowness = bellhop::dot(
      invalidUnitEvent.incidentSlowness,
      invalidUnitEvent.boundaryTangent);
  invalidUnitEvent.normalSlowness = bellhop::dot(
      invalidUnitEvent.incidentSlowness,
      invalidUnitEvent.outwardNormal);
  RayPathCache invalidUnitFrameNormCache;
  invalidUnitFrameNormCache.append(std::move(invalidUnitFrameNorm));
  context.expectThrows<ValidationError>(
      [&invalidUnitFrameNormCache] { invalidUnitFrameNormCache.freeze(); },
      "unit boundary frames retain the slowness-norm contract");

  context.expectThrows<std::logic_error>(
      [&cache] { cache.append(makeRayPath()); },
      "frozen RayPathCache rejects mutation");

  const double cachedTravelTime =
      cache.at(0U).points.back().realTravelTime;
  RayFrequencyState frequencyState{
      .frequency = 50.0,
      .points = std::vector<RayFrequencyPoint>(
          cache.at(0U).points.size(),
          RayFrequencyPoint{.complexTravelTime = {1.0, 2.0},
                            .amplitude = 0.5,
                            .reflectionPhase = 0.25,
                            .active = true})};
  frequencyState.points.back().amplitude = 0.25;
  context.check(
      cache.at(0U).points.back().realTravelTime == cachedTravelTime,
      "frequency-state mutation cannot change cached geometry");

  RayPath invalidEventPath = makeRayPath();
  invalidEventPath.events.push_back(
      {.rayPointIndex = 99U,
       .reflectedRayPointIndex = 100U,
       .boundary = bellhop::ReflectionBoundary::SeaSurface,
       .boundarySegmentIndex = 0U,
       .boundaryCurvature = 0.0,
       .position = {0.0, 0.0},
       .boundaryTangent = {1.0, 0.0},
       .outwardNormal = {0.0, -1.0},
       .incidentSlowness = {1.0 / 1500.0, -1.0 / 1500.0},
       .reflectedSlowness = {1.0 / 1500.0, 1.0 / 1500.0},
       .tangentSlowness = 1.0 / 1500.0,
       .normalSlowness = 1.0 / 1500.0,
       .longMaterialOverride = std::nullopt});
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
  bellhop::FrequencyWorkspace workspace(50.0, receivers);
  context.check(workspace.pressure().size() == 6U,
                "workspace allocates depth-major pressure slice");
  workspace.at(1U, 2U) = std::complex<double>{3.0, -4.0};
  context.check(workspace.at(1U, 2U) == std::complex<double>{3.0, -4.0},
                "workspace range is contiguous inside each depth");
  workspace.clear();
  context.check(workspace.at(1U, 2U) == std::complex<double>{},
                "workspace clear resets pressure");
  context.expectThrows<std::out_of_range>(
      [&workspace] { static_cast<void>(workspace.at(2U, 0U)); },
      "workspace rejects invalid depth index");
  context.expectThrows<ValidationError>(
      [&receivers] {
        static_cast<void>(bellhop::FrequencyWorkspace(0.0, receivers));
      },
      "workspace rejects non-positive frequency");

  bellhop::IntensityWorkspace intensity(50.0, receivers);
  context.check(
      intensity.frequency() == 50.0 && intensity.depthCount() == 2U &&
          intensity.rangeCount() == 3U && intensity.intensity().size() == 6U,
      "intensity workspace retains frequency and receiver dimensions");
  intensity.add(1U, 2U, 2.5);
  intensity.add(1U, 2U, 3.5);
  context.check(intensity.at(1U, 2U) == 6.0,
                "intensity workspace adds non-negative contributions");
  context.expectThrows<ValidationError>(
      [&intensity] { intensity.add(1U, 2U, -1.0); },
      "intensity workspace rejects negative contributions");
  context.expectThrows<ValidationError>(
      [&intensity] {
        intensity.add(1U, 2U,
                      std::numeric_limits<double>::quiet_NaN());
      },
      "intensity workspace rejects non-finite contributions");
  context.check(intensity.at(1U, 2U) == 6.0,
                "rejected intensity additions leave the cell unchanged");
  intensity.clear();
  intensity.add(0U, 0U, std::numeric_limits<double>::max());
  context.expectThrows<ValidationError>(
      [&intensity] {
        intensity.add(0U, 0U, std::numeric_limits<double>::max());
      },
      "intensity workspace rejects accumulation overflow");
  context.check(
      intensity.at(0U, 0U) == std::numeric_limits<double>::max(),
      "overflowing intensity addition is atomic");
  context.expectThrows<std::out_of_range>(
      [&intensity] { intensity.add(2U, 0U, 1.0); },
      "intensity workspace rejects invalid indices");
  context.expectThrows<ValidationError>(
      [&receivers] {
        static_cast<void>(bellhop::IntensityWorkspace(0.0, receivers));
      },
      "intensity workspace rejects non-positive frequency");

  const ReceiverGrid irregular(
      {15.0, 25.0, 35.0}, {100.0, 200.0, 300.0},
      ReceiverGridLayout::Irregular);
  bellhop::FrequencyWorkspace irregularWorkspace(50.0, irregular);
  context.check(
      irregular.isIrregular() && irregular.receiversPerRange() == 1U &&
          irregularWorkspace.depthCount() == 1U &&
          irregularWorkspace.pressure().size() == 3U &&
          irregular.depthAt(0U, 2U) == 35.0,
      "irregular receiver pairs use one pressure value per range");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(ReceiverGrid(
            {10.0, 20.0}, {100.0, 200.0, 300.0},
            ReceiverGridLayout::Irregular));
      },
      "irregular receiver grids require matching coordinate counts");

  std::vector<double> manyDepths(2000U);
  std::vector<double> manyRanges(1001U);
  std::iota(manyDepths.begin(), manyDepths.end(), 0.0);
  std::iota(manyRanges.begin(), manyRanges.end(), 0.0);
  for (double& depth : manyDepths) {
    depth *= 0.5;
  }
  const ReceiverGrid largeRayOnlyGrid(manyDepths, manyRanges);
  for (const bellhop::SimulationRunMode mode : {
           bellhop::SimulationRunMode::CoherentTransmissionLoss,
           bellhop::SimulationRunMode::IncoherentTransmissionLoss,
           bellhop::SimulationRunMode::SemiCoherentTransmissionLoss}) {
    context.expectThrows<ValidationError>(
        [&largeRayOnlyGrid, mode] {
          static_cast<void>(SimulationCase(
              makeEnvironment(), Source{.depth = 500.0}, largeRayOnlyGrid,
              FrequencyGrid({50.0}),
              LaunchFan{.minimumAngle = -0.1,
                        .maximumAngle = 0.1,
                        .explicitLaunchAngleCount = 2U},
              IntegratorSettings{.stepLength = 10.0,
                                 .rangeLimit = 5100.0,
                                 .depthLimit = 1100.0,
                                 .maximumRayPoints = 10000U},
              bellhop::SourceBeamPattern::omnidirectional(), mode));
        },
        "every TL mode rejects an oversized receiver workspace");
  }
  const SimulationCase largeRayOnlyCase(
      makeEnvironment(), Source{.depth = 500.0}, largeRayOnlyGrid,
      FrequencyGrid({50.0}),
      LaunchFan{.minimumAngle = -0.1,
                .maximumAngle = 0.1,
                .explicitLaunchAngleCount = 2U},
      IntegratorSettings{.stepLength = 10.0,
                         .rangeLimit = 5100.0,
                         .depthLimit = 1100.0,
                         .maximumRayPoints = 10000U},
      bellhop::SourceBeamPattern::omnidirectional(),
      bellhop::SimulationRunMode::RayTrace);
  context.check(
      largeRayOnlyCase.receivers().depthCount() == manyDepths.size() &&
          largeRayOnlyCase.receivers().rangeCount() == manyRanges.size(),
      "ray trace accepts a receiver product that does not allocate pressure");

  std::vector<double> workspaceDepths(1001U);
  std::vector<double> workspaceRanges(1000U);
  std::iota(workspaceDepths.begin(), workspaceDepths.end(), 0.0);
  std::iota(workspaceRanges.begin(), workspaceRanges.end(), 0.0);
  const ReceiverGrid largeButValidGrid(
      std::move(workspaceDepths), std::move(workspaceRanges));
  context.expectThrows<ValidationError>(
      [&largeButValidGrid] {
        static_cast<void>(SimulationCase(
            makeEnvironment(),
            std::vector<Source>{{.depth = 400.0}, {.depth = 600.0}},
            largeButValidGrid, FrequencyGrid({50.0}),
            LaunchFan{.minimumAngle = -0.1, .maximumAngle = 0.1},
            IntegratorSettings{.stepLength = 10.0,
                               .rangeLimit = 5100.0,
                               .depthLimit = 1100.0,
                               .maximumRayPoints = 10000U}));
      },
      "multi-source retained workspaces are rejected before receiver scans");
}

}  // namespace

int main() {
  Context context;
  testVec2(context);
  testEnvironmentVolumeAttenuation(context);
  testSimulationCase(context);
  testSourceBeamPattern(context);
  testQuadrilateralGrid(context);
  testRayPathCache(context);
  testFrequencyWorkspace(context);

  if (context.failureCount() != 0) {
    std::cerr << context.failureCount() << " test assertion(s) failed\n";
    return 1;
  }
  std::cout << "All Bellhop F2CPP core type tests passed\n";
  return 0;
}
