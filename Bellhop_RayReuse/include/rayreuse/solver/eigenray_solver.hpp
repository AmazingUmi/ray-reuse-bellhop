#pragma once

#include <cstddef>
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
  std::size_t peakRayCacheBytes{};
  double traceSeconds{};
  double projectSeconds{};
  double influenceSeconds{};
  double consumeSeconds{};
};

using FrozenFrequencyEigenrayConsumer = std::function<void(
    std::size_t frequencyIndex, const RayPathCache&,
    const std::vector<std::pair<std::size_t, EigenrayHit>>&)>;

class EigenraySolver {
 public:
  [[nodiscard]] static EigenraySolverStatistics solve(
      const SimulationCase& simulation,
      const FrozenFrequencyEigenrayConsumer& consumer);
  [[nodiscard]] static EigenraySolverStatistics solveNonReuse(
      const SimulationCase& simulation,
      const FrozenFrequencyEigenrayConsumer& consumer);
  [[nodiscard]] static EigenraySolverStatistics solveParallel(
      const SimulationCase& simulation,
      const FrozenFrequencyEigenrayConsumer& consumer, std::size_t workerCount);
};

}  // namespace rayreuse
