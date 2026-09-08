#pragma once

#include <cstddef>
#include <vector>

#include "broadband/field/cartesian_cerveny_influence.hpp"
#include "broadband/model/simulation_case.hpp"
#include "broadband/solver/single_frequency_solver.hpp"

namespace broadband {

struct NonReuseStatistics {
  std::size_t tracePassCount{};
  std::size_t totalRayCount{};
  std::size_t totalRayPointCount{};
  std::size_t cumulativeRayCacheBytes{};
  std::size_t peakRayCacheBytes{};
  SingleFrequencyTimings phaseTotals;
  double wallSeconds{};
};

struct NonReuseResult {
  // Input frequency order is preserved. Each element retains its own ray,
  // cache, and phase-timing statistics.
  std::vector<SingleFrequencyResult> frequencyResults;
  NonReuseStatistics statistics;
};

class NonReuseSolver {
 public:
  [[nodiscard]] static NonReuseResult solve(
      const SimulationCase& simulation, double epsilonMultiplier,
      double loopRange, CartesianCervenySettings influenceSettings = {},
      RayFanTraceSettings traceSettings = {});
};

}  // namespace broadband
