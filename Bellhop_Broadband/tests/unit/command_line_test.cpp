#include "broadband/io/command_line.hpp"

#include <cstddef>
#include <initializer_list>
#include <iostream>
#include <string_view>
#include <utility>
#include <vector>

#include "broadband/error.hpp"
#include "support/test_harness.hpp"

namespace {

using broadband::CommandLineOptions;
using broadband::ExecutionMode;
using broadband::ReuseMode;
using broadband::parseCommandLine;
using broadband::test::Context;
using broadband::ValidationError;

CommandLineOptions parse(std::initializer_list<std::string_view> arguments) {
  const std::vector<std::string_view> values(arguments);
  return parseCommandLine(values);
}

void testDefaults(Context& context) {
  const CommandLineOptions options = parse({"case/root"});
  context.check(!options.showHelp, "normal invocation does not show help");
  context.check(!options.showVersion,
                "normal invocation does not show the version");
  context.check(options.fileRoot == "case/root",
                "single invocation preserves root");
  context.check(!options.frequencyOverrideHz.has_value(),
                "single invocation leaves the environment frequency unchanged");
  context.check(options.executionMode == ExecutionMode::NonReuse,
                "nonreuse is the default execution mode");
  context.check(!options.executionModeSpecified,
                "single invocation does not mark execution mode explicit");
  context.check(options.reuseMode == ReuseMode::Serial,
                "serial is the default reuse mode");
  context.check(!options.reuseModeSpecified,
                "single invocation does not mark reuse mode explicit");
  context.check(!options.verifyCache,
                "cache fingerprint verification is disabled by default");
  context.check(!options.profileInfluence,
                "Influence profiling is disabled by default");
  context.check(!options.profileFrequencyTasks,
                "frequency-task profiling is disabled by default");
  context.check(options.traceWorkerCount == 1U,
                "trace worker count defaults to 1");
  context.check(!options.traceWorkerCountSpecified,
                "trace worker count is unmarked by default");
  context.check(options.reuseWorkerCount == 1U,
                "reuse worker count defaults to 1");
  context.check(!options.reuseWorkerCountSpecified,
                "reuse worker count is unmarked by default");
}

void testFrequencyOverride(Context& context) {
  const CommandLineOptions options =
      parse({"case/root", "--frequencies-hz", " 50,250, 5000 "});
  context.check(options.frequencyOverrideHz.has_value(),
                "frequency override is present");
  if (options.frequencyOverrideHz.has_value()) {
    context.check(*options.frequencyOverrideHz ==
                      std::vector<double>({50.0, 250.0, 5000.0}),
                  "frequency override preserves ascending values");
  }
}

void testExecutionMode(Context& context) {
  const CommandLineOptions options =
      parse({"case/root", "--frequencies-hz", "50,250", "--execution-mode",
             "reuse", "--verify-cache", "--profile-influence"});
  context.check(options.executionMode == ExecutionMode::Reuse,
                "reuse execution mode is selected explicitly");
  context.check(options.executionModeSpecified,
                "explicit execution mode is tracked for product validation");
  context.check(options.verifyCache,
                "cache fingerprint verification is selected explicitly");
  context.check(options.profileInfluence,
                "Influence profiling is selected explicitly");
  const CommandLineOptions nonReuse = parse(
      {"case/root", "--frequencies-hz", "50,250", "--execution-mode",
       "nonreuse"});
  context.check(nonReuse.executionMode == ExecutionMode::NonReuse,
                "nonreuse execution mode is selected explicitly");
  context.check(nonReuse.executionModeSpecified,
                "explicit nonreuse execution mode is tracked");
  const CommandLineOptions traceParallel = parse(
      {"root", "--execution-mode", "reuse", "--trace-workers", "8"});
  context.check(traceParallel.traceWorkerCountSpecified &&
                    traceParallel.traceWorkerCount == 8U,
                "reuse trace worker count is parsed");
  const CommandLineOptions traceWorkers =
      parse({"root", "--trace-workers", "2"});
  context.check(traceWorkers.traceWorkerCountSpecified &&
                    traceWorkers.traceWorkerCount == 2U,
                "trace worker count is product- and mode-independent");
}

void testReuseMode(Context& context) {
  const std::pair<std::string_view, ReuseMode> legalValues[] = {
      {"serial", ReuseMode::Serial},
      {"frequency", ReuseMode::Frequency},
      {"range", ReuseMode::Range}};
  for (const auto& [value, mode] : legalValues) {
    const CommandLineOptions options = parse(
        {"root", "--execution-mode", "reuse", "--reuse-mode", value});
    context.check(options.executionMode == ExecutionMode::Reuse &&
                      options.reuseMode == mode && options.reuseModeSpecified,
                  "reuse mode value is parsed under reuse execution");
  }
  context.expectThrows<ValidationError>(
      [] { static_cast<void>(parse({"root", "--reuse-mode", "serial"})); },
      "reuse mode without reuse execution is rejected");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(parse(
            {"root", "--execution-mode", "nonreuse", "--reuse-mode", "range"}));
      },
      "reuse mode under nonreuse execution is rejected");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(parse(
            {"root", "--execution-mode", "reuse", "--reuse-mode", "fused"}));
      },
      "unknown reuse mode value is rejected");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(parse(
            {"root", "--execution-mode", "reuse", "--reuse-mode"}));
      },
      "missing reuse mode value is rejected");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(parse({"root", "--execution-mode", "reuse",
                                 "--reuse-mode", "serial", "--reuse-mode",
                                 "serial"}));
      },
      "duplicate reuse mode option is rejected");
}

void testReuseWorkers(Context& context) {
  const std::pair<std::string_view, std::size_t> workerCounts[] = {
      {"1", 1U}, {"2", 2U}};
  for (const std::string_view route : {"frequency", "range"}) {
    for (const auto& workerCount : workerCounts) {
      const CommandLineOptions options = parse(
          {"root", "--execution-mode", "reuse", "--reuse-mode", route,
           "--reuse-workers", workerCount.first});
      context.check(
          options.reuseWorkerCountSpecified &&
              options.reuseWorkerCount == workerCount.second,
          "reuse worker count is parsed on worker-splitting routes");
    }
  }
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(parse({"root", "--execution-mode", "reuse",
                                 "--reuse-mode", "serial", "--reuse-workers",
                                 "1"}));
      },
      "explicit reuse worker count 1 is rejected on the serial route");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(parse({"root", "--execution-mode", "reuse",
                                 "--reuse-mode", "serial", "--reuse-workers",
                                 "2"}));
      },
      "reuse worker count 2 is rejected on the serial route");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(parse(
            {"root", "--execution-mode", "reuse", "--reuse-workers", "2"}));
      },
      "reuse workers under the default serial reuse mode are rejected");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(parse({"root", "--reuse-workers", "2"}));
      },
      "reuse workers under nonreuse execution are rejected");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(parse(
            {"root", "--execution-mode", "reuse", "--reuse-mode", "frequency",
             "--reuse-workers", "0"}));
      },
      "zero reuse worker count is rejected");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(parse(
            {"root", "--execution-mode", "reuse", "--reuse-mode", "frequency",
             "--reuse-workers", "-1"}));
      },
      "negative reuse worker count is rejected");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(parse(
            {"root", "--execution-mode", "reuse", "--reuse-mode", "range",
             "--reuse-workers", "1.5"}));
      },
      "non-integral reuse worker count is rejected");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(parse(
            {"root", "--execution-mode", "reuse", "--reuse-mode", "range",
             "--reuse-workers", "8", "--reuse-workers", "8"}));
      },
      "duplicate reuse workers option is rejected");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(parse(
            {"root", "--execution-mode", "reuse", "--reuse-mode", "range",
             "--reuse-workers"}));
      },
      "missing reuse workers value is rejected");
}

void testFrequencyRouteTuning(Context& context) {
  const CommandLineOptions frequencyRoute = parse(
      {"root", "--execution-mode", "reuse", "--reuse-mode", "frequency",
       "--reuse-workers", "8", "--output-queue-capacity", "2",
       "--memory-budget-mib", "4096", "--profile-frequency-tasks"});
  context.check(frequencyRoute.reuseMode == ReuseMode::Frequency,
                "frequency route is selected explicitly");
  context.check(frequencyRoute.reuseWorkerCount == 8U,
                "frequency route reuse worker count is parsed");
  context.check(frequencyRoute.outputQueueCapacity == 2U,
                "frequency route output queue capacity is parsed");
  context.check(frequencyRoute.memoryBudgetMiB == 4096U,
                "frequency route memory budget is parsed");
  context.check(frequencyRoute.profileFrequencyTasks,
                "frequency route frequency-task profiling is selected");
  context.check(frequencyRoute.outputQueueCapacitySpecified &&
                    frequencyRoute.memoryBudgetSpecified,
                "frequency route tuning option presence is tracked");
  for (const std::string_view route : {"serial", "range"}) {
    context.expectThrows<ValidationError>(
        [&route] {
          static_cast<void>(parse({"root", "--execution-mode", "reuse",
                                   "--reuse-mode", route,
                                   "--output-queue-capacity", "2"}));
        },
        "output queue tuning is rejected outside the frequency route");
    context.expectThrows<ValidationError>(
        [&route] {
          static_cast<void>(parse({"root", "--execution-mode", "reuse",
                                   "--reuse-mode", route,
                                   "--memory-budget-mib", "4096"}));
        },
        "memory budget tuning is rejected outside the frequency route");
    context.expectThrows<ValidationError>(
        [&route] {
          static_cast<void>(parse({"root", "--execution-mode", "reuse",
                                   "--reuse-mode", route,
                                   "--profile-frequency-tasks"}));
        },
        "frequency-task profiling is rejected outside the frequency route");
  }
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(parse({"root", "--output-queue-capacity", "2"}));
      },
      "output queue tuning without reuse execution is rejected");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(parse({"root", "--memory-budget-mib", "4096"}));
      },
      "memory budget tuning without reuse execution is rejected");
  context.expectThrows<ValidationError>(
      [] { static_cast<void>(parse({"root", "--profile-frequency-tasks"})); },
      "frequency-task profiling without reuse execution is rejected");
}

void testInvalidArguments(Context& context) {
  context.expectThrows<ValidationError>([] { static_cast<void>(parse({})); },
                                        "missing root is rejected");
  context.expectThrows<ValidationError>(
      [] { static_cast<void>(parse({"root", "--frequencies-hz", "50,,100"})); },
      "empty frequency token is rejected");
  context.expectThrows<ValidationError>(
      [] { static_cast<void>(parse({"root", "--frequencies-hz", "100,50"})); },
      "descending frequency list is rejected");
  context.expectThrows<ValidationError>(
      [] { static_cast<void>(parse({"root", "--frequencies-hz", "50,50"})); },
      "duplicate frequency is rejected");
  context.expectThrows<ValidationError>(
      [] { static_cast<void>(parse({"root", "--unknown"})); },
      "unknown option is rejected");
  context.expectThrows<ValidationError>(
      [] { static_cast<void>(parse({"root", "--workers", "2"})); },
      "the legacy --workers option is rejected as unknown");
  context.expectThrows<ValidationError>(
      [] { static_cast<void>(parse({"root", "--range-parallel"})); },
      "the legacy --range-parallel option is rejected as unknown");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(parse({"root", "--execution-mode", "parallel"}));
      },
      "the parallel execution value is rejected");
  context.expectThrows<ValidationError>(
      [] { static_cast<void>(parse({"root", "--execution-mode", "fused"})); },
      "the fused execution value is rejected");
  context.expectThrows<ValidationError>(
      [] { static_cast<void>(parse({"root", "--execution-mode", "invalid"})); },
      "unknown execution mode is rejected");
  context.expectThrows<ValidationError>(
      [] { static_cast<void>(parse({"root", "--execution-mode"})); },
      "missing execution mode value is rejected");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(parse(
            {"root", "--execution-mode", "reuse", "--execution-mode",
             "nonreuse"}));
      },
      "duplicate execution mode option is rejected");
  context.expectThrows<ValidationError>(
      [] { static_cast<void>(parse({"root", "--trace-workers", "0"})); },
      "zero trace worker count is rejected");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(parse({"root", "--output-queue-capacity", "-1"}));
      },
      "negative queue capacity is rejected");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(parse(
            {"root", "--execution-mode", "reuse", "--reuse-mode", "frequency",
             "--output-queue-capacity", "3"}));
      },
      "queue capacity above the single-writer bound is rejected");
  context.expectThrows<ValidationError>(
      [] { static_cast<void>(parse({"root", "--memory-budget-mib", "1.5"})); },
      "non-integral memory budget is rejected");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(
            parse({"root", "--profile-influence", "--profile-influence"}));
      },
      "duplicate Influence profiling option is rejected");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(parse(
            {"root", "--execution-mode", "reuse", "--reuse-mode", "frequency",
             "--profile-frequency-tasks", "--profile-frequency-tasks"}));
      },
      "duplicate frequency-task profiling option is rejected");
  context.expectThrows<ValidationError>(
      [] { static_cast<void>(parse({"root", "--frequencies-hz"})); },
      "missing frequency list value is rejected");
}

void testHelp(Context& context) {
  const CommandLineOptions options = parse({"--help"});
  context.check(options.showHelp, "standalone help is accepted");
}

void testVersion(Context& context) {
  const CommandLineOptions options = parse({"--version"});
  context.check(options.showVersion, "standalone version is accepted");
}

}  // namespace

int main() {
  Context context;
  testDefaults(context);
  testFrequencyOverride(context);
  testExecutionMode(context);
  testReuseMode(context);
  testReuseWorkers(context);
  testFrequencyRouteTuning(context);
  testInvalidArguments(context);
  testHelp(context);
  testVersion(context);
  if (context.failureCount() != 0) {
    std::cerr << context.failureCount()
              << " command-line assertion(s) failed\n";
    return 1;
  }
  std::cout << "All Bellhop Broadband command-line tests passed\n";
  return 0;
}
