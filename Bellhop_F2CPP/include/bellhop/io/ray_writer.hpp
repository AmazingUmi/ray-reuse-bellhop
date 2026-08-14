#pragma once

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>

#include "bellhop/cache/ray_path_cache.hpp"
#include "bellhop/model/simulation_case.hpp"

namespace bellhop {

class RayWriter {
 public:
  RayWriter(std::filesystem::path outputPath, std::string title,
            const SimulationCase& simulation);
  RayWriter(const RayWriter&) = delete;
  RayWriter& operator=(const RayWriter&) = delete;
  ~RayWriter();

  void appendSource(std::size_t sourceIndex, const RayPathCache& cache);
  void finalize();

 private:
  std::filesystem::path outputPath_;
  std::filesystem::path temporaryPath_;
  const SimulationCase& simulation_;
  std::ofstream output_;
  std::size_t nextSourceIndex_{};
  bool finalized_{};
};

}  // namespace bellhop
