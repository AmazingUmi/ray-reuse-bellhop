#include "rayreuse/solver/single_frequency_solver.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <iostream>
#include <numbers>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "rayreuse/error.hpp"
#include "rayreuse/model/simulation_case.hpp"
#include "support/munk_case_fixture.hpp"
#include "support/test_harness.hpp"

namespace {

using rayreuse::AcousticMaterial;
using rayreuse::BoundaryModel;
using rayreuse::CartesianCervenySettings;
using rayreuse::Environment;
using rayreuse::FrequencyGrid;
using rayreuse::IntegratorSettings;
using rayreuse::LaunchAngleDegreeBounds;
using rayreuse::LaunchFan;
using rayreuse::RawAttenuation;
using rayreuse::ReceiverGrid;
using rayreuse::SimulationCase;
using rayreuse::SimulationRunMode;
using rayreuse::SingleFrequencyResult;
using rayreuse::SingleFrequencySolver;
using rayreuse::SoundSpeedPoint;
using rayreuse::SoundSpeedProfile;
using rayreuse::Source;
using rayreuse::SourceBeamPattern;
using rayreuse::ValidationError;
using rayreuse::VolumeAttenuationModel;
using rayreuse::semiCoherentLloydMirrorFactor;
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

SimulationCase makeSimulation(std::size_t maximumRayPoints,
                              std::vector<double> frequencies = {50.0},
                              SimulationRunMode runMode =
                                  SimulationRunMode::Coherent,
                              SourceBeamPattern sourceBeamPattern =
                                  SourceBeamPattern::omnidirectional()) {
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
      std::move(sourceBeamPattern), runMode);
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
  context.checkNear(
      semiCoherentLloydMirrorFactor(50.0, 1500.0, 100.0, 0.0), 0.0, 0.0,
      "zero launch angle is a Lloyd null");
  context.checkNear(
      semiCoherentLloydMirrorFactor(50.0, 1500.0, 100.0, -angle), factor, 0.0,
      "Lloyd factor is symmetric in launch angle");
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
      makeSimulation(1000U, {50.0}, SimulationRunMode::Coherent), 1.0,
      500.0);
  const SingleFrequencyResult incoherent = SingleFrequencySolver::solve(
      makeSimulation(1000U, {50.0}, SimulationRunMode::Incoherent), 1.0,
      500.0);
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
  for (std::size_t index = 0U;
       index < coherent.workspace.pressure().size(); ++index) {
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
    const double tolerance = mode == SimulationRunMode::Coherent
                                 ? 2.0e-15
                                 : 2.0e-11;
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
    for (std::size_t index = 0U;
         index < omni.workspace.pressure().size(); ++index) {
      context.checkNear(
          std::abs(directional.workspace.pressure()[index] -
                   amplitude * omni.workspace.pressure()[index]),
          0.0, tolerance,
          "constant source beam pattern scales C/I/S source amplitude");
    }
  }
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

}  // namespace

int main() {
  Context context;
  testSemiCoherentLloydMirrorFactor(context);
  testEndToEndSolve(context);
  testCoherenceModesShareGeometryAndSeparateFieldLaws(context);
  testSourceBeamPatternScalesAllCoherenceModes(context);
  testAbnormalRayTerminationFails(context);
  testExplicitFrequencyEntryPoint(context);
  testSixCaseSanitizerSmoke(context);

  if (context.failureCount() != 0) {
    std::cerr << context.failureCount()
              << " single-frequency-solver assertion(s) failed\n";
    return 1;
  }
  std::cout << "All Bellhop RayReuse single-frequency-solver tests passed\n";
  return 0;
}
