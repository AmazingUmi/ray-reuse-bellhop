#include "broadband/io/command_line.hpp"

#include <charconv>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <system_error>
#include <utility>

#include "broadband/error.hpp"

namespace broadband {
namespace {

[[nodiscard]] std::string_view trimAsciiWhitespace(std::string_view value) {
  const std::size_t first = value.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos) {
    return {};
  }
  const std::size_t last = value.find_last_not_of(" \t\r\n");
  return value.substr(first, last - first + 1U);
}

[[nodiscard]] double parseFrequency(std::string_view token) {
  token = trimAsciiWhitespace(token);
  if (token.empty()) {
    throw ValidationError("--frequencies-hz contains an empty frequency");
  }

  double value = 0.0;
  const char* const begin = token.data();
  const char* const end = token.data() + token.size();
  const auto result =
      std::from_chars(begin, end, value, std::chars_format::general);
  if (result.ec != std::errc{} || result.ptr != end || !std::isfinite(value) ||
      value <= 0.0) {
    throw ValidationError(
        "--frequencies-hz values must be positive finite numbers");
  }
  return value;
}

[[nodiscard]] std::vector<double> parseFrequencyList(std::string_view text) {
  std::vector<double> frequencies;
  std::size_t begin = 0U;
  while (begin <= text.size()) {
    const std::size_t comma = text.find(',', begin);
    const std::size_t end =
        comma == std::string_view::npos ? text.size() : comma;
    const double value = parseFrequency(text.substr(begin, end - begin));
    if (!frequencies.empty() && frequencies.back() >= value) {
      throw ValidationError(
          "--frequencies-hz values must be strictly increasing");
    }
    frequencies.push_back(value);
    if (comma == std::string_view::npos) {
      break;
    }
    begin = comma + 1U;
  }
  return frequencies;
}

[[nodiscard]] std::size_t parsePositiveSize(std::string_view text,
                                            std::string_view optionName) {
  text = trimAsciiWhitespace(text);
  unsigned long long value = 0U;
  const char* const begin = text.data();
  const char* const end = text.data() + text.size();
  const auto result = std::from_chars(begin, end, value);
  if (text.empty() || result.ec != std::errc{} || result.ptr != end ||
      value == 0U ||
      value > static_cast<unsigned long long>(
                  std::numeric_limits<std::size_t>::max())) {
    throw ValidationError(std::string(optionName) +
                          " requires a positive integer");
  }
  return static_cast<std::size_t>(value);
}

}  // namespace

CommandLineOptions parseCommandLine(
    std::span<const std::string_view> arguments) {
  if (arguments.size() == 1U && arguments.front() == "--help") {
    CommandLineOptions options;
    options.showHelp = true;
    return options;
  }
  if (arguments.size() == 1U && arguments.front() == "--version") {
    CommandLineOptions options;
    options.showVersion = true;
    return options;
  }
  if (arguments.empty()) {
    throw ValidationError("a file root is required");
  }

  CommandLineOptions options;
  bool executionModeSpecified = false;
  bool verifyCacheSpecified = false;
  bool profileInfluenceSpecified = false;
  bool profileFrequencyTasksSpecified = false;
  bool reuseModeSpecified = false;
  bool traceWorkerCountSpecified = false;
  bool reuseWorkerCountSpecified = false;
  bool outputQueueCapacitySpecified = false;
  bool memoryBudgetSpecified = false;
  for (std::size_t index = 0U; index < arguments.size(); ++index) {
    const std::string_view argument = arguments[index];
    if (argument == "--frequencies-hz") {
      if (options.frequencyOverrideHz.has_value()) {
        throw ValidationError("--frequencies-hz may be specified only once");
      }
      if (index + 1U >= arguments.size()) {
        throw ValidationError(
            "--frequencies-hz requires a comma-separated value");
      }
      options.frequencyOverrideHz = parseFrequencyList(arguments[++index]);
      continue;
    }
    if (argument == "--execution-mode") {
      if (executionModeSpecified) {
        throw ValidationError("--execution-mode may be specified only once");
      }
      if (index + 1U >= arguments.size()) {
        throw ValidationError(
            "--execution-mode requires 'nonreuse' or 'reuse'");
      }
      const std::string_view value = arguments[++index];
      if (value == "nonreuse") {
        options.executionMode = ExecutionMode::NonReuse;
      } else if (value == "reuse") {
        options.executionMode = ExecutionMode::Reuse;
      } else {
        throw ValidationError(
            "--execution-mode must be 'nonreuse' or 'reuse'");
      }
      executionModeSpecified = true;
      options.executionModeSpecified = true;
      continue;
    }
    if (argument == "--reuse-mode") {
      if (reuseModeSpecified) {
        throw ValidationError("--reuse-mode may be specified only once");
      }
      if (index + 1U >= arguments.size()) {
        throw ValidationError(
            "--reuse-mode requires 'serial', 'frequency', or 'range'");
      }
      const std::string_view value = arguments[++index];
      if (value == "serial") {
        options.reuseMode = ReuseMode::Serial;
      } else if (value == "frequency") {
        options.reuseMode = ReuseMode::Frequency;
      } else if (value == "range") {
        options.reuseMode = ReuseMode::Range;
      } else {
        throw ValidationError(
            "--reuse-mode must be 'serial', 'frequency', or 'range'");
      }
      reuseModeSpecified = true;
      options.reuseModeSpecified = true;
      continue;
    }
    if (argument == "--verify-cache") {
      if (verifyCacheSpecified) {
        throw ValidationError("--verify-cache may be specified only once");
      }
      options.verifyCache = true;
      verifyCacheSpecified = true;
      continue;
    }
    if (argument == "--profile-influence") {
      if (profileInfluenceSpecified) {
        throw ValidationError("--profile-influence may be specified only once");
      }
      options.profileInfluence = true;
      profileInfluenceSpecified = true;
      continue;
    }
    if (argument == "--profile-frequency-tasks") {
      if (profileFrequencyTasksSpecified) {
        throw ValidationError(
            "--profile-frequency-tasks may be specified only once");
      }
      options.profileFrequencyTasks = true;
      profileFrequencyTasksSpecified = true;
      continue;
    }
    if (argument == "--trace-workers") {
      if (traceWorkerCountSpecified) {
        throw ValidationError("--trace-workers may be specified only once");
      }
      if (index + 1U >= arguments.size()) {
        throw ValidationError("--trace-workers requires a positive integer");
      }
      options.traceWorkerCount =
          parsePositiveSize(arguments[++index], "--trace-workers");
      options.traceWorkerCountSpecified = true;
      traceWorkerCountSpecified = true;
      continue;
    }
    if (argument == "--reuse-workers") {
      if (reuseWorkerCountSpecified) {
        throw ValidationError("--reuse-workers may be specified only once");
      }
      if (index + 1U >= arguments.size()) {
        throw ValidationError("--reuse-workers requires a positive integer");
      }
      options.reuseWorkerCount =
          parsePositiveSize(arguments[++index], "--reuse-workers");
      reuseWorkerCountSpecified = true;
      options.reuseWorkerCountSpecified = true;
      continue;
    }
    if (argument == "--output-queue-capacity") {
      if (outputQueueCapacitySpecified) {
        throw ValidationError(
            "--output-queue-capacity may be specified only once");
      }
      if (index + 1U >= arguments.size()) {
        throw ValidationError(
            "--output-queue-capacity requires a positive integer");
      }
      options.outputQueueCapacity =
          parsePositiveSize(arguments[++index], "--output-queue-capacity");
      if (options.outputQueueCapacity > 2U) {
        throw ValidationError("--output-queue-capacity must be 1 or 2");
      }
      outputQueueCapacitySpecified = true;
      options.outputQueueCapacitySpecified = true;
      continue;
    }
    if (argument == "--memory-budget-mib") {
      if (memoryBudgetSpecified) {
        throw ValidationError("--memory-budget-mib may be specified only once");
      }
      if (index + 1U >= arguments.size()) {
        throw ValidationError(
            "--memory-budget-mib requires a positive integer");
      }
      options.memoryBudgetMiB =
          parsePositiveSize(arguments[++index], "--memory-budget-mib");
      memoryBudgetSpecified = true;
      options.memoryBudgetSpecified = true;
      continue;
    }
    if (argument.starts_with('-')) {
      throw ValidationError("unknown command-line option: " +
                            std::string(argument));
    }
    if (!options.fileRoot.empty()) {
      throw ValidationError("only one file root may be specified");
    }
    if (argument.empty()) {
      throw ValidationError("the file root must not be empty");
    }
    options.fileRoot = std::string(argument);
  }

  if (options.fileRoot.empty()) {
    throw ValidationError("a file root is required");
  }
  if (reuseModeSpecified && options.executionMode != ExecutionMode::Reuse) {
    throw ValidationError("--reuse-mode requires --execution-mode reuse");
  }
  if (reuseWorkerCountSpecified &&
      options.executionMode != ExecutionMode::Reuse) {
    throw ValidationError("--reuse-workers requires --execution-mode reuse");
  }
  if (options.executionMode == ExecutionMode::Reuse &&
      options.reuseMode == ReuseMode::Serial && reuseWorkerCountSpecified) {
    throw ValidationError(
        "--reuse-workers is not accepted with --reuse-mode serial");
  }
  if ((outputQueueCapacitySpecified || memoryBudgetSpecified ||
       profileFrequencyTasksSpecified) &&
      !(options.executionMode == ExecutionMode::Reuse &&
        options.reuseMode == ReuseMode::Frequency)) {
    throw ValidationError(
        "--output-queue-capacity, --memory-budget-mib, and "
        "--profile-frequency-tasks require --execution-mode reuse "
        "--reuse-mode frequency");
  }
  return options;
}

}  // namespace broadband
