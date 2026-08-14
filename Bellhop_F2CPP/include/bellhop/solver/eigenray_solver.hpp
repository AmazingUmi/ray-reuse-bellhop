#pragma once

#include <cstddef>
#include <functional>

#include "bellhop/cache/ray_path_cache.hpp"
#include "bellhop/field/eigenray_hit.hpp"
#include "bellhop/model/simulation_case.hpp"

namespace bellhop {

struct EigenraySolverStatistics {
  std::size_t sourceCount{};
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

using FrozenEigenrayConsumer = std::function<void(
    std::size_t sourceIndex, std::size_t launchIndex,
    const RayPathCache& sourceCache, const RayPath& path,
    const EigenrayHit& hit)>;

class EigenraySolver {
 public:
  [[nodiscard]] static EigenraySolverStatistics solve(
      const SimulationCase& simulation,
      const FrozenEigenrayConsumer& consumer);
};

}  // namespace bellhop
