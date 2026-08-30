#include "rayreuse/solver/ray_trace_product.hpp"

#include <string>
#include <utility>

#include "rayreuse/error.hpp"
#include "rayreuse/ray/geometry_tracer.hpp"

namespace rayreuse {

namespace {

RayPathCache traceSourceRayProduct(const SimulationCase& simulation,
                                   std::size_t sourceIndex) {
  const LaunchFanPlan& plan = simulation.launchFanPlan();
  if (plan.launchAngles.empty()) {
    throw ValidationError("R launch plan must contain at least one angle");
  }
  const Source& source = simulation.sources().at(sourceIndex);
  GeometryTracer tracer(simulation);
  RayPathCache cache;
  cache.reserve(plan.launchAngles.size());
  for (std::size_t launchIndex = 0U; launchIndex < plan.launchAngles.size();
       ++launchIndex) {
    RayPath path = tracer.trace(source, plan.launchAngles[launchIndex]);
    if (path.terminationReason != RayTerminationReason::ExitedDomain) {
      // F2CPP ray-trace diagnostic semantics: source and launch indices.
      throw ValidationError(
          "R trace encountered a ray that did not exit "
          "the spatial domain normally (source index " +
          std::to_string(sourceIndex) + ", launch index " +
          std::to_string(launchIndex) + ")");
    }
    cache.append(std::move(path));
  }
  cache.freeze();
  return cache;
}

}  // namespace

RayPathCache traceRayProduct(const SimulationCase& simulation) {
  if (simulation.runMode() != SimulationRunMode::RayTrace) {
    throw ValidationError("R trace product requires ray-trace run mode");
  }
  return traceSourceRayProduct(simulation, 0U);
}

std::vector<RayPathCache> traceRayProducts(const SimulationCase& simulation) {
  if (simulation.runMode() != SimulationRunMode::RayTrace) {
    throw ValidationError("R trace product requires ray-trace run mode");
  }
  std::vector<RayPathCache> caches;
  caches.reserve(simulation.sourceCount());
  for (std::size_t sourceIndex = 0U; sourceIndex < simulation.sourceCount();
       ++sourceIndex) {
    caches.push_back(traceSourceRayProduct(simulation, sourceIndex));
  }
  return caches;
}

}  // namespace rayreuse
