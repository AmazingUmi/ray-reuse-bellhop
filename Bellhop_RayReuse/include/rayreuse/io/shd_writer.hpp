#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <span>
#include <string_view>

#include "rayreuse/field/frequency_workspace.hpp"
#include "rayreuse/model/simulation_case.hpp"

namespace rayreuse {

class ShdFrequencyWriter {
 public:
  ShdFrequencyWriter(const std::filesystem::path& path, std::string_view title,
                     const SimulationCase& simulation);
  ~ShdFrequencyWriter() noexcept;

  ShdFrequencyWriter(const ShdFrequencyWriter&) = delete;
  ShdFrequencyWriter& operator=(const ShdFrequencyWriter&) = delete;
  ShdFrequencyWriter(ShdFrequencyWriter&&) noexcept;
  ShdFrequencyWriter& operator=(ShdFrequencyWriter&&) noexcept;

  void writeFrequency(std::size_t index, const FrequencyWorkspace& workspace);
  void finalize();

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

class ShdWriter {
 public:
  static void writeSingleFrequency(const std::filesystem::path& path,
                                   std::string_view title,
                                   const SimulationCase& simulation,
                                   const FrequencyWorkspace& workspace);

  static void writeFrequencies(const std::filesystem::path& path,
                               std::string_view title,
                               const SimulationCase& simulation,
                               std::span<const FrequencyWorkspace> workspaces);
};

}  // namespace rayreuse
