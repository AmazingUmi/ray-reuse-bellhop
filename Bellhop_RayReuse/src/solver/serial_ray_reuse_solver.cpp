#include "rayreuse/solver/serial_ray_reuse_solver.hpp"

#include <chrono>
#include <utility>

#include "rayreuse/error.hpp"

namespace rayreuse {
namespace {

using Clock = std::chrono::steady_clock;

[[nodiscard]] double elapsedSeconds(Clock::time_point begin,
                                    Clock::time_point end) {
  return std::chrono::duration<double>(end - begin).count();
}

void accumulateProjectionTimings(SingleFrequencyTimings& total,
                                 const SingleFrequencyTimings& value) {
  total.projectSeconds += value.projectSeconds;
  total.influenceSeconds += value.influenceSeconds;
  total.scaleSeconds += value.scaleSeconds;
  accumulateCartesianCervenyStatistics(total.influenceStatistics,
                                       value.influenceStatistics);
}

}  // namespace

SerialRayReuseStatistics SerialRayReuseSolver::solveStreaming(
    const SimulationCase& simulation, double epsilonMultiplier,
    double loopRange, const RayReuseFrequencyConsumer& consumer,
    CartesianCervenySettings influenceSettings, bool verifyCacheFingerprint) {
  if (!consumer) {
    throw ValidationError(
        "serial ray-reuse frequency consumer must be callable");
  }

  SerialRayReuseStatistics statistics;

  const Clock::time_point wallBegin = Clock::now();
  // One frozen cache per source (Worklist FP-2F §1.2): the reuse unit is
  // "(source, frozen fan)", reused across every frequency. The cache vector
  // is owned by this orchestration layer and only handed out as const.
  const std::vector<RayFanTraceResult> sourceTraces =
      SingleFrequencySolver::traceAllSourceFans(simulation);

  statistics.tracePassCount = sourceTraces.size();
  for (const RayFanTraceResult& trace : sourceTraces) {
    statistics.rayCount += trace.cache.size();
    statistics.totalRayPointCount += trace.totalRayPointCount;
    statistics.rayCacheBytes += trace.cache.memoryFootprintBytes();
    statistics.phaseTotals.traceSeconds += trace.traceSeconds;
  }
  statistics.cacheFingerprintVerified = verifyCacheFingerprint;
  if (verifyCacheFingerprint) {
    statistics.sourceCacheFingerprintsBefore.reserve(sourceTraces.size());
    for (const RayFanTraceResult& trace : sourceTraces) {
      statistics.sourceCacheFingerprintsBefore.push_back(
          trace.cache.contentFingerprint());
    }
    statistics.cacheFingerprintBefore =
        statistics.sourceCacheFingerprintsBefore.front();
  }

  const std::vector<double>& frequencies = simulation.frequencies().values();
  for (std::size_t frequencyIndex = 0U; frequencyIndex < frequencies.size();
       ++frequencyIndex) {
    std::vector<FrequencyWorkspace> sourceWorkspaces;
    sourceWorkspaces.reserve(sourceTraces.size());
    SingleFrequencyTimings frequencyTimings;
    for (std::size_t sourceIndex = 0U; sourceIndex < sourceTraces.size();
         ++sourceIndex) {
      SingleFrequencyResult sourceResult =
          SingleFrequencySolver::solveFrequencyFromSourceCache(
              simulation, frequencies[frequencyIndex],
              sourceTraces[sourceIndex].cache, sourceIndex, epsilonMultiplier,
              loopRange, influenceSettings);
      accumulateProjectionTimings(frequencyTimings, sourceResult.timings);
      sourceWorkspaces.push_back(std::move(sourceResult.workspace));
    }
    accumulateProjectionTimings(statistics.phaseTotals, frequencyTimings);
    consumer(frequencyIndex, std::move(sourceWorkspaces), frequencyTimings);
  }

  if (verifyCacheFingerprint) {
    statistics.sourceCacheFingerprintsAfter.reserve(sourceTraces.size());
    for (const RayFanTraceResult& trace : sourceTraces) {
      statistics.sourceCacheFingerprintsAfter.push_back(
          trace.cache.contentFingerprint());
    }
    statistics.cacheFingerprintAfter =
        statistics.sourceCacheFingerprintsAfter.front();
    if (statistics.sourceCacheFingerprintsAfter !=
        statistics.sourceCacheFingerprintsBefore) {
      throw ValidationError("serial ray-reuse modified the frozen ray cache");
    }
  }
  statistics.wallSeconds = elapsedSeconds(wallBegin, Clock::now());
  return statistics;
}

SerialRayReuseResult SerialRayReuseSolver::solve(
    const SimulationCase& simulation, double epsilonMultiplier,
    double loopRange, CartesianCervenySettings influenceSettings,
    bool verifyCacheFingerprint) {
  SerialRayReuseResult result;
  result.frequencyResults.reserve(simulation.frequencies().size());

  result.statistics = solveStreaming(
      simulation, epsilonMultiplier, loopRange,
      [&result](std::size_t, std::vector<FrequencyWorkspace>&& workspaces,
                const SingleFrequencyTimings& timings) {
        result.frequencyResults.push_back(SerialRayReuseFrequencyResult{
            .workspaces = std::move(workspaces), .timings = timings});
      },
      influenceSettings, verifyCacheFingerprint);
  return result;
}

}  // namespace rayreuse
