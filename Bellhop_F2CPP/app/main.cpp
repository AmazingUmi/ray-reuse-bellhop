#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>

#include "bellhop/error.hpp"
#include "bellhop/io/environment_parser.hpp"
#include "bellhop/io/shd_writer.hpp"
#include "bellhop/solver/single_frequency_solver.hpp"

namespace {

using Clock = std::chrono::steady_clock;

void printUsage(std::ostream& stream) {
  stream << "Usage: bellhop_f2cpp <file-root>\n"
         << "\n"
         << "Reads <file-root>.env and writes <file-root>.prt and "
            "<file-root>.shd.\n";
}

void writeConfigurationSummary(
    std::ostream& stream,
    const bellhop::ParsedEnvironment& parsed) {
  const bellhop::SimulationCase& simulation =
      parsed.simulationCase;
  const bellhop::Environment& environment =
      simulation.environment();
  stream << "BELLHOP F2CPP\n\n"
         << parsed.title << '\n'
         << "frequency = "
         << simulation.frequencies().values().front()
         << " Hz\n"
         << "Coherent TL calculation\n"
         << "Cartesian beams\n"
         << "Point source (cylindrical coordinates)\n"
         << "Rectilinear receiver grid\n"
         << "VACUUM sea surface\n";
  if (environment.seabed().kind() ==
      bellhop::BoundaryKind::Rigid) {
    stream << "Perfectly RIGID seabed\n";
  } else {
    stream << "ACOUSTO-ELASTIC half-space seabed\n";
  }
  if (environment.soundSpeedProfile()
          .points()
          .front()
          .attenuation.volumeModel ==
      bellhop::VolumeAttenuationModel::Thorp) {
    stream << "THORP volume attenuation added\n";
  }
  stream << "launch angles = "
         << simulation.launchFanPlan().launchAngleCount
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

}  // namespace

int main(int argumentCount, char* arguments[]) {
  if (argumentCount == 2 && std::string_view(arguments[1]) == "--help") {
    printUsage(std::cout);
    return 0;
  }

  if (argumentCount != 2) {
    printUsage(std::cerr);
    return 2;
  }

  const std::string fileRoot(arguments[1]);
  const std::filesystem::path environmentPath(
      fileRoot + ".env");
  const std::filesystem::path printPath(
      fileRoot + ".prt");
  const std::filesystem::path shadePath(
      fileRoot + ".shd");

  std::ofstream printLog(
      printPath, std::ios::out | std::ios::trunc);
  if (!printLog.is_open()) {
    std::cerr << "bellhop_f2cpp: unable to open print output: "
              << printPath << '\n';
    return 1;
  }
  printLog << std::setprecision(17);

  try {
    const bellhop::ParsedEnvironment parsed =
        bellhop::EnvironmentParser::parseFile(
            environmentPath);
    writeConfigurationSummary(printLog, parsed);

    const bellhop::SingleFrequencyResult result =
        bellhop::SingleFrequencySolver::solve(
            parsed.simulationCase,
            parsed.beam.epsilonMultiplier,
            parsed.beam.loopRange,
            parsed.beam.influence);

    const Clock::time_point writeBegin = Clock::now();
    bellhop::ShdWriter::writeSingleFrequency(
        shadePath, parsed.title,
        parsed.simulationCase, result.workspace);
    const Clock::time_point writeEnd = Clock::now();
    const double writeSeconds =
        std::chrono::duration<double>(
            writeEnd - writeBegin)
            .count();

    printLog
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
        << result.timings.scaleSeconds << '\n'
        << "SHD seconds = " << writeSeconds << '\n'
        << "Bellhop F2CPP completed successfully\n";
    printLog.close();
    if (!printLog) {
      throw bellhop::BellhopError(
          "failed to finalize print output: " +
          printPath.string());
    }
    return 0;
  } catch (const std::exception& error) {
    printLog << "\nFATAL ERROR: " << error.what() << '\n';
    printLog.close();
    std::cerr << "bellhop_f2cpp: " << error.what() << '\n';
    return 1;
  }
}
