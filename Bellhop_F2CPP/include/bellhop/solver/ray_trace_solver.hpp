#pragma once

#include <cstddef>
#include <functional>

#include "bellhop/cache/ray_path_cache.hpp"
#include "bellhop/model/simulation_case.hpp"

namespace bellhop {

struct RayTraceStatistics {
  std::size_t rayCount{};
  std::size_t totalRayPointCount{};
  std::size_t peakRayCacheBytes{};
  double traceSeconds{};
  double writeSeconds{};
};

using FrozenSourceRayConsumer =
    std::function<void(std::size_t, const RayPathCache&)>;

class RayTraceSolver {
 public:
  [[nodiscard]] static RayTraceStatistics trace(
      const SimulationCase& simulation,
      const FrozenSourceRayConsumer& consumer);
};

}  // namespace bellhop
