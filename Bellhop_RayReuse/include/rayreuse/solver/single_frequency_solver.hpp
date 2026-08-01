#pragma once

#include <cstddef>

#include "rayreuse/cache/ray_path_cache.hpp"
#include "rayreuse/field/cartesian_cerveny_influence.hpp"
#include "rayreuse/field/frequency_workspace.hpp"
#include "rayreuse/model/simulation_case.hpp"

namespace rayreuse {

struct SingleFrequencyTimings {
  double traceSeconds{};
  double projectSeconds{};
  double influenceSeconds{};
  double scaleSeconds{};
  CartesianCervenyStatistics influenceStatistics;
};

struct SingleFrequencyResult {
  FrequencyWorkspace workspace;
  std::size_t rayCount{};
  std::size_t totalRayPointCount{};
  std::size_t rayCacheBytes{};
  SingleFrequencyTimings timings;
};

struct RayFanTraceResult {
  RayPathCache cache;
  std::size_t totalRayPointCount{};
  double traceSeconds{};
};

class SingleFrequencySolver {
 public:
  [[nodiscard]] static RayFanTraceResult traceRayFan(
      const SimulationCase& simulation);

  [[nodiscard]] static SingleFrequencyResult solveFrequencyFromCache(
      const SimulationCase& simulation, double frequency,
      const RayPathCache& rayCache, double epsilonMultiplier, double loopRange,
      CartesianCervenySettings influenceSettings = {});

  [[nodiscard]] static SingleFrequencyResult solve(
      const SimulationCase& simulation, double epsilonMultiplier,
      double loopRange, CartesianCervenySettings influenceSettings = {});

  [[nodiscard]] static SingleFrequencyResult solveAtFrequency(
      const SimulationCase& simulation, double frequency,
      double epsilonMultiplier, double loopRange,
      CartesianCervenySettings influenceSettings = {});
};

}  // namespace rayreuse
