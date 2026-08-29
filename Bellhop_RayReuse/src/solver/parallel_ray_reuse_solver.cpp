#include "rayreuse/solver/parallel_ray_reuse_solver.hpp"

#include <algorithm>
#include <chrono>
#include <complex>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <exception>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include "rayreuse/error.hpp"

namespace rayreuse {
namespace {

using Clock = std::chrono::steady_clock;

struct CompletedFrequency {
  std::size_t index{};
  // Per-source workspace sequence for the frequency, indexed by
  // SimulationCase::sources() order.
  std::vector<FrequencyWorkspace> workspaces;
  SingleFrequencyTimings timings;
};

struct WorkState {
  std::mutex mutex;
  std::condition_variable resultAvailable;
  std::condition_variable queueSpaceAvailable;
  std::deque<CompletedFrequency> completed;
  std::size_t nextIndex{};
  std::size_t workersRemaining{};
  std::size_t peakQueuedResults{};
  bool stopping{};
  std::exception_ptr failure;
};

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

[[nodiscard]] std::size_t checkedMultiply(std::size_t left, std::size_t right,
                                          const char* message) {
  if (left != 0U && right > std::numeric_limits<std::size_t>::max() / left) {
    throw ValidationError(message);
  }
  return left * right;
}

[[nodiscard]] std::size_t checkedAdd(std::size_t left, std::size_t right,
                                     const char* message) {
  if (right > std::numeric_limits<std::size_t>::max() - left) {
    throw ValidationError(message);
  }
  return left + right;
}

[[nodiscard]] std::size_t workspaceBytes(const SimulationCase& simulation) {
  const std::size_t cellCount = checkedMultiply(
      simulation.receivers().receiversPerRange(),
      simulation.receivers().rangeCount(),
      "frequency workspace cell count overflows size_t");
  const std::size_t bytesPerCell =
      fieldAccumulationKind(simulation.runMode()) ==
              FieldAccumulationKind::Intensity
          ? sizeof(std::complex<double>) + sizeof(double)
          : sizeof(std::complex<double>);
  return checkedMultiply(cellCount, bytesPerCell,
                         "frequency workspace byte count overflows size_t");
}

[[nodiscard]] std::size_t estimatedPeakBytes(std::size_t rayCacheBytes,
                                             std::size_t workspaceByteCount,
                                             std::size_t frequencyCount,
                                             std::size_t activeFrequencyLimit,
                                             std::size_t queueCapacity) {
  const std::size_t simultaneousWorkspaceCount = std::min(
      frequencyCount,
      checkedAdd(checkedAdd(activeFrequencyLimit, queueCapacity,
                            "parallel workspace count overflows size_t"),
                 1U, "parallel consumer workspace count overflows size_t"));
  return checkedAdd(
      rayCacheBytes,
      checkedMultiply(simultaneousWorkspaceCount, workspaceByteCount,
                      "parallel workspace memory estimate overflows size_t"),
      "parallel peak memory estimate overflows size_t");
}

[[nodiscard]] std::size_t selectActiveFrequencyLimit(
    std::size_t frequencyCount, std::size_t requestedWorkerCount,
    std::size_t queueCapacity, std::size_t rayCacheBytes,
    std::size_t workspaceByteCount, std::size_t memoryBudgetBytes) {
  const std::size_t unconstrained =
      std::min(frequencyCount, requestedWorkerCount);
  if (memoryBudgetBytes == 0U) {
    return unconstrained;
  }

  for (std::size_t candidate = unconstrained; candidate > 0U; --candidate) {
    if (estimatedPeakBytes(rayCacheBytes, workspaceByteCount, frequencyCount,
                           candidate, queueCapacity) <= memoryBudgetBytes) {
      return candidate;
    }
  }
  throw ValidationError(
      "parallel ray-reuse memory budget cannot accommodate "
      "one active frequency");
}

void recordFailure(WorkState& state, std::exception_ptr failure) {
  {
    const std::lock_guard lock(state.mutex);
    if (!state.failure) {
      state.failure = std::move(failure);
    }
    state.stopping = true;
  }
  state.resultAvailable.notify_all();
  state.queueSpaceAvailable.notify_all();
}

}  // namespace

ParallelRayReuseStatistics ParallelRayReuseSolver::solveStreaming(
    const SimulationCase& simulation, double epsilonMultiplier,
    double loopRange, const RayReuseFrequencyConsumer& consumer,
    ParallelRayReuseSettings settings,
    CartesianCervenySettings influenceSettings, bool verifyCacheFingerprint) {
  if (!consumer) {
    throw ValidationError(
        "parallel ray-reuse frequency consumer must be callable");
  }
  if (settings.workerCount == 0U) {
    throw ValidationError("parallel ray-reuse worker count must be positive");
  }
  if (settings.outputQueueCapacity == 0U) {
    throw ValidationError(
        "parallel ray-reuse output queue capacity must be positive");
  }
  if (settings.outputQueueCapacity > 2U) {
    throw ValidationError(
        "parallel ray-reuse output queue capacity must be 1 or 2");
  }

  const Clock::time_point wallBegin = Clock::now();
  // One frozen cache per source (Worklist FP-2F §1.2), owned by this
  // orchestration layer; frequency workers only read the vector as const.
  const std::vector<RayFanTraceResult> sourceTraces =
      SingleFrequencySolver::traceAllSourceFans(simulation);
  const std::size_t sourceCount = sourceTraces.size();
  std::size_t totalRayCount = 0U;
  std::size_t totalRayPointCount = 0U;
  std::size_t totalCacheBytes = 0U;
  double traceSeconds = 0.0;
  for (const RayFanTraceResult& trace : sourceTraces) {
    totalRayCount += trace.cache.size();
    totalRayPointCount += trace.totalRayPointCount;
    totalCacheBytes += trace.cache.memoryFootprintBytes();
    traceSeconds += trace.traceSeconds;
  }
  const std::size_t frequencyCount = simulation.frequencies().size();
  const std::size_t effectiveQueueCapacity =
      std::min(settings.outputQueueCapacity, frequencyCount);
  // One frequency product now spans every source's workspace.
  const std::size_t frequencyWorkspaceBytes =
      checkedMultiply(workspaceBytes(simulation), sourceCount,
                      "parallel frequency workspace bytes overflows size_t");
  const std::size_t activeFrequencyLimit = selectActiveFrequencyLimit(
      frequencyCount, settings.workerCount, effectiveQueueCapacity,
      totalCacheBytes, frequencyWorkspaceBytes, settings.memoryBudgetBytes);

  ParallelRayReuseStatistics statistics;
  statistics.tracePassCount = sourceCount;
  statistics.rayCount = totalRayCount;
  statistics.totalRayPointCount = totalRayPointCount;
  statistics.rayCacheBytes = totalCacheBytes;
  statistics.requestedWorkerCount = settings.workerCount;
  statistics.activeFrequencyLimit = activeFrequencyLimit;
  statistics.outputQueueCapacity = effectiveQueueCapacity;
  statistics.estimatedWorkspaceBytes = frequencyWorkspaceBytes;
  statistics.estimatedPeakMemoryBytes =
      estimatedPeakBytes(totalCacheBytes, frequencyWorkspaceBytes,
                         frequencyCount, activeFrequencyLimit,
                         effectiveQueueCapacity);
  statistics.memoryBudgetBytes = settings.memoryBudgetBytes;
  statistics.phaseTotals.traceSeconds = traceSeconds;
  statistics.frequencyTimings.resize(frequencyCount);
  statistics.cacheFingerprintVerified = verifyCacheFingerprint;
  if (verifyCacheFingerprint) {
    statistics.sourceCacheFingerprintsBefore.reserve(sourceCount);
    for (const RayFanTraceResult& trace : sourceTraces) {
      statistics.sourceCacheFingerprintsBefore.push_back(
          trace.cache.contentFingerprint());
    }
    statistics.cacheFingerprintBefore =
        statistics.sourceCacheFingerprintsBefore.front();
  }

  WorkState state;
  state.workersRemaining = activeFrequencyLimit;
  std::vector<std::jthread> workers;
  workers.reserve(activeFrequencyLimit);

  const auto worker = [&]() {
    try {
      while (true) {
        std::size_t frequencyIndex{};
        {
          const std::lock_guard lock(state.mutex);
          if (state.stopping || state.nextIndex >= frequencyCount) {
            break;
          }
          frequencyIndex = state.nextIndex;
          ++state.nextIndex;
        }

        // Workers hold the shared per-source cache vector only as a const
        // reference (Worklist FP-2F §1.3); per-source workspace state is
        // frequency-local to this worker's result.
        std::vector<FrequencyWorkspace> sourceWorkspaces;
        sourceWorkspaces.reserve(sourceCount);
        SingleFrequencyTimings frequencyTimings;
        for (std::size_t sourceIndex = 0U; sourceIndex < sourceCount;
             ++sourceIndex) {
          SingleFrequencyResult sourceResult =
              SingleFrequencySolver::solveFrequencyFromSourceCache(
                  simulation,
                  simulation.frequencies().values()[frequencyIndex],
                  sourceTraces[sourceIndex].cache, sourceIndex,
                  epsilonMultiplier, loopRange, influenceSettings);
          accumulateProjectionTimings(frequencyTimings, sourceResult.timings);
          sourceWorkspaces.push_back(std::move(sourceResult.workspace));
        }

        {
          std::unique_lock lock(state.mutex);
          state.queueSpaceAvailable.wait(lock, [&]() {
            return state.stopping ||
                   state.completed.size() < effectiveQueueCapacity;
          });
          if (state.stopping) {
            break;
          }
          state.completed.push_back(CompletedFrequency{
              .index = frequencyIndex,
              .workspaces = std::move(sourceWorkspaces),
              .timings = frequencyTimings});
          state.peakQueuedResults =
              std::max(state.peakQueuedResults, state.completed.size());
        }
        state.resultAvailable.notify_one();
      }
    } catch (...) {
      recordFailure(state, std::current_exception());
    }

    {
      const std::lock_guard lock(state.mutex);
      --state.workersRemaining;
    }
    state.resultAvailable.notify_all();
  };

  try {
    for (std::size_t workerIndex = 0U; workerIndex < activeFrequencyLimit;
         ++workerIndex) {
      workers.emplace_back(worker);
    }
  } catch (...) {
    recordFailure(state, std::current_exception());
  }

  std::size_t consumedCount{};
  while (consumedCount < frequencyCount) {
    std::exception_ptr failure;
    {
      std::unique_lock lock(state.mutex);
      state.resultAvailable.wait(lock, [&]() {
        return state.failure || !state.completed.empty() ||
               state.workersRemaining == 0U;
      });
      failure = state.failure;
    }
    if (failure) {
      break;
    }

    CompletedFrequency completed = [&]() {
      std::lock_guard lock(state.mutex);
      if (state.completed.empty()) {
        throw ValidationError(
            "parallel ray-reuse workers stopped before "
            "all frequencies completed");
      }
      CompletedFrequency value = std::move(state.completed.front());
      state.completed.pop_front();
      return value;
    }();
    state.queueSpaceAvailable.notify_one();

    try {
      statistics.frequencyTimings[completed.index] = completed.timings;
      accumulateProjectionTimings(statistics.phaseTotals, completed.timings);
      consumer(completed.index, std::move(completed.workspaces),
               completed.timings);
      ++consumedCount;
    } catch (...) {
      recordFailure(state, std::current_exception());
      break;
    }
  }

  {
    const std::lock_guard lock(state.mutex);
    state.stopping = true;
  }
  state.resultAvailable.notify_all();
  state.queueSpaceAvailable.notify_all();
  workers.clear();

  if (state.failure) {
    std::rethrow_exception(state.failure);
  }

  if (verifyCacheFingerprint) {
    statistics.sourceCacheFingerprintsAfter.reserve(sourceCount);
    for (const RayFanTraceResult& trace : sourceTraces) {
      statistics.sourceCacheFingerprintsAfter.push_back(
          trace.cache.contentFingerprint());
    }
    statistics.cacheFingerprintAfter =
        statistics.sourceCacheFingerprintsAfter.front();
    if (statistics.sourceCacheFingerprintsAfter !=
        statistics.sourceCacheFingerprintsBefore) {
      throw ValidationError("parallel ray-reuse modified the frozen ray cache");
    }
  }
  statistics.peakQueuedResults = state.peakQueuedResults;
  statistics.wallSeconds = elapsedSeconds(wallBegin, Clock::now());
  return statistics;
}

}  // namespace rayreuse
