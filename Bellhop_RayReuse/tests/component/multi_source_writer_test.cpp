// FP-2F F06: multi-source and irregular-receiver product layouts for the
// SHD/ARR/E/RAY writers, plus the NSz == 1 rectilinear byte-identity gate.

#include <bit>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numbers>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "rayreuse/cache/ray_path_cache.hpp"
#include "rayreuse/error.hpp"
#include "rayreuse/field/arrival_workspace.hpp"
#include "rayreuse/field/eigenray_hit.hpp"
#include "rayreuse/io/arrival_writer.hpp"
#include "rayreuse/io/eigenray_writer.hpp"
#include "rayreuse/io/ray_writer.hpp"
#include "rayreuse/io/shd_writer.hpp"
#include "rayreuse/model/environment.hpp"
#include "rayreuse/model/simulation_case.hpp"
#include "rayreuse/solver/ray_trace_product.hpp"
#include "rayreuse/solver/single_frequency_solver.hpp"
#include "support/test_harness.hpp"

namespace {

using rayreuse::ArrivalEncoding;
using rayreuse::ArrivalWorkspace;
using rayreuse::BeamFamily;
using rayreuse::BoundaryCurvatureMode;
using rayreuse::BoundaryModel;
using rayreuse::Environment;
using rayreuse::EigenrayHit;
using rayreuse::EigenrayWriter;
using rayreuse::FrequencyGrid;
using rayreuse::FrequencyWorkspace;
using rayreuse::IntegratorSettings;
using rayreuse::LaunchFan;
using rayreuse::RayPathCache;
using rayreuse::RayWriter;
using rayreuse::ReceiverGrid;
using rayreuse::ReceiverGridLayout;
using rayreuse::ShdFrequencyWriter;
using rayreuse::ShdWriter;
using rayreuse::SimulationCase;
using rayreuse::SimulationRunMode;
using rayreuse::SingleFrequencySolver;
using rayreuse::SoundSpeedPoint;
using rayreuse::SoundSpeedProfile;
using rayreuse::Source;
using rayreuse::ArrivalWriter;
using rayreuse::ValidationError;
using rayreuse::traceRayProduct;
using rayreuse::traceRayProducts;
using rayreuse::test::Context;

constexpr double kFrequency = 50.0;

class TemporaryDirectory {
 public:
  TemporaryDirectory() {
    const auto suffix =
        std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() /
            ("rayreuse_f06_writers_" + std::to_string(suffix));
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

std::vector<std::byte> readBytes(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) {
    throw std::runtime_error("failed to open product for reading");
  }
  const std::streamsize size = input.tellg();
  input.seekg(0);
  std::vector<std::byte> bytes(static_cast<std::size_t>(size));
  input.read(reinterpret_cast<char*>(bytes.data()), size);
  if (!input) {
    throw std::runtime_error("failed to read product fixture");
  }
  return bytes;
}

std::vector<std::string> readLines(const std::filesystem::path& path) {
  std::ifstream input(path);
  std::vector<std::string> lines;
  std::string line;
  while (std::getline(input, line)) {
    lines.push_back(line);
  }
  return lines;
}

Environment makeEnvironment() {
  return Environment(
      SoundSpeedProfile(
          {SoundSpeedPoint{
               .depth = 0.0, .soundSpeed = 1500.0, .density = 1000.0},
           SoundSpeedPoint{
               .depth = 100.0, .soundSpeed = 1500.0, .density = 1000.0}}),
      BoundaryModel::vacuum(0.0), BoundaryModel::rigid(100.0));
}

LaunchFan makeFan() {
  return LaunchFan{.minimumAngle = -2.0 * std::numbers::pi / 180.0,
                   .maximumAngle = 2.0 * std::numbers::pi / 180.0,
                   .explicitLaunchAngleCount = 21U};
}

IntegratorSettings makeIntegrator() {
  return IntegratorSettings{.stepLength = 10.0,
                            .rangeLimit = 3100.0,
                            .depthLimit = 110.0,
                            .maximumRayPoints = 1000U};
}

// Dual-source fixture given out of depth order; the model sorts ascending.
std::vector<Source> dualSources() {
  return {Source{.depth = 70.0, .amplitude = 1.0},
          Source{.depth = 30.0, .amplitude = 1.0}};
}

SimulationCase makeCase(std::vector<Source> sources, const ReceiverGrid& receivers,
                        std::vector<double> frequencies,
                        SimulationRunMode runMode,
                        BeamFamily beamFamily = BeamFamily::CervenyGaussian) {
  return SimulationCase(
      makeEnvironment(), std::move(sources), receivers,
      FrequencyGrid(std::move(frequencies)), makeFan(), makeIntegrator(),
      rayreuse::SourceBeamPattern::omnidirectional(), runMode, beamFamily,
      rayreuse::FieldComponent::Pressure,
      BoundaryCurvatureMode::Standard);
}

SimulationCase makeSingleSourceCase(const ReceiverGrid& receivers,
                                    std::vector<double> frequencies,
                                    SimulationRunMode runMode,
                                    BeamFamily beamFamily =
                                        BeamFamily::CervenyGaussian) {
  return makeCase({Source{.depth = 30.0, .amplitude = 1.0}}, receivers,
                  std::move(frequencies), runMode, beamFamily);
}

void fillWorkspace(FrequencyWorkspace& workspace, std::size_t frequencyIndex,
                   std::size_t sourceIndex) {
  for (std::size_t depthIndex = 0U; depthIndex < workspace.depthCount();
       ++depthIndex) {
    for (std::size_t rangeIndex = 0U; rangeIndex < workspace.rangeCount();
         ++rangeIndex) {
      const double marker = 1000.0 * static_cast<double>(frequencyIndex) +
                            100.0 * static_cast<double>(sourceIndex) +
                            10.0 * static_cast<double>(depthIndex) +
                            static_cast<double>(rangeIndex) + 0.5;
      workspace.at(depthIndex, rangeIndex) = {marker, -marker};
    }
  }
}

std::vector<FrequencyWorkspace> makeSourceWorkspaces(
    std::size_t sourceCount, const ReceiverGrid& receivers,
    std::size_t frequencyIndex = 0U, double frequency = kFrequency) {
  std::vector<FrequencyWorkspace> workspaces;
  for (std::size_t sourceIndex = 0U; sourceIndex < sourceCount; ++sourceIndex) {
    workspaces.emplace_back(frequency, receivers);
    fillWorkspace(workspaces.back(), frequencyIndex, sourceIndex);
  }
  return workspaces;
}

// ---------------------------------------------------------------------------
// SHD
// ---------------------------------------------------------------------------

void testShdMultiSourceHeaderAndRecordLayout(Context& context) {
  TemporaryDirectory directory;
  const ReceiverGrid receivers({20.0, 30.0}, {1000.0, 2000.0});
  const SimulationCase simulation = makeCase(
      dualSources(), receivers, {kFrequency}, SimulationRunMode::Coherent);
  const std::vector<FrequencyWorkspace> workspaces =
      makeSourceWorkspaces(2U, receivers);
  const std::filesystem::path path = directory.path() / "dual_source.shd";

  ShdWriter::writeSingleFrequency(path, "Dual-source SHD fixture", simulation,
                                  workspaces.front(),
                                  std::span(workspaces).subspan(1U));

  constexpr std::size_t kRecordBytes = 164U;  // 41 words: max(41, NSz, NRz, 2*NRr, ...)
  const std::vector<std::byte> bytes = readBytes(path);
  context.check(bytes.size() == 14U * kRecordBytes,
                "dual-source SHD holds ten header and NSz*NRz pressure records");

  const std::size_t dimensions = 2U * kRecordBytes;
  context.check(loadInt32(bytes, dimensions + 16U) == 2 &&
                    loadInt32(bytes, dimensions + 20U) == 2 &&
                    loadInt32(bytes, dimensions + 24U) == 2,
                "SHD header stores NSz=2, NRz=2, NRr=2");
  context.check(loadFloat32(bytes, 7U * kRecordBytes) == 30.0F &&
                    loadFloat32(bytes, 7U * kRecordBytes + 4U) == 70.0F,
                "SHD source-depth record lists both depths ascending");

  // Origin addressing: IRec = 10 + sourceIndex*NRz_per_range + depthIndex.
  const std::size_t pressure = 10U * kRecordBytes;
  context.check(loadFloat32(bytes, pressure) == 0.5F &&
                    loadFloat32(bytes, pressure + kRecordBytes) == 10.5F &&
                    loadFloat32(bytes, pressure + 2U * kRecordBytes) ==
                        100.5F &&
                    loadFloat32(bytes, pressure + 3U * kRecordBytes) ==
                        110.5F,
                "dual-source pressure records are source-major");
}

void testShdMultiFrequencyMultiSourceLayout(Context& context) {
  TemporaryDirectory directory;
  const ReceiverGrid receivers({20.0, 30.0}, {1000.0, 2000.0});
  const SimulationCase simulation =
      makeCase(dualSources(), receivers, {50.0, 100.0},
               SimulationRunMode::Coherent);
  std::vector<std::vector<FrequencyWorkspace>> perFrequency;
  perFrequency.push_back(makeSourceWorkspaces(2U, receivers, 0U, 50.0));
  perFrequency.push_back(makeSourceWorkspaces(2U, receivers, 1U, 100.0));
  const std::filesystem::path path =
      directory.path() / "dual_source_broadband.shd";

  ShdWriter::writeFrequencies(path, "Broadband dual-source fixture", simulation,
                              perFrequency);

  constexpr std::size_t kRecordBytes = 164U;
  const std::vector<std::byte> bytes = readBytes(path);
  context.check(bytes.size() == 18U * kRecordBytes,
                "two-frequency dual-source SHD holds 10 + 2*(NSz*NRz) records");
  // Frequency block f starts at record 10 + f*NSz*NRz; within a block records
  // are source-major, then depth.
  const std::size_t secondFrequency = 14U * kRecordBytes;
  context.check(loadFloat32(bytes, secondFrequency) == 1000.5F &&
                    loadFloat32(bytes, secondFrequency + kRecordBytes) ==
                        1010.5F &&
                    loadFloat32(bytes, secondFrequency + 2U * kRecordBytes) ==
                        1100.5F &&
                    loadFloat32(bytes, secondFrequency + 3U * kRecordBytes) ==
                        1110.5F,
                "frequency blocks stack frequency-major over source blocks");
}

void testShdIrregularLayout(Context& context) {
  TemporaryDirectory directory;
  const ReceiverGrid irregular(
      {20.0, 30.0, 40.0}, {1000.0, 2000.0, 3000.0},
      ReceiverGridLayout::Irregular);
  const SimulationCase simulation = makeCase(
      dualSources(), irregular, {kFrequency}, SimulationRunMode::Coherent);
  const std::vector<FrequencyWorkspace> workspaces =
      makeSourceWorkspaces(2U, irregular);
  const std::filesystem::path path = directory.path() / "irregular.shd";

  context.check(workspaces.front().depthCount() == 1U &&
                    workspaces.front().rangeCount() == 3U,
                "irregular workspaces hold one paired depth row");

  ShdWriter::writeSingleFrequency(path, "Irregular SHD fixture", simulation,
                                  workspaces.front(),
                                  std::span(workspaces).subspan(1U));

  constexpr std::size_t kRecordBytes = 164U;
  const std::vector<std::byte> bytes = readBytes(path);
  const std::string plotType(
      reinterpret_cast<const char*>(bytes.data() + kRecordBytes), 10U);
  context.check(plotType == "irregular ",
                "irregular SHD writes the Origin 'irregular ' plot type");
  const std::size_t dimensions = 2U * kRecordBytes;
  context.check(loadInt32(bytes, dimensions + 16U) == 2 &&
                    loadInt32(bytes, dimensions + 20U) == 3 &&
                    loadInt32(bytes, dimensions + 24U) == 3,
                "irregular SHD header keeps NSz, NRz, NRr counts");
  context.check(bytes.size() == 12U * kRecordBytes,
                "irregular SHD holds one paired record per source");
  const std::size_t pressure = 10U * kRecordBytes;
  context.check(loadFloat32(bytes, pressure) == 0.5F &&
                    loadFloat32(bytes, pressure + 8U) == 1.5F &&
                    loadFloat32(bytes, pressure + 16U) == 2.5F,
                "irregular record carries the paired receiver row");
  context.check(loadFloat32(bytes, pressure + kRecordBytes) == 100.5F,
                "second irregular record carries the second source's row");
}

void testShdSingleSourceByteIdentity(Context& context) {
  TemporaryDirectory directory;
  const ReceiverGrid receivers({20.0, 30.0}, {1000.0, 2000.0, 3000.0});
  const SimulationCase simulation = makeSingleSourceCase(
      receivers, {kFrequency}, SimulationRunMode::Coherent);
  const std::vector<FrequencyWorkspace> workspaces =
      makeSourceWorkspaces(1U, receivers);

  const std::filesystem::path legacyPath = directory.path() / "legacy.shd";
  const std::filesystem::path perSourcePath =
      directory.path() / "per_source.shd";
  const std::filesystem::path streamSinglePath =
      directory.path() / "stream_single.shd";
  const std::filesystem::path streamSpanPath =
      directory.path() / "stream_span.shd";

  ShdWriter::writeSingleFrequency(legacyPath, "Byte identity", simulation,
                                  workspaces.front());
  ShdWriter::writeSingleFrequency(perSourcePath, "Byte identity", simulation,
                                  workspaces.front(), {});
  context.check(readBytes(legacyPath) == readBytes(perSourcePath),
                "NSz==1 SHD per-source entry is byte-identical to legacy");

  {
    ShdFrequencyWriter writer(streamSinglePath, "Byte identity", simulation);
    writer.writeFrequency(0U, workspaces.front());
    writer.finalize();
  }
  {
    ShdFrequencyWriter writer(streamSpanPath, "Byte identity", simulation);
    writer.writeFrequency(0U, workspaces);
    writer.finalize();
  }
  context.check(readBytes(streamSinglePath) == readBytes(legacyPath) &&
                    readBytes(streamSpanPath) == readBytes(legacyPath),
                "NSz==1 streaming entries stay byte-identical to batch");

  context.expectThrows<ValidationError>(
      [&] {
        ShdWriter::writeSingleFrequency(legacyPath, "wrong source count",
                                        simulation, workspaces.front(),
                                        makeSourceWorkspaces(1U, receivers));
      },
      "SHD rejects additional sources on a single-source simulation");
}

// ---------------------------------------------------------------------------
// ARR
// ---------------------------------------------------------------------------

void testArrivalMultiSourceLayout(Context& context) {
  TemporaryDirectory directory;
  const ReceiverGrid receivers({25.0, 50.0, 75.0}, {10.0, 55.0, 100.0});
  const SimulationCase simulation = makeCase(
      dualSources(), receivers, {kFrequency}, SimulationRunMode::AsciiArrivals);
  const std::vector<ArrivalWorkspace> workspaces = [] {
    std::vector<ArrivalWorkspace> workspaces;
    workspaces.emplace_back(kFrequency,
                            ReceiverGrid({25.0, 50.0, 75.0},
                                         {10.0, 55.0, 100.0}));
    workspaces.emplace_back(kFrequency,
                            ReceiverGrid({25.0, 50.0, 75.0},
                                         {10.0, 55.0, 100.0}));
    return workspaces;
  }();
  const std::filesystem::path asciiPath = directory.path() / "dual.arr";
  const std::filesystem::path binaryPath = directory.path() / "dual_a.arr";

  ArrivalWriter::write(asciiPath, "Dual ARR fixture", simulation, workspaces,
                       ArrivalEncoding::Ascii);

  const std::vector<std::string> lines = readLines(asciiPath);
  context.check(lines.size() == 5U + 2U * (1U + 9U),
                "dual-source ARR ASCII holds a per-source block of nine cells");
  context.check(lines[2U] == "2 30 70",
                "ARR ASCII header line carries the source count and depths");
  for (std::size_t block = 0U; block < 2U; ++block) {
    const std::size_t base = 5U + block * 10U;
    bool zeroCells = true;
    for (std::size_t cell = 0U; cell < 9U; ++cell) {
      zeroCells = zeroCells && lines[base + 1U + cell] == "0";
    }
    context.check(lines[base] == "0" && zeroCells,
                  "each ARR source block holds the maximum and nine cells");
  }

  const SimulationCase binarySimulation = makeCase(
      dualSources(), receivers, {kFrequency}, SimulationRunMode::BinaryArrivals);
  ArrivalWriter::write(binaryPath, "Dual ARR fixture", binarySimulation,
                       workspaces, ArrivalEncoding::Binary);
  const std::vector<std::byte> bytes = readBytes(binaryPath);
  // Records (marker+payload+marker): '2D'(4+4+4) + freq(4+4+4) +
  // sources(4+(4+4*2)+4) + depths(4+(4+4*3)+4) + ranges(4+(4+8*3)+4) +
  // 2 blocks * (max(4) + 9 cell counts(4)), each with markers.
  const std::size_t headerBytes = 12U + 12U + 20U + 24U + 36U;
  const std::size_t bodyBytes = 2U * (12U + 9U * 12U);
  context.check(bytes.size() == headerBytes + bodyBytes,
                "dual-source ARR binary size matches the record layout");
  // Source-depth record starts at byte 24: marker(4) then NSz + two float32
  // depths in the payload.
  context.check(loadInt32(bytes, 24U) == 12 &&
                    loadInt32(bytes, 28U) == 2 &&
                    loadFloat32(bytes, 32U) == 30.0F &&
                    loadFloat32(bytes, 36U) == 70.0F,
                "ARR binary source record stores NSz and both depths");
}

void testArrivalSingleSourceByteIdentity(Context& context) {
  TemporaryDirectory directory;
  const ReceiverGrid receivers({25.0, 50.0}, {10.0, 55.0});
  const SimulationCase asciiSimulation = makeSingleSourceCase(
      receivers, {kFrequency}, SimulationRunMode::AsciiArrivals);
  const SimulationCase binarySimulation = makeSingleSourceCase(
      receivers, {kFrequency}, SimulationRunMode::BinaryArrivals);
  const ArrivalWorkspace asciiWorkspace(kFrequency, receivers);
  const ArrivalWorkspace binaryWorkspace(kFrequency, receivers);

  const std::filesystem::path legacyAscii = directory.path() / "legacy.arr";
  const std::filesystem::path spanAscii = directory.path() / "span.arr";
  const std::filesystem::path legacyBinary = directory.path() / "legacy_a.arr";
  const std::filesystem::path spanBinary = directory.path() / "span_a.arr";

  ArrivalWriter::write(legacyAscii, "Byte identity", asciiSimulation,
                       asciiWorkspace, ArrivalEncoding::Ascii);
  ArrivalWriter::write(spanAscii, "Byte identity", asciiSimulation,
                       std::span(&asciiWorkspace, 1U),
                       ArrivalEncoding::Ascii);
  ArrivalWriter::write(legacyBinary, "Byte identity", binarySimulation,
                       binaryWorkspace, ArrivalEncoding::Binary);
  ArrivalWriter::write(spanBinary, "Byte identity", binarySimulation,
                       std::span(&binaryWorkspace, 1U),
                       ArrivalEncoding::Binary);
  context.check(readBytes(legacyAscii) == readBytes(spanAscii) &&
                    readBytes(legacyBinary) == readBytes(spanBinary),
                "NSz==1 ARR per-source entries are byte-identical to legacy");

  context.expectThrows<ValidationError>(
      [&] {
        const SimulationCase dual = makeCase(
            dualSources(), receivers, {kFrequency},
            SimulationRunMode::AsciiArrivals);
        ArrivalWriter::write(spanAscii, "wrong source count", dual,
                             std::span(&asciiWorkspace, 1U),
                             ArrivalEncoding::Ascii);
      },
      "ARR rejects a workspace count below the simulation source count");
}

// ---------------------------------------------------------------------------
// E (eigenray) and R (ray trace) products
// ---------------------------------------------------------------------------

void writeDualSourceRayTrace(Context& context,
                             const std::filesystem::path& dualPath,
                             const std::filesystem::path& singlePath) {
  const ReceiverGrid receivers({25.0, 50.0}, {10.0, 55.0});
  const SimulationCase dual =
      makeCase(dualSources(), receivers, {100.0}, SimulationRunMode::RayTrace);
  const SimulationCase single = makeSingleSourceCase(
      receivers, {100.0}, SimulationRunMode::RayTrace);
  const std::vector<RayPathCache> caches = traceRayProducts(dual);
  const RayPathCache singleCache = traceRayProduct(single);
  context.check(caches.size() == 2U, "R dual-source trace yields two fans");
  {
    RayWriter writer(dualPath, "Dual R fixture", dual, 100.0);
    for (std::size_t sourceIndex = 0U; sourceIndex < caches.size();
         ++sourceIndex) {
      writer.appendSource(sourceIndex, caches[sourceIndex]);
    }
    writer.finalize();
  }
  {
    RayWriter writer(singlePath, "Dual R fixture", single, 100.0);
    writer.append(singleCache);
    writer.finalize();
  }
}

void testRayWriterMultiSourceLayout(Context& context) {
  TemporaryDirectory directory;
  const std::filesystem::path dualPath =
      directory.path() / "dual.ray";
  const std::filesystem::path singlePath =
      directory.path() / "single.ray";
  writeDualSourceRayTrace(context, dualPath, singlePath);

  const std::vector<std::string> dualLines = readLines(dualPath);
  const std::vector<std::string> singleLines = readLines(singlePath);
  context.check(dualLines.size() > 7U && dualLines[2U] == "1 1 2",
                "R dual-source header carries '1 1 NSz'");
  context.check(singleLines[2U] == "1 1 1",
                "R single-source header stays '1 1 1'");
  // The first source's block must equal the whole single-source body (the
  // model sorts the dual fixture to the same shallowest source).
  const std::size_t headerLines = 7U;
  bool prefixMatches = dualLines.size() > singleLines.size();
  for (std::size_t index = headerLines; prefixMatches && index < singleLines.size();
       ++index) {
    prefixMatches = dualLines[index] == singleLines[index];
  }
  context.check(prefixMatches,
                "R dual-source file embeds the first source's single-source "
                "block verbatim");
}

void testRayWriterPerSourceValidation(Context& context) {
  TemporaryDirectory directory;
  const ReceiverGrid receivers({25.0, 50.0}, {10.0, 55.0});
  const SimulationCase dual =
      makeCase(dualSources(), receivers, {100.0}, SimulationRunMode::RayTrace);
  const std::vector<RayPathCache> caches = traceRayProducts(dual);
  const std::filesystem::path path = directory.path() / "order.ray";
  {
    RayWriter writer(path, "Order", dual, 100.0);
    context.expectThrows<ValidationError>(
        [&writer, &caches] { writer.appendSource(1U, caches[1U]); },
        "R writer rejects an out-of-order source");
    context.expectThrows<ValidationError>(
        [&writer] { writer.finalize(); },
        "R writer rejects finalize before every source is appended");
    writer.appendSource(0U, caches[0U]);
    context.expectThrows<ValidationError>(
        [&writer, &caches] { writer.appendSource(0U, caches[0U]); },
        "R writer rejects a repeated source index");
    writer.appendSource(1U, caches[1U]);
    writer.finalize();
  }
  context.check(std::filesystem::file_size(path) > 0U,
                "R per-source sequence publishes the dual-source product");
}

void testRayWriterSingleSourceByteIdentity(Context& context) {
  TemporaryDirectory directory;
  const ReceiverGrid receivers({25.0, 50.0}, {10.0, 55.0});
  const SimulationCase single = makeSingleSourceCase(
      receivers, {100.0}, SimulationRunMode::RayTrace);
  const RayPathCache cache = traceRayProduct(single);
  const std::filesystem::path appendPath = directory.path() / "append.ray";
  const std::filesystem::path appendSourcePath =
      directory.path() / "append_source.ray";
  {
    RayWriter writer(appendPath, "Byte identity", single, 100.0);
    writer.append(cache);
    writer.finalize();
  }
  {
    RayWriter writer(appendSourcePath, "Byte identity", single, 100.0);
    writer.appendSource(0U, cache);
    writer.finalize();
  }
  context.check(readBytes(appendPath) == readBytes(appendSourcePath),
                "NSz==1 R append entries are byte-identical");
}

void testEigenrayMultiSourceLayout(Context& context) {
  TemporaryDirectory directory;
  const ReceiverGrid receivers({25.0, 50.0}, {10.0, 55.0});
  const SimulationCase dual = makeCase(
      dualSources(), receivers, {100.0}, SimulationRunMode::Eigenray,
      BeamFamily::GeometricGaussian);
  const std::vector<rayreuse::RayFanTraceResult> fans =
      SingleFrequencySolver::traceAllSourceFans(dual);
  std::vector<RayPathCache> caches;
  for (const rayreuse::RayFanTraceResult& fan : fans) {
    caches.push_back(fan.cache);
  }
  std::vector<std::vector<std::pair<std::size_t, EigenrayHit>>> sourceHits;
  sourceHits.push_back({{1U, EigenrayHit{0U, 0U, 3U}}});
  sourceHits.push_back({{0U, EigenrayHit{1U, 1U, 3U}}});
  const std::filesystem::path path = directory.path() / "dual_eigenray.ray";

  EigenrayWriter::write(path, "Dual E fixture", dual, 100.0, caches,
                        sourceHits);

  const std::vector<std::string> lines = readLines(path);
  context.check(lines.size() > 7U && lines[2U] == "1 1 2",
                "E dual-source header carries '1 1 NSz'");
  // Body order: source 0's hit (launch index 1) before source 1's hit
  // (launch index 0); each hit block is angle line + count line + points.
  const std::size_t firstAngleLine = 7U;
  const double firstAngle = std::stod(lines[firstAngleLine]);
  const double secondAngle = std::stod(lines[firstAngleLine + 5U]);
  const double expectedFirst =
      dual.launchFanPlan().launchAngles[1U] * 180.0 / std::numbers::pi;
  const double expectedSecond =
      dual.launchFanPlan().launchAngles[0U] * 180.0 / std::numbers::pi;
  context.check(std::abs(firstAngle - expectedFirst) < 1.0e-9 &&
                    std::abs(secondAngle - expectedSecond) < 1.0e-9,
                "E body lists source-major hit sections");

  context.expectThrows<ValidationError>(
      [&] {
        EigenrayWriter::write(path, "wrong source count", dual, 100.0,
                              std::span<const RayPathCache>(&caches.front(),
                                                            1U),
                              std::span(sourceHits).subspan(0U, 1U));
      },
      "E writer rejects a cache count below the simulation source count");
}

void testEigenraySingleSourceByteIdentity(Context& context) {
  TemporaryDirectory directory;
  const ReceiverGrid receivers({25.0, 50.0}, {10.0, 55.0});
  const SimulationCase single = makeSingleSourceCase(
      receivers, {100.0}, SimulationRunMode::Eigenray,
      BeamFamily::GeometricGaussian);
  const std::vector<rayreuse::RayFanTraceResult> fans =
      SingleFrequencySolver::traceAllSourceFans(single);
  const RayPathCache& cache = fans.front().cache;
  const std::vector<std::pair<std::size_t, EigenrayHit>> hits = {
      {0U, EigenrayHit{0U, 0U, 3U}}};
  const std::filesystem::path legacyPath = directory.path() / "legacy.ray";
  const std::filesystem::path spanPath = directory.path() / "span.ray";

  EigenrayWriter::write(legacyPath, "Byte identity", single, 100.0, cache,
                        hits);
  EigenrayWriter::write(spanPath, "Byte identity", single, 100.0,
                        std::span<const RayPathCache>(&cache, 1U),
                        std::span(&hits, 1U));
  context.check(readBytes(legacyPath) == readBytes(spanPath),
                "NSz==1 E per-source entry is byte-identical to legacy");
}

}  // namespace

int main() {
  Context context;
  testShdMultiSourceHeaderAndRecordLayout(context);
  testShdMultiFrequencyMultiSourceLayout(context);
  testShdIrregularLayout(context);
  testShdSingleSourceByteIdentity(context);
  testArrivalMultiSourceLayout(context);
  testArrivalSingleSourceByteIdentity(context);
  testRayWriterMultiSourceLayout(context);
  testRayWriterPerSourceValidation(context);
  testRayWriterSingleSourceByteIdentity(context);
  testEigenrayMultiSourceLayout(context);
  testEigenraySingleSourceByteIdentity(context);

  if (context.failureCount() != 0U) {
    std::cerr << context.failureCount()
              << " multi-source writer assertion(s) failed\n";
    return 1;
  }
  std::cout << "All Bellhop RayReuse multi-source writer tests passed\n";
  return 0;
}
