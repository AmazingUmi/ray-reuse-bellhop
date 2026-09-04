#pragma once

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "rayreuse/field/arrival_workspace.hpp"
#include "rayreuse/field/broadband_arrival_workspace.hpp"
#include "rayreuse/model/simulation_case.hpp"

namespace rayreuse {

enum class ArrivalEncoding { Ascii, Binary };

// Narrow deterministic failure seam used only by component tests. Empty
// hooks are the production default and add no filesystem behavior.
struct ArrivalWriterTestHooks {
  std::function<void()> afterTemporaryOpen;
  std::function<void(std::size_t)> beforeFrequencyPublish;
};

class ArrivalWriter {
 public:
  ArrivalWriter(std::filesystem::path outputPath, std::string title,
                const SimulationCase& simulation, double frequency,
                ArrivalEncoding encoding = ArrivalEncoding::Ascii,
                ArrivalWriterTestHooks testHooks = {});
  ArrivalWriter(const ArrivalWriter&) = delete;
  ArrivalWriter& operator=(const ArrivalWriter&) = delete;
  ~ArrivalWriter();

  // Append exactly one source block. Both overloads write the same ARR
  // records; FrequencyView is the zero-copy fused path.
  void appendSource(std::size_t sourceIndex,
                    const ArrivalWorkspace& workspace);
  void appendSource(
      std::size_t sourceIndex,
      BroadbandArrivalWorkspace::FrequencyView frequencyView);

  // Direct-writer convenience. BroadbandArrivalWriterSet uses the split
  // complete/publish lifecycle so every frequency temp is complete before
  // any final product is made visible.
  void finalize();

  // Single-source entry. Requires simulation.sourceCount() == 1; multi-source
  // simulations must use the per-source overload below.
  static void write(const std::filesystem::path& path, std::string_view title,
                    const SimulationCase& simulation,
                    const ArrivalWorkspace& workspace,
                    ArrivalEncoding encoding = ArrivalEncoding::Ascii);

  // Per-source entry (F2CPP `ArrivalWriter` append-source shape, batch form).
  // The file header carries the source count and every source depth; the body
  // holds one block per source in SimulationCase::sources() order (depth
  // ascending), each block covering receiversPerRange x rangeCount cells.
  static void write(const std::filesystem::path& path, std::string_view title,
                    const SimulationCase& simulation,
                    std::span<const ArrivalWorkspace> sourceWorkspaces,
                    ArrivalEncoding encoding = ArrivalEncoding::Ascii);

 private:
  friend class BroadbandArrivalWriterSet;

  void complete();
  void publish();
  [[nodiscard]] std::error_code discardTemporary() noexcept;

  std::filesystem::path outputPath_;
  std::filesystem::path temporaryPath_;
  const SimulationCase& simulation_;
  double frequency_{};
  ArrivalEncoding encoding_{};
  std::ofstream output_;
  std::size_t nextSourceIndex_{};
  bool completed_{};
  bool published_{};
};

// Owns one stateful ARR writer per frequency. A source-local broadband
// workspace is consumed in frequency order and can be released immediately
// after appendSource returns. finalize() publishes the completed temp files
// as one coordinated set and rolls back partial publication on failure.
class BroadbandArrivalWriterSet {
 public:
  BroadbandArrivalWriterSet(
      std::span<const std::filesystem::path> outputPaths, std::string title,
      const SimulationCase& simulation,
      ArrivalEncoding encoding = ArrivalEncoding::Ascii,
      ArrivalWriterTestHooks testHooks = {});
  BroadbandArrivalWriterSet(const BroadbandArrivalWriterSet&) = delete;
  BroadbandArrivalWriterSet& operator=(const BroadbandArrivalWriterSet&) =
      delete;

  void appendSource(std::size_t sourceIndex,
                    const BroadbandArrivalWorkspace& workspace);
  void finalize();

 private:
  std::vector<std::unique_ptr<ArrivalWriter>> writers_;
  ArrivalWriterTestHooks testHooks_;
  bool finalized_{};
};

}  // namespace rayreuse
