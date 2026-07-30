#include <initializer_list>
#include <iostream>
#include <string_view>
#include <vector>

#include "rayreuse/error.hpp"
#include "rayreuse/io/command_line.hpp"
#include "support/test_harness.hpp"

namespace {

using rayreuse::CommandLineOptions;
using rayreuse::BroadbandExecutionMode;
using rayreuse::ValidationError;
using rayreuse::parseCommandLine;
using rayreuse::test::Context;

CommandLineOptions parse(
    std::initializer_list<std::string_view> arguments) {
  const std::vector<std::string_view> values(arguments);
  return parseCommandLine(values);
}

void testSingleFrequencyCompatibility(Context& context) {
  const CommandLineOptions options = parse({"case/root"});
  context.check(!options.showHelp, "normal invocation does not show help");
  context.check(
      options.fileRoot == "case/root", "single invocation preserves root");
  context.check(
      !options.frequencyOverrideHz.has_value(),
      "single invocation leaves the environment frequency unchanged");
  context.check(
      options.executionMode == BroadbandExecutionMode::NonReuse,
      "non-reuse remains the default during the baseline gate");
  context.check(
      !options.verifyCache,
      "cache fingerprint verification is disabled by default");
}

void testExecutionMode(Context& context) {
  const CommandLineOptions options =
      parse(
          {"case/root", "--frequencies-hz", "50,250",
           "--execution-mode", "reuse", "--verify-cache"});
  context.check(
      options.executionMode == BroadbandExecutionMode::Reuse,
      "reuse execution mode is selected explicitly");
  context.check(
      options.verifyCache,
      "cache fingerprint verification is selected explicitly");

  const CommandLineOptions parallel =
      parse(
          {"root", "--execution-mode", "parallel",
           "--workers", "8", "--output-queue-capacity", "2",
           "--memory-budget-mib", "4096"});
  context.check(
      parallel.executionMode == BroadbandExecutionMode::Parallel,
      "parallel execution mode is selected explicitly");
  context.check(
      parallel.workerCount == 8U,
      "parallel worker count is parsed");
  context.check(
      parallel.outputQueueCapacity == 2U,
      "parallel output queue capacity is parsed");
  context.check(
      parallel.memoryBudgetMiB == 4096U,
      "parallel memory budget is parsed");
}

void testFrequencyOverride(Context& context) {
  const CommandLineOptions options =
      parse({"case/root", "--frequencies-hz", " 50,250, 5000 "});
  context.check(
      options.frequencyOverrideHz.has_value(),
      "frequency override is present");
  if (options.frequencyOverrideHz.has_value()) {
    context.check(
        *options.frequencyOverrideHz ==
            std::vector<double>({50.0, 250.0, 5000.0}),
        "frequency override preserves ascending values");
  }
}

void testInvalidArguments(Context& context) {
  context.expectThrows<ValidationError>(
      [] { static_cast<void>(parse({})); },
      "missing root is rejected");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(
            parse({"root", "--frequencies-hz", "50,,100"}));
      },
      "empty frequency token is rejected");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(
            parse({"root", "--frequencies-hz", "100,50"}));
      },
      "descending frequency list is rejected");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(
            parse({"root", "--frequencies-hz", "50,50"}));
      },
      "duplicate frequency is rejected");
  context.expectThrows<ValidationError>(
      [] { static_cast<void>(parse({"root", "--unknown"})); },
      "unknown option is rejected");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(
            parse({"root", "--execution-mode", "invalid"}));
      },
      "unknown execution mode is rejected");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(
            parse({"root", "--workers", "0"}));
      },
      "zero worker count is rejected");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(
            parse({"root", "--output-queue-capacity", "-1"}));
      },
      "negative queue capacity is rejected");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(
            parse(
                {"root", "--execution-mode", "parallel",
                 "--output-queue-capacity", "3"}));
      },
      "queue capacity above the single-writer bound is rejected");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(
            parse({"root", "--memory-budget-mib", "1.5"}));
      },
      "non-integral memory budget is rejected");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(
            parse({"root", "--workers", "4"}));
      },
      "parallel tuning requires parallel mode");
}

void testHelp(Context& context) {
  const CommandLineOptions options = parse({"--help"});
  context.check(options.showHelp, "standalone help is accepted");
}

}  // namespace

int main() {
  Context context;
  testSingleFrequencyCompatibility(context);
  testFrequencyOverride(context);
  testExecutionMode(context);
  testInvalidArguments(context);
  testHelp(context);
  if (context.failureCount() != 0) {
    std::cerr << context.failureCount()
              << " command-line assertion(s) failed\n";
    return 1;
  }
  std::cout << "All Bellhop RayReuse command-line tests passed\n";
  return 0;
}
