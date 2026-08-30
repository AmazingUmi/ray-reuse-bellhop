#pragma once

#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

#include "rayreuse/field/frequency_workspace.hpp"
#include "rayreuse/solver/single_frequency_solver.hpp"

namespace rayreuse {

// Receives the per-source workspace sequence for one frequency. The vector is
// indexed by SimulationCase::sources() order (depth ascending) and sized
// sourceCount(); the workspaces are frequency-local product state and never
// written back to the frozen caches.
using RayReuseFrequencyConsumer =
    std::function<void(std::size_t frequencyIndex,
                       std::vector<FrequencyWorkspace>&& sourceWorkspaces,
                       const SingleFrequencyTimings& timings)>;

}  // namespace rayreuse
