#include "broadband/solver/ray_trace_product.hpp"

#include <utility>

#include "broadband/error.hpp"
#include "broadband/solver/single_frequency_solver.hpp"

namespace broadband {

namespace {

// The R product delegates its fan, worker statistics, and abnormal-
// termination diagnostic to the shared production trace seam (Worklist
// PERF-TRACE-PAR-1 A01); only the launch-plan precheck is R-local.
RayFanTraceResult traceSourceRayProduct(const SimulationCase& simulation,
                                        std::size_t sourceIndex,
                                        RayFanTraceSettings settings) {
  const LaunchFanPlan& plan = simulation.launchFanPlan();
  if (plan.launchAngles.empty()) {
    throw ValidationError("R launch plan must contain at least one angle");
  }
  return SingleFrequencySolver::traceSourceFan(
      simulation, sourceIndex, settings, RayFanTraceProduct::RayTrace);
}

}  // namespace

RayFanTraceResult traceRayProduct(const SimulationCase& simulation,
                                  RayFanTraceSettings settings) {
  if (simulation.runMode() != SimulationRunMode::RayTrace) {
    throw ValidationError("R trace product requires ray-trace run mode");
  }
  return traceSourceRayProduct(simulation, 0U, settings);
}

std::vector<RayFanTraceResult> traceRayProducts(
    const SimulationCase& simulation, RayFanTraceSettings settings) {
  if (simulation.runMode() != SimulationRunMode::RayTrace) {
    throw ValidationError("R trace product requires ray-trace run mode");
  }
  std::vector<RayFanTraceResult> sourceTraces;
  sourceTraces.reserve(simulation.sourceCount());
  for (std::size_t sourceIndex = 0U; sourceIndex < simulation.sourceCount();
       ++sourceIndex) {
    sourceTraces.push_back(
        traceSourceRayProduct(simulation, sourceIndex, settings));
  }
  return sourceTraces;
}

}  // namespace broadband
