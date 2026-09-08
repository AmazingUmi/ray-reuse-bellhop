#include "broadband/solver/nonreuse_solver.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

namespace broadband {
namespace {

using Clock = std::chrono::steady_clock;

[[nodiscard]] double elapsedSeconds(Clock::time_point begin,
                                    Clock::time_point end) {
  return std::chrono::duration<double>(end - begin).count();
}

void accumulateTimings(SingleFrequencyTimings& total,
                       const SingleFrequencyTimings& value) {
  total.traceSeconds += value.traceSeconds;
  total.projectSeconds += value.projectSeconds;
  total.influenceSeconds += value.influenceSeconds;
  total.scaleSeconds += value.scaleSeconds;
  accumulateCartesianCervenyStatistics(total.influenceStatistics,
                                       value.influenceStatistics);
}

}  // namespace

NonReuseResult NonReuseSolver::solve(const SimulationCase& simulation,
                                     double epsilonMultiplier, double loopRange,
                                     CartesianCervenySettings influenceSettings,
                                     RayFanTraceSettings traceSettings) {
  NonReuseResult result;
  result.frequencyResults.reserve(simulation.frequencies().size());

  const Clock::time_point wallBegin = Clock::now();
  for (const double frequency : simulation.frequencies().values()) {
    SingleFrequencyResult frequencyResult =
        SingleFrequencySolver::solveAtFrequency(
            simulation, frequency, epsilonMultiplier, loopRange,
            influenceSettings, traceSettings);

    // solveAtFrequency traces every source's fan once (Worklist FP-2F §1.5:
    // non-reuse trace passes = Nfreq x NSz).
    result.statistics.tracePassCount += frequencyResult.sourceCount();
    result.statistics.totalRayCount += frequencyResult.rayCount;
    result.statistics.totalRayPointCount += frequencyResult.totalRayPointCount;
    result.statistics.cumulativeRayCacheBytes += frequencyResult.rayCacheBytes;
    result.statistics.peakRayCacheBytes = std::max(
        result.statistics.peakRayCacheBytes, frequencyResult.rayCacheBytes);
    accumulateTimings(result.statistics.phaseTotals, frequencyResult.timings);
    result.frequencyResults.push_back(std::move(frequencyResult));
  }
  result.statistics.wallSeconds = elapsedSeconds(wallBegin, Clock::now());

  return result;
}

}  // namespace broadband
