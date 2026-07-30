#include <algorithm>
#include <complex>
#include <cstddef>
#include <iostream>
#include <numbers>
#include <optional>
#include <utility>
#include <vector>

#include "rayreuse/model/simulation_case.hpp"
#include "rayreuse/solver/broadband_nonreuse_solver.hpp"
#include "rayreuse/solver/serial_ray_reuse_solver.hpp"
#include "support/test_harness.hpp"

namespace {

using rayreuse::BoundaryModel;
using rayreuse::BroadbandNonReuseResult;
using rayreuse::BroadbandNonReuseSolver;
using rayreuse::Environment;
using rayreuse::FrequencyGrid;
using rayreuse::IntegratorSettings;
using rayreuse::LaunchFan;
using rayreuse::ReceiverGrid;
using rayreuse::SerialRayReuseFrequencyResult;
using rayreuse::SerialRayReuseResult;
using rayreuse::SerialRayReuseSolver;
using rayreuse::SimulationCase;
using rayreuse::SoundSpeedPoint;
using rayreuse::SoundSpeedProfile;
using rayreuse::Source;
using rayreuse::test::Context;

SimulationCase makeSimulation() {
  return SimulationCase(
      Environment(
          SoundSpeedProfile(
              {SoundSpeedPoint{
                   .depth = 0.0,
                   .soundSpeed = 1500.0,
                   .density = 1000.0},
               SoundSpeedPoint{
                   .depth = 100.0,
                   .soundSpeed = 1500.0,
                   .density = 1000.0}}),
          BoundaryModel::vacuum(0.0),
          BoundaryModel::rigid(100.0)),
      Source{.depth = 50.0, .amplitude = 1.0},
      ReceiverGrid(
          {25.0, 50.0, 75.0}, {10.0, 55.0, 100.0}),
      FrequencyGrid({50.0, 100.0}),
      LaunchFan{
          .minimumAngle =
              -2.0 * std::numbers::pi / 180.0,
          .maximumAngle =
              2.0 * std::numbers::pi / 180.0,
          .explicitLaunchAngleCount = 300U},
      IntegratorSettings{
          .stepLength = 10.0,
          .rangeLimit = 110.0,
          .depthLimit = 110.0,
          .maximumRayPoints = 100U});
}

void checkPressureEqual(
    Context& context,
    const SerialRayReuseFrequencyResult& actual,
    const rayreuse::SingleFrequencyResult& expected,
    const char* message) {
  context.check(
      actual.workspace.frequency() ==
              expected.workspace.frequency() &&
          actual.workspace.depthCount() ==
              expected.workspace.depthCount() &&
          actual.workspace.rangeCount() ==
              expected.workspace.rangeCount() &&
          std::equal(
              actual.workspace.pressure().begin(),
              actual.workspace.pressure().end(),
              expected.workspace.pressure().begin(),
              expected.workspace.pressure().end()),
      message);
}

void testTwoFrequencySerialReuse(Context& context) {
  const SimulationCase simulation = makeSimulation();
  const BroadbandNonReuseResult nonReuse =
      BroadbandNonReuseSolver::solve(
          simulation, 1.0, 50.0);
  const SerialRayReuseResult reuse =
      SerialRayReuseSolver::solve(
          simulation, 1.0, 50.0, {}, true);

  context.check(
      reuse.frequencyResults.size() == 2U &&
          reuse.frequencyResults[0].workspace.frequency() ==
              50.0 &&
          reuse.frequencyResults[1].workspace.frequency() ==
              100.0,
      "serial reuse preserves input frequency order");
  checkPressureEqual(
      context, reuse.frequencyResults[0],
      nonReuse.frequencyResults[0],
      "first reused frequency is bitwise equal to non-reuse");
  checkPressureEqual(
      context, reuse.frequencyResults[1],
      nonReuse.frequencyResults[1],
      "second reused frequency is bitwise equal to non-reuse");

  context.check(
      reuse.statistics.tracePassCount == 1U &&
          nonReuse.statistics.tracePassCount == 2U,
      "serial reuse traces once while two-frequency non-reuse traces twice");
  context.check(
      reuse.statistics.rayCount ==
              simulation.launchFanPlan().launchAngleCount &&
          reuse.statistics.totalRayPointCount >
              reuse.statistics.rayCount &&
          reuse.statistics.rayCacheBytes > 0U,
      "serial reuse reports the shared ray-cache metrics");
  context.check(
          reuse.statistics.cacheFingerprintVerified &&
          reuse.statistics.cacheFingerprintBefore ==
              reuse.statistics.cacheFingerprintAfter,
      "serial frequency projection leaves the frozen cache unchanged");

  const auto& firstTimings =
      reuse.frequencyResults[0].timings;
  const auto& secondTimings =
      reuse.frequencyResults[1].timings;
  context.check(
      firstTimings.traceSeconds == 0.0 &&
          secondTimings.traceSeconds == 0.0 &&
          firstTimings.projectSeconds >= 0.0 &&
          firstTimings.influenceSeconds >= 0.0 &&
          firstTimings.scaleSeconds >= 0.0 &&
          secondTimings.projectSeconds >= 0.0 &&
          secondTimings.influenceSeconds >= 0.0 &&
          secondTimings.scaleSeconds >= 0.0 &&
          reuse.statistics.phaseTotals.traceSeconds >= 0.0 &&
          reuse.statistics.wallSeconds >= 0.0,
      "serial reuse exposes one trace timing and per-frequency timings");
}

void testStreamingSerialReuse(Context& context) {
  const SimulationCase simulation = makeSimulation();
  const SerialRayReuseResult collected =
      SerialRayReuseSolver::solve(
          simulation, 1.0, 50.0);
  std::vector<std::optional<rayreuse::FrequencyWorkspace>>
      streamed(simulation.frequencies().size());
  std::vector<std::size_t> callbackCounts(
      simulation.frequencies().size(), 0U);
  std::vector<std::size_t> callbackOrder;

  const rayreuse::SerialRayReuseStatistics statistics =
      SerialRayReuseSolver::solveStreaming(
          simulation, 1.0, 50.0,
          [&](std::size_t frequencyIndex,
              rayreuse::FrequencyWorkspace&& workspace,
              const rayreuse::SingleFrequencyTimings&) {
            ++callbackCounts.at(frequencyIndex);
            callbackOrder.push_back(frequencyIndex);
            streamed.at(frequencyIndex).emplace(
                std::move(workspace));
          });

  context.check(
      callbackOrder == std::vector<std::size_t>{0U, 1U},
      "serial streaming callback preserves frequency order");
  context.check(
      callbackCounts == std::vector<std::size_t>{1U, 1U},
      "serial streaming callback consumes every frequency once");
  context.check(
      statistics.tracePassCount == 1U,
      "serial streaming traces the ray fan once");

  for (std::size_t index = 0U;
       index < streamed.size(); ++index) {
    context.check(
        streamed[index].has_value() &&
            std::equal(
                streamed[index]->pressure().begin(),
                streamed[index]->pressure().end(),
                collected.frequencyResults[index]
                    .workspace.pressure().begin(),
                collected.frequencyResults[index]
                    .workspace.pressure().end()),
        "serial streamed workspace is bitwise equal to "
        "the collected result");
  }
}

}  // namespace

int main() {
  Context context;
  testTwoFrequencySerialReuse(context);
  testStreamingSerialReuse(context);

  if (context.failureCount() != 0) {
    std::cerr << context.failureCount()
              << " serial-ray-reuse-solver assertion(s) failed\n";
    return 1;
  }
  std::cout
      << "All Bellhop RayReuse serial-ray-reuse-solver tests passed\n";
  return 0;
}
