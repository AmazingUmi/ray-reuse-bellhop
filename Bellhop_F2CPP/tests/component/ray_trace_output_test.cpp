#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "bellhop/error.hpp"
#include "bellhop/io/environment_parser.hpp"
#include "bellhop/io/ray_writer.hpp"
#include "bellhop/model/simulation_case.hpp"
#include "bellhop/solver/ray_trace_solver.hpp"
#include "support/test_harness.hpp"

namespace {

using bellhop::EnvironmentParser;
using bellhop::BellhopError;
using bellhop::BoundaryModel;
using bellhop::Environment;
using bellhop::LaunchFan;
using bellhop::ParsedEnvironment;
using bellhop::RayPathCache;
using bellhop::RayTraceSolver;
using bellhop::RayTraceStatistics;
using bellhop::RayWriter;
using bellhop::ReflectionBoundary;
using bellhop::SimulationRunMode;
using bellhop::SimulationCase;
using bellhop::SourceBeamPattern;
using bellhop::SourceBeamPatternSample;
using bellhop::ValidationError;
using bellhop::Vec2;
using bellhop::test::Context;

class TemporaryDirectory {
 public:
  TemporaryDirectory() {
    const auto suffix =
        std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() /
            ("bellhop_f2cpp_ray_trace_" + std::to_string(suffix));
    std::filesystem::create_directory(path_);
  }

  TemporaryDirectory(const TemporaryDirectory&) = delete;
  TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

  ~TemporaryDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

  [[nodiscard]] const std::filesystem::path& path() const noexcept {
    return path_;
  }

 private:
  std::filesystem::path path_;
};

std::string rayTraceEnvironment(bool includeTransmissionLossTail = false) {
  std::ostringstream stream;
  stream << "'Two-source ray trace'\n"
         << "1000.0\n"
         << "1\n"
         << "'CVW'\n"
         << "2 0.0 100.0\n"
         << "0.0 1500.0 /\n"
         << "100.0 1500.0 /\n"
         << "'R' 0.0\n"
         << "2\n"
         << "75.0 25.0 /\n"
         << "2\n"
         << "20.0 80.0 /\n"
         << "2\n"
         << "0.1 0.2 /\n"
         << "'R'\n"
         << "3\n"
         << "-60.0 60.0 /\n"
         << "10.0 110.0 0.22\n";
  if (includeTransmissionLossTail) {
    stream << "'MS' 1.0 0.1\n"
           << "1 5 'P'\n";
  }
  return stream.str();
}

ParsedEnvironment parseRayTraceEnvironment(bool includeTail = false) {
  std::istringstream input(rayTraceEnvironment(includeTail));
  return EnvironmentParser::parse(input, "ray_trace.env");
}

struct ExpectedRay {
  std::size_t topBounces{};
  std::size_t bottomBounces{};
};

struct WrittenRay {
  double launchAngleDegrees{};
  std::size_t pointCount{};
  std::size_t topBounces{};
  std::size_t bottomBounces{};
  std::vector<Vec2> points;
};

struct WrittenRayFile {
  std::string title;
  double frequency{};
  std::size_t sourceXCount{};
  std::size_t sourceYCount{};
  std::size_t sourceDepthCount{};
  std::size_t launchAngleCount{};
  std::size_t bearingCount{};
  double topDepth{};
  double bottomDepth{};
  std::string coordinateSystem;
  std::vector<WrittenRay> rays;
};

WrittenRayFile readRayFile(const std::filesystem::path& path,
                           std::size_t expectedRayCount) {
  std::ifstream input(path);
  if (!input.is_open()) {
    throw std::runtime_error("unable to open ray test output");
  }
  WrittenRayFile file;
  std::getline(input, file.title);
  if (file.title.size() < 2U || file.title.front() != '\'' ||
      file.title.back() != '\'') {
    throw std::runtime_error("ray test output has a malformed title");
  }
  file.title = file.title.substr(1U, file.title.size() - 2U);
  input >> file.frequency >> file.sourceXCount >> file.sourceYCount >>
      file.sourceDepthCount >> file.launchAngleCount >> file.bearingCount >>
      file.topDepth >> file.bottomDepth;
  input >> std::ws;
  std::getline(input, file.coordinateSystem);
  if (file.coordinateSystem.size() < 2U ||
      file.coordinateSystem.front() != '\'' ||
      file.coordinateSystem.back() != '\'') {
    throw std::runtime_error(
        "ray test output has a malformed coordinate system");
  }
  file.coordinateSystem =
      file.coordinateSystem.substr(1U, file.coordinateSystem.size() - 2U);
  file.rays.reserve(expectedRayCount);
  for (std::size_t rayIndex = 0U; rayIndex < expectedRayCount; ++rayIndex) {
    WrittenRay ray;
    input >> ray.launchAngleDegrees >> ray.pointCount >> ray.topBounces >>
        ray.bottomBounces;
    ray.points.resize(ray.pointCount);
    for (Vec2& point : ray.points) {
      input >> point.range >> point.depth;
    }
    file.rays.push_back(std::move(ray));
  }
  std::string trailing;
  input >> trailing;
  if (!input.eof() || !trailing.empty()) {
    throw std::runtime_error("ray test output has malformed or trailing data");
  }
  return file;
}

std::size_t countBounces(const bellhop::RayPath& path,
                         ReflectionBoundary boundary) {
  return static_cast<std::size_t>(std::count_if(
      path.events.begin(), path.events.end(),
      [boundary](const bellhop::ReflectionEvent& event) {
        return event.boundary == boundary;
      }));
}

void testRayRunParserDoesNotConsumeBeamLines(Context& context) {
  const ParsedEnvironment parsed = parseRayTraceEnvironment();
  context.check(
      parsed.simulationCase.runMode() == SimulationRunMode::RayTrace &&
          parsed.simulationCase.sourceCount() == 2U &&
          parsed.simulationCase.sources()[0U].depth == 25.0 &&
          parsed.simulationCase.sources()[1U].depth == 75.0,
      "R mode parses and sorts two source depths without a TL beam tail");
  context.check(
      parsed.beam.epsilonMultiplier == 1.0 &&
          parsed.beam.loopRange == 1.0 &&
          parsed.beam.influence.imageCount == 1U &&
          parsed.beam.influence.beamWindow == 1,
      "R mode leaves Cartesian Cerveny settings at inert defaults");
  context.expectThrows<ValidationError>(
      [] { static_cast<void>(parseRayTraceEnvironment(true)); },
      "R mode rejects trailing MS/image records instead of consuming them");
}

void testTwoSourceRayTraceFile(Context& context) {
  const ParsedEnvironment parsed = parseRayTraceEnvironment();
  const auto& simulation = parsed.simulationCase;
  const std::size_t raysPerSource =
      simulation.launchFanPlan().launchAngleCount;
  const std::size_t expectedRayCount =
      raysPerSource * simulation.sourceCount();

  TemporaryDirectory directory;
  const std::filesystem::path outputPath = directory.path() / "case.ray";
  RayWriter writer(outputPath, parsed.title, simulation);
  std::vector<ExpectedRay> expected;
  expected.reserve(expectedRayCount);
  std::vector<std::size_t> observedSourceIndices;
  const RayTraceStatistics statistics = RayTraceSolver::trace(
      simulation,
      [&](std::size_t sourceIndex, const RayPathCache& cache) {
        observedSourceIndices.push_back(sourceIndex);
        context.check(cache.frozen() && cache.size() == raysPerSource,
                      "ray solver exposes one frozen complete fan per source");
        for (const auto& path : cache.paths()) {
          expected.push_back(
              ExpectedRay{
                  .topBounces =
                      countBounces(path, ReflectionBoundary::SeaSurface),
                  .bottomBounces =
                      countBounces(path, ReflectionBoundary::Seabed)});
        }
        writer.appendSource(sourceIndex, cache);
      });
  writer.finalize();

  context.check(observedSourceIndices == std::vector<std::size_t>{0U, 1U},
                "ray solver delivers source caches in ascending source order");
  context.check(statistics.rayCount == expectedRayCount &&
                    statistics.totalRayPointCount > statistics.rayCount &&
                    statistics.peakRayCacheBytes > 0U &&
                    statistics.traceSeconds >= 0.0 &&
                    statistics.writeSeconds >= 0.0,
                "ray solver reports two-source trace statistics");

  const WrittenRayFile file = readRayFile(outputPath, expectedRayCount);
  context.check(
      file.title == "BELLHOP- Two-source ray trace" &&
      file.frequency == 1000.0 &&
          file.sourceXCount == 1U && file.sourceYCount == 1U &&
          file.sourceDepthCount == 2U &&
          file.launchAngleCount == raysPerSource &&
          file.bearingCount == 1U && file.topDepth == 0.0 &&
          file.bottomDepth == 100.0 && file.coordinateSystem == "rz",
      "ray writer emits the canonical 2-D header and two-source dimensions");
  context.check(file.rays.size() == expected.size(),
                "ray file contains one launch fan for each source");

  bool sawReflection = false;
  bool sawIncidentReflectedDuplicate = false;
  for (std::size_t rayIndex = 0U; rayIndex < file.rays.size(); ++rayIndex) {
    const WrittenRay& ray = file.rays[rayIndex];
    const double expectedSourceDepth =
        rayIndex < raysPerSource ? 25.0 : 75.0;
    context.check(!ray.points.empty() &&
                      ray.points.front().range == 0.0 &&
                      ray.points.front().depth == expectedSourceDepth,
                  "ray blocks preserve source-major ordering");
    context.check(ray.topBounces == expected[rayIndex].topBounces &&
                      ray.bottomBounces == expected[rayIndex].bottomBounces,
                  "ray block bounce counts match frozen reflection events");

    const std::size_t bounceCount = ray.topBounces + ray.bottomBounces;
    std::size_t duplicateCount = 0U;
    for (std::size_t pointIndex = 1U;
         pointIndex < ray.points.size(); ++pointIndex) {
      if (ray.points[pointIndex] == ray.points[pointIndex - 1U]) {
        ++duplicateCount;
      }
    }
    if (bounceCount > 0U) {
      sawReflection = true;
      context.check(duplicateCount == bounceCount,
                    "each bounce writes its incident/reflected duplicate pair");
      sawIncidentReflectedDuplicate =
          sawIncidentReflectedDuplicate || duplicateCount > 0U;
    }
  }
  context.check(sawReflection,
                "ray fixture contains at least one reflected trajectory");
  context.check(sawIncidentReflectedDuplicate,
                "ray output retains an observable pre/post reflection point");
}

void testRaySolverEnforcesTheSafeLibrarySubset(Context& context) {
  const SimulationCase& baseline =
      parseRayTraceEnvironment().simulationCase;
  const LaunchFan launchFan{
      .minimumAngle = baseline.launchFanPlan().launchAngles.front(),
      .maximumAngle = baseline.launchFanPlan().launchAngles.back(),
      .explicitLaunchAngleCount =
          baseline.launchFanPlan().launchAngleCount};
  const auto consumer = [](std::size_t, const RayPathCache&) {};

  const SimulationCase directional(
      baseline.environment(), baseline.sources(), baseline.receivers(),
      baseline.frequencies(), launchFan, baseline.integrator(),
      SourceBeamPattern::directional(
          std::vector<SourceBeamPatternSample>{{-60.0, 0.0},
                                               {60.0, 0.0}}),
      SimulationRunMode::RayTrace);
  context.expectThrows<ValidationError>(
      [&directional, &consumer]() {
        static_cast<void>(RayTraceSolver::trace(directional, consumer));
      },
      "ray solver rejects a directional pattern constructed outside parser");

  const Environment nonRigidEnvironment(
      baseline.environment().soundSpeedProfile(),
      baseline.environment().seaSurface(),
      BoundaryModel::grainSizeHalfSpace(
          baseline.environment().seabed().depth(), 3.0),
      baseline.environment().volumeAttenuation());
  const SimulationCase nonRigid(
      nonRigidEnvironment, baseline.sources(), baseline.receivers(),
      baseline.frequencies(), launchFan, baseline.integrator(),
      SourceBeamPattern::omnidirectional(), SimulationRunMode::RayTrace);
  context.expectThrows<ValidationError>(
      [&nonRigid, &consumer]() {
        static_cast<void>(RayTraceSolver::trace(nonRigid, consumer));
      },
      "ray solver rejects a lossy boundary constructed outside parser");
}

void testRayWriterCleansTemporaryOutputOnFailure(Context& context) {
  const SimulationCase& baseline =
      parseRayTraceEnvironment().simulationCase;
  const LaunchFan launchFan{
      .minimumAngle = baseline.launchFanPlan().launchAngles.front(),
      .maximumAngle = baseline.launchFanPlan().launchAngles.back(),
      .explicitLaunchAngleCount =
          baseline.launchFanPlan().launchAngleCount};
  const SimulationCase coherent(
      baseline.environment(), baseline.sources(), baseline.receivers(),
      baseline.frequencies(), launchFan, baseline.integrator());
  TemporaryDirectory directory;
  const std::filesystem::path rejected = directory.path() / "rejected.ray";
  context.expectThrows<ValidationError>(
      [&] { RayWriter writer(rejected, "wrong mode", coherent); },
      "ray writer rejects coherent mode before opening a temp file");
  context.check(
      !std::filesystem::exists(rejected.string() + ".tmp"),
      "ray writer mode validation leaves no temporary output");

  const std::filesystem::path destination = directory.path() / "blocked.ray";
  std::filesystem::create_directory(destination);
  const std::filesystem::path sentinel = destination / "keep.txt";
  {
    std::ofstream output(sentinel);
    output << "keep";
  }
  {
    RayWriter writer(destination, "publish failure", baseline);
    static_cast<void>(RayTraceSolver::trace(
        baseline,
        [&writer](std::size_t sourceIndex, const RayPathCache& cache) {
          writer.appendSource(sourceIndex, cache);
        }));
    context.expectThrows<BellhopError>(
        [&writer] { writer.finalize(); },
        "ray writer reports a failed final publish");
  }
  context.check(
      std::filesystem::is_directory(destination) &&
          std::filesystem::is_regular_file(sentinel) &&
          !std::filesystem::exists(destination.string() + ".tmp"),
      "failed ray publication preserves destination and removes temp");
}

}  // namespace

int main() {
  Context context;
  testRayRunParserDoesNotConsumeBeamLines(context);
  testTwoSourceRayTraceFile(context);
  testRaySolverEnforcesTheSafeLibrarySubset(context);
  testRayWriterCleansTemporaryOutputOnFailure(context);
  if (context.failureCount() != 0) {
    std::cerr << context.failureCount()
              << " ray-trace output assertion(s) failed\n";
    return 1;
  }
  std::cout << "All Bellhop F2CPP ray-trace output tests passed\n";
  return 0;
}
