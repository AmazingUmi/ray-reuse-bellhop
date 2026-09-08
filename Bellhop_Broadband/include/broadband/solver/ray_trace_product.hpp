#pragma once

#include <cstddef>
#include <vector>

#include "broadband/model/simulation_case.hpp"
#include "broadband/solver/single_frequency_solver.hpp"

namespace broadband {

// Trace the generalized R fan owned by the RayTrace SimulationCase and freeze
// the resulting frequency-independent geometry.
// First-source legacy entry point; with NSz > 1 it traces sources().front()
// (the shallowest source).
[[nodiscard]] RayFanTraceResult traceRayProduct(
    const SimulationCase& simulation, RayFanTraceSettings settings = {});

// Traces every source's launch fan into NSz independent frozen caches, one
// entry per SimulationCase::sources() entry (depth ascending). R stays a
// single-frequency product; the per-source caches are the R product payload
// (per-source writer blocks are assembled by the ray writer).
[[nodiscard]] std::vector<RayFanTraceResult> traceRayProducts(
    const SimulationCase& simulation, RayFanTraceSettings settings = {});

}  // namespace broadband
