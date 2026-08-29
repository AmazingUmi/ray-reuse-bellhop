#pragma once

#include <cstddef>
#include <filesystem>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "rayreuse/cache/ray_path_cache.hpp"
#include "rayreuse/field/eigenray_hit.hpp"
#include "rayreuse/model/simulation_case.hpp"

namespace rayreuse {

class EigenrayWriter {
 public:
  // Single-source entry. Requires simulation.sourceCount() == 1; multi-source
  // simulations must use the per-source overload below.
  static void write(
      const std::filesystem::path& path, std::string_view title,
      const SimulationCase& simulation, double frequency,
      const RayPathCache& cache,
      const std::vector<std::pair<std::size_t, EigenrayHit>>& hits);

  // Per-source entry (F2CPP `EigenrayWriter` append-hit shape, batch form).
  // The header carries `1 1 NSz` and the body holds one hit section per
  // source in SimulationCase::sources() order (depth ascending); within a
  // section hits appear in launch order.
  static void write(
      const std::filesystem::path& path, std::string_view title,
      const SimulationCase& simulation, double frequency,
      std::span<const RayPathCache> sourceCaches,
      std::span<const std::vector<std::pair<std::size_t, EigenrayHit>>>
          sourceHits);
};

}  // namespace rayreuse
