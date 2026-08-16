#include "rayreuse/solver/ray_trace_product.hpp"

#include "rayreuse/error.hpp"
#include "rayreuse/ray/geometry_tracer.hpp"

namespace rayreuse {

RayPathCache traceRayProduct(const SimulationCase& simulation) {
  if (simulation.runMode() != SimulationRunMode::RayTrace) {
    throw ValidationError("R trace product requires ray-trace run mode");
  }
  const LaunchFanPlan& plan = simulation.launchFanPlan();
  if (plan.launchAngles.empty()) {
    throw ValidationError("R launch plan must contain at least one angle");
  }

  GeometryTracer tracer(simulation);
  RayPathCache cache;
  cache.reserve(plan.launchAngles.size());
  for (const double angle : plan.launchAngles) {
    RayPath path = tracer.trace(simulation.source(), angle);
    if (path.terminationReason != RayTerminationReason::ExitedDomain) {
      throw ValidationError(
          "R trace encountered a ray that did not exit "
          "the spatial domain normally");
    }
    cache.append(std::move(path));
  }
  cache.freeze();
  return cache;
}

}  // namespace rayreuse
