#pragma once

#include <cstddef>
#include <functional>

#include "bellhop/cache/ray_path_cache.hpp"
#include "bellhop/field/arrival_workspace.hpp"
#include "bellhop/model/simulation_case.hpp"

namespace bellhop {

struct ArrivalSolverStatistics {
  std::size_t sourceCount{};
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
};

using FrozenSourceArrivalConsumer = std::function<void(
    std::size_t, const RayPathCache&, const ArrivalWorkspace&)>;

class ArrivalSolver {
 public:
  [[nodiscard]] static ArrivalSolverStatistics solve(
      const SimulationCase& simulation,
      const FrozenSourceArrivalConsumer& consumer);
};

}  // namespace bellhop
