#pragma once

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "rayreuse/cache/ray_path_cache.hpp"
#include "rayreuse/field/frequency_projector.hpp"
#include "rayreuse/model/simulation_case.hpp"

namespace rayreuse {

// I/O stays outside the model; the returned value is the shared model type.
[[nodiscard]] SourceBeamPattern readSourceBeamPattern(
    const std::filesystem::path& path);

// Origin's R product has no defined multi-frequency file schema. This writer
// therefore requires an explicitly selected frequency and rejects a
// SimulationCase containing more than one frequency.
class RayWriter {
 public:
  RayWriter(std::filesystem::path outputPath, std::string title,
            const SimulationCase& simulation, double frequency,
            std::vector<double> launchAngles = {});
  RayWriter(const RayWriter&) = delete;
  RayWriter& operator=(const RayWriter&) = delete;
  ~RayWriter();

  // Appends one source's complete frozen launch fan. Sources must be appended
  // in SimulationCase::sources() order (depth ascending); finalize() requires
  // every source. The header line is `1 1 NSz` (Origin WriteRay ray-file
  // header) and each source contributes one fan block.
  void appendSource(std::size_t sourceIndex, const RayPathCache& cache);

  // Single-source legacy entry: equivalent to appendSource(0, cache).
  // Multi-source runs must append every source via appendSource; finalize()
  // rejects an incomplete source sequence.
  void append(const RayPathCache& cache);
  void finalize();

 private:
  std::filesystem::path outputPath_;
  std::filesystem::path temporaryPath_;
  const SimulationCase& simulation_;
  double frequency_{};
  std::vector<double> launchAngles_;
  FrequencyProjector projector_;
  std::ofstream output_;
  std::size_t nextSourceIndex_{};
  bool finalized_{};
};

}  // namespace rayreuse
