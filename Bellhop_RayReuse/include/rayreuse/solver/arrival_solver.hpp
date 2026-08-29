#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "rayreuse/cache/ray_path_cache.hpp"
#include "rayreuse/field/arrival_workspace.hpp"
#include "rayreuse/model/simulation_case.hpp"

namespace rayreuse {

struct ArrivalSolverStatistics {
  std::size_t frequencyCount{};
  std::size_t rayCount{};
  std::size_t projectedRayCount{};
  std::size_t totalRayPointCount{};
  std::size_t candidateCount{};
  std::size_t saturatedCellCount{};
  // Peak single-source frozen cache bytes (F2CPP reports the max over
  // sources).
  std::size_t peakRayCacheBytes{};
  std::size_t peakArrivalWorkspaceBytes{};
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

// Receives one frequency's per-source product state. Both vectors are indexed
// by SimulationCase::sources() order (depth ascending) and sized
// sourceCount(); the caches stay frozen and the workspaces are
// frequency-local.
using FrozenFrequencyArrivalConsumer = std::function<void(
    std::size_t frequencyIndex, const std::vector<RayPathCache>& caches,
    const std::vector<ArrivalWorkspace>& workspaces)>;

class ArrivalSolver {
 public:
  [[nodiscard]] static ArrivalSolverStatistics solve(
      const SimulationCase& simulation,
      const FrozenFrequencyArrivalConsumer& consumer, bool verifyCache = false);
  [[nodiscard]] static ArrivalSolverStatistics solveNonReuse(
      const SimulationCase& simulation,
      const FrozenFrequencyArrivalConsumer& consumer, bool verifyCache = false);
  [[nodiscard]] static ArrivalSolverStatistics solveParallel(
      const SimulationCase& simulation,
      const FrozenFrequencyArrivalConsumer& consumer, std::size_t workerCount,
      bool verifyCache = false);
};

}  // namespace rayreuse
