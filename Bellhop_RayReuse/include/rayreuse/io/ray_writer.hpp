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
  bool appended_{};
  bool finalized_{};
};

}  // namespace rayreuse
