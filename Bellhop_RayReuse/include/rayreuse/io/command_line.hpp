#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rayreuse {

// Two-layer execution model: the execution layer selects nonreuse vs reuse;
// the reuse layer selects how reuse work is scheduled (serial, split across
// frequency tasks, or split across receiver-range blocks).
enum class ExecutionMode {
  NonReuse,
  Reuse,
};

enum class ReuseMode {
  Serial,
  Frequency,
  Range,
};

struct CommandLineOptions {
  bool showHelp{};
  bool showVersion{};
  std::string fileRoot;
  std::optional<std::vector<double>> frequencyOverrideHz;
  ExecutionMode executionMode{ExecutionMode::NonReuse};
  bool executionModeSpecified{};
  ReuseMode reuseMode{ReuseMode::Serial};
  bool reuseModeSpecified{};
  bool verifyCache{};
  bool profileInfluence{};
  bool profileFrequencyTasks{};
  std::size_t traceWorkerCount{1U};
  bool traceWorkerCountSpecified{};
  std::size_t reuseWorkerCount{1U};
  bool reuseWorkerCountSpecified{};
  std::size_t outputQueueCapacity{2U};
  std::size_t memoryBudgetMiB{};
  bool outputQueueCapacitySpecified{};
  bool memoryBudgetSpecified{};
};

[[nodiscard]] CommandLineOptions parseCommandLine(
    std::span<const std::string_view> arguments);

}  // namespace rayreuse
