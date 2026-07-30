#include <chrono>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "rayreuse/error.hpp"
#include "rayreuse/io/command_line.hpp"
#include "rayreuse/io/environment_parser.hpp"
#include "rayreuse/io/shd_writer.hpp"
#include "rayreuse/solver/broadband_nonreuse_solver.hpp"
#include "rayreuse/solver/parallel_ray_reuse_solver.hpp"
#include "rayreuse/solver/serial_ray_reuse_solver.hpp"
#include "rayreuse/solver/single_frequency_solver.hpp"

namespace {

using Clock = std::chrono::steady_clock;

void printUsage(std::ostream& stream) {
  stream << "Usage: bellhop_rayreuse <file-root> "
            "[--frequencies-hz <f0,f1,...>] "
            "[--execution-mode <nonreuse|reuse|parallel>] "
            "[--verify-cache] [--workers <count>] "
            "[--output-queue-capacity <count>] "
            "[--memory-budget-mib <MiB>]\n"
         << "\n"
         << "Reads <file-root>.env and writes <file-root>.prt and "
            "<file-root>.shd.\n"
         << "Without --frequencies-hz, the scalar frequency in the "
            ".env file is used.\n"
         << "With --frequencies-hz, the strictly increasing list "
            "overrides the .env frequency.\n"
         << "Broadband execution defaults to nonreuse; use "
            "--execution-mode reuse for serial trace-once or parallel "
            "for bounded frequency concurrency.\n"
         << "--verify-cache hashes the complete frozen ray cache before "
            "and after projection and is intended for validation.\n"
         << "Parallel tuning options require --execution-mode parallel; "
            "worker count defaults to hardware concurrency, the output "
            "queue defaults to 2, and a zero/unset memory budget means "
            "no explicit budget.\n";
}

void writeConfigurationSummary(
    std::ostream& stream,
    const rayreuse::ParsedEnvironment& parsed) {
  const rayreuse::SimulationCase& simulation =
      parsed.simulationCase;
  const rayreuse::Environment& environment =
      simulation.environment();
  stream << "BELLHOP RAYREUSE\n\n"
         << parsed.title << '\n';
  if (simulation.frequencies().size() == 1U) {
    stream << "frequency = "
           << simulation.frequencies().values().front()
           << " Hz\n";
  } else {
    stream << "frequency count = "
           << simulation.frequencies().size() << '\n'
           << "frequencies Hz =";
    for (const double frequency :
         simulation.frequencies().values()) {
      stream << ' ' << frequency;
    }
    stream << '\n';
  }
  stream << "design frequency = "
         << simulation.frequencies().designFrequency()
         << " Hz\n"
         << "Coherent TL calculation\n"
         << "Cartesian beams\n"
         << "Point source (cylindrical coordinates)\n"
         << "Rectilinear receiver grid\n"
         << "VACUUM sea surface\n";
  if (environment.seabed().kind() ==
      rayreuse::BoundaryKind::Rigid) {
    stream << "Perfectly RIGID seabed\n";
  } else {
    stream << "ACOUSTO-ELASTIC half-space seabed\n";
  }
  if (environment.soundSpeedProfile()
          .points()
          .front()
          .attenuation.volumeModel ==
      rayreuse::VolumeAttenuationModel::Thorp) {
    stream << "THORP volume attenuation added\n";
  }
  stream << "launch angles = "
         << simulation.launchFanPlan().launchAngleCount
         << '\n'
         << "phase criterion angles = "
         << simulation.launchFanPlan().phaseCriterionCount
         << '\n'
         << "depth criterion angles = "
         << simulation.launchFanPlan().depthCriterionCount
         << '\n'
         << "sufficiency check angles = "
         << simulation.launchFanPlan().minimumRecommendedAngleCount
         << '\n'
         << "receiver depths = "
         << simulation.receivers().depthCount() << '\n'
         << "receiver ranges = "
         << simulation.receivers().rangeCount() << '\n'
         << "step length = "
         << simulation.integrator().stepLength << " m\n"
         << "range limit = "
         << simulation.integrator().rangeLimit << " m\n"
         << "depth limit = "
         << simulation.integrator().depthLimit << " m\n\n";
}

void writeSingleFrequencySummary(
    std::ostream& stream,
    const rayreuse::SingleFrequencyResult& result) {
  stream
      << "frequency result = "
      << result.workspace.frequency() << " Hz\n"
      << "ray count = " << result.rayCount << '\n'
      << "ray point count = "
      << result.totalRayPointCount << '\n'
      << "ray cache bytes = "
      << result.rayCacheBytes << '\n'
      << "Trace seconds = "
      << result.timings.traceSeconds << '\n'
      << "Project seconds = "
      << result.timings.projectSeconds << '\n'
      << "Influence seconds = "
      << result.timings.influenceSeconds << '\n'
      << "Scale seconds = "
      << result.timings.scaleSeconds << '\n';
}

[[nodiscard]] std::size_t resolvedWorkerCount(
    std::size_t requestedWorkerCount) {
  if (requestedWorkerCount != 0U) {
    return requestedWorkerCount;
  }
  const unsigned int hardwareCount =
      std::thread::hardware_concurrency();
  return hardwareCount == 0U
             ? 1U
             : static_cast<std::size_t>(hardwareCount);
}

[[nodiscard]] std::size_t memoryBudgetBytes(
    std::size_t memoryBudgetMiB) {
  constexpr std::size_t bytesPerMiB = 1024U * 1024U;
  if (memoryBudgetMiB >
      std::numeric_limits<std::size_t>::max() / bytesPerMiB) {
    throw rayreuse::ValidationError(
        "--memory-budget-mib exceeds the platform size limit");
  }
  return memoryBudgetMiB * bytesPerMiB;
}

}  // namespace

int main(int argumentCount, char* arguments[]) {
  std::vector<std::string_view> argumentViews;
  argumentViews.reserve(
      argumentCount > 0
          ? static_cast<std::size_t>(argumentCount - 1)
          : 0U);
  for (int index = 1; index < argumentCount; ++index) {
    argumentViews.emplace_back(arguments[index]);
  }

  rayreuse::CommandLineOptions options;
  try {
    options = rayreuse::parseCommandLine(argumentViews);
  } catch (const std::exception& error) {
    printUsage(std::cerr);
    std::cerr << "bellhop_rayreuse: " << error.what() << '\n';
    return 2;
  }
  if (options.showHelp) {
    printUsage(std::cout);
    return 0;
  }

  const std::string& fileRoot = options.fileRoot;
  const std::filesystem::path environmentPath(
      fileRoot + ".env");
  const std::filesystem::path printPath(
      fileRoot + ".prt");
  const std::filesystem::path shadePath(
      fileRoot + ".shd");

  std::ofstream printLog(
      printPath, std::ios::out | std::ios::trunc);
  if (!printLog.is_open()) {
    std::cerr << "bellhop_rayreuse: unable to open print output: "
              << printPath << '\n';
    return 1;
  }
  printLog << std::setprecision(17);

  try {
    const rayreuse::ParsedEnvironment parsed =
        rayreuse::EnvironmentParser::parseFile(
            environmentPath,
            std::move(options.frequencyOverrideHz));
    writeConfigurationSummary(printLog, parsed);

    const Clock::time_point solveBegin = Clock::now();
    if (parsed.simulationCase.frequencies().size() == 1U) {
      const rayreuse::SingleFrequencyResult result =
          rayreuse::SingleFrequencySolver::solve(
              parsed.simulationCase,
              parsed.beam.epsilonMultiplier,
              parsed.beam.loopRange,
              parsed.beam.influence);

      const Clock::time_point writeBegin = Clock::now();
      rayreuse::ShdWriter::writeSingleFrequency(
          shadePath, parsed.title,
          parsed.simulationCase, result.workspace);
      const double writeSeconds =
          std::chrono::duration<double>(
              Clock::now() - writeBegin)
              .count();

      printLog << "execution mode = single-frequency\n"
               << "Trace passes = 1\n";
      writeSingleFrequencySummary(printLog, result);
      printLog << "SHD seconds = " << writeSeconds << '\n';
    } else if (
        options.executionMode ==
        rayreuse::BroadbandExecutionMode::NonReuse) {
      rayreuse::BroadbandNonReuseResult result =
          rayreuse::BroadbandNonReuseSolver::solve(
              parsed.simulationCase,
              parsed.beam.epsilonMultiplier,
              parsed.beam.loopRange,
              parsed.beam.influence);

      std::vector<rayreuse::FrequencyWorkspace> workspaces;
      workspaces.reserve(result.frequencyResults.size());
      for (rayreuse::SingleFrequencyResult& frequencyResult :
           result.frequencyResults) {
        workspaces.push_back(
            std::move(frequencyResult.workspace));
      }

      const Clock::time_point writeBegin = Clock::now();
      rayreuse::ShdWriter::writeFrequencies(
          shadePath, parsed.title,
          parsed.simulationCase, workspaces);
      const double writeSeconds =
          std::chrono::duration<double>(
              Clock::now() - writeBegin)
              .count();

      printLog
          << "execution mode = broadband non-reuse\n"
          << "Trace passes = "
          << result.statistics.tracePassCount << '\n'
          << "total ray count = "
          << result.statistics.totalRayCount << '\n'
          << "total ray point count = "
          << result.statistics.totalRayPointCount << '\n'
          << "cumulative ray cache bytes = "
          << result.statistics.cumulativeRayCacheBytes << '\n'
          << "peak ray cache bytes = "
          << result.statistics.peakRayCacheBytes << '\n'
          << "Trace seconds = "
          << result.statistics.phaseTotals.traceSeconds << '\n'
          << "Project seconds = "
          << result.statistics.phaseTotals.projectSeconds << '\n'
          << "Influence seconds = "
          << result.statistics.phaseTotals.influenceSeconds << '\n'
          << "Scale seconds = "
          << result.statistics.phaseTotals.scaleSeconds << '\n'
          << "non-reuse wall seconds = "
          << result.statistics.wallSeconds << '\n'
          << "SHD seconds = " << writeSeconds << '\n';
    } else if (
        options.executionMode ==
        rayreuse::BroadbandExecutionMode::Reuse) {
      double writeSeconds = 0.0;
      const Clock::time_point writerSetupBegin = Clock::now();
      rayreuse::ShdFrequencyWriter writer(
          shadePath, parsed.title, parsed.simulationCase);
      writeSeconds +=
          std::chrono::duration<double>(
              Clock::now() - writerSetupBegin)
              .count();
      const rayreuse::RayReuseFrequencyConsumer consumer =
          [&](std::size_t frequencyIndex,
              rayreuse::FrequencyWorkspace&& workspace,
              const rayreuse::SingleFrequencyTimings&) {
            const Clock::time_point writeBegin = Clock::now();
            writer.writeFrequency(frequencyIndex, workspace);
            writeSeconds +=
                std::chrono::duration<double>(
                    Clock::now() - writeBegin)
                    .count();
          };
      const rayreuse::SerialRayReuseStatistics statistics =
          rayreuse::SerialRayReuseSolver::solveStreaming(
              parsed.simulationCase,
              parsed.beam.epsilonMultiplier,
              parsed.beam.loopRange,
              consumer,
              parsed.beam.influence,
              options.verifyCache);
      const Clock::time_point finalizeBegin = Clock::now();
      writer.finalize();
      writeSeconds +=
          std::chrono::duration<double>(
              Clock::now() - finalizeBegin)
              .count();

      printLog
          << "execution mode = broadband reuse\n"
          << "Trace passes = "
          << statistics.tracePassCount << '\n'
          << "ray count = "
          << statistics.rayCount << '\n'
          << "ray point count = "
          << statistics.totalRayPointCount << '\n'
          << "ray cache bytes = "
          << statistics.rayCacheBytes << '\n'
          << "Trace seconds = "
          << statistics.phaseTotals.traceSeconds << '\n'
          << "Project seconds = "
          << statistics.phaseTotals.projectSeconds << '\n'
          << "Influence seconds = "
          << statistics.phaseTotals.influenceSeconds << '\n'
          << "Scale seconds = "
          << statistics.phaseTotals.scaleSeconds << '\n'
          << "reuse wall seconds = "
          << statistics.wallSeconds << '\n'
          << "SHD seconds = " << writeSeconds << '\n';
      if (statistics.cacheFingerprintVerified) {
        printLog
            << "cache fingerprint verification = enabled\n"
            << "cache fingerprint before = "
            << statistics.cacheFingerprintBefore << '\n'
            << "cache fingerprint after = "
            << statistics.cacheFingerprintAfter << '\n';
      } else {
        printLog
            << "cache fingerprint verification = disabled\n";
      }
    } else {
      double writeSeconds = 0.0;
      const Clock::time_point writerSetupBegin = Clock::now();
      rayreuse::ShdFrequencyWriter writer(
          shadePath, parsed.title, parsed.simulationCase);
      writeSeconds +=
          std::chrono::duration<double>(
              Clock::now() - writerSetupBegin)
              .count();
      const rayreuse::RayReuseFrequencyConsumer consumer =
          [&](std::size_t frequencyIndex,
              rayreuse::FrequencyWorkspace&& workspace,
              const rayreuse::SingleFrequencyTimings&) {
            const Clock::time_point writeBegin = Clock::now();
            writer.writeFrequency(frequencyIndex, workspace);
            writeSeconds +=
                std::chrono::duration<double>(
                    Clock::now() - writeBegin)
                    .count();
          };
      const rayreuse::ParallelRayReuseSettings settings{
          .workerCount =
              resolvedWorkerCount(options.workerCount),
          .outputQueueCapacity =
              options.outputQueueCapacity,
          .memoryBudgetBytes =
              memoryBudgetBytes(options.memoryBudgetMiB),
      };
      const rayreuse::ParallelRayReuseStatistics statistics =
          rayreuse::ParallelRayReuseSolver::solveStreaming(
              parsed.simulationCase,
              parsed.beam.epsilonMultiplier,
              parsed.beam.loopRange,
              consumer,
              settings,
              parsed.beam.influence,
              options.verifyCache);
      const Clock::time_point finalizeBegin = Clock::now();
      writer.finalize();
      writeSeconds +=
          std::chrono::duration<double>(
              Clock::now() - finalizeBegin)
              .count();

      printLog
          << "execution mode = broadband parallel reuse\n"
          << "Trace passes = "
          << statistics.tracePassCount << '\n'
          << "ray count = "
          << statistics.rayCount << '\n'
          << "ray point count = "
          << statistics.totalRayPointCount << '\n'
          << "ray cache bytes = "
          << statistics.rayCacheBytes << '\n'
          << "requested worker count = "
          << statistics.requestedWorkerCount << '\n'
          << "active frequency limit = "
          << statistics.activeFrequencyLimit << '\n'
          << "output queue capacity = "
          << statistics.outputQueueCapacity << '\n'
          << "peak queued results = "
          << statistics.peakQueuedResults << '\n'
          << "estimated workspace bytes = "
          << statistics.estimatedWorkspaceBytes << '\n'
          << "estimated peak memory bytes = "
          << statistics.estimatedPeakMemoryBytes << '\n'
          << "memory budget bytes = "
          << statistics.memoryBudgetBytes << '\n'
          << "Trace seconds = "
          << statistics.phaseTotals.traceSeconds << '\n'
          << "Project seconds = "
          << statistics.phaseTotals.projectSeconds << '\n'
          << "Influence seconds = "
          << statistics.phaseTotals.influenceSeconds << '\n'
          << "Scale seconds = "
          << statistics.phaseTotals.scaleSeconds << '\n'
          << "parallel reuse wall seconds = "
          << statistics.wallSeconds << '\n'
          << "SHD seconds = " << writeSeconds << '\n';
      if (statistics.cacheFingerprintVerified) {
        printLog
            << "cache fingerprint verification = enabled\n"
            << "cache fingerprint before = "
            << statistics.cacheFingerprintBefore << '\n'
            << "cache fingerprint after = "
            << statistics.cacheFingerprintAfter << '\n';
      } else {
        printLog
            << "cache fingerprint verification = disabled\n";
      }
    }

    printLog
        << "Total solver and SHD seconds = "
        << std::chrono::duration<double>(
               Clock::now() - solveBegin)
               .count()
        << '\n'
        << "Bellhop RayReuse completed successfully\n";
    printLog.close();
    if (!printLog) {
      throw rayreuse::BellhopError(
          "failed to finalize print output: " +
          printPath.string());
    }
    return 0;
  } catch (const std::exception& error) {
    printLog << "\nFATAL ERROR: " << error.what() << '\n';
    printLog.close();
    std::cerr << "bellhop_rayreuse: " << error.what() << '\n';
    return 1;
  }
}
