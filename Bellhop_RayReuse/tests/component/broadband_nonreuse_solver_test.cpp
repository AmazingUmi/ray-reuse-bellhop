#include "rayreuse/solver/broadband_nonreuse_solver.hpp"

#include <algorithm>
#include <complex>
#include <cstddef>
#include <iostream>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

#include "rayreuse/model/simulation_case.hpp"
#include "rayreuse/solver/single_frequency_solver.hpp"
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
using rayreuse::SimulationCase;
using rayreuse::SingleFrequencyResult;
using rayreuse::SingleFrequencySolver;
using rayreuse::SoundSpeedPoint;
using rayreuse::SoundSpeedProfile;
using rayreuse::Source;
using rayreuse::test::Context;

SimulationCase makeSimulation() {
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
      FrequencyGrid({50.0, 100.0}),
      LaunchFan{.minimumAngle = -2.0 * std::numbers::pi / 180.0,
                .maximumAngle = 2.0 * std::numbers::pi / 180.0,
                .explicitLaunchAngleCount = 300U},
      IntegratorSettings{.stepLength = 10.0,
                         .rangeLimit = 110.0,
                         .depthLimit = 110.0,
                         .maximumRayPoints = 100U});
}

void checkPressureEqual(Context& context, const SingleFrequencyResult& actual,
                        const SingleFrequencyResult& expected,
                        const std::string& message) {
  context.check(
      actual.workspace.frequency() == expected.workspace.frequency() &&
          actual.workspace.depthCount() == expected.workspace.depthCount() &&
          actual.workspace.rangeCount() == expected.workspace.rangeCount() &&
          std::equal(actual.workspace.pressure().begin(),
                     actual.workspace.pressure().end(),
                     expected.workspace.pressure().begin(),
                     expected.workspace.pressure().end()),
      message);
}

void testTwoFrequencyNonReuseSolve(Context& context) {
  const SimulationCase simulation = makeSimulation();
  const BroadbandNonReuseResult broadband =
      BroadbandNonReuseSolver::solve(simulation, 1.0, 50.0);

  context.check(
      broadband.frequencyResults.size() == 2U &&
          broadband.frequencyResults[0].workspace.frequency() == 50.0 &&
          broadband.frequencyResults[1].workspace.frequency() == 100.0,
      "broadband non-reuse preserves the input frequency order");

  std::size_t expectedTotalRayCount = 0U;
  std::size_t expectedTotalRayPointCount = 0U;
  std::size_t expectedCumulativeCacheBytes = 0U;
  rayreuse::SingleFrequencyTimings expectedPhaseTotals;
  for (const SingleFrequencyResult& result : broadband.frequencyResults) {
    context.check(result.workspace.depthCount() == 3U &&
                      result.workspace.rangeCount() == 3U,
                  "each frequency owns a complete receiver-grid workspace");
    context.check(
        result.rayCount == simulation.launchFanPlan().launchAngleCount &&
            result.totalRayPointCount > result.rayCount &&
            result.rayCacheBytes > 0U,
        "each frequency performs and records a complete trace pass");
    expectedTotalRayCount += result.rayCount;
    expectedTotalRayPointCount += result.totalRayPointCount;
    expectedCumulativeCacheBytes += result.rayCacheBytes;
    expectedPhaseTotals.traceSeconds += result.timings.traceSeconds;
    expectedPhaseTotals.projectSeconds += result.timings.projectSeconds;
    expectedPhaseTotals.influenceSeconds += result.timings.influenceSeconds;
    expectedPhaseTotals.scaleSeconds += result.timings.scaleSeconds;
  }

  context.check(
      broadband.statistics.tracePassCount == 2U &&
          broadband.statistics.totalRayCount == expectedTotalRayCount &&
          broadband.statistics.totalRayPointCount ==
              expectedTotalRayPointCount &&
          broadband.statistics.cumulativeRayCacheBytes ==
              expectedCumulativeCacheBytes &&
          broadband.statistics.peakRayCacheBytes > 0U &&
          broadband.statistics.phaseTotals.traceSeconds ==
              expectedPhaseTotals.traceSeconds &&
          broadband.statistics.phaseTotals.projectSeconds ==
              expectedPhaseTotals.projectSeconds &&
          broadband.statistics.phaseTotals.influenceSeconds ==
              expectedPhaseTotals.influenceSeconds &&
          broadband.statistics.phaseTotals.scaleSeconds ==
              expectedPhaseTotals.scaleSeconds &&
          broadband.statistics.wallSeconds >= 0.0,
      "broadband non-reuse aggregates per-frequency trace statistics");

  const SingleFrequencyResult first =
      SingleFrequencySolver::solveAtFrequency(simulation, 50.0, 1.0, 50.0);
  const SingleFrequencyResult second =
      SingleFrequencySolver::solveAtFrequency(simulation, 100.0, 1.0, 50.0);
  checkPressureEqual(
      context, broadband.frequencyResults[0], first,
      "first broadband slice equals an independent member-frequency solve");
  checkPressureEqual(
      context, broadband.frequencyResults[1], second,
      "second broadband slice equals an independent member-frequency solve");
}

}  // namespace

int main() {
  Context context;
  testTwoFrequencyNonReuseSolve(context);

  if (context.failureCount() != 0) {
    std::cerr << context.failureCount()
              << " broadband-nonreuse-solver assertion(s) failed\n";
    return 1;
  }
  std::cout << "All Bellhop RayReuse broadband-nonreuse-solver tests passed\n";
  return 0;
}
