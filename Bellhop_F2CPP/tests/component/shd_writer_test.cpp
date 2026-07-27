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
#include <string>
#include <vector>

#include "bellhop/error.hpp"
#include "bellhop/io/shd_writer.hpp"
#include "bellhop/model/simulation_case.hpp"
#include "support/test_harness.hpp"

namespace {

using bellhop::BoundaryModel;
using bellhop::Environment;
using bellhop::FrequencyGrid;
using bellhop::FrequencyWorkspace;
using bellhop::IntegratorSettings;
using bellhop::LaunchFan;
using bellhop::ReceiverGrid;
using bellhop::ShdWriter;
using bellhop::SimulationCase;
using bellhop::SoundSpeedPoint;
using bellhop::SoundSpeedProfile;
using bellhop::Source;
using bellhop::ValidationError;
using bellhop::test::Context;

class TemporaryDirectory {
 public:
  TemporaryDirectory() {
    const auto suffix =
        std::chrono::steady_clock::now()
            .time_since_epoch()
            .count();
    path_ = std::filesystem::temp_directory_path() /
            ("bellhop_f2cpp_shd_" +
             std::to_string(suffix));
    std::filesystem::create_directory(path_);
  }

  ~TemporaryDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

  [[nodiscard]] const std::filesystem::path& path() const {
    return path_;
  }

 private:
  std::filesystem::path path_;
};

template <typename Unsigned>
Unsigned loadLittleEndian(
    const std::vector<std::byte>& bytes, std::size_t offset) {
  Unsigned value{};
  for (std::size_t index = 0U;
       index < sizeof(Unsigned); ++index) {
    value |= static_cast<Unsigned>(
                 std::to_integer<unsigned int>(
                     bytes.at(offset + index)))
             << (8U * index);
  }
  return value;
}

std::int32_t loadInt32(
    const std::vector<std::byte>& bytes, std::size_t offset) {
  return std::bit_cast<std::int32_t>(
      loadLittleEndian<std::uint32_t>(bytes, offset));
}

float loadFloat32(
    const std::vector<std::byte>& bytes, std::size_t offset) {
  return std::bit_cast<float>(
      loadLittleEndian<std::uint32_t>(bytes, offset));
}

double loadFloat64(
    const std::vector<std::byte>& bytes, std::size_t offset) {
  return std::bit_cast<double>(
      loadLittleEndian<std::uint64_t>(bytes, offset));
}

std::vector<std::byte> readBytes(
    const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  const std::streamsize size = input.tellg();
  input.seekg(0);
  std::vector<std::byte> bytes(
      static_cast<std::size_t>(size));
  input.read(
      reinterpret_cast<char*>(bytes.data()), size);
  if (!input) {
    throw std::runtime_error("failed to read SHD test output");
  }
  return bytes;
}

SimulationCase makeSimulation(const ReceiverGrid& receivers,
                              double frequency = 50.0) {
  return SimulationCase(
      Environment(
          SoundSpeedProfile(
              {SoundSpeedPoint{
                   .depth = 0.0,
                   .soundSpeed = 1500.0,
                   .density = 1000.0},
               SoundSpeedPoint{
                   .depth = 100.0,
                   .soundSpeed = 1500.0,
                   .density = 1000.0}}),
          BoundaryModel::vacuum(0.0),
          BoundaryModel::rigid(100.0)),
      Source{.depth = 50.0, .amplitude = 1.0},
      receivers, FrequencyGrid({frequency}),
      LaunchFan{
          .minimumAngle =
              -5.0 * std::numbers::pi / 180.0,
          .maximumAngle =
              5.0 * std::numbers::pi / 180.0,
          .explicitLaunchAngleCount = 300U},
      IntegratorSettings{
          .stepLength = 10.0,
          .rangeLimit = 3100.0,
          .depthLimit = 110.0,
          .maximumRayPoints = 1000U});
}

void testBinaryLayout(Context& context) {
  TemporaryDirectory directory;
  const std::filesystem::path path =
      directory.path() / "fixture.shd";
  const ReceiverGrid receivers(
      {20.0, 30.0}, {1000.0, 2000.0, 3000.0});
  const SimulationCase simulation =
      makeSimulation(receivers);
  FrequencyWorkspace workspace(50.0, receivers);
  workspace.at(0U, 0U) = {0.111, -0.0555};
  workspace.at(0U, 1U) = {0.112, -0.056};
  workspace.at(0U, 2U) = {0.113, -0.0565};
  workspace.at(1U, 0U) = {0.121, -0.0605};
  workspace.at(1U, 1U) = {0.122, -0.061};
  workspace.at(1U, 2U) = {0.123, -0.0615};

  ShdWriter::writeSingleFrequency(
      path, "Synthetic F2CPP SHD fixture",
      simulation, workspace);

  const std::vector<std::byte> bytes = readBytes(path);
  constexpr std::size_t recordBytes = 164U;
  context.check(
      bytes.size() == 12U * recordBytes,
      "SHD file has ten header and two pressure records");
  context.check(
      loadInt32(bytes, 0U) == 41,
      "SHD first word stores record length in four-byte words");
  const std::string title(
      reinterpret_cast<const char*>(bytes.data() + 4U), 27U);
  context.check(
      title == "Synthetic F2CPP SHD fixture",
      "SHD record one stores the title");
  const std::string plotType(
      reinterpret_cast<const char*>(
          bytes.data() + recordBytes),
      10U);
  context.check(
      plotType == "rectilin  ",
      "SHD record two stores rectilinear plot type");

  const std::size_t dimensions = 2U * recordBytes;
  context.check(
      loadInt32(bytes, dimensions + 0U) == 1 &&
          loadInt32(bytes, dimensions + 4U) == 1 &&
          loadInt32(bytes, dimensions + 8U) == 1 &&
          loadInt32(bytes, dimensions + 12U) == 1 &&
          loadInt32(bytes, dimensions + 16U) == 1 &&
          loadInt32(bytes, dimensions + 20U) == 2 &&
          loadInt32(bytes, dimensions + 24U) == 3,
      "SHD record three stores F2CPP dimensions");
  context.checkNear(
      loadFloat64(bytes, dimensions + 28U),
      50.0, 0.0, "SHD nominal frequency");
  context.checkNear(
      loadFloat64(bytes, 3U * recordBytes),
      50.0, 0.0, "SHD frequency axis");
  context.checkNear(
      static_cast<double>(
          loadFloat32(bytes, 7U * recordBytes)),
      50.0, 0.0, "SHD source-depth float axis");
  context.checkNear(
      static_cast<double>(
          loadFloat32(bytes, 8U * recordBytes + 4U)),
      30.0, 0.0, "SHD receiver-depth float axis");
  context.checkNear(
      loadFloat64(bytes, 9U * recordBytes + 16U),
      3000.0, 0.0, "SHD receiver-range double axis");

  const std::size_t firstPressure = 10U * recordBytes;
  context.check(
      loadFloat32(bytes, firstPressure) ==
              static_cast<float>(0.111) &&
          loadFloat32(bytes, firstPressure + 4U) ==
              static_cast<float>(-0.0555) &&
          loadFloat32(bytes, firstPressure + 16U) ==
              static_cast<float>(0.113),
      "SHD pressure record is range-major interleaved complex64");
  const std::size_t secondPressure = 11U * recordBytes;
  context.check(
      loadFloat32(bytes, secondPressure) ==
              static_cast<float>(0.121) &&
          loadFloat32(bytes, secondPressure + 20U) ==
              static_cast<float>(-0.0615),
      "SHD emits one pressure record per receiver depth");
  context.check(
      workspace.at(1U, 2U) ==
          std::complex<double>{0.123, -0.0615},
      "writer does not modify the double workspace");
}

void testValidation(Context& context) {
  TemporaryDirectory directory;
  const ReceiverGrid receivers(
      {20.0, 30.0}, {1000.0, 2000.0, 3000.0});
  const SimulationCase simulation =
      makeSimulation(receivers);

  context.expectThrows<ValidationError>(
      [&] {
        FrequencyWorkspace wrongFrequency(100.0, receivers);
        ShdWriter::writeSingleFrequency(
            directory.path() / "wrong_frequency.shd",
            "wrong frequency", simulation, wrongFrequency);
      },
      "writer rejects workspace frequency mismatch");
  context.expectThrows<ValidationError>(
      [&] {
        FrequencyWorkspace wrongShape(
            50.0, ReceiverGrid({20.0}, {1000.0, 2000.0}));
        ShdWriter::writeSingleFrequency(
            directory.path() / "wrong_shape.shd",
            "wrong shape", simulation, wrongShape);
      },
      "writer rejects workspace shape mismatch");
  context.expectThrows<ValidationError>(
      [&] {
        FrequencyWorkspace nonfinite(50.0, receivers);
        nonfinite.at(0U, 0U) = {
            std::numeric_limits<double>::quiet_NaN(), 0.0};
        ShdWriter::writeSingleFrequency(
            directory.path() / "nonfinite.shd",
            "nonfinite", simulation, nonfinite);
      },
      "writer rejects non-finite pressure");
  context.expectThrows<ValidationError>(
      [&] {
        FrequencyWorkspace overflow(50.0, receivers);
        overflow.at(0U, 0U) = {
            std::numeric_limits<double>::max(), 0.0};
        ShdWriter::writeSingleFrequency(
            directory.path() / "overflow.shd",
            "overflow", simulation, overflow);
      },
      "writer rejects pressure outside float32 range");
  context.expectThrows<ValidationError>(
      [&] {
        FrequencyWorkspace workspace(50.0, receivers);
        ShdWriter::writeSingleFrequency(
            directory.path() / "bad_title.shd",
            "bad\n title", simulation, workspace);
      },
      "writer rejects non-ASCII-control title");
}

}  // namespace

int main() {
  Context context;
  testBinaryLayout(context);
  testValidation(context);

  if (context.failureCount() != 0) {
    std::cerr << context.failureCount()
              << " SHD-writer assertion(s) failed\n";
    return 1;
  }
  std::cout << "All Bellhop F2CPP SHD-writer tests passed\n";
  return 0;
}
