#pragma once

#include <cstddef>
#include <filesystem>
#include <string_view>
#include <utility>
#include <vector>

#include "rayreuse/cache/ray_path_cache.hpp"
#include "rayreuse/field/eigenray_hit.hpp"
#include "rayreuse/model/simulation_case.hpp"

namespace rayreuse {

class EigenrayWriter {
 public:
  static void write(
      const std::filesystem::path& path, std::string_view title,
      const SimulationCase& simulation, double frequency,
      const RayPathCache& cache,
      const std::vector<std::pair<std::size_t, EigenrayHit>>& hits);
};

}  // namespace rayreuse
