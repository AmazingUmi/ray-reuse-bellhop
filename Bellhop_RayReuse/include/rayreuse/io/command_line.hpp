#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rayreuse {

enum class BroadbandExecutionMode {
  NonReuse,
  Reuse,
  Parallel,
};

struct CommandLineOptions {
  bool showHelp{};
  std::string fileRoot;
  std::optional<std::vector<double>> frequencyOverrideHz;
  BroadbandExecutionMode executionMode{
      BroadbandExecutionMode::NonReuse};
  bool verifyCache{};
  bool profileInfluence{};
  bool profileFrequencyTasks{};
  std::size_t workerCount{};
  std::size_t outputQueueCapacity{2U};
  std::size_t memoryBudgetMiB{};
};

[[nodiscard]] CommandLineOptions parseCommandLine(
    std::span<const std::string_view> arguments);

}  // namespace rayreuse
