#pragma once

#include <cstddef>
#include <functional>

#include "rayreuse/field/frequency_workspace.hpp"
#include "rayreuse/solver/single_frequency_solver.hpp"

namespace rayreuse {

using RayReuseFrequencyConsumer = std::function<void(
    std::size_t frequencyIndex, FrequencyWorkspace&& workspace,
    const SingleFrequencyTimings& timings)>;

}  // namespace rayreuse
