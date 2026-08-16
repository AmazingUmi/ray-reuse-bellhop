#pragma once

#include "rayreuse/cache/ray_path_cache.hpp"
#include "rayreuse/model/simulation_case.hpp"

namespace rayreuse {

// Trace the generalized R fan owned by the RayTrace SimulationCase and freeze
// the resulting frequency-independent geometry.
[[nodiscard]] RayPathCache traceRayProduct(const SimulationCase& simulation);

}  // namespace rayreuse
