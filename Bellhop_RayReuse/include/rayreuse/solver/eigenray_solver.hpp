#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

#include "rayreuse/cache/ray_path_cache.hpp"
#include "rayreuse/field/eigenray_hit.hpp"
#include "rayreuse/model/simulation_case.hpp"

namespace rayreuse {

struct EigenraySolverStatistics {
  std::size_t frequencyCount{};
  std::size_t rayCount{};
  std::size_t projectedRayCount{};
  std::size_t totalRayPointCount{};
  std::size_t totalHitCount{};
  std::size_t totalPrefixPointCount{};
  // Peak single-source frozen cache bytes (F2CPP reports the max over
  // sources).
  std::size_t peakRayCacheBytes{};
  double traceSeconds{};
  double projectSeconds{};
  double influenceSeconds{};
  double consumeSeconds{};
  bool cacheFingerprintVerified{};
  // First-source fingerprints (identical to the per-source vectors at
  // index 0); retained so single-source output is unchanged.
  std::uint64_t cacheFingerprintBefore{};
  std::uint64_t cacheFingerprintAfter{};
  // Per-source fingerprints, one entry per SimulationCase::sources() entry.
  std::vector<std::uint64_t> sourceCacheFingerprintsBefore;
  std::vector<std::uint64_t> sourceCacheFingerprintsAfter;
};

// Hits collected from one source's frozen fan; launch indices are local to
// that source's cache.
using EigenraySourceHits = std::vector<std::pair<std::size_t, EigenrayHit>>;

// Receives one frequency's per-source product state. Both vectors are indexed
// by SimulationCase::sources() order (depth ascending) and sized
// sourceCount(); the caches stay frozen and the hit sets are
// frequency-local.
using FrozenFrequencyEigenrayConsumer = std::function<void(
    std::size_t frequencyIndex, const std::vector<RayPathCache>& caches,
    const std::vector<EigenraySourceHits>& sourceHits)>;

class EigenraySolver {
 public:
  [[nodiscard]] static EigenraySolverStatistics solve(
      const SimulationCase& simulation,
      const FrozenFrequencyEigenrayConsumer& consumer,
      bool verifyCache = false);
  [[nodiscard]] static EigenraySolverStatistics solveNonReuse(
      const SimulationCase& simulation,
      const FrozenFrequencyEigenrayConsumer& consumer,
      bool verifyCache = false);
  [[nodiscard]] static EigenraySolverStatistics solveParallel(
      const SimulationCase& simulation,
      const FrozenFrequencyEigenrayConsumer& consumer, std::size_t workerCount,
      bool verifyCache = false);
};

}  // namespace rayreuse
