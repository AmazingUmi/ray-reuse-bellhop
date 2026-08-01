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
  RayFanTraceResult trace = SingleFrequencySolver::traceRayFan(simulation);

  statistics.tracePassCount = 1U;
  statistics.rayCount = trace.cache.size();
  statistics.totalRayPointCount = trace.totalRayPointCount;
  statistics.rayCacheBytes = trace.cache.memoryFootprintBytes();
  statistics.phaseTotals.traceSeconds = trace.traceSeconds;
  statistics.cacheFingerprintVerified = verifyCacheFingerprint;
  if (verifyCacheFingerprint) {
    statistics.cacheFingerprintBefore = trace.cache.contentFingerprint();
  }

  const std::vector<double>& frequencies = simulation.frequencies().values();
  for (std::size_t frequencyIndex = 0U; frequencyIndex < frequencies.size();
       ++frequencyIndex) {
    SingleFrequencyResult frequencyResult =
        SingleFrequencySolver::solveFrequencyFromCache(
            simulation, frequencies[frequencyIndex], trace.cache,
            epsilonMultiplier, loopRange, influenceSettings);
    accumulateProjectionTimings(statistics.phaseTotals,
                                frequencyResult.timings);
    consumer(frequencyIndex, std::move(frequencyResult.workspace),
             frequencyResult.timings);
  }

  if (verifyCacheFingerprint) {
    statistics.cacheFingerprintAfter = trace.cache.contentFingerprint();
    if (statistics.cacheFingerprintAfter != statistics.cacheFingerprintBefore) {
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
      [&result](std::size_t, FrequencyWorkspace&& workspace,
                const SingleFrequencyTimings& timings) {
        result.frequencyResults.push_back(SerialRayReuseFrequencyResult{
            .workspace = std::move(workspace), .timings = timings});
      },
      influenceSettings, verifyCacheFingerprint);
  return result;
}

}  // namespace rayreuse
