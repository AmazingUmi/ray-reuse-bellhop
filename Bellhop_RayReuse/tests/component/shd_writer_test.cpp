#include "rayreuse/io/shd_writer.hpp"

#include <bit>
#include <chrono>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <numbers>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "rayreuse/error.hpp"
#include "rayreuse/model/simulation_case.hpp"
#include "support/test_harness.hpp"

namespace {

using rayreuse::BoundaryModel;
using rayreuse::Environment;
using rayreuse::FrequencyGrid;
using rayreuse::FrequencyWorkspace;
using rayreuse::IntegratorSettings;
using rayreuse::LaunchFan;
using rayreuse::ReceiverGrid;
using rayreuse::ShdFrequencyWriter;
using rayreuse::ShdWriter;
using rayreuse::SimulationCase;
using rayreuse::SoundSpeedPoint;
using rayreuse::SoundSpeedProfile;
using rayreuse::Source;
using rayreuse::ValidationError;
using rayreuse::test::Context;

static_assert(!std::is_copy_constructible_v<ShdFrequencyWriter>);
static_assert(!std::is_copy_assignable_v<ShdFrequencyWriter>);
static_assert(std::is_nothrow_move_constructible_v<ShdFrequencyWriter>);
static_assert(std::is_nothrow_move_assignable_v<ShdFrequencyWriter>);
static_assert(std::is_nothrow_destructible_v<ShdFrequencyWriter>);

class TemporaryDirectory {
 public:
  TemporaryDirectory() {
    const auto suffix =
        std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() /
            ("bellhop_rayreuse_shd_" + std::to_string(suffix));
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

template <typename Unsigned>
Unsigned loadLittleEndian(const std::vector<std::byte>& bytes,
                          std::size_t offset) {
  Unsigned value{};
  for (std::size_t index = 0U; index < sizeof(Unsigned); ++index) {
    value |= static_cast<Unsigned>(
                 std::to_integer<unsigned int>(bytes.at(offset + index)))
             << (8U * index);
  }
  return value;
}

std::int32_t loadInt32(const std::vector<std::byte>& bytes,
                       std::size_t offset) {
  return std::bit_cast<std::int32_t>(
      loadLittleEndian<std::uint32_t>(bytes, offset));
}

float loadFloat32(const std::vector<std::byte>& bytes, std::size_t offset) {
  return std::bit_cast<float>(loadLittleEndian<std::uint32_t>(bytes, offset));
}

double loadFloat64(const std::vector<std::byte>& bytes, std::size_t offset) {
  return std::bit_cast<double>(loadLittleEndian<std::uint64_t>(bytes, offset));
}

std::vector<std::byte> readBytes(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  const std::streamsize size = input.tellg();
  input.seekg(0);
  std::vector<std::byte> bytes(static_cast<std::size_t>(size));
  input.read(reinterpret_cast<char*>(bytes.data()), size);
  if (!input) {
    throw std::runtime_error("failed to read SHD test output");
  }
  return bytes;
}

SimulationCase makeSimulation(const ReceiverGrid& receivers,
                              std::vector<double> frequencies = {50.0}) {
  return SimulationCase(
      Environment(
          SoundSpeedProfile(
              {SoundSpeedPoint{
                   .depth = 0.0, .soundSpeed = 1500.0, .density = 1000.0},
               SoundSpeedPoint{
                   .depth = 100.0, .soundSpeed = 1500.0, .density = 1000.0}}),
          BoundaryModel::vacuum(0.0), BoundaryModel::rigid(100.0)),
      Source{.depth = 50.0, .amplitude = 1.0}, receivers,
      FrequencyGrid(std::move(frequencies)),
      LaunchFan{.minimumAngle = -5.0 * std::numbers::pi / 180.0,
                .maximumAngle = 5.0 * std::numbers::pi / 180.0,
                .explicitLaunchAngleCount = 300U},
      IntegratorSettings{.stepLength = 10.0,
                         .rangeLimit = 3100.0,
                         .depthLimit = 110.0,
                         .maximumRayPoints = 1000U});
}

void testMultiFrequencyBinaryLayout(Context& context) {
  TemporaryDirectory directory;
  const std::filesystem::path path = directory.path() / "multi_frequency.shd";
  const ReceiverGrid receivers({20.0, 30.0}, {1000.0, 2000.0});
  const SimulationCase simulation = makeSimulation(receivers, {50.0, 100.0});
  std::vector<FrequencyWorkspace> workspaces;
  workspaces.emplace_back(50.0, receivers);
  workspaces.emplace_back(100.0, receivers);
  for (std::size_t frequencyIndex = 0U; frequencyIndex < workspaces.size();
       ++frequencyIndex) {
    for (std::size_t depthIndex = 0U; depthIndex < receivers.depthCount();
         ++depthIndex) {
      for (std::size_t rangeIndex = 0U; rangeIndex < receivers.rangeCount();
           ++rangeIndex) {
        const double marker = 100.0 * static_cast<double>(frequencyIndex) +
                              10.0 * static_cast<double>(depthIndex) +
                              static_cast<double>(rangeIndex) + 1.0;
        workspaces[frequencyIndex].at(depthIndex, rangeIndex) = {marker,
                                                                 -marker};
      }
    }
  }

  ShdWriter::writeFrequencies(path, "Two-frequency RayReuse fixture",
                              simulation, workspaces);

  const std::vector<std::byte> bytes = readBytes(path);
  constexpr std::size_t recordBytes = 164U;
  context.check(bytes.size() == 14U * recordBytes,
                "two-frequency SHD has ten header and four pressure records");
  context.check(loadInt32(bytes, 2U * recordBytes) == 2,
                "multi-frequency SHD header stores nfreq");
  context.checkNear(loadFloat64(bytes, 3U * recordBytes), 50.0, 0.0,
                    "multi-frequency SHD first frequency");
  context.checkNear(loadFloat64(bytes, 3U * recordBytes + 8U), 100.0, 0.0,
                    "multi-frequency SHD second frequency");

  const std::size_t pressureStart = 10U * recordBytes;
  context.check(
      loadFloat32(bytes, pressureStart) == 1.0F &&
          loadFloat32(bytes, pressureStart + recordBytes) == 11.0F &&
          loadFloat32(bytes, pressureStart + 2U * recordBytes) == 101.0F &&
          loadFloat32(bytes, pressureStart + 3U * recordBytes) == 111.0F,
      "multi-frequency pressure records are frequency-major");
  context.check(
      loadFloat32(bytes, pressureStart + 2U * recordBytes + 12U) == -102.0F,
      "multi-frequency pressure records retain complex64 range order");
}

void testOutOfOrderStreamingMatchesBatch(Context& context) {
  TemporaryDirectory directory;
  const std::filesystem::path batchPath = directory.path() / "batch.shd";
  const std::filesystem::path streamingPath =
      directory.path() / "streaming.shd";
  const ReceiverGrid receivers({20.0, 30.0}, {1000.0, 2000.0});
  const SimulationCase simulation = makeSimulation(receivers, {50.0, 100.0});
  std::vector<FrequencyWorkspace> workspaces;
  workspaces.emplace_back(50.0, receivers);
  workspaces.emplace_back(100.0, receivers);
  for (std::size_t frequencyIndex = 0U; frequencyIndex < workspaces.size();
       ++frequencyIndex) {
    for (std::size_t depthIndex = 0U; depthIndex < receivers.depthCount();
         ++depthIndex) {
      for (std::size_t rangeIndex = 0U; rangeIndex < receivers.rangeCount();
           ++rangeIndex) {
        const double marker = 100.0 * static_cast<double>(frequencyIndex) +
                              10.0 * static_cast<double>(depthIndex) +
                              static_cast<double>(rangeIndex) + 0.25;
        workspaces[frequencyIndex].at(depthIndex, rangeIndex) = {marker,
                                                                 -marker};
      }
    }
  }

  constexpr std::string_view title = "Out-of-order streaming fixture";
  ShdWriter::writeFrequencies(batchPath, title, simulation, workspaces);

  ShdFrequencyWriter writer(streamingPath, title, simulation);
  writer.writeFrequency(1U, workspaces[1U]);
  writer.writeFrequency(0U, workspaces[0U]);
  writer.finalize();

  context.check(readBytes(streamingPath) == readBytes(batchPath),
                "out-of-order streaming is byte-identical to batch output");
}

void testStreamingValidation(Context& context) {
  TemporaryDirectory directory;
  const ReceiverGrid receivers({20.0, 30.0}, {1000.0, 2000.0});
  const SimulationCase simulation = makeSimulation(receivers, {50.0, 100.0});
  FrequencyWorkspace first(50.0, receivers);
  FrequencyWorkspace second(100.0, receivers);
  ShdFrequencyWriter writer(directory.path() / "validation.shd",
                            "Streaming validation fixture", simulation);

  context.expectThrows<ValidationError>(
      [&] { writer.writeFrequency(2U, first); },
      "streaming writer rejects an out-of-range frequency index");

  writer.writeFrequency(0U, first);
  context.expectThrows<ValidationError>(
      [&] { writer.writeFrequency(0U, first); },
      "streaming writer rejects a duplicate frequency index");
  context.expectThrows<ValidationError>(
      [&] { writer.finalize(); },
      "streaming writer rejects finalize with a missing frequency");

  writer.writeFrequency(1U, second);
  writer.finalize();
}

void testFrequencyAxisControlsRecordLength(Context& context) {
  TemporaryDirectory directory;
  const std::filesystem::path path =
      directory.path() / "wide_frequency_axis.shd";
  const ReceiverGrid receivers({20.0}, {1000.0});
  std::vector<double> frequencies;
  std::vector<FrequencyWorkspace> workspaces;
  for (std::size_t index = 0U; index < 21U; ++index) {
    const double frequency = 50.0 + static_cast<double>(index);
    frequencies.push_back(frequency);
    workspaces.emplace_back(frequency, receivers);
  }
  const SimulationCase simulation = makeSimulation(receivers, frequencies);

  ShdWriter::writeFrequencies(path, "Wide frequency-axis fixture", simulation,
                              workspaces);

  const std::vector<std::byte> bytes = readBytes(path);
  constexpr std::size_t recordWords = 42U;
  constexpr std::size_t recordBytes = 4U * recordWords;
  context.check(loadInt32(bytes, 0U) == static_cast<std::int32_t>(recordWords),
                "SHD record length expands to contain the frequency axis");
  context.checkNear(loadFloat64(bytes, 3U * recordBytes + 20U * sizeof(double)),
                    70.0, 0.0,
                    "expanded SHD record contains the last frequency");
}

void testBinaryLayout(Context& context) {
  constexpr std::string_view titleText = "Synthetic RayReuse SHD fixture";
  TemporaryDirectory directory;
  const std::filesystem::path path = directory.path() / "fixture.shd";
  const ReceiverGrid receivers({20.0, 30.0}, {1000.0, 2000.0, 3000.0});
  const SimulationCase simulation = makeSimulation(receivers);
  FrequencyWorkspace workspace(50.0, receivers);
  workspace.at(0U, 0U) = {0.111, -0.0555};
  workspace.at(0U, 1U) = {0.112, -0.056};
  workspace.at(0U, 2U) = {0.113, -0.0565};
  workspace.at(1U, 0U) = {0.121, -0.0605};
  workspace.at(1U, 1U) = {0.122, -0.061};
  workspace.at(1U, 2U) = {0.123, -0.0615};

  ShdWriter::writeSingleFrequency(path, titleText, simulation, workspace);

  const std::vector<std::byte> bytes = readBytes(path);
  constexpr std::size_t recordBytes = 164U;
  context.check(bytes.size() == 12U * recordBytes,
                "SHD file has ten header and two pressure records");
  context.check(loadInt32(bytes, 0U) == 41,
                "SHD first word stores record length in four-byte words");
  const std::string title(reinterpret_cast<const char*>(bytes.data() + 4U),
                          titleText.size());
  context.check(title == titleText, "SHD record one stores the title");
  const std::string plotType(
      reinterpret_cast<const char*>(bytes.data() + recordBytes), 10U);
  context.check(plotType == "rectilin  ",
                "SHD record two stores rectilinear plot type");

  const std::size_t dimensions = 2U * recordBytes;
  context.check(loadInt32(bytes, dimensions + 0U) == 1 &&
                    loadInt32(bytes, dimensions + 4U) == 1 &&
                    loadInt32(bytes, dimensions + 8U) == 1 &&
                    loadInt32(bytes, dimensions + 12U) == 1 &&
                    loadInt32(bytes, dimensions + 16U) == 1 &&
                    loadInt32(bytes, dimensions + 20U) == 2 &&
                    loadInt32(bytes, dimensions + 24U) == 3,
                "SHD record three stores RayReuse dimensions");
  context.checkNear(loadFloat64(bytes, dimensions + 28U), 50.0, 0.0,
                    "SHD nominal frequency");
  context.checkNear(loadFloat64(bytes, 3U * recordBytes), 50.0, 0.0,
                    "SHD frequency axis");
  context.checkNear(static_cast<double>(loadFloat32(bytes, 7U * recordBytes)),
                    50.0, 0.0, "SHD source-depth float axis");
  context.checkNear(
      static_cast<double>(loadFloat32(bytes, 8U * recordBytes + 4U)), 30.0, 0.0,
      "SHD receiver-depth float axis");
  context.checkNear(loadFloat64(bytes, 9U * recordBytes + 16U), 3000.0, 0.0,
                    "SHD receiver-range double axis");

  const std::size_t firstPressure = 10U * recordBytes;
  context.check(
      loadFloat32(bytes, firstPressure) == static_cast<float>(0.111) &&
          loadFloat32(bytes, firstPressure + 4U) ==
              static_cast<float>(-0.0555) &&
          loadFloat32(bytes, firstPressure + 16U) == static_cast<float>(0.113),
      "SHD pressure record is range-major interleaved complex64");
  const std::size_t secondPressure = 11U * recordBytes;
  context.check(
      loadFloat32(bytes, secondPressure) == static_cast<float>(0.121) &&
          loadFloat32(bytes, secondPressure + 20U) ==
              static_cast<float>(-0.0615),
      "SHD emits one pressure record per receiver depth");
  context.check(workspace.at(1U, 2U) == std::complex<double>{0.123, -0.0615},
                "writer does not modify the double workspace");
}

void testValidation(Context& context) {
  TemporaryDirectory directory;
  const ReceiverGrid receivers({20.0, 30.0}, {1000.0, 2000.0, 3000.0});
  const SimulationCase simulation = makeSimulation(receivers);

  context.expectThrows<ValidationError>(
      [&] {
        FrequencyWorkspace wrongFrequency(100.0, receivers);
        ShdWriter::writeSingleFrequency(
            directory.path() / "wrong_frequency.shd", "wrong frequency",
            simulation, wrongFrequency);
      },
      "writer rejects workspace frequency mismatch");

  const SimulationCase multiFrequencySimulation =
      makeSimulation(receivers, {50.0, 100.0});
  context.expectThrows<ValidationError>(
      [&] {
        std::vector<FrequencyWorkspace> missingWorkspace;
        missingWorkspace.emplace_back(50.0, receivers);
        ShdWriter::writeFrequencies(directory.path() / "wrong_count.shd",
                                    "wrong count", multiFrequencySimulation,
                                    missingWorkspace);
      },
      "multi-frequency writer rejects workspace count mismatch");
  context.expectThrows<ValidationError>(
      [&] {
        std::vector<FrequencyWorkspace> wrongOrder;
        wrongOrder.emplace_back(100.0, receivers);
        wrongOrder.emplace_back(50.0, receivers);
        ShdWriter::writeFrequencies(directory.path() / "wrong_order.shd",
                                    "wrong order", multiFrequencySimulation,
                                    wrongOrder);
      },
      "multi-frequency writer rejects workspace frequency order");
  context.expectThrows<ValidationError>(
      [&] {
        FrequencyWorkspace wrongShape(50.0,
                                      ReceiverGrid({20.0}, {1000.0, 2000.0}));
        ShdWriter::writeSingleFrequency(directory.path() / "wrong_shape.shd",
                                        "wrong shape", simulation, wrongShape);
      },
      "writer rejects workspace shape mismatch");
  context.expectThrows<ValidationError>(
      [&] {
        FrequencyWorkspace nonfinite(50.0, receivers);
        nonfinite.at(0U, 0U) = {std::numeric_limits<double>::quiet_NaN(), 0.0};
        ShdWriter::writeSingleFrequency(directory.path() / "nonfinite.shd",
                                        "nonfinite", simulation, nonfinite);
      },
      "writer rejects non-finite pressure");
  context.expectThrows<ValidationError>(
      [&] {
        FrequencyWorkspace overflow(50.0, receivers);
        overflow.at(0U, 0U) = {std::numeric_limits<double>::max(), 0.0};
        ShdWriter::writeSingleFrequency(directory.path() / "overflow.shd",
                                        "overflow", simulation, overflow);
      },
      "writer rejects pressure outside float32 range");
  context.expectThrows<ValidationError>(
      [&] {
        FrequencyWorkspace workspace(50.0, receivers);
        ShdWriter::writeSingleFrequency(directory.path() / "bad_title.shd",
                                        "bad\n title", simulation, workspace);
      },
      "writer rejects non-ASCII-control title");
}

}  // namespace

int main() {
  Context context;
  testBinaryLayout(context);
  testMultiFrequencyBinaryLayout(context);
  testOutOfOrderStreamingMatchesBatch(context);
  testStreamingValidation(context);
  testFrequencyAxisControlsRecordLength(context);
  testValidation(context);

  if (context.failureCount() != 0) {
    std::cerr << context.failureCount() << " SHD-writer assertion(s) failed\n";
    return 1;
  }
  std::cout << "All Bellhop RayReuse SHD-writer tests passed\n";
  return 0;
}
