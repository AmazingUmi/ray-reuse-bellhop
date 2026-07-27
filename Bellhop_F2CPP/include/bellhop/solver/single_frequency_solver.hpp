#pragma once

#include <cstddef>

#include "bellhop/field/cartesian_cerveny_influence.hpp"
#include "bellhop/field/frequency_workspace.hpp"
#include "bellhop/model/simulation_case.hpp"

namespace bellhop {

struct SingleFrequencyTimings {
  double traceSeconds{};
  double projectSeconds{};
  double influenceSeconds{};
  double scaleSeconds{};
};

struct SingleFrequencyResult {
  FrequencyWorkspace workspace;
  std::size_t rayCount{};
  std::size_t totalRayPointCount{};
  std::size_t rayCacheBytes{};
  SingleFrequencyTimings timings;
};

class SingleFrequencySolver {
 public:
  [[nodiscard]] static SingleFrequencyResult solve(
      const SimulationCase& simulation,
      double epsilonMultiplier, double loopRange,
      CartesianCervenySettings influenceSettings = {});
};

}  // namespace bellhop
