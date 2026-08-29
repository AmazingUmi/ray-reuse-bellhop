#include "rayreuse/solver/single_frequency_solver.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <numbers>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "rayreuse/error.hpp"
#include "rayreuse/io/environment_parser.hpp"
#include "rayreuse/model/simulation_case.hpp"
#include "rayreuse/model/sound_speed_evaluator.hpp"
#include "support/munk_case_fixture.hpp"
#include "support/test_harness.hpp"

namespace {

using rayreuse::AcousticMaterial;
using rayreuse::BeamFamily;
using rayreuse::BeamWidthMode;
using rayreuse::BoundaryCurvatureMode;
using rayreuse::BoundaryModel;
using rayreuse::CartesianCervenySettings;
using rayreuse::CervenyCoordinateSystem;
using rayreuse::Environment;
using rayreuse::FieldComponent;
using rayreuse::FrequencyGrid;
using rayreuse::IntegratorSettings;
using rayreuse::LaunchAngleDegreeBounds;
using rayreuse::LaunchFan;
using rayreuse::RawAttenuation;
using rayreuse::ReceiverGrid;
using rayreuse::semiCoherentLloydMirrorFactor;
using rayreuse::SimulationCase;
using rayreuse::SimulationRunMode;
using rayreuse::SingleFrequencyResult;
using rayreuse::SingleFrequencySolver;
using rayreuse::SoundSpeedPoint;
using rayreuse::SoundSpeedProfile;
using rayreuse::Source;
using rayreuse::SourceBeamPattern;
using rayreuse::SourceGeometry;
using rayreuse::ValidationError;
using rayreuse::VolumeAttenuationModel;
using rayreuse::test::Context;

std::vector<double> linearGrid(double first, double last, std::size_t count) {
  std::vector<double> values;
  values.reserve(count);
  const double delta = (last - first) / static_cast<double>(count - 1U);
  for (std::size_t index = 0U; index < count; ++index) {
    values.push_back(first + static_cast<double>(index) * delta);
  }
  return values;
}

Environment makeEnvironment() {
  return Environment(
      SoundSpeedProfile(
          {SoundSpeedPoint{
               .depth = 0.0, .soundSpeed = 1500.0, .density = 1000.0},
           SoundSpeedPoint{
               .depth = 1000.0, .soundSpeed = 1500.0, .density = 1000.0}}),
      BoundaryModel::vacuum(0.0),
      BoundaryModel::acousticHalfSpace(
          1000.0, AcousticMaterial{.compressionalSoundSpeed = 1600.0,
                                   .shearSoundSpeed = 0.0,
                                   .density = 1800.0}));
}

SimulationCase makeSimulation(
    std::size_t maximumRayPoints, std::vector<double> frequencies = {50.0},
    SimulationRunMode runMode = SimulationRunMode::Coherent,
    SourceBeamPattern sourceBeamPattern = SourceBeamPattern::omnidirectional(),
    BeamFamily beamFamily = BeamFamily::CervenyGaussian,
    FieldComponent fieldComponent = FieldComponent::Pressure,
    BoundaryCurvatureMode curvatureMode = BoundaryCurvatureMode::Standard,
    BeamWidthMode beamWidthMode = BeamWidthMode::MinimumWidth,
    CervenyCoordinateSystem coordinateSystem =
        CervenyCoordinateSystem::Cartesian,
    SourceGeometry sourceGeometry = SourceGeometry::Point) {
  return SimulationCase(
      makeEnvironment(), Source{.depth = 500.0, .amplitude = 1.0},
      ReceiverGrid(linearGrid(400.0, 600.0, 5U),
                   linearGrid(100.0, 1000.0, 10U)),
      FrequencyGrid(std::move(frequencies)),
      LaunchFan{.minimumAngle = -5.0 * std::numbers::pi / 180.0,
                .maximumAngle = 5.0 * std::numbers::pi / 180.0,
                .explicitLaunchAngleCount = 300U},
      IntegratorSettings{.stepLength = 10.0,
                         .rangeLimit = 1100.0,
                         .depthLimit = 1100.0,
                         .maximumRayPoints = maximumRayPoints},
      std::move(sourceBeamPattern), runMode, beamFamily, fieldComponent,
      curvatureMode, beamWidthMode, coordinateSystem, sourceGeometry);
}

SimulationCase makeCurvatureSimulation(
    SimulationRunMode runMode, FieldComponent fieldComponent,
    BoundaryCurvatureMode curvatureMode,
    BeamWidthMode beamWidthMode = BeamWidthMode::MinimumWidth,
    std::vector<double> frequencies = {100.0}) {
  return SimulationCase(
      Environment(
          SoundSpeedProfile(
              {SoundSpeedPoint{
                   .depth = 0.0, .soundSpeed = 1500.0, .density = 1000.0},
               SoundSpeedPoint{
                   .depth = 100.0, .soundSpeed = 1600.0, .density = 1000.0}}),
          BoundaryModel::vacuum(0.0), BoundaryModel::rigid(100.0)),
      Source{.depth = 50.0, .amplitude = 1.0},
      ReceiverGrid({20.0, 50.0, 80.0}, {50.0, 175.0, 300.0}),
      FrequencyGrid(std::move(frequencies)),
      LaunchFan{.minimumAngle = -60.0 * std::numbers::pi / 180.0,
                .maximumAngle = 60.0 * std::numbers::pi / 180.0,
                .explicitLaunchAngleCount = 121U},
      IntegratorSettings{.stepLength = 5.0,
                         .rangeLimit = 350.0,
                         .depthLimit = 110.0,
                         .maximumRayPoints = 1000U},
      SourceBeamPattern::omnidirectional(), runMode,
      BeamFamily::CervenyGaussian, fieldComponent, curvatureMode,
      beamWidthMode);
}

void testSemiCoherentLloydMirrorFactor(Context& context) {
  const double angle = 30.0 * std::numbers::pi / 180.0;
  const double factor =
      semiCoherentLloydMirrorFactor(50.0, 1500.0, 100.0, angle);
  context.checkNear(
      factor, 1.2247448504309721, 0.0,
      "semi-coherent Lloyd factor preserves Origin mixed precision");
  context.checkNear(factor * factor, 1.4999999486571842, 0.0,
                    "semi-coherent Lloyd power anchor");
  context.checkNear(semiCoherentLloydMirrorFactor(50.0, 1500.0, 100.0, 0.0),
                    0.0, 0.0, "zero launch angle is a Lloyd null");
  context.checkNear(semiCoherentLloydMirrorFactor(50.0, 1500.0, 100.0, -angle),
                    factor, 0.0, "Lloyd factor is symmetric in launch angle");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(
            semiCoherentLloydMirrorFactor(0.0, 1500.0, 100.0, 0.1));
      },
      "non-positive Lloyd frequency is rejected");
}

void testEndToEndSolve(Context& context) {
  const SimulationCase simulation = makeSimulation(1000U);
  const SingleFrequencyResult result = SingleFrequencySolver::solve(
      simulation, 1.0, 500.0,
      CartesianCervenySettings{.collectStatistics = true});

  context.check(result.rayCount == simulation.launchFanPlan().launchAngleCount,
                "solver traces the complete fixed-order launch fan");
  context.check(result.rayCount == 300U,
                "small direct case retains the D-02 minimum fan");
  context.check(
      result.timings.influenceStatistics.rayAccumulations == result.rayCount &&
          result.timings.influenceStatistics.validatedRayPoints == 0U &&
          result.timings.influenceStatistics.validatedWorkspaceValues == 0U,
      "solver statistics prove the frozen-cache prevalidated path "
      "avoids repeated full validation");
  context.check(
      result.timings.influenceStatistics.activeRayPoints >= result.rayCount &&
          result.timings.influenceStatistics.segmentCandidates > 0U &&
          result.timings.influenceStatistics.receiverDepthEvaluations > 0U &&
          result.timings.influenceStatistics.imageEvaluations > 0U,
      "solver opt-in statistics expose Influence hot-path work");
  context.check(
      result.totalRayPointCount > result.rayCount && result.rayCacheBytes > 0U,
      "solver reports non-empty frozen ray-cache metrics");
  context.check(result.workspace.frequency() == 50.0 &&
                    result.workspace.depthCount() == 5U &&
                    result.workspace.rangeCount() == 10U,
                "solver returns the single-frequency receiver workspace");

  bool haveNonzeroPressure = false;
  for (const std::complex<double> pressure : result.workspace.pressure()) {
    context.check(
        std::isfinite(pressure.real()) && std::isfinite(pressure.imag()),
        "solver pressure remains finite");
    haveNonzeroPressure =
        haveNonzeroPressure || pressure != std::complex<double>{};
  }
  context.check(
      haveNonzeroPressure,
      "complete trace/project/influence/scale chain produces pressure");
  for (std::size_t depthIndex = 0U; depthIndex < result.workspace.depthCount();
       ++depthIndex) {
    context.check(
        result.workspace.at(depthIndex, 0U) == std::complex<double>{},
        "legacy Influence leaves receiver range index zero unwritten");
  }
  context.check(result.timings.traceSeconds >= 0.0 &&
                    result.timings.projectSeconds >= 0.0 &&
                    result.timings.influenceSeconds >= 0.0 &&
                    result.timings.scaleSeconds >= 0.0,
                "solver exposes non-negative phase timings");
}

void testCoherenceModesShareGeometryAndSeparateFieldLaws(Context& context) {
  const SingleFrequencyResult coherent = SingleFrequencySolver::solve(
      makeSimulation(1000U, {50.0}, SimulationRunMode::Coherent), 1.0, 500.0);
  const SingleFrequencyResult incoherent = SingleFrequencySolver::solve(
      makeSimulation(1000U, {50.0}, SimulationRunMode::Incoherent), 1.0, 500.0);
  const SingleFrequencyResult semiCoherent = SingleFrequencySolver::solve(
      makeSimulation(1000U, {50.0}, SimulationRunMode::SemiCoherent), 1.0,
      500.0);
  context.check(
      coherent.rayCount == incoherent.rayCount &&
          coherent.rayCount == semiCoherent.rayCount &&
          coherent.totalRayPointCount == incoherent.totalRayPointCount &&
          coherent.totalRayPointCount == semiCoherent.totalRayPointCount &&
          coherent.rayCacheBytes == incoherent.rayCacheBytes &&
          coherent.rayCacheBytes == semiCoherent.rayCacheBytes,
      "C/I/S modes share the same frozen geometry");

  bool coherentHasImaginaryPressure = false;
  bool incoherentHasPressure = false;
  bool semiCoherentHasPressure = false;
  bool lloydChangesField = false;
  for (std::size_t index = 0U; index < coherent.workspace.pressure().size();
       ++index) {
    const std::complex<double> c = coherent.workspace.pressure()[index];
    const std::complex<double> i = incoherent.workspace.pressure()[index];
    const std::complex<double> s = semiCoherent.workspace.pressure()[index];
    coherentHasImaginaryPressure =
        coherentHasImaginaryPressure || c.imag() != 0.0;
    context.check(i.imag() == 0.0 && s.imag() == 0.0,
                  "I/S final pressure amplitudes are purely real");
    context.check(i.real() <= 0.0 && s.real() <= 0.0,
                  "point-source I/S scaling preserves the Origin sign");
    incoherentHasPressure = incoherentHasPressure || i.real() != 0.0;
    semiCoherentHasPressure = semiCoherentHasPressure || s.real() != 0.0;
    lloydChangesField = lloydChangesField || i != s;
  }
  context.check(coherentHasImaginaryPressure,
                "coherent mode retains complex interference");
  context.check(incoherentHasPressure && semiCoherentHasPressure,
                "I/S modes produce non-empty final pressure amplitudes");
  context.check(lloydChangesField,
                "semi-coherent Lloyd weighting distinguishes S from I");
}

void testCartesianComponentsPreserveLegacySelectorSemantics(Context& context) {
  for (const SimulationRunMode mode :
       {SimulationRunMode::Coherent, SimulationRunMode::Incoherent,
        SimulationRunMode::SemiCoherent}) {
    const SingleFrequencyResult pressure = SingleFrequencySolver::solve(
        makeSimulation(1000U, {50.0}, mode,
                       SourceBeamPattern::omnidirectional(),
                       BeamFamily::CervenyGaussian, FieldComponent::Pressure),
        1.0, 500.0);
    for (const FieldComponent component :
         {FieldComponent::Vertical, FieldComponent::Horizontal}) {
      const SingleFrequencyResult candidate = SingleFrequencySolver::solve(
          makeSimulation(1000U, {50.0}, mode,
                         SourceBeamPattern::omnidirectional(),
                         BeamFamily::CervenyGaussian, component),
          1.0, 500.0);
      context.check(
          pressure.rayCount == candidate.rayCount &&
              pressure.totalRayPointCount == candidate.totalRayPointCount &&
              pressure.rayCacheBytes == candidate.rayCacheBytes &&
              std::equal(pressure.workspace.pressure().begin(),
                         pressure.workspace.pressure().end(),
                         candidate.workspace.pressure().begin(),
                         candidate.workspace.pressure().end()),
          "Cartesian Cerveny P/V/H selectors preserve identical C/I/S "
          "legacy fields");
    }
  }
}

void testCartesianCurvatureModesPreserveFieldSemantics(Context& context) {
  for (const SimulationRunMode mode :
       {SimulationRunMode::Coherent, SimulationRunMode::Incoherent,
        SimulationRunMode::SemiCoherent}) {
    std::vector<std::vector<std::complex<double>>> pressureFields;
    for (const BoundaryCurvatureMode curvatureMode :
         {BoundaryCurvatureMode::Double, BoundaryCurvatureMode::Standard,
          BoundaryCurvatureMode::Zero}) {
      const SingleFrequencyResult pressure = SingleFrequencySolver::solve(
          makeCurvatureSimulation(mode, FieldComponent::Pressure,
                                  curvatureMode),
          1.0, 1000.0);
      pressureFields.emplace_back(pressure.workspace.pressure().begin(),
                                  pressure.workspace.pressure().end());
      for (const FieldComponent component :
           {FieldComponent::Vertical, FieldComponent::Horizontal}) {
        const SingleFrequencyResult candidate = SingleFrequencySolver::solve(
            makeCurvatureSimulation(mode, component, curvatureMode), 1.0,
            1000.0);
        context.check(std::equal(pressure.workspace.pressure().begin(),
                                 pressure.workspace.pressure().end(),
                                 candidate.workspace.pressure().begin(),
                                 candidate.workspace.pressure().end()),
                      "D/S/Z preserve Cartesian P/V/H legacy field identity");
      }
    }
    context.check(pressureFields.size() == 3U &&
                      pressureFields[0U] != pressureFields[1U] &&
                      pressureFields[1U] != pressureFields[2U] &&
                      pressureFields[0U] != pressureFields[2U],
                  "D/S/Z produce distinct reflected Cartesian C/I/S fields");
  }
}

void testCartesianBeamWidthModesPreserveFieldSemantics(Context& context) {
  for (const SimulationRunMode mode :
       {SimulationRunMode::Coherent, SimulationRunMode::Incoherent,
        SimulationRunMode::SemiCoherent}) {
    for (const BoundaryCurvatureMode curvatureMode :
         {BoundaryCurvatureMode::Double, BoundaryCurvatureMode::Standard,
          BoundaryCurvatureMode::Zero}) {
      std::vector<std::vector<std::complex<double>>> pressureFields;
      for (const BeamWidthMode widthMode :
           {BeamWidthMode::SpaceFilling, BeamWidthMode::MinimumWidth,
            BeamWidthMode::Wkb}) {
        const SingleFrequencyResult pressure = SingleFrequencySolver::solve(
            makeCurvatureSimulation(mode, FieldComponent::Pressure,
                                    curvatureMode, widthMode),
            1.0, 1000.0);
        pressureFields.emplace_back(pressure.workspace.pressure().begin(),
                                    pressure.workspace.pressure().end());
        for (const FieldComponent component :
             {FieldComponent::Vertical, FieldComponent::Horizontal}) {
          const SingleFrequencyResult candidate = SingleFrequencySolver::solve(
              makeCurvatureSimulation(mode, component, curvatureMode,
                                      widthMode),
              1.0, 1000.0);
          context.check(std::equal(pressure.workspace.pressure().begin(),
                                   pressure.workspace.pressure().end(),
                                   candidate.workspace.pressure().begin(),
                                   candidate.workspace.pressure().end()),
                        "F/M/W preserve Cartesian P/V/H legacy field identity");
        }
      }
      context.check(pressureFields.size() == 3U &&
                        pressureFields[0U] != pressureFields[1U] &&
                        pressureFields[1U] != pressureFields[2U] &&
                        pressureFields[0U] != pressureFields[2U],
                    "F/M/W produce distinct Cartesian C/I/S fields");
    }
  }
}

void testBeamWidthDoesNotChangeFrozenGeometry(Context& context) {
  std::vector<std::uint64_t> fingerprints;
  for (const BeamWidthMode widthMode :
       {BeamWidthMode::SpaceFilling, BeamWidthMode::MinimumWidth,
        BeamWidthMode::Wkb}) {
    const rayreuse::RayFanTraceResult trace =
        SingleFrequencySolver::traceRayFan(makeCurvatureSimulation(
            SimulationRunMode::Coherent, FieldComponent::Pressure,
            BoundaryCurvatureMode::Standard, widthMode));
    fingerprints.push_back(trace.cache.contentFingerprint());
  }
  context.check(fingerprints.size() == 3U &&
                    fingerprints[0U] == fingerprints[1U] &&
                    fingerprints[1U] == fingerprints[2U],
                "F/M/W leave frozen trajectory and dynamic-ray bases "
                "bitwise unchanged");
}

SimulationCase makeMultiSourceSimulation(std::vector<Source> sources,
                                         std::size_t maximumRayPoints) {
  return SimulationCase(
      makeEnvironment(), std::move(sources),
      ReceiverGrid(linearGrid(400.0, 600.0, 5U),
                   linearGrid(100.0, 1000.0, 10U)),
      FrequencyGrid({50.0}),
      LaunchFan{.minimumAngle = -5.0 * std::numbers::pi / 180.0,
                .maximumAngle = 5.0 * std::numbers::pi / 180.0,
                .explicitLaunchAngleCount = 300U},
      IntegratorSettings{.stepLength = 10.0,
                         .rangeLimit = 1100.0,
                         .depthLimit = 1100.0,
                         .maximumRayPoints = maximumRayPoints});
}

void testPerSourceTraceProducesIndependentFrozenCaches(Context& context) {
  const SimulationCase simulation = makeMultiSourceSimulation(
      {{.depth = 750.0, .amplitude = 1.0},
       {.depth = 250.0, .amplitude = 1.0},
       {.depth = 500.0, .amplitude = 1.0}},
      1000U);
  context.check(simulation.sourceCount() == 3U &&
                    simulation.sources()[0U].depth == 250.0 &&
                    simulation.sources()[1U].depth == 500.0 &&
                    simulation.sources()[2U].depth == 750.0,
                "per-source trace fixture receives depth-sorted sources");

  const std::vector<rayreuse::RayFanTraceResult> sourceTraces =
      SingleFrequencySolver::traceAllSourceFans(simulation);
  context.check(sourceTraces.size() == simulation.sourceCount(),
                "per-source trace yields exactly one trace result per source");
  bool fansComplete = true;
  bool cachesFrozen = true;
  bool cachesMatchSourceDepth = true;
  std::vector<std::uint64_t> fingerprints;
  fingerprints.reserve(sourceTraces.size());
  for (std::size_t sourceIndex = 0U; sourceIndex < sourceTraces.size();
       ++sourceIndex) {
    const rayreuse::RayFanTraceResult& trace = sourceTraces[sourceIndex];
    fansComplete =
        fansComplete && trace.cache.size() > 0U &&
        trace.cache.size() == simulation.launchFanPlan().launchAngleCount;
    cachesFrozen = cachesFrozen && trace.cache.frozen();
    cachesMatchSourceDepth =
        cachesMatchSourceDepth &&
        trace.cache.at(0U).points.front().position.range == 0.0 &&
        trace.cache.at(0U).points.front().position.depth ==
            simulation.sources()[sourceIndex].depth;
    fingerprints.push_back(trace.cache.contentFingerprint());
  }
  context.check(fansComplete,
                "each per-source cache holds the complete shared launch fan");
  context.check(cachesFrozen, "each per-source cache is independently frozen");
  context.check(cachesMatchSourceDepth,
                "cache index pairs with the depth-sorted source index");
  context.check(fingerprints.size() == 3U &&
                    fingerprints[0U] != fingerprints[1U] &&
                    fingerprints[1U] != fingerprints[2U] &&
                    fingerprints[0U] != fingerprints[2U],
                "distinct source depths trace distinct frozen geometries");

  const std::vector<rayreuse::RayFanTraceResult> repeatTraces =
      SingleFrequencySolver::traceAllSourceFans(simulation);
  bool repeatFingerprintsStable = repeatTraces.size() == fingerprints.size();
  for (std::size_t sourceIndex = 0U; sourceIndex < fingerprints.size();
       ++sourceIndex) {
    repeatFingerprintsStable =
        repeatFingerprintsStable &&
        repeatTraces[sourceIndex].cache.contentFingerprint() ==
            fingerprints[sourceIndex];
  }
  context.check(repeatFingerprintsStable,
                "per-source fingerprints are stable across trace passes");

  const rayreuse::RayFanTraceResult firstSourceTrace =
      SingleFrequencySolver::traceRayFan(simulation);
  context.check(
      firstSourceTrace.cache.contentFingerprint() == fingerprints[0U] &&
          firstSourceTrace.totalRayPointCount ==
              sourceTraces[0U].totalRayPointCount,
      "legacy first-source fan entry matches per-source entry zero");
}

void testSingleSourcePerSourceTraceEquivalence(Context& context) {
  const SimulationCase simulation = makeSimulation(1000U);
  const rayreuse::RayFanTraceResult legacy =
      SingleFrequencySolver::traceRayFan(simulation);
  const std::vector<rayreuse::RayFanTraceResult> perSource =
      SingleFrequencySolver::traceAllSourceFans(simulation);
  context.check(
      simulation.sourceCount() == 1U && perSource.size() == 1U &&
          perSource[0U].cache.frozen() &&
          perSource[0U].cache.size() == legacy.cache.size() &&
          perSource[0U].cache.contentFingerprint() ==
              legacy.cache.contentFingerprint() &&
          perSource[0U].totalRayPointCount == legacy.totalRayPointCount,
      "NSz==1 per-source trace reproduces the legacy single-source fan");
}

void testPerSourceTraceDiagnostics(Context& context) {
  const SimulationCase simulation = makeMultiSourceSimulation(
      {{.depth = 500.0, .amplitude = 1.0}, {.depth = 700.0, .amplitude = 1.0}},
      1000U);
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(
            SingleFrequencySolver::traceSourceFan(simulation, 2U));
      },
      "per-source trace rejects an out-of-range source index");

  const SimulationCase pointLimited = makeMultiSourceSimulation(
      {{.depth = 500.0, .amplitude = 1.0}, {.depth = 700.0, .amplitude = 1.0}},
      20U);
  std::string diagnostic;
  try {
    static_cast<void>(
        SingleFrequencySolver::traceSourceFan(pointLimited, 1U));
  } catch (const ValidationError& error) {
    diagnostic = error.what();
  }
  context.check(!diagnostic.empty() &&
                    diagnostic.rfind("single-frequency solve encountered a "
                                     "ray that did not exit the spatial domain "
                                     "normally (source index 1, launch index ",
                                     0U) == 0U &&
                    diagnostic.find(", angle ") != std::string::npos &&
                    diagnostic.find(", reason ") != std::string::npos &&
                    diagnostic.back() == ')',
                "per-source trace failures carry the F2CPP-aligned "
                "source-index diagnostic");
}

void testSourceBeamPatternScalesAllCoherenceModes(Context& context) {
  const double amplitude = std::pow(10.0, -6.0 / 20.0);
  for (const SimulationRunMode mode :
       {SimulationRunMode::Coherent, SimulationRunMode::Incoherent,
        SimulationRunMode::SemiCoherent}) {
    // I/S apply ABS, square, ray-wise accumulation, and SQRT before the final
    // scale, so multiplying the finished field is only an algebraic oracle,
    // not the same floating-point operation order. Reuse the existing
    // Cartesian contribution tolerance for that check; the executable oracle
    // cases separately require exact F2CPP equality.
    const double tolerance =
        mode == SimulationRunMode::Coherent ? 2.0e-15 : 2.0e-11;
    const SingleFrequencyResult omni = SingleFrequencySolver::solve(
        makeSimulation(1000U, {50.0}, mode,
                       SourceBeamPattern::omnidirectional()),
        1.0, 500.0);
    const SingleFrequencyResult directional = SingleFrequencySolver::solve(
        makeSimulation(1000U, {50.0}, mode,
                       SourceBeamPattern::directional(
                           {{.angleDegrees = -10.0, .powerDecibels = -6.0},
                            {.angleDegrees = 10.0, .powerDecibels = -6.0}})),
        1.0, 500.0);
    context.check(
        directional.rayCount == omni.rayCount &&
            directional.totalRayPointCount == omni.totalRayPointCount &&
            directional.rayCacheBytes == omni.rayCacheBytes,
        "source beam pattern leaves C/I/S frozen geometry unchanged");
    for (std::size_t index = 0U; index < omni.workspace.pressure().size();
         ++index) {
      context.checkNear(
          std::abs(directional.workspace.pressure()[index] -
                   amplitude * omni.workspace.pressure()[index]),
          0.0, tolerance,
          "constant source beam pattern scales C/I/S source amplitude");
    }
  }
}

void testGeometricHatCoherenceAndDirectionalPattern(Context& context) {
  const double directionalAmplitude = std::pow(10.0, -6.0 / 20.0);
  for (const CervenyCoordinateSystem coordinates :
       {CervenyCoordinateSystem::Cartesian,
        CervenyCoordinateSystem::RayCentered}) {
    std::vector<SingleFrequencyResult> omniResults;
    omniResults.reserve(3U);
    for (const SimulationRunMode mode :
         {SimulationRunMode::Coherent, SimulationRunMode::Incoherent,
          SimulationRunMode::SemiCoherent}) {
      const SingleFrequencyResult omni = SingleFrequencySolver::solve(
          makeSimulation(1000U, {50.0}, mode,
                         SourceBeamPattern::omnidirectional(),
                         BeamFamily::GeometricHat, FieldComponent::Pressure,
                         BoundaryCurvatureMode::Standard,
                         BeamWidthMode::MinimumWidth, coordinates),
          1.0, 500.0);
      const SingleFrequencyResult directional = SingleFrequencySolver::solve(
          makeSimulation(1000U, {50.0}, mode,
                         SourceBeamPattern::directional(
                             {{.angleDegrees = -10.0, .powerDecibels = -6.0},
                              {.angleDegrees = 10.0, .powerDecibels = -6.0}}),
                         BeamFamily::GeometricHat, FieldComponent::Pressure,
                         BoundaryCurvatureMode::Standard,
                         BeamWidthMode::MinimumWidth, coordinates),
          1.0, 500.0);
      context.check(
          omni.rayCount == directional.rayCount &&
              omni.totalRayPointCount == directional.totalRayPointCount &&
              omni.rayCacheBytes == directional.rayCacheBytes,
          "GeoHat directional source weighting leaves frozen geometry "
          "unchanged");
      bool havePressure = false;
      for (std::size_t index = 0U; index < omni.workspace.pressure().size();
           ++index) {
        const std::complex<double> pressure = omni.workspace.pressure()[index];
        havePressure = havePressure || pressure != std::complex<double>{};
        if (mode != SimulationRunMode::Coherent) {
          context.check(pressure.imag() == 0.0 && pressure.real() <= 0.0,
                        "GeoHat I/S final pressure is real with Origin sign");
        }
        const double tolerance =
            mode == SimulationRunMode::Coherent ? 2.0e-15 : 2.0e-11;
        context.checkNear(
            std::abs(directional.workspace.pressure()[index] -
                     directionalAmplitude * pressure),
            0.0, tolerance,
            "GeoHat .sbp weighting scales C/I/S source amplitude");
      }
      context.check(havePressure, "GeoHat C/I/S produces a non-empty field");
      omniResults.push_back(omni);
    }
    context.check(
        omniResults[0U].rayCount == omniResults[1U].rayCount &&
            omniResults[0U].rayCount == omniResults[2U].rayCount &&
            omniResults[0U].totalRayPointCount ==
                omniResults[1U].totalRayPointCount &&
            omniResults[0U].totalRayPointCount ==
                omniResults[2U].totalRayPointCount &&
            omniResults[0U].rayCacheBytes == omniResults[1U].rayCacheBytes &&
            omniResults[0U].rayCacheBytes == omniResults[2U].rayCacheBytes,
        "GeoHat C/I/S share the same frozen trajectory");
    context.check(!std::equal(omniResults[1U].workspace.pressure().begin(),
                              omniResults[1U].workspace.pressure().end(),
                              omniResults[2U].workspace.pressure().begin()),
                  "GeoHat semicoherent Lloyd weighting distinguishes S from I");
  }
}

void testGeometricGaussianCoherenceAndDirectionalPattern(Context& context) {
  const double directionalAmplitude = std::pow(10.0, -6.0 / 20.0);
  std::vector<SingleFrequencyResult> omniResults;
  omniResults.reserve(3U);
  for (const SimulationRunMode mode :
       {SimulationRunMode::Coherent, SimulationRunMode::Incoherent,
        SimulationRunMode::SemiCoherent}) {
    const SingleFrequencyResult omni = SingleFrequencySolver::solve(
        makeSimulation(1000U, {50.0}, mode,
                       SourceBeamPattern::omnidirectional(),
                       BeamFamily::GeometricGaussian),
        1.0, 500.0);
    const SingleFrequencyResult directional = SingleFrequencySolver::solve(
        makeSimulation(1000U, {50.0}, mode,
                       SourceBeamPattern::directional(
                           {{.angleDegrees = -10.0, .powerDecibels = -6.0},
                            {.angleDegrees = 10.0, .powerDecibels = -6.0}}),
                       BeamFamily::GeometricGaussian),
        1.0, 500.0);
    context.check(
        omni.rayCount == directional.rayCount &&
            omni.totalRayPointCount == directional.totalRayPointCount &&
            omni.rayCacheBytes == directional.rayCacheBytes,
        "GeoGaussian directional source weighting leaves frozen geometry "
        "unchanged");
    bool havePressure = false;
    for (std::size_t index = 0U; index < omni.workspace.pressure().size();
         ++index) {
      const std::complex<double> pressure = omni.workspace.pressure()[index];
      havePressure = havePressure || pressure != std::complex<double>{};
      if (mode != SimulationRunMode::Coherent) {
        context.check(pressure.imag() == 0.0 && pressure.real() <= 0.0,
                      "GeoGaussian I/S final pressure is real with Origin "
                      "sign");
      }
      const double tolerance =
          mode == SimulationRunMode::Coherent ? 2.0e-15 : 2.0e-11;
      context.checkNear(
          std::abs(directional.workspace.pressure()[index] -
                   directionalAmplitude * pressure),
          0.0, tolerance,
          "GeoGaussian .sbp weighting scales C/I/S source amplitude");
    }
    context.check(havePressure, "GeoGaussian C/I/S produces a non-empty field");
    omniResults.push_back(omni);
  }
  context.check(
      omniResults[0U].rayCount == omniResults[1U].rayCount &&
          omniResults[0U].rayCount == omniResults[2U].rayCount &&
          omniResults[0U].totalRayPointCount ==
              omniResults[1U].totalRayPointCount &&
          omniResults[0U].totalRayPointCount ==
              omniResults[2U].totalRayPointCount &&
          omniResults[0U].rayCacheBytes == omniResults[1U].rayCacheBytes &&
          omniResults[0U].rayCacheBytes == omniResults[2U].rayCacheBytes,
      "GeoGaussian C/I/S share the same frozen trajectory");
  context.check(
      !std::equal(omniResults[1U].workspace.pressure().begin(),
                  omniResults[1U].workspace.pressure().end(),
                  omniResults[2U].workspace.pressure().begin()),
      "GeoGaussian semicoherent Lloyd weighting distinguishes S from I");
}

void testSimpleGaussianCoherentDirectionalPattern(Context& context) {
  const double directionalAmplitude = std::pow(10.0, -6.0 / 20.0);
  const SingleFrequencyResult omni = SingleFrequencySolver::solve(
      makeSimulation(1000U, {50.0}, SimulationRunMode::Coherent,
                     SourceBeamPattern::omnidirectional(),
                     BeamFamily::SimpleGaussian),
      1.0, 500.0);
  const SingleFrequencyResult directional = SingleFrequencySolver::solve(
      makeSimulation(1000U, {50.0}, SimulationRunMode::Coherent,
                     SourceBeamPattern::directional(
                         {{.angleDegrees = -10.0, .powerDecibels = -6.0},
                          {.angleDegrees = 10.0, .powerDecibels = -6.0}}),
                     BeamFamily::SimpleGaussian),
      1.0, 500.0);
  context.check(omni.rayCount == directional.rayCount &&
                    omni.totalRayPointCount == directional.totalRayPointCount &&
                    omni.rayCacheBytes == directional.rayCacheBytes,
                "Simple Gaussian directional weighting leaves frozen geometry "
                "unchanged");
  bool havePressure = false;
  for (std::size_t index = 0U; index < omni.workspace.pressure().size();
       ++index) {
    const std::complex<double> pressure = omni.workspace.pressure()[index];
    havePressure = havePressure || pressure != std::complex<double>{};
    context.checkNear(
        std::abs(directional.workspace.pressure()[index] -
                 directionalAmplitude * pressure),
        0.0, 2.0e-15,
        "Simple Gaussian .sbp weighting scales coherent source amplitude");
  }
  context.check(havePressure,
                "Simple Gaussian coherent solve produces a non-empty field");
}

void testAbnormalRayTerminationFails(Context& context) {
  const SimulationCase simulation = makeSimulation(20U);
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(SingleFrequencySolver::solve(simulation, 1.0, 500.0));
      },
      "point-limited ray prevents a partial field result");
}

void testExplicitFrequencyEntryPoint(Context& context) {
  const SimulationCase simulation = makeSimulation(1000U, {25.0, 50.0});

  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(SingleFrequencySolver::solve(simulation, 1.0, 500.0));
      },
      "single-frequency solve rejects a multi-frequency simulation");
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(SingleFrequencySolver::solveAtFrequency(
            simulation, 75.0, 1.0, 500.0));
      },
      "explicit-frequency solve rejects a frequency outside the simulation");

  const SingleFrequencyResult result =
      SingleFrequencySolver::solveAtFrequency(simulation, 25.0, 1.0, 500.0);
  context.check(
      result.workspace.frequency() == 25.0 &&
          result.rayCount == simulation.launchFanPlan().launchAngleCount &&
          result.totalRayPointCount > result.rayCount,
      "explicit-frequency solve traces the shared fan at a member frequency");
}

SimulationCase makeConstantSmokeSimulation(
    double waterDepth, double sourceDepth, double frequency,
    double maximumRange, double minimumAngleDegrees, double maximumAngleDegrees,
    double stepLength, BoundaryModel seabed,
    VolumeAttenuationModel volumeModel = VolumeAttenuationModel::None) {
  const RawAttenuation waterAttenuation{.volumeModel = volumeModel};
  const double radiansPerDegree = std::numbers::pi / 180.0;
  return SimulationCase(
      Environment(
          SoundSpeedProfile({SoundSpeedPoint{.depth = 0.0,
                                             .soundSpeed = 1500.0,
                                             .density = 1000.0,
                                             .attenuation = waterAttenuation},
                             SoundSpeedPoint{.depth = waterDepth,
                                             .soundSpeed = 1500.0,
                                             .density = 1000.0,
                                             .attenuation = waterAttenuation}}),
          BoundaryModel::vacuum(0.0), std::move(seabed)),
      Source{.depth = sourceDepth, .amplitude = 1.0},
      ReceiverGrid(linearGrid(0.1 * waterDepth, 0.9 * waterDepth, 5U),
                   linearGrid(10.0, maximumRange, 9U)),
      FrequencyGrid({frequency}),
      LaunchFan{.minimumAngle = minimumAngleDegrees * radiansPerDegree,
                .maximumAngle = maximumAngleDegrees * radiansPerDegree,
                .explicitLaunchAngleCount = std::nullopt,
                .inputDegreeBounds =
                    LaunchAngleDegreeBounds{.minimum = minimumAngleDegrees,
                                            .maximum = maximumAngleDegrees}},
      IntegratorSettings{.stepLength = stepLength,
                         .rangeLimit = maximumRange + stepLength,
                         .depthLimit = waterDepth + 0.1 * stepLength,
                         .maximumRayPoints = 2000U});
}

void checkSmokeSolve(Context& context, const std::string& name,
                     const SimulationCase& simulation, double epsilonMultiplier,
                     double loopRange) {
  const SingleFrequencyResult result =
      SingleFrequencySolver::solve(simulation, epsilonMultiplier, loopRange);
  context.check(
      result.rayCount == simulation.launchFanPlan().launchAngleCount &&
          result.rayCount >= 300U,
      name + " traces its complete planned launch fan");
  context.check(
      result.totalRayPointCount > result.rayCount && result.rayCacheBytes > 0U,
      name + " produces a non-empty frozen ray cache");

  bool haveNonzeroPressure = false;
  for (const std::complex<double> pressure : result.workspace.pressure()) {
    context.check(
        std::isfinite(pressure.real()) && std::isfinite(pressure.imag()),
        name + " pressure remains finite");
    haveNonzeroPressure =
        haveNonzeroPressure || pressure != std::complex<double>{};
  }
  context.check(haveNonzeroPressure,
                name + " produces nonzero coherent pressure");
}

void testSixCaseSanitizerSmoke(Context& context) {
  checkSmokeSolve(
      context, "constant-speed direct smoke",
      makeConstantSmokeSimulation(
          1000.0, 500.0, 50.0, 1000.0, -5.0, 5.0, 20.0,
          BoundaryModel::acousticHalfSpace(
              1000.0, AcousticMaterial{.compressionalSoundSpeed = 1500.0,
                                       .shearSoundSpeed = 0.0,
                                       .density = 1000.0})),
      1.0, 500.0);
  checkSmokeSolve(
      context, "vacuum-rigid reflection smoke",
      makeConstantSmokeSimulation(100.0, 50.0, 250.0, 500.0, -80.0, 80.0, 10.0,
                                  BoundaryModel::rigid(100.0)),
      2.0, 100.0);
  checkSmokeSolve(
      context, "lossy acoustic-bottom smoke",
      makeConstantSmokeSimulation(
          100.0, 50.0, 250.0, 500.0, -80.0, 80.0, 10.0,
          BoundaryModel::acousticHalfSpace(
              100.0,
              AcousticMaterial{
                  .compressionalSoundSpeed = 1590.0,
                  .shearSoundSpeed = 0.0,
                  .density = 1200.0,
                  .compressionalAttenuation =
                      {.value = 0.5,
                       .unit =
                           rayreuse::AttenuationUnit::DecibelsPerWavelength}})),
      2.0, 100.0);
  checkSmokeSolve(
      context, "5 kHz lossless smoke",
      makeConstantSmokeSimulation(
          1000.0, 500.0, 5000.0, 100.0, -2.0, 2.0, 10.0,
          BoundaryModel::acousticHalfSpace(
              1000.0, AcousticMaterial{.compressionalSoundSpeed = 1500.0,
                                       .shearSoundSpeed = 0.0,
                                       .density = 1000.0})),
      1.0, 500.0);
  checkSmokeSolve(
      context, "5 kHz Thorp smoke",
      makeConstantSmokeSimulation(
          1000.0, 500.0, 5000.0, 100.0, -2.0, 2.0, 10.0,
          BoundaryModel::acousticHalfSpace(
              1000.0, AcousticMaterial{.compressionalSoundSpeed = 1500.0,
                                       .shearSoundSpeed = 0.0,
                                       .density = 1000.0}),
          VolumeAttenuationModel::Thorp),
      1.0, 500.0);

  const double radiansPerDegree = std::numbers::pi / 180.0;
  const SimulationCase munk(
      rayreuse::test::makeMunkEnvironment(),
      Source{.depth = 1000.0, .amplitude = 1.0},
      ReceiverGrid(linearGrid(500.0, 4500.0, 5U),
                   linearGrid(100.0, 10000.0, 11U)),
      FrequencyGrid({50.0}),
      LaunchFan{.minimumAngle = -20.3 * radiansPerDegree,
                .maximumAngle = 20.3 * radiansPerDegree,
                .explicitLaunchAngleCount = std::nullopt,
                .inputDegreeBounds =
                    LaunchAngleDegreeBounds{.minimum = -20.3, .maximum = 20.3}},
      IntegratorSettings{.stepLength = 500.0,
                         .rangeLimit = 10500.0,
                         .depthLimit = 5500.0,
                         .maximumRayPoints = 2000U});
  checkSmokeSolve(context, "Munk layered/caustic smoke", munk, 1.0, 25000.0);
}

// Executable-level cubic-spline smoke: a real .env payload whose SSP line is
// 'SVW' is parsed by the production EnvironmentParser and solved through the
// shared SimulationCase -> SingleFrequencySolver path, so the spline backend
// really reaches the frozen geometry traced by GeometrySspEvaluator/stepRay.
// The identical case with 'CVW' isolates silent fallback: a fallback would
// reproduce the C-linear pressures and frozen-cache fingerprint bit-for-bit.
std::string renderSspSmokeCase(char sspOption) {
  std::ostringstream contents;
  contents << "'Cubic-spline SSP solver smoke'  ! TITLE\n";
  contents << "50.0          ! FREQ (Hz)\n";
  contents << "1                       ! NMEDIA\n";
  contents << "'" << sspOption << "VW'                   ! SSP option\n";
  contents << "3  0.0  1000.0          ! SSP points, sigma, bottom depth\n";
  contents << "0.0     1500.0 /\n";
  contents << "500.0   1520.0 /\n";
  contents << "1000.0  1480.0 /\n";
  contents << "'A' 0.0                 ! Acoustic half-space bottom\n";
  contents << "1000.0  1600.0  0.0  1.8  0.0 /\n";
  contents << "1                       ! NSD\n";
  contents << "500.0 /\n";
  contents << "21                      ! NRD\n";
  contents << "400.0  600.0 /\n";
  contents << "51                      ! NR\n";
  contents << "0.1  5.0 /\n";
  contents << "'CC'                    ! Coherent TL, Cartesian Cerveny\n";
  contents << "300                     ! launch-angle count\n";
  contents << "-5.0  5.0 /\n";
  contents << "10.0  1100.0  5.1\n";
  contents << "'MS' 1.0  2.5\n";
  contents << "3  5  'P'\n";
  return contents.str();
}

rayreuse::ParsedEnvironment parseSspSmokeCase(char sspOption) {
  std::istringstream input(renderSspSmokeCase(sspOption));
  return rayreuse::EnvironmentParser::parse(input, "ssp_smoke.env");
}

void testSplineEnvironmentSolverSmoke(Context& context) {
  const rayreuse::ParsedEnvironment splineParsed = parseSspSmokeCase('S');
  const SimulationCase& splineSimulation = splineParsed.simulationCase;
  context.check(
      splineSimulation.environment().soundSpeedProfile().interpolationKind() ==
          rayreuse::SspInterpolationKind::CubicSpline,
      "real 'SVW' environment parses as a cubic-spline simulation case");

  const rayreuse::Vec2 midDepthSample{.range = 0.0, .depth = 250.0};
  const rayreuse::GeometrySspEvaluator splineEvaluator(
      splineSimulation.environment().soundSpeedProfile());
  const double splineSampleSpeed =
      splineEvaluator.evaluate(midDepthSample, 0U).soundSpeed;
  const rayreuse::ParsedEnvironment cParsedCase = parseSspSmokeCase('C');
  const SimulationCase& cSimulation = cParsedCase.simulationCase;
  const rayreuse::GeometrySspEvaluator cEvaluator(
      cSimulation.environment().soundSpeedProfile());
  context.check(
      splineSampleSpeed !=
          cEvaluator.evaluate(midDepthSample, 0U).soundSpeed,
      "spline and C-linear evaluations of the same nodes differ, so the "
      "input itself distinguishes the backends");

  const rayreuse::RayFanTraceResult splineTrace =
      SingleFrequencySolver::traceRayFan(splineSimulation);
  const rayreuse::RayFanTraceResult cTrace =
      SingleFrequencySolver::traceRayFan(cSimulation);
  context.check(
      splineTrace.cache.size() == cTrace.cache.size() &&
          splineTrace.cache.size() ==
              splineSimulation.launchFanPlan().launchAngleCount,
      "spline environment traces the complete planned launch fan");
  context.check(
      splineTrace.cache.contentFingerprint() != cTrace.cache.contentFingerprint(),
      "frozen spline geometry differs from C-linear, proving the variant "
      "backend really enters the tracer");

  const SingleFrequencyResult splineResult =
      SingleFrequencySolver::solve(splineSimulation, 1.0, 500.0);
  const SingleFrequencyResult cResult =
      SingleFrequencySolver::solve(cSimulation, 1.0, 500.0);
  context.check(splineResult.workspace.depthCount() == 21U &&
                    splineResult.workspace.rangeCount() == 51U,
                "spline solve fills the expected receiver workspace");
  bool haveFinite = true;
  bool haveNonzeroPressure = false;
  bool differsFromCLinear = false;
  for (std::size_t index = 0U; index < splineResult.workspace.pressure().size();
       ++index) {
    const std::complex<double> pressure = splineResult.workspace.pressure()[index];
    haveFinite = haveFinite && std::isfinite(pressure.real()) &&
                 std::isfinite(pressure.imag());
    haveNonzeroPressure = haveNonzeroPressure || pressure != std::complex<double>{};
    differsFromCLinear =
        differsFromCLinear || pressure != cResult.workspace.pressure()[index];
  }
  context.check(haveFinite, "spline solver pressure remains finite");
  context.check(haveNonzeroPressure,
                "spline environment produces nonzero coherent pressure");
  context.check(differsFromCLinear,
                "spline solve result differs from the identical C-linear "
                "solve, excluding a silent fallback backend");
}

void testSourceGeometryChangesFieldOnly(Context& context) {
  const SingleFrequencyResult point = SingleFrequencySolver::solve(
      makeSimulation(1000U, {50.0}, SimulationRunMode::Coherent,
                     SourceBeamPattern::omnidirectional(),
                     BeamFamily::CervenyGaussian, FieldComponent::Pressure,
                     BoundaryCurvatureMode::Standard,
                     BeamWidthMode::MinimumWidth,
                     CervenyCoordinateSystem::Cartesian, SourceGeometry::Point),
      1.0, 500.0);
  const SingleFrequencyResult line = SingleFrequencySolver::solve(
      makeSimulation(1000U, {50.0}, SimulationRunMode::Coherent,
                     SourceBeamPattern::omnidirectional(),
                     BeamFamily::CervenyGaussian, FieldComponent::Pressure,
                     BoundaryCurvatureMode::Standard,
                     BeamWidthMode::MinimumWidth,
                     CervenyCoordinateSystem::Cartesian, SourceGeometry::Line),
      1.0, 500.0);
  context.check(
      point.rayCount == line.rayCount &&
          point.totalRayPointCount == line.totalRayPointCount &&
          point.rayCacheBytes == line.rayCacheBytes,
      "point/line source selection leaves frozen geometry metrics unchanged");
  bool fieldDiffers = false;
  for (std::size_t index = 0U; index < point.workspace.pressure().size();
       ++index) {
    if (point.workspace.pressure()[index] != line.workspace.pressure()[index]) {
      fieldDiffers = true;
      break;
    }
  }
  context.check(fieldDiffers,
                "point/line source selection changes the scaled field");
}

}  // namespace

int main() {
  Context context;
  testSemiCoherentLloydMirrorFactor(context);
  testEndToEndSolve(context);
  testCoherenceModesShareGeometryAndSeparateFieldLaws(context);
  testCartesianComponentsPreserveLegacySelectorSemantics(context);
  testCartesianCurvatureModesPreserveFieldSemantics(context);
  testCartesianBeamWidthModesPreserveFieldSemantics(context);
  testBeamWidthDoesNotChangeFrozenGeometry(context);
  testPerSourceTraceProducesIndependentFrozenCaches(context);
  testSingleSourcePerSourceTraceEquivalence(context);
  testPerSourceTraceDiagnostics(context);
  testSourceBeamPatternScalesAllCoherenceModes(context);
  testGeometricHatCoherenceAndDirectionalPattern(context);
  testGeometricGaussianCoherenceAndDirectionalPattern(context);
  testSimpleGaussianCoherentDirectionalPattern(context);
  testAbnormalRayTerminationFails(context);
  testExplicitFrequencyEntryPoint(context);
  testSixCaseSanitizerSmoke(context);
  testSourceGeometryChangesFieldOnly(context);
  testSplineEnvironmentSolverSmoke(context);

  if (context.failureCount() != 0) {
    std::cerr << context.failureCount()
              << " single-frequency-solver assertion(s) failed\n";
    return 1;
  }
  std::cout << "All Bellhop RayReuse single-frequency-solver tests passed\n";
  return 0;
}
