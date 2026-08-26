#include "rayreuse/solver/parallel_ray_reuse_solver.hpp"

#include <algorithm>
#include <complex>
#include <cstddef>
#include <iostream>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "rayreuse/error.hpp"
#include "rayreuse/model/simulation_case.hpp"
#include "rayreuse/solver/broadband_nonreuse_solver.hpp"
#include "rayreuse/solver/serial_ray_reuse_solver.hpp"
#include "support/test_harness.hpp"

namespace {

using rayreuse::BeamFamily;
using rayreuse::BeamWidthMode;
using rayreuse::BoundaryCurvatureMode;
using rayreuse::BoundaryModel;
using rayreuse::BroadbandNonReuseResult;
using rayreuse::BroadbandNonReuseSolver;
using rayreuse::CervenyCoordinateSystem;
using rayreuse::Environment;
using rayreuse::FieldComponent;
using rayreuse::FrequencyGrid;
using rayreuse::IntegratorSettings;
using rayreuse::LaunchFan;
using rayreuse::ParallelRayReuseSettings;
using rayreuse::ParallelRayReuseSolver;
using rayreuse::ParallelRayReuseStatistics;
using rayreuse::ReceiverGrid;
using rayreuse::SerialRayReuseResult;
using rayreuse::SerialRayReuseSolver;
using rayreuse::SimulationCase;
using rayreuse::SimulationRunMode;
using rayreuse::SoundSpeedPoint;
using rayreuse::SoundSpeedProfile;
using rayreuse::Source;
using rayreuse::SourceBeamPattern;
using rayreuse::ValidationError;
using rayreuse::test::Context;

SimulationCase makeSimulation(
    std::vector<double> frequencies,
    SimulationRunMode runMode = SimulationRunMode::Coherent,
    BeamFamily beamFamily = BeamFamily::CervenyGaussian,
    FieldComponent fieldComponent = FieldComponent::Pressure,
    BoundaryCurvatureMode curvatureMode = BoundaryCurvatureMode::Standard,
    BeamWidthMode beamWidthMode = BeamWidthMode::MinimumWidth,
    CervenyCoordinateSystem coordinateSystem =
        CervenyCoordinateSystem::Cartesian) {
  return SimulationCase(
      Environment(
          SoundSpeedProfile(
              {SoundSpeedPoint{
                   .depth = 0.0, .soundSpeed = 1500.0, .density = 1000.0},
               SoundSpeedPoint{
                   .depth = 100.0, .soundSpeed = 1500.0, .density = 1000.0}}),
          BoundaryModel::vacuum(0.0), BoundaryModel::rigid(100.0)),
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

std::vector<double> makeFrequencies(std::size_t count) {
  std::vector<double> frequencies;
  frequencies.reserve(count);
  for (std::size_t index = 0U; index < count; ++index) {
    frequencies.push_back(50.0 + 25.0 * static_cast<double>(index));
  }
  return frequencies;
}

struct StreamedParallelRun {
  std::vector<std::optional<rayreuse::FrequencyWorkspace>> workspaces;
  std::vector<std::size_t> callbackCounts;
  ParallelRayReuseStatistics statistics;
};

StreamedParallelRun runParallel(const SimulationCase& simulation,
                                ParallelRayReuseSettings settings,
                                bool verifyCacheFingerprint = false) {
  StreamedParallelRun run{
      .workspaces = std::vector<std::optional<rayreuse::FrequencyWorkspace>>(
          simulation.frequencies().size()),
      .callbackCounts =
          std::vector<std::size_t>(simulation.frequencies().size(), 0U),
      .statistics = {}};
  run.statistics = ParallelRayReuseSolver::solveStreaming(
      simulation, 1.0, 50.0,
      [&run](std::size_t frequencyIndex,
             rayreuse::FrequencyWorkspace&& workspace,
             const rayreuse::SingleFrequencyTimings&) {
        ++run.callbackCounts.at(frequencyIndex);
        run.workspaces.at(frequencyIndex).emplace(std::move(workspace));
      },
      settings, {}, verifyCacheFingerprint);
  return run;
}

void checkWorkspaceEqual(Context& context,
                         const rayreuse::FrequencyWorkspace& actual,
                         const rayreuse::FrequencyWorkspace& expected,
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
    const BroadbandNonReuseResult nonReuse =
        BroadbandNonReuseSolver::solve(simulation, 1.0, 50.0);
    const SerialRayReuseResult serial =
        SerialRayReuseSolver::solve(simulation, 1.0, 50.0);
    const StreamedParallelRun parallel =
        runParallel(simulation,
                    ParallelRayReuseSettings{.workerCount = 4U,
                                             .outputQueueCapacity = 2U,
                                             .memoryBudgetBytes = 0U},
                    frequencyCount == 2U);

    context.check(parallel.statistics.tracePassCount == 1U &&
                      serial.statistics.tracePassCount == 1U &&
                      nonReuse.statistics.tracePassCount == frequencyCount,
                  "parallel and serial reuse trace once for "
                  "1/2/16 frequencies");
    context.check(parallel.statistics.activeFrequencyLimit ==
                          std::min<std::size_t>(4U, frequencyCount) &&
                      parallel.statistics.peakQueuedResults <=
                          parallel.statistics.outputQueueCapacity &&
                      parallel.statistics.outputQueueCapacity ==
                          std::min<std::size_t>(2U, frequencyCount),
                  "parallel workers and completed queue stay "
                  "within configured bounds");
    context.check(
        parallel.callbackCounts == std::vector<std::size_t>(frequencyCount, 1U),
        "parallel callback consumes every frequency "
        "exactly once");
    context.check(
        parallel.statistics.frequencyTimings.size() == frequencyCount &&
            parallel.statistics.rayCount == serial.statistics.rayCount &&
            parallel.statistics.rayCacheBytes ==
                serial.statistics.rayCacheBytes &&
            parallel.statistics.estimatedWorkspaceBytes ==
                3U * 3U * sizeof(std::complex<double>) &&
            parallel.statistics.estimatedPeakMemoryBytes >=
                parallel.statistics.rayCacheBytes,
        "parallel statistics expose cache, workspace, "
        "and per-frequency timing metrics");
    if (frequencyCount == 2U) {
      context.check(parallel.statistics.cacheFingerprintVerified &&
                        parallel.statistics.cacheFingerprintBefore ==
                            parallel.statistics.cacheFingerprintAfter,
                    "parallel frequency projection leaves the "
                    "frozen cache unchanged");
    }

    for (std::size_t index = 0U; index < frequencyCount; ++index) {
      context.check(parallel.workspaces[index].has_value(),
                    "parallel run returns every indexed workspace");
      if (parallel.workspaces[index]) {
        checkWorkspaceEqual(context, *parallel.workspaces[index],
                            serial.frequencyResults[index].workspace,
                            "parallel pressure is bitwise equal to "
                            "serial reuse");
        checkWorkspaceEqual(context, *parallel.workspaces[index],
                            nonReuse.frequencyResults[index].workspace,
                            "parallel pressure is bitwise equal to "
                            "non-reuse");
      }
    }
  }
}

void testRepeatedRunIsDeterministic(Context& context) {
  const SimulationCase simulation = makeSimulation(makeFrequencies(16U));
  const ParallelRayReuseSettings settings{
      .workerCount = 4U, .outputQueueCapacity = 1U, .memoryBudgetBytes = 0U};
  const StreamedParallelRun first = runParallel(simulation, settings);
  const StreamedParallelRun second = runParallel(simulation, settings);

  for (std::size_t index = 0U; index < first.workspaces.size(); ++index) {
    context.check(first.workspaces[index].has_value() &&
                      second.workspaces[index].has_value(),
                  "repeated parallel runs return every workspace");
    if (first.workspaces[index] && second.workspaces[index]) {
      checkWorkspaceEqual(
          context, *first.workspaces[index], *second.workspaces[index],
          "repeated parallel pressure is bitwise deterministic");
    }
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
      const BroadbandNonReuseResult nonReuse =
          BroadbandNonReuseSolver::solve(simulation, 1.0, 50.0);
      const SerialRayReuseResult reuse =
          SerialRayReuseSolver::solve(simulation, 1.0, 50.0, {}, true);
      const StreamedParallelRun parallel =
          runParallel(simulation,
                      ParallelRayReuseSettings{.workerCount = 2U,
                                               .outputQueueCapacity = 1U,
                                               .memoryBudgetBytes = 0U},
                      true);
      context.check(
          reuse.statistics.cacheFingerprintVerified &&
              reuse.statistics.cacheFingerprintBefore ==
                  reuse.statistics.cacheFingerprintAfter &&
              parallel.statistics.cacheFingerprintVerified &&
              parallel.statistics.cacheFingerprintBefore ==
                  parallel.statistics.cacheFingerprintAfter,
          "C/I/S Cerveny, GeoHat, and GeoGaussian reuse paths preserve the "
          "frozen cache "
          "fingerprint");
      for (std::size_t index = 0U; index < 2U; ++index) {
        context.check(parallel.workspaces[index].has_value(),
                      "parallel C/I/S returns every frequency workspace");
        if (!parallel.workspaces[index].has_value()) {
          continue;
        }
        checkWorkspaceEqual(context, reuse.frequencyResults[index].workspace,
                            nonReuse.frequencyResults[index].workspace,
                            "serial reuse C/I/S is bitwise equal to non-reuse");
        checkWorkspaceEqual(
            context, *parallel.workspaces[index],
            nonReuse.frequencyResults[index].workspace,
            "parallel reuse C/I/S is bitwise equal to non-reuse");
      }
    }
  }
}

void testSimpleGaussianMatchesAcrossExecution(Context& context) {
  const SimulationCase simulation = makeSimulation(
      {50.0, 100.0}, SimulationRunMode::Coherent, BeamFamily::SimpleGaussian);
  const BroadbandNonReuseResult nonReuse =
      BroadbandNonReuseSolver::solve(simulation, 1.0, 50.0);
  const SerialRayReuseResult reuse =
      SerialRayReuseSolver::solve(simulation, 1.0, 50.0, {}, true);
  const StreamedParallelRun parallel =
      runParallel(simulation,
                  ParallelRayReuseSettings{.workerCount = 2U,
                                           .outputQueueCapacity = 1U,
                                           .memoryBudgetBytes = 0U},
                  true);
  context.check(
      reuse.statistics.cacheFingerprintVerified &&
          reuse.statistics.cacheFingerprintBefore ==
              reuse.statistics.cacheFingerprintAfter &&
          parallel.statistics.cacheFingerprintVerified &&
          parallel.statistics.cacheFingerprintBefore ==
              parallel.statistics.cacheFingerprintAfter,
      "Simple Gaussian reuse paths preserve the frozen cache fingerprint");
  for (std::size_t index = 0U; index < 2U; ++index) {
    context.check(parallel.workspaces[index].has_value(),
                  "parallel Simple Gaussian returns every frequency");
    if (!parallel.workspaces[index].has_value()) {
      continue;
    }
    checkWorkspaceEqual(
        context, reuse.frequencyResults[index].workspace,
        nonReuse.frequencyResults[index].workspace,
        "serial reuse Simple Gaussian is bitwise equal to non-reuse");
    checkWorkspaceEqual(
        context, *parallel.workspaces[index],
        nonReuse.frequencyResults[index].workspace,
        "parallel reuse Simple Gaussian is bitwise equal to non-reuse");
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
        std::optional<SerialRayReuseResult> pressure;
        for (const FieldComponent component :
             {FieldComponent::Pressure, FieldComponent::Vertical,
              FieldComponent::Horizontal}) {
          const SimulationCase simulation =
              makeSimulation({50.0, 100.0}, mode, BeamFamily::CervenyGaussian,
                             component, curvatureMode, widthMode);
          const BroadbandNonReuseResult nonReuse =
              BroadbandNonReuseSolver::solve(simulation, 1.0, 50.0);
          const SerialRayReuseResult reuse =
              SerialRayReuseSolver::solve(simulation, 1.0, 50.0, {}, true);
          const StreamedParallelRun parallel =
              runParallel(simulation,
                          ParallelRayReuseSettings{.workerCount = 2U,
                                                   .outputQueueCapacity = 1U,
                                                   .memoryBudgetBytes = 0U},
                          true);
          context.check(
              reuse.statistics.cacheFingerprintVerified &&
                  reuse.statistics.cacheFingerprintBefore ==
                      reuse.statistics.cacheFingerprintAfter &&
                  parallel.statistics.cacheFingerprintVerified &&
                  parallel.statistics.cacheFingerprintBefore ==
                      parallel.statistics.cacheFingerprintAfter,
              "Cartesian Cerveny C/I/S x F/M/W x D/S/Z x P/V/H preserves "
              "frozen cache");
          for (std::size_t index = 0U; index < 2U; ++index) {
            context.check(
                parallel.workspaces[index].has_value(),
                "parallel Cartesian width/curvature returns every frequency");
            if (!parallel.workspaces[index].has_value()) {
              continue;
            }
            checkWorkspaceEqual(
                context, reuse.frequencyResults[index].workspace,
                nonReuse.frequencyResults[index].workspace,
                "Cartesian width/curvature serial reuse equals non-reuse "
                "bitwise");
            checkWorkspaceEqual(
                context, *parallel.workspaces[index],
                nonReuse.frequencyResults[index].workspace,
                "Cartesian width/curvature parallel reuse equals non-reuse "
                "bitwise");
            if (pressure.has_value()) {
              checkWorkspaceEqual(
                  context, reuse.frequencyResults[index].workspace,
                  pressure->frequencyResults[index].workspace,
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
              curvatureMode, widthMode,
              CervenyCoordinateSystem::RayCentered);
          const BroadbandNonReuseResult nonReuse =
              BroadbandNonReuseSolver::solve(simulation, 1.0, 50.0);
          const SerialRayReuseResult reuse =
              SerialRayReuseSolver::solve(simulation, 1.0, 50.0, {}, true);
          const StreamedParallelRun parallel =
              runParallel(simulation,
                          ParallelRayReuseSettings{.workerCount = 2U,
                                                   .outputQueueCapacity = 1U,
                                                   .memoryBudgetBytes = 0U},
                          true);
          context.check(
              reuse.statistics.cacheFingerprintVerified &&
                  reuse.statistics.cacheFingerprintBefore ==
                      reuse.statistics.cacheFingerprintAfter &&
                  parallel.statistics.cacheFingerprintVerified &&
                  parallel.statistics.cacheFingerprintBefore ==
                      parallel.statistics.cacheFingerprintAfter,
              "ray-centered C/I/S x F/M/W x D/S/Z x P/V/H preserves "
              "the frozen cache");
          for (std::size_t index = 0U; index < 2U; ++index) {
            context.check(
                parallel.workspaces[index].has_value(),
                "parallel ray-centered matrix returns every frequency");
            if (!parallel.workspaces[index].has_value()) {
              continue;
            }
            checkWorkspaceEqual(
                context, reuse.frequencyResults[index].workspace,
                nonReuse.frequencyResults[index].workspace,
                "ray-centered serial reuse equals non-reuse bitwise");
            checkWorkspaceEqual(
                context, *parallel.workspaces[index],
                nonReuse.frequencyResults[index].workspace,
                "ray-centered parallel reuse equals non-reuse bitwise");
          }
        }
      }
    }
  }
}

void testMemoryBudget(Context& context) {
  const SimulationCase simulation = makeSimulation(makeFrequencies(16U));
  const StreamedParallelRun unrestricted = runParallel(
      simulation, ParallelRayReuseSettings{.workerCount = 4U,
                                           .outputQueueCapacity = 1U,
                                           .memoryBudgetBytes = 0U});
  const std::size_t cacheBytes = unrestricted.statistics.rayCacheBytes;
  const std::size_t workspaceBytes =
      unrestricted.statistics.estimatedWorkspaceBytes;
  const std::size_t twoWorkerBudget = cacheBytes + 4U * workspaceBytes;

  const StreamedParallelRun constrained = runParallel(
      simulation,
      ParallelRayReuseSettings{.workerCount = 4U,
                               .outputQueueCapacity = 1U,
                               .memoryBudgetBytes = twoWorkerBudget});
  context.check(
      constrained.statistics.activeFrequencyLimit == 2U &&
          constrained.statistics.estimatedPeakMemoryBytes <= twoWorkerBudget,
      "memory budget lowers the active frequency limit");

  context.expectThrows<ValidationError>(
      [&]() {
        static_cast<void>(runParallel(
            simulation,
            ParallelRayReuseSettings{
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
        static_cast<void>(ParallelRayReuseSolver::solveStreaming(
            simulation, 1.0, 50.0,
            [](std::size_t, rayreuse::FrequencyWorkspace&&,
               const rayreuse::SingleFrequencyTimings&) {},
            ParallelRayReuseSettings{.workerCount = 0U,
                                     .outputQueueCapacity = 1U,
                                     .memoryBudgetBytes = 0U}));
      },
      "parallel solver rejects zero workers");
  context.expectThrows<ValidationError>(
      [&]() {
        static_cast<void>(ParallelRayReuseSolver::solveStreaming(
            simulation, 1.0, 50.0,
            [](std::size_t, rayreuse::FrequencyWorkspace&&,
               const rayreuse::SingleFrequencyTimings&) {},
            ParallelRayReuseSettings{.workerCount = 1U,
                                     .outputQueueCapacity = 0U,
                                     .memoryBudgetBytes = 0U}));
      },
      "parallel solver rejects an empty output queue");
  context.expectThrows<ValidationError>(
      [&]() {
        static_cast<void>(ParallelRayReuseSolver::solveStreaming(
            simulation, 1.0, 50.0,
            [](std::size_t, rayreuse::FrequencyWorkspace&&,
               const rayreuse::SingleFrequencyTimings&) {},
            ParallelRayReuseSettings{.workerCount = 1U,
                                     .outputQueueCapacity = 3U,
                                     .memoryBudgetBytes = 0U}));
      },
      "parallel solver rejects output queue capacity above two");
  context.expectThrows<std::runtime_error>(
      [&]() {
        static_cast<void>(ParallelRayReuseSolver::solveStreaming(
            simulation, 1.0, 50.0,
            [](std::size_t, rayreuse::FrequencyWorkspace&&,
               const rayreuse::SingleFrequencyTimings&) {
              throw std::runtime_error("consumer failure");
            },
            ParallelRayReuseSettings{.workerCount = 2U,
                                     .outputQueueCapacity = 1U,
                                     .memoryBudgetBytes = 0U}));
      },
      "parallel solver stops workers and propagates "
      "consumer failures");
}

}  // namespace

int main() {
  Context context;
  testFrequencyCounts(context);
  testRepeatedRunIsDeterministic(context);
  testCoherenceModesMatchAcrossExecution(context);
  testSimpleGaussianMatchesAcrossExecution(context);
  testCartesianComponentsMatchAcrossExecution(context);
  testRayCenteredMatrixMatchesAcrossExecution(context);
  testMemoryBudget(context);
  testInvalidSettingsAndConsumerFailure(context);

  if (context.failureCount() != 0) {
    std::cerr << context.failureCount()
              << " parallel-ray-reuse-solver assertion(s) failed\n";
    return 1;
  }
  std::cout << "All Bellhop RayReuse parallel-ray-reuse-solver tests passed\n";
  return 0;
}
