#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

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

  // Single-source entry. Requires simulation.sourceCount() == 1; multi-source
  // simulations must use the per-source overloads below.
  void writeFrequency(std::size_t index, const FrequencyWorkspace& workspace);

  // Per-source entry (F2CPP `writeSingleFrequency` parameter shape): the
  // first (shallowest) source's workspace plus the remaining sources'
  // workspaces in SimulationCase::sources() order. Each frequency block holds
  // NSz x receiversPerRange pressure records, laid out source-major (Origin:
  // IRec = 10 + freqIndex*NSz*NRz_per_range + sourceIndex*NRz_per_range +
  // depthIndex).
  void writeFrequency(std::size_t index,
                      const FrequencyWorkspace& firstSourceWorkspace,
                      std::span<const FrequencyWorkspace>
                          additionalSourceWorkspaces);

  // Per-source entry over one contiguous source-major workspace sequence.
  void writeFrequency(std::size_t index,
                      std::span<const FrequencyWorkspace> sourceWorkspaces);
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

  // Multi-source single-frequency output (F2CPP `writeSingleFrequency`
  // overload shape): the first source's workspace plus the remaining sources
  // in SimulationCase::sources() order.
  static void writeSingleFrequency(
      const std::filesystem::path& path, std::string_view title,
      const SimulationCase& simulation,
      const FrequencyWorkspace& firstSourceWorkspace,
      std::span<const FrequencyWorkspace> additionalSourceWorkspaces);

  static void writeFrequencies(const std::filesystem::path& path,
                               std::string_view title,
                               const SimulationCase& simulation,
                               std::span<const FrequencyWorkspace> workspaces);

  // Per-frequency, per-source broadband output. The outer vector holds one
  // source-major workspace vector per simulation frequency.
  static void writeFrequencies(
      const std::filesystem::path& path, std::string_view title,
      const SimulationCase& simulation,
      const std::vector<std::vector<FrequencyWorkspace>>& workspaces);
};

}  // namespace rayreuse
