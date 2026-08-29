#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "rayreuse/field/cartesian_cerveny_influence.hpp"
#include "rayreuse/model/simulation_case.hpp"
#include "rayreuse/solver/ray_reuse_frequency_consumer.hpp"
#include "rayreuse/solver/single_frequency_solver.hpp"

namespace rayreuse {

struct ParallelRayReuseSettings {
  std::size_t workerCount{1U};
  std::size_t outputQueueCapacity{2U};
  // Zero disables the explicit memory-budget limit.
  std::size_t memoryBudgetBytes{};
};

struct ParallelRayReuseStatistics {
  // Frozen semantics (Worklist FP-2F §1.5): per-source fan trace count
  // (NSz for parallel reuse; NSz == 1 keeps the legacy value 1).
  std::size_t tracePassCount{};
  std::size_t rayCount{};
  std::size_t totalRayPointCount{};
  // Sum of the per-source frozen cache bytes.
  std::size_t rayCacheBytes{};
  std::size_t requestedWorkerCount{};
  std::size_t activeFrequencyLimit{};
  std::size_t outputQueueCapacity{};
  std::size_t peakQueuedResults{};
  // Bytes for one frequency's full per-source workspace sequence.
  std::size_t estimatedWorkspaceBytes{};
  // Cache plus active workers, queued results, and one consumer-held result,
  // capped by the total frequency count.
  std::size_t estimatedPeakMemoryBytes{};
  std::size_t memoryBudgetBytes{};
  SingleFrequencyTimings phaseTotals;
  std::vector<SingleFrequencyTimings> frequencyTimings;
  double wallSeconds{};
  bool cacheFingerprintVerified{};
  // First-source fingerprints (identical to the per-source vectors at
  // index 0); retained so single-source PRT/statistics output is unchanged.
  std::uint64_t cacheFingerprintBefore{};
  std::uint64_t cacheFingerprintAfter{};
  // Per-source fingerprints, one entry per SimulationCase::sources() entry.
  std::vector<std::uint64_t> sourceCacheFingerprintsBefore;
  std::vector<std::uint64_t> sourceCacheFingerprintsAfter;
};

class ParallelRayReuseSolver {
 public:
  [[nodiscard]] static ParallelRayReuseStatistics solveStreaming(
      const SimulationCase& simulation, double epsilonMultiplier,
      double loopRange, const RayReuseFrequencyConsumer& consumer,
      ParallelRayReuseSettings settings = {},
      CartesianCervenySettings influenceSettings = {},
      bool verifyCacheFingerprint = false);
};

}  // namespace rayreuse
