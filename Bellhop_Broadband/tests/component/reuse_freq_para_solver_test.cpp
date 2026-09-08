#include "broadband/solver/reuse_freq_para_solver.hpp"

#include <algorithm>
#include <complex>
#include <cstddef>
#include <iostream>
#include <memory>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "broadband/error.hpp"
#include "broadband/model/simulation_case.hpp"
#include "broadband/solver/nonreuse_solver.hpp"
#include "broadband/solver/reuse_serial_solver.hpp"
#include "support/test_harness.hpp"

namespace {

using broadband::BeamFamily;
using broadband::BeamWidthMode;
using broadband::BiologicalAttenuationLayers;
using broadband::BoundaryCurvatureMode;
using broadband::BoundaryModel;
using broadband::CervenyCoordinateSystem;
using broadband::Environment;
using broadband::FieldComponent;
using broadband::FrancoisGarrisonParameters;
using broadband::FrequencyGrid;
using broadband::IntegratorSettings;
using broadband::LaunchFan;
using broadband::NonReuseResult;
using broadband::NonReuseSolver;
using broadband::ReceiverGrid;
using broadband::ReuseFreqParaSettings;
using broadband::ReuseFreqParaSolver;
using broadband::ReuseFreqParaStatistics;
using broadband::ReuseSerialResult;
using broadband::ReuseSerialSolver;
using broadband::SimulationCase;
using broadband::SimulationRunMode;
using broadband::SoundSpeedPoint;
using broadband::SoundSpeedProfile;
using broadband::Source;
using broadband::SourceBeamPattern;
using broadband::ValidationError;
using broadband::VolumeAttenuation;
using broadband::VolumeAttenuationModel;
using broadband::test::Context;

VolumeAttenuation makeThorpAttenuation() {
  return VolumeAttenuation{.model = VolumeAttenuationModel::Thorp};
}

VolumeAttenuation makeFrancoisGarrisonAttenuation(double temperature) {
  return VolumeAttenuation{.model = VolumeAttenuationModel::FrancoisGarrison,
                           .parameters = FrancoisGarrisonParameters{
                               .temperatureCelsius = temperature,
                               .salinityPsu = 35.0,
                               .pH = 8.0,
                               .meanDepthMeters = 50.0}};
}

VolumeAttenuation makeBiologicalAttenuation(double coefficient) {
  return VolumeAttenuation{
      .model = VolumeAttenuationModel::Biological,
      .parameters = std::make_shared<const BiologicalAttenuationLayers>(
          BiologicalAttenuationLayers{
              {.minimumDepth = 0.0,
               .maximumDepth = 100.0,
               .resonanceFrequency = 1000.0,
               .qualityFactor = 2.0,
               .attenuationCoefficientDecibelsPerKilometer = coefficient}})};
}

SimulationCase makeSimulation(
    std::vector<double> frequencies,
    SimulationRunMode runMode = SimulationRunMode::Coherent,
    BeamFamily beamFamily = BeamFamily::CervenyGaussian,
    FieldComponent fieldComponent = FieldComponent::Pressure,
    BoundaryCurvatureMode curvatureMode = BoundaryCurvatureMode::Standard,
    BeamWidthMode beamWidthMode = BeamWidthMode::MinimumWidth,
    CervenyCoordinateSystem coordinateSystem =
        CervenyCoordinateSystem::Cartesian,
    VolumeAttenuation volumeAttenuation = {}) {
  return SimulationCase(
      Environment(
          SoundSpeedProfile(
              {SoundSpeedPoint{
                   .depth = 0.0, .soundSpeed = 1500.0, .density = 1000.0},
               SoundSpeedPoint{
                   .depth = 100.0, .soundSpeed = 1500.0, .density = 1000.0}}),
          BoundaryModel::vacuum(0.0), BoundaryModel::rigid(100.0),
          std::move(volumeAttenuation)),
      Source{.depth = 50.0, .amplitude = 1.0},
      ReceiverGrid({25.0, 50.0, 75.0}, {10.0, 55.0, 100.0}),
      FrequencyGrid(std::move(frequencies)),
      LaunchFan{.minimumAngle = -2.0 * std::numbers::pi / 180.0,
                .maximumAngle = 2.0 * std::numbers::pi / 180.0,
                .explicitLaunchAngleCount = 48U},
      IntegratorSettings{.stepLength = 10.0,
                         .rangeLimit = 110.0,
                         .depthLimit = 110.0,
                         .maximumRayPoints = 100U},
      SourceBeamPattern::omnidirectional(), runMode, beamFamily, fieldComponent,
      curvatureMode, beamWidthMode, coordinateSystem);
}

SimulationCase makeAttenuatedSimulation(std::vector<double> frequencies,
                                        VolumeAttenuation volumeAttenuation) {
  return makeSimulation(
      std::move(frequencies), SimulationRunMode::Coherent,
      BeamFamily::CervenyGaussian, FieldComponent::Pressure,
      BoundaryCurvatureMode::Standard, BeamWidthMode::MinimumWidth,
      CervenyCoordinateSystem::Cartesian, std::move(volumeAttenuation));
}

std::vector<double> makeFrequencies(std::size_t count) {
  std::vector<double> frequencies;
  frequencies.reserve(count);
  for (std::size_t index = 0U; index < count; ++index) {
    frequencies.push_back(50.0 + 25.0 * static_cast<double>(index));
  }
  return frequencies;
}

struct StreamedFrequencyRun {
  std::vector<std::optional<std::vector<broadband::FrequencyWorkspace>>>
      workspaces;
  std::vector<std::size_t> callbackCounts;
  ReuseFreqParaStatistics statistics;
};

StreamedFrequencyRun runFrequency(const SimulationCase& simulation,
                                  ReuseFreqParaSettings settings,
                                  bool verifyCacheFingerprint = false) {
  StreamedFrequencyRun run{
      .workspaces = std::vector<
          std::optional<std::vector<broadband::FrequencyWorkspace>>>(
          simulation.frequencies().size()),
      .callbackCounts =
          std::vector<std::size_t>(simulation.frequencies().size(), 0U),
      .statistics = {}};
  run.statistics = ReuseFreqParaSolver::solveStreaming(
      simulation, 1.0, 50.0,
      [&run](std::size_t frequencyIndex,
             std::vector<broadband::FrequencyWorkspace>&& sourceWorkspaces,
             const broadband::SingleFrequencyTimings&) {
        ++run.callbackCounts.at(frequencyIndex);
        run.workspaces.at(frequencyIndex).emplace(std::move(sourceWorkspaces));
      },
      settings, {}, verifyCacheFingerprint);
  return run;
}

void checkWorkspaceEqual(Context& context,
                         const broadband::FrequencyWorkspace& actual,
                         const broadband::FrequencyWorkspace& expected,
                         const char* message) {
  context.check(
      actual.frequency() == expected.frequency() &&
          actual.depthCount() == expected.depthCount() &&
          actual.rangeCount() == expected.rangeCount() &&
          std::equal(actual.pressure().begin(), actual.pressure().end(),
                     expected.pressure().begin(), expected.pressure().end()),
      message);
}

void testFrequencyCounts(Context& context) {
  for (const std::size_t frequencyCount : {1U, 2U, 16U}) {
    const SimulationCase simulation =
        makeSimulation(makeFrequencies(frequencyCount));
    const NonReuseResult nonReuse =
        NonReuseSolver::solve(simulation, 1.0, 50.0);
    const ReuseSerialResult serial =
        ReuseSerialSolver::solve(simulation, 1.0, 50.0);
    const StreamedFrequencyRun frequency =
        runFrequency(simulation,
                     ReuseFreqParaSettings{.workerCount = 4U,
                                           .outputQueueCapacity = 2U,
                                           .memoryBudgetBytes = 0U},
                     frequencyCount == 2U);

    context.check(frequency.statistics.tracePassCount == 1U &&
                      serial.statistics.tracePassCount == 1U &&
                      nonReuse.statistics.tracePassCount == frequencyCount,
                  "frequency and serial reuse trace once for "
                  "1/2/16 frequencies");
    context.check(frequency.statistics.activeFrequencyLimit ==
                          std::min<std::size_t>(4U, frequencyCount) &&
                      frequency.statistics.peakQueuedResults <=
                          frequency.statistics.outputQueueCapacity &&
                      frequency.statistics.outputQueueCapacity ==
                          std::min<std::size_t>(2U, frequencyCount),
                  "frequency workers and completed queue stay "
                  "within configured bounds");
    context.check(frequency.callbackCounts ==
                      std::vector<std::size_t>(frequencyCount, 1U),
                  "frequency callback consumes every frequency "
                  "exactly once");
    context.check(
        frequency.statistics.frequencyTimings.size() == frequencyCount &&
            frequency.statistics.rayCount == serial.statistics.rayCount &&
            frequency.statistics.rayCacheBytes ==
                serial.statistics.rayCacheBytes &&
            frequency.statistics.estimatedWorkspaceBytes ==
                3U * 3U * sizeof(std::complex<double>) &&
            frequency.statistics.estimatedPeakMemoryBytes >=
                frequency.statistics.rayCacheBytes,
        "frequency statistics expose cache, workspace, "
        "and per-frequency timing metrics");
    if (frequencyCount == 2U) {
      context.check(frequency.statistics.cacheFingerprintVerified &&
                        frequency.statistics.cacheFingerprintBefore ==
                            frequency.statistics.cacheFingerprintAfter,
                    "frequency frequency projection leaves the "
                    "frozen cache unchanged");
    }

    for (std::size_t index = 0U; index < frequencyCount; ++index) {
      context.check(frequency.workspaces[index].has_value(),
                    "frequency run returns every indexed workspace");
      if (frequency.workspaces[index]) {
        checkWorkspaceEqual(context, frequency.workspaces[index]->front(),
                            serial.frequencyResults[index].workspaces.front(),
                            "frequency pressure is bitwise equal to "
                            "serial reuse");
        checkWorkspaceEqual(context, frequency.workspaces[index]->front(),
                            nonReuse.frequencyResults[index].workspace,
                            "frequency pressure is bitwise equal to "
                            "non-reuse");
      }
    }
  }
}

void testRepeatedRunIsDeterministic(Context& context) {
  const SimulationCase simulation = makeSimulation(makeFrequencies(16U));
  const ReuseFreqParaSettings settings{
      .workerCount = 4U, .outputQueueCapacity = 1U, .memoryBudgetBytes = 0U};
  const StreamedFrequencyRun first = runFrequency(simulation, settings);
  const StreamedFrequencyRun second = runFrequency(simulation, settings);

  for (std::size_t index = 0U; index < first.workspaces.size(); ++index) {
    context.check(first.workspaces[index].has_value() &&
                      second.workspaces[index].has_value(),
                  "repeated frequency runs return every workspace");
    if (first.workspaces[index] && second.workspaces[index]) {
      checkWorkspaceEqual(
          context, first.workspaces[index]->front(),
          second.workspaces[index]->front(),
          "repeated frequency pressure is bitwise deterministic");
    }
  }
}

void testVolumeAttenuationExecutionInvariants(Context& context) {
  const std::vector<VolumeAttenuation> models = {
      makeThorpAttenuation(), makeFrancoisGarrisonAttenuation(10.0),
      makeBiologicalAttenuation(100.0)};
  const ReuseFreqParaSettings settings{
      .workerCount = 3U, .outputQueueCapacity = 1U, .memoryBudgetBytes = 0U};

  for (const VolumeAttenuation& model : models) {
    const SimulationCase simulation =
        makeAttenuatedSimulation({500.0, 1000.0, 2000.0}, model);
    const NonReuseResult nonReuse =
        NonReuseSolver::solve(simulation, 1.0, 50.0);
    const ReuseSerialResult serial =
        ReuseSerialSolver::solve(simulation, 1.0, 50.0, {}, true);
    const StreamedFrequencyRun first = runFrequency(simulation, settings, true);
    const StreamedFrequencyRun repeated =
        runFrequency(simulation, settings, true);

    context.check(
        serial.statistics.cacheFingerprintVerified &&
            serial.statistics.cacheFingerprintBefore ==
                serial.statistics.cacheFingerprintAfter &&
            first.statistics.cacheFingerprintVerified &&
            first.statistics.cacheFingerprintBefore ==
                first.statistics.cacheFingerprintAfter &&
            repeated.statistics.cacheFingerprintVerified &&
            repeated.statistics.cacheFingerprintBefore ==
                repeated.statistics.cacheFingerprintAfter &&
            first.statistics.cacheFingerprintBefore ==
                repeated.statistics.cacheFingerprintBefore,
        "Thorp/FG/biological serial and repeated frequency projection preserve "
        "the frozen cache fingerprint");

    for (std::size_t index = 0U; index < simulation.frequencies().size();
         ++index) {
      context.check(first.workspaces[index].has_value() &&
                        repeated.workspaces[index].has_value(),
                    "attenuated frequency runs publish every frequency");
      if (!first.workspaces[index] || !repeated.workspaces[index]) continue;
      checkWorkspaceEqual(
          context, serial.frequencyResults[index].workspaces.front(),
          nonReuse.frequencyResults[index].workspace,
          "attenuated serial reuse pressure equals non-reuse bitwise");
      checkWorkspaceEqual(
          context, first.workspaces[index]->front(),
          nonReuse.frequencyResults[index].workspace,
          "attenuated Frequency Reuse pressure equals non-reuse bitwise");
      checkWorkspaceEqual(
          context, repeated.workspaces[index]->front(),
          first.workspaces[index]->front(),
          "repeated attenuated frequency pressure is bitwise deterministic");
    }
  }

  for (const auto& parameterPair :
       {std::pair{makeFrancoisGarrisonAttenuation(10.0),
                  makeFrancoisGarrisonAttenuation(18.0)},
        std::pair{makeBiologicalAttenuation(100.0),
                  makeBiologicalAttenuation(250.0)}}) {
    const SimulationCase firstSimulation =
        makeAttenuatedSimulation({500.0, 1000.0, 2000.0}, parameterPair.first);
    const SimulationCase changedSimulation =
        makeAttenuatedSimulation({500.0, 1000.0, 2000.0}, parameterPair.second);
    const ReuseSerialResult first =
        ReuseSerialSolver::solve(firstSimulation, 1.0, 50.0, {}, true);
    const ReuseSerialResult changed =
        ReuseSerialSolver::solve(changedSimulation, 1.0, 50.0, {}, true);
    context.check(
        first.statistics.cacheFingerprintBefore ==
                changed.statistics.cacheFingerprintBefore &&
            first.statistics.cacheFingerprintBefore ==
                first.statistics.cacheFingerprintAfter &&
            changed.statistics.cacheFingerprintBefore ==
                changed.statistics.cacheFingerprintAfter,
        "changing FG/biological payload preserves identical frozen geometry");

    bool pressureDiffers = false;
    for (std::size_t index = 0U; index < first.frequencyResults.size();
         ++index) {
      pressureDiffers =
          pressureDiffers ||
          !std::equal(
              first.frequencyResults[index]
                  .workspaces.front()
                  .pressure()
                  .begin(),
              first.frequencyResults[index].workspaces.front().pressure().end(),
              changed.frequencyResults[index]
                  .workspaces.front()
                  .pressure()
                  .begin());
    }
    context.check(pressureDiffers,
                  "changing FG/biological payload changes projected pressure");
  }
}

void testCoherenceModesMatchAcrossExecution(Context& context) {
  for (const BeamFamily beamFamily :
       {BeamFamily::CervenyGaussian, BeamFamily::GeometricHat,
        BeamFamily::GeometricGaussian}) {
    for (const SimulationRunMode mode :
         {SimulationRunMode::Coherent, SimulationRunMode::Incoherent,
          SimulationRunMode::SemiCoherent}) {
      const SimulationCase simulation =
          makeSimulation({50.0, 100.0}, mode, beamFamily);
      const NonReuseResult nonReuse =
          NonReuseSolver::solve(simulation, 1.0, 50.0);
      const ReuseSerialResult reuse =
          ReuseSerialSolver::solve(simulation, 1.0, 50.0, {}, true);
      const StreamedFrequencyRun frequency =
          runFrequency(simulation,
                       ReuseFreqParaSettings{.workerCount = 2U,
                                             .outputQueueCapacity = 1U,
                                             .memoryBudgetBytes = 0U},
                       true);
      context.check(
          reuse.statistics.cacheFingerprintVerified &&
              reuse.statistics.cacheFingerprintBefore ==
                  reuse.statistics.cacheFingerprintAfter &&
              frequency.statistics.cacheFingerprintVerified &&
              frequency.statistics.cacheFingerprintBefore ==
                  frequency.statistics.cacheFingerprintAfter,
          "C/I/S Cerveny, GeoHat, and GeoGaussian reuse paths preserve the "
          "frozen cache "
          "fingerprint");
      for (std::size_t index = 0U; index < 2U; ++index) {
        context.check(frequency.workspaces[index].has_value(),
                      "frequency C/I/S returns every frequency workspace");
        if (!frequency.workspaces[index].has_value()) {
          continue;
        }
        checkWorkspaceEqual(context,
                            reuse.frequencyResults[index].workspaces.front(),
                            nonReuse.frequencyResults[index].workspace,
                            "serial reuse C/I/S is bitwise equal to non-reuse");
        checkWorkspaceEqual(
            context, frequency.workspaces[index]->front(),
            nonReuse.frequencyResults[index].workspace,
            "Frequency Reuse C/I/S is bitwise equal to non-reuse");
      }
    }
  }
}

void testSimpleGaussianMatchesAcrossExecution(Context& context) {
  const SimulationCase simulation = makeSimulation(
      {50.0, 100.0}, SimulationRunMode::Coherent, BeamFamily::SimpleGaussian);
  const NonReuseResult nonReuse = NonReuseSolver::solve(simulation, 1.0, 50.0);
  const ReuseSerialResult reuse =
      ReuseSerialSolver::solve(simulation, 1.0, 50.0, {}, true);
  const StreamedFrequencyRun frequency =
      runFrequency(simulation,
                   ReuseFreqParaSettings{.workerCount = 2U,
                                         .outputQueueCapacity = 1U,
                                         .memoryBudgetBytes = 0U},
                   true);
  context.check(
      reuse.statistics.cacheFingerprintVerified &&
          reuse.statistics.cacheFingerprintBefore ==
              reuse.statistics.cacheFingerprintAfter &&
          frequency.statistics.cacheFingerprintVerified &&
          frequency.statistics.cacheFingerprintBefore ==
              frequency.statistics.cacheFingerprintAfter,
      "Simple Gaussian reuse paths preserve the frozen cache fingerprint");
  for (std::size_t index = 0U; index < 2U; ++index) {
    context.check(frequency.workspaces[index].has_value(),
                  "frequency Simple Gaussian returns every frequency");
    if (!frequency.workspaces[index].has_value()) {
      continue;
    }
    checkWorkspaceEqual(
        context, reuse.frequencyResults[index].workspaces.front(),
        nonReuse.frequencyResults[index].workspace,
        "serial reuse Simple Gaussian is bitwise equal to non-reuse");
    checkWorkspaceEqual(
        context, frequency.workspaces[index]->front(),
        nonReuse.frequencyResults[index].workspace,
        "Frequency Reuse Simple Gaussian is bitwise equal to non-reuse");
  }
}

void testCartesianComponentsMatchAcrossExecution(Context& context) {
  for (const SimulationRunMode mode :
       {SimulationRunMode::Coherent, SimulationRunMode::Incoherent,
        SimulationRunMode::SemiCoherent}) {
    for (const BoundaryCurvatureMode curvatureMode :
         {BoundaryCurvatureMode::Double, BoundaryCurvatureMode::Standard,
          BoundaryCurvatureMode::Zero}) {
      for (const BeamWidthMode widthMode :
           {BeamWidthMode::SpaceFilling, BeamWidthMode::MinimumWidth,
            BeamWidthMode::Wkb}) {
        std::optional<ReuseSerialResult> pressure;
        for (const FieldComponent component :
             {FieldComponent::Pressure, FieldComponent::Vertical,
              FieldComponent::Horizontal}) {
          const SimulationCase simulation =
              makeSimulation({50.0, 100.0}, mode, BeamFamily::CervenyGaussian,
                             component, curvatureMode, widthMode);
          const NonReuseResult nonReuse =
              NonReuseSolver::solve(simulation, 1.0, 50.0);
          const ReuseSerialResult reuse =
              ReuseSerialSolver::solve(simulation, 1.0, 50.0, {}, true);
          const StreamedFrequencyRun frequency =
              runFrequency(simulation,
                           ReuseFreqParaSettings{.workerCount = 2U,
                                                 .outputQueueCapacity = 1U,
                                                 .memoryBudgetBytes = 0U},
                           true);
          context.check(
              reuse.statistics.cacheFingerprintVerified &&
                  reuse.statistics.cacheFingerprintBefore ==
                      reuse.statistics.cacheFingerprintAfter &&
                  frequency.statistics.cacheFingerprintVerified &&
                  frequency.statistics.cacheFingerprintBefore ==
                      frequency.statistics.cacheFingerprintAfter,
              "Cartesian Cerveny C/I/S x F/M/W x D/S/Z x P/V/H preserves "
              "frozen cache");
          for (std::size_t index = 0U; index < 2U; ++index) {
            context.check(
                frequency.workspaces[index].has_value(),
                "frequency Cartesian width/curvature returns every frequency");
            if (!frequency.workspaces[index].has_value()) {
              continue;
            }
            checkWorkspaceEqual(
                context, reuse.frequencyResults[index].workspaces.front(),
                nonReuse.frequencyResults[index].workspace,
                "Cartesian width/curvature serial reuse equals non-reuse "
                "bitwise");
            checkWorkspaceEqual(
                context, frequency.workspaces[index]->front(),
                nonReuse.frequencyResults[index].workspace,
                "Cartesian width/curvature Frequency Reuse equals non-reuse "
                "bitwise");
            if (pressure.has_value()) {
              checkWorkspaceEqual(
                  context, reuse.frequencyResults[index].workspaces.front(),
                  pressure->frequencyResults[index].workspaces.front(),
                  "Cartesian P/V/H legacy selectors are bitwise identical");
            }
          }
          if (!pressure.has_value()) {
            pressure.emplace(reuse);
          }
        }
      }
    }
  }
}

void testRayCenteredMatrixMatchesAcrossExecution(Context& context) {
  for (const SimulationRunMode mode :
       {SimulationRunMode::Coherent, SimulationRunMode::Incoherent,
        SimulationRunMode::SemiCoherent}) {
    for (const BoundaryCurvatureMode curvatureMode :
         {BoundaryCurvatureMode::Double, BoundaryCurvatureMode::Standard,
          BoundaryCurvatureMode::Zero}) {
      for (const BeamWidthMode widthMode :
           {BeamWidthMode::SpaceFilling, BeamWidthMode::MinimumWidth,
            BeamWidthMode::Wkb}) {
        for (const FieldComponent component :
             {FieldComponent::Pressure, FieldComponent::Vertical,
              FieldComponent::Horizontal}) {
          const SimulationCase simulation = makeSimulation(
              {50.0, 100.0}, mode, BeamFamily::CervenyGaussian, component,
              curvatureMode, widthMode, CervenyCoordinateSystem::RayCentered);
          const NonReuseResult nonReuse =
              NonReuseSolver::solve(simulation, 1.0, 50.0);
          const ReuseSerialResult reuse =
              ReuseSerialSolver::solve(simulation, 1.0, 50.0, {}, true);
          const StreamedFrequencyRun frequency =
              runFrequency(simulation,
                           ReuseFreqParaSettings{.workerCount = 2U,
                                                 .outputQueueCapacity = 1U,
                                                 .memoryBudgetBytes = 0U},
                           true);
          context.check(reuse.statistics.cacheFingerprintVerified &&
                            reuse.statistics.cacheFingerprintBefore ==
                                reuse.statistics.cacheFingerprintAfter &&
                            frequency.statistics.cacheFingerprintVerified &&
                            frequency.statistics.cacheFingerprintBefore ==
                                frequency.statistics.cacheFingerprintAfter,
                        "ray-centered C/I/S x F/M/W x D/S/Z x P/V/H preserves "
                        "the frozen cache");
          for (std::size_t index = 0U; index < 2U; ++index) {
            context.check(
                frequency.workspaces[index].has_value(),
                "frequency ray-centered matrix returns every frequency");
            if (!frequency.workspaces[index].has_value()) {
              continue;
            }
            checkWorkspaceEqual(
                context, reuse.frequencyResults[index].workspaces.front(),
                nonReuse.frequencyResults[index].workspace,
                "ray-centered serial reuse equals non-reuse bitwise");
            checkWorkspaceEqual(
                context, frequency.workspaces[index]->front(),
                nonReuse.frequencyResults[index].workspace,
                "ray-centered Frequency Reuse equals non-reuse bitwise");
          }
        }
      }
    }
  }
}

void testRayCenteredGeometricHatMatchesAcrossExecution(Context& context) {
  for (const SimulationRunMode mode :
       {SimulationRunMode::Coherent, SimulationRunMode::Incoherent,
        SimulationRunMode::SemiCoherent}) {
    const SimulationCase simulation = makeSimulation(
        {50.0, 100.0}, mode, BeamFamily::GeometricHat, FieldComponent::Pressure,
        BoundaryCurvatureMode::Standard, BeamWidthMode::MinimumWidth,
        CervenyCoordinateSystem::RayCentered);
    const NonReuseResult nonReuse =
        NonReuseSolver::solve(simulation, 1.0, 50.0);
    const ReuseSerialResult reuse =
        ReuseSerialSolver::solve(simulation, 1.0, 50.0, {}, true);
    const StreamedFrequencyRun frequency =
        runFrequency(simulation,
                     ReuseFreqParaSettings{.workerCount = 2U,
                                           .outputQueueCapacity = 1U,
                                           .memoryBudgetBytes = 0U},
                     true);
    context.check(
        reuse.statistics.cacheFingerprintVerified &&
            reuse.statistics.cacheFingerprintBefore ==
                reuse.statistics.cacheFingerprintAfter &&
            frequency.statistics.cacheFingerprintVerified &&
            frequency.statistics.cacheFingerprintBefore ==
                frequency.statistics.cacheFingerprintAfter,
        "ray-centered GeoHat C/I/S preserves the frozen cache fingerprint");
    for (std::size_t index = 0U; index < 2U; ++index) {
      context.check(frequency.workspaces[index].has_value(),
                    "frequency ray-centered GeoHat returns every frequency");
      if (!frequency.workspaces[index].has_value()) continue;
      checkWorkspaceEqual(context,
                          reuse.frequencyResults[index].workspaces.front(),
                          nonReuse.frequencyResults[index].workspace,
                          "ray-centered GeoHat reuse equals non-reuse bitwise");
      checkWorkspaceEqual(
          context, frequency.workspaces[index]->front(),
          nonReuse.frequencyResults[index].workspace,
          "ray-centered GeoHat frequency equals non-reuse bitwise");
    }
  }
}

void testMemoryBudget(Context& context) {
  const SimulationCase simulation = makeSimulation(makeFrequencies(16U));
  const StreamedFrequencyRun unrestricted =
      runFrequency(simulation, ReuseFreqParaSettings{.workerCount = 4U,
                                                     .outputQueueCapacity = 1U,
                                                     .memoryBudgetBytes = 0U});
  const std::size_t cacheBytes = unrestricted.statistics.rayCacheBytes;
  const std::size_t workspaceBytes =
      unrestricted.statistics.estimatedWorkspaceBytes;
  const std::size_t twoWorkerBudget = cacheBytes + 4U * workspaceBytes;

  const StreamedFrequencyRun constrained = runFrequency(
      simulation, ReuseFreqParaSettings{.workerCount = 4U,
                                        .outputQueueCapacity = 1U,
                                        .memoryBudgetBytes = twoWorkerBudget});
  context.check(
      constrained.statistics.activeFrequencyLimit == 2U &&
          constrained.statistics.estimatedPeakMemoryBytes <= twoWorkerBudget,
      "memory budget lowers the active frequency limit");

  context.expectThrows<ValidationError>(
      [&]() {
        static_cast<void>(runFrequency(
            simulation,
            ReuseFreqParaSettings{
                .workerCount = 4U,
                .outputQueueCapacity = 1U,
                .memoryBudgetBytes = cacheBytes + 2U * workspaceBytes}));
      },
      "memory budget rejects a run that cannot hold "
      "one active frequency, its output queue, "
      "and the consumer workspace");
}

void testInvalidSettingsAndConsumerFailure(Context& context) {
  const SimulationCase simulation = makeSimulation(makeFrequencies(2U));
  context.expectThrows<ValidationError>(
      [&]() {
        static_cast<void>(ReuseFreqParaSolver::solveStreaming(
            simulation, 1.0, 50.0,
            [](std::size_t, std::vector<broadband::FrequencyWorkspace>&&,
               const broadband::SingleFrequencyTimings&) {},
            ReuseFreqParaSettings{.workerCount = 0U,
                                  .outputQueueCapacity = 1U,
                                  .memoryBudgetBytes = 0U}));
      },
      "frequency solver rejects zero workers");
  context.expectThrows<ValidationError>(
      [&]() {
        static_cast<void>(ReuseFreqParaSolver::solveStreaming(
            simulation, 1.0, 50.0,
            [](std::size_t, std::vector<broadband::FrequencyWorkspace>&&,
               const broadband::SingleFrequencyTimings&) {},
            ReuseFreqParaSettings{.workerCount = 1U,
                                  .outputQueueCapacity = 0U,
                                  .memoryBudgetBytes = 0U}));
      },
      "frequency solver rejects an empty output queue");
  context.expectThrows<ValidationError>(
      [&]() {
        static_cast<void>(ReuseFreqParaSolver::solveStreaming(
            simulation, 1.0, 50.0,
            [](std::size_t, std::vector<broadband::FrequencyWorkspace>&&,
               const broadband::SingleFrequencyTimings&) {},
            ReuseFreqParaSettings{.workerCount = 1U,
                                  .outputQueueCapacity = 3U,
                                  .memoryBudgetBytes = 0U}));
      },
      "frequency solver rejects output queue capacity above two");
  context.expectThrows<std::runtime_error>(
      [&]() {
        static_cast<void>(ReuseFreqParaSolver::solveStreaming(
            simulation, 1.0, 50.0,
            [](std::size_t, std::vector<broadband::FrequencyWorkspace>&&,
               const broadband::SingleFrequencyTimings&) {
              throw std::runtime_error("consumer failure");
            },
            ReuseFreqParaSettings{.workerCount = 2U,
                                  .outputQueueCapacity = 1U,
                                  .memoryBudgetBytes = 0U}));
      },
      "frequency solver stops workers and propagates "
      "consumer failures");
}

}  // namespace

int main() {
  Context context;
  testFrequencyCounts(context);
  testRepeatedRunIsDeterministic(context);
  testVolumeAttenuationExecutionInvariants(context);
  testCoherenceModesMatchAcrossExecution(context);
  testSimpleGaussianMatchesAcrossExecution(context);
  testCartesianComponentsMatchAcrossExecution(context);
  testRayCenteredMatrixMatchesAcrossExecution(context);
  testRayCenteredGeometricHatMatchesAcrossExecution(context);
  testMemoryBudget(context);
  testInvalidSettingsAndConsumerFailure(context);

  if (context.failureCount() != 0) {
    std::cerr << context.failureCount()
              << " reuse-freq-para-solver assertion(s) failed\n";
    return 1;
  }
  std::cout << "All Bellhop Broadband reuse-freq-para-solver tests passed\n";
  return 0;
}
