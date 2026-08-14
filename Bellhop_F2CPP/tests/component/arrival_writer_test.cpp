#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numbers>
#include <string>
#include <vector>

#include "bellhop/error.hpp"
#include "bellhop/field/arrival_workspace.hpp"
#include "bellhop/io/arrival_writer.hpp"
#include "bellhop/model/simulation_case.hpp"
#include "support/test_harness.hpp"

namespace {

using bellhop::Arrival;
using bellhop::ArrivalCandidate;
using bellhop::ArrivalEncoding;
using bellhop::ArrivalWriter;
using bellhop::ArrivalWorkspace;
using bellhop::BoundaryModel;
using bellhop::Environment;
using bellhop::FrequencyGrid;
using bellhop::IntegratorSettings;
using bellhop::LaunchFan;
using bellhop::ReceiverGrid;
using bellhop::SimulationCase;
using bellhop::SimulationRunMode;
using bellhop::SoundSpeedPoint;
using bellhop::SoundSpeedProfile;
using bellhop::Source;
using bellhop::SourceGeometry;
using bellhop::ValidationError;
using bellhop::test::Context;

class TemporaryDirectory {
 public:
  TemporaryDirectory() {
    path_ = std::filesystem::temp_directory_path() /
            ("bellhop_f2cpp_arrival_writer_" +
             std::to_string(std::chrono::steady_clock::now()
                                .time_since_epoch()
                                .count()));
    std::filesystem::create_directory(path_);
  }
  ~TemporaryDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }
  [[nodiscard]] const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
};

SimulationCase makeSimulation(SimulationRunMode mode,
                              SourceGeometry geometry = SourceGeometry::Point) {
  return SimulationCase(
      Environment(
          SoundSpeedProfile({SoundSpeedPoint{.depth = 0.0,
                                               .soundSpeed = 1500.0,
                                               .density = 1000.0},
                            SoundSpeedPoint{.depth = 100.0,
                                             .soundSpeed = 1500.0,
                                             .density = 1000.0}}),
          BoundaryModel::vacuum(0.0), BoundaryModel::rigid(100.0)),
      Source{.depth = 50.0, .amplitude = 1.0},
      ReceiverGrid({20.0, 30.0}, {0.0, 100.0}), FrequencyGrid({50.0}),
      LaunchFan{.minimumAngle = -0.1,
                .maximumAngle = 0.1,
                .explicitLaunchAngleCount = 3U},
      IntegratorSettings{.stepLength = 10.0,
                         .rangeLimit = 200.0,
                         .depthLimit = 100.0,
                         .maximumRayPoints = 100U},
      bellhop::SourceBeamPattern::omnidirectional(), mode,
      bellhop::FieldComponent::Pressure, geometry,
      bellhop::CervenyCoordinateSystem::Cartesian,
      bellhop::BeamFamily::GeometricHat);
}

ArrivalWorkspace makeWorkspace(const SimulationCase& simulation) {
  ArrivalWorkspace workspace(simulation.frequencies().values().front(),
                             simulation.receivers());
  workspace.addCandidate(
      workspace.frequency(),
      ArrivalCandidate{.amplitude = 2.0,
                       .phaseRadians = 0.25,
                       .delaySeconds = {0.1, -0.02},
                       .sourceDeclinationDegrees = 10.0,
                       .receiverDeclinationDegrees = 20.0,
                       .topBounceCount = 1,
                       .bottomBounceCount = 2},
      0U, 0U);
  workspace.addCandidate(
      workspace.frequency(),
      ArrivalCandidate{.amplitude = 1.0,
                       .phaseRadians = -0.5,
                       .delaySeconds = {0.2, 0.01},
                       .sourceDeclinationDegrees = -10.0,
                       .receiverDeclinationDegrees = -20.0,
                       .topBounceCount = 3,
                       .bottomBounceCount = 4},
      1U, 1U);
  return workspace;
}

std::vector<std::byte> readBytes(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  const std::streamsize size = input.tellg();
  input.seekg(0);
  std::vector<std::byte> bytes(static_cast<std::size_t>(size));
  input.read(reinterpret_cast<char*>(bytes.data()), size);
  return bytes;
}

std::int32_t loadInt32(const std::vector<std::byte>& bytes,
                       std::size_t offset) {
  std::uint32_t value = 0U;
  for (std::size_t index = 0U; index < 4U; ++index) {
    value |= static_cast<std::uint32_t>(
                 std::to_integer<unsigned int>(bytes.at(offset + index)))
             << (8U * index);
  }
  return std::bit_cast<std::int32_t>(value);
}

void testAscii(Context& context) {
  TemporaryDirectory directory;
  const SimulationCase simulation = makeSimulation(SimulationRunMode::AsciiArrivals);
  ArrivalWorkspace workspace = makeWorkspace(simulation);
  const Arrival before = workspace.arrivalsAt(0U, 0U).front();
  const auto path = directory.path() / "fixture.arr";
  {
    ArrivalWriter writer(path, simulation);
    context.check(writer.layout().actualCellsPerSource == 4U,
                  "ASCII layout uses depth-major/range-minor cells");
    writer.appendSource(0U, workspace);
    writer.finalize();
  }
  std::ifstream input(path);
  std::string text((std::istreambuf_iterator<char>(input)), {});
  context.check(text.find("'2D'") == 0U, "ASCII header contains 2D tag");
  context.check(text.find("100000") != std::string::npos,
                "ASCII point source uses the zero-range factor");
  context.check(text.find("14.3239") != std::string::npos,
                "ASCII phase is converted to degrees");
  const Arrival after = workspace.arrivalsAt(0U, 0U).front();
  context.check(std::memcmp(&before, &after, sizeof(Arrival)) == 0,
                "ASCII writing does not mutate the workspace");
  context.expectThrows<ValidationError>(
      [&] { ArrivalWriter bad(path, simulation, ArrivalEncoding::Binary); },
      "binary encoding is rejected for ASCII run mode");
}

void testBinaryAndLifecycle(Context& context) {
  TemporaryDirectory directory;
  const SimulationCase simulation = makeSimulation(SimulationRunMode::BinaryArrivals,
                                                    SourceGeometry::Line);
  ArrivalWorkspace workspace = makeWorkspace(simulation);
  const auto path = directory.path() / "fixture.arr";
  {
    ArrivalWriter writer(path, simulation, ArrivalEncoding::Binary);
    writer.appendSource(0U, workspace);
    writer.finalize();
  }
  const std::vector<std::byte> bytes = readBytes(path);
  context.check(loadInt32(bytes, 0U) == 4 &&
                    std::to_integer<unsigned char>(bytes.at(4U)) == '\'',
                "binary header starts with a four-byte 2D tag record");
  context.check(std::filesystem::exists(path) &&
                    !std::filesystem::exists(path.string() + ".tmp"),
                "binary finalize atomically publishes and removes temporary");

  const auto incomplete = directory.path() / "incomplete.arr";
  context.expectThrows<ValidationError>(
      [&] {
        ArrivalWriter writer(incomplete, simulation);
        writer.finalize();
      },
      "incomplete source stream is rejected");
  context.check(!std::filesystem::exists(incomplete.string() + ".tmp"),
                "failed incomplete stream leaves no temporary ARR");
}

}  // namespace

int main() {
  Context context;
  testAscii(context);
  testBinaryAndLifecycle(context);
  if (context.failureCount() != 0) {
    std::cerr << context.failureCount() << " test assertion(s) failed\n";
    return 1;
  }
  std::cout << "All Bellhop F2CPP arrival writer tests passed\n";
  return 0;
}
