#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

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
  std::size_t peakRayCacheBytes{};
  std::size_t peakArrivalWorkspaceBytes{};
  double traceSeconds{};
  double projectSeconds{};
  double influenceSeconds{};
  double consumeSeconds{};
  bool cacheFingerprintVerified{};
  std::uint64_t cacheFingerprintBefore{};
  std::uint64_t cacheFingerprintAfter{};
};

using FrozenFrequencyArrivalConsumer = std::function<void(
    std::size_t frequencyIndex, const RayPathCache&, const ArrivalWorkspace&)>;

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
