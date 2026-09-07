#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "broadband/field/cartesian_cerveny_influence.hpp"
#include "broadband/field/frequency_workspace.hpp"
#include "broadband/model/simulation_case.hpp"
#include "broadband/solver/ray_reuse_frequency_consumer.hpp"
#include "broadband/solver/single_frequency_solver.hpp"

namespace broadband {

struct ReuseSerialFrequencyResult {
  // Per-source workspace sequence for one frequency, indexed by
  // SimulationCase::sources() order (depth ascending); size == sourceCount().
  std::vector<FrequencyWorkspace> workspaces;
  SingleFrequencyTimings timings;
};

struct ReuseSerialStatistics {
  // Frozen semantics (Worklist FP-2F §1.5): per-source fan trace count
  // (NSz for reuse; NSz == 1 keeps the legacy value 1).
  std::size_t tracePassCount{};
  std::size_t rayCount{};
  std::size_t totalRayPointCount{};
  // Sum of the per-source frozen cache bytes.
  std::size_t rayCacheBytes{};
  SingleFrequencyTimings phaseTotals;
  double wallSeconds{};
  bool cacheFingerprintVerified{};
  // First-source fingerprints (identical to the per-source vectors at
  // index 0); retained so single-source PRT/statistics output is unchanged.
  std::uint64_t cacheFingerprintBefore{};
  std::uint64_t cacheFingerprintAfter{};
  // Per-source fingerprints, one entry per SimulationCase::sources() entry.
  std::vector<std::uint64_t> sourceCacheFingerprintsBefore;
  std::vector<std::uint64_t> sourceCacheFingerprintsAfter;
  std::size_t requestedTraceWorkerCount{1U};
  std::size_t effectiveTraceWorkerCount{1U};
  std::vector<std::vector<double>> traceWorkerSecondsBySource;
};

struct ReuseSerialResult {
  // Compatibility collection API. Input frequency order is preserved.
  // New callers can use solveStreaming to keep only one frequency's
  // per-source workspace sequence resident.
  std::vector<ReuseSerialFrequencyResult> frequencyResults;
  ReuseSerialStatistics statistics;
};

class ReuseSerialSolver {
 public:
  [[nodiscard]] static ReuseSerialStatistics solveStreaming(
      const SimulationCase& simulation, double epsilonMultiplier,
      double loopRange, const RayReuseFrequencyConsumer& consumer,
      CartesianCervenySettings influenceSettings = {},
      bool verifyCacheFingerprint = false,
      RayFanTraceSettings traceSettings = {});

  [[nodiscard]] static ReuseSerialResult solve(
      const SimulationCase& simulation, double epsilonMultiplier,
      double loopRange, CartesianCervenySettings influenceSettings = {},
      bool verifyCacheFingerprint = false,
      RayFanTraceSettings traceSettings = {});
};

}  // namespace broadband
