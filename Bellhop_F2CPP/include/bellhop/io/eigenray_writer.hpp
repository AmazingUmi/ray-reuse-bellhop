#pragma once

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>

#include "bellhop/cache/ray_path_cache.hpp"
#include "bellhop/field/eigenray_hit.hpp"
#include "bellhop/model/simulation_case.hpp"

namespace bellhop {

class EigenrayWriter {
 public:
  EigenrayWriter(std::filesystem::path outputPath, std::string title,
                 const SimulationCase& simulation);
  EigenrayWriter(const EigenrayWriter&) = delete;
  EigenrayWriter& operator=(const EigenrayWriter&) = delete;
  ~EigenrayWriter();

  void appendHit(std::size_t sourceIndex, std::size_t launchIndex,
                 const RayPathCache& cache, const RayPath& path,
                 const EigenrayHit& hit);
  void finalize();

 private:
  std::filesystem::path outputPath_;
  std::filesystem::path temporaryPath_;
  const SimulationCase& simulation_;
  std::ofstream output_;
  std::size_t lastSourceIndex_{};
  std::size_t lastLaunchIndex_{};
  bool haveHit_{false};
  bool finalized_{false};
};

}  // namespace bellhop
