#pragma once

#include <cstddef>
#include <vector>

#include "rayreuse/cache/ray_path_cache.hpp"
#include "rayreuse/model/simulation_case.hpp"

namespace rayreuse {

// Trace the generalized R fan owned by the RayTrace SimulationCase and freeze
// the resulting frequency-independent geometry.
// First-source legacy entry point; with NSz > 1 it traces sources().front()
// (the shallowest source).
[[nodiscard]] RayPathCache traceRayProduct(const SimulationCase& simulation);

// Traces every source's launch fan into NSz independent frozen caches, one
// entry per SimulationCase::sources() entry (depth ascending). R stays a
// single-frequency product; the per-source caches are the R product payload
// (per-source writer blocks are assembled by the ray writer).
[[nodiscard]] std::vector<RayPathCache> traceRayProducts(
    const SimulationCase& simulation);

}  // namespace rayreuse
