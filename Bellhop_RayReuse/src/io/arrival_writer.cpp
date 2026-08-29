#include "rayreuse/io/arrival_writer.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numbers>
#include <span>
#include <string>
#include <system_error>
#include <vector>

#include "rayreuse/error.hpp"

namespace rayreuse {
namespace {
void checkWorkspaces(const SimulationCase& simulation,
                     std::span<const ArrivalWorkspace> sourceWorkspaces,
                     ArrivalEncoding encoding) {
  const SimulationRunMode requiredMode =
      encoding == ArrivalEncoding::Ascii ? SimulationRunMode::AsciiArrivals
                                         : SimulationRunMode::BinaryArrivals;
  if (simulation.runMode() != requiredMode) {
    throw ValidationError(
        "ARR encoding does not match the simulation run mode");
  }
  if (sourceWorkspaces.size() != simulation.sourceCount()) {
    throw ValidationError(
        "ARR source workspace count must match the simulation sources");
  }
  const double frequency = sourceWorkspaces.front().frequency();
  if (frequency <= 0.0 ||
      std::find(simulation.frequencies().values().begin(),
                simulation.frequencies().values().end(),
                frequency) == simulation.frequencies().values().end()) {
    throw ValidationError("ARR workspace metadata does not match simulation");
  }
  for (const ArrivalWorkspace& workspace : sourceWorkspaces) {
    if (workspace.frequency() != frequency ||
        workspace.depthCount() !=
            simulation.receivers().receiversPerRange() ||
        workspace.rangeCount() != simulation.receivers().rangeCount()) {
      throw ValidationError("ARR workspace metadata does not match simulation");
    }
  }
}
float pointSourceScale(double range) {
  if (!std::isfinite(range) || range < 0.0)
    throw ValidationError("ARR receiver range must be finite and non-negative");
  return range == 0.0 ? 1.0e5F : static_cast<float>(1.0 / std::sqrt(range));
}
void writeAsciiSourceBlock(std::ofstream& output,
                           const SimulationCase& simulation,
                           const ArrivalWorkspace& workspace) {
  std::size_t maximum = 0U;
  for (std::size_t cell = 0U; cell < workspace.receiverCellCount(); ++cell)
    maximum = std::max(maximum, workspace.cellAt(cell).size());
  output << std::setprecision(std::numeric_limits<float>::max_digits10)
         << maximum << '\n';
  const float radiansToDegrees = static_cast<float>(180.0 / std::numbers::pi);
  for (std::size_t d = 0U; d < workspace.depthCount(); ++d)
    for (std::size_t r = 0U; r < workspace.rangeCount(); ++r) {
      const auto arrivals = workspace.arrivalsAt(d, r);
      const float scale = pointSourceScale(simulation.receivers().ranges()[r]);
      output << arrivals.size() << '\n';
      for (const Arrival& a : arrivals)
        output << scale * a.amplitude << ' '
               << radiansToDegrees * a.phaseRadians << ' '
               << a.delaySeconds.real() << ' ' << a.delaySeconds.imag() << ' '
               << a.sourceDeclinationDegrees << ' '
               << a.receiverDeclinationDegrees << ' ' << a.topBounceCount
               << ' ' << a.bottomBounceCount << '\n';
    }
}
void writeAscii(const std::filesystem::path& path, std::string_view title,
                const SimulationCase& simulation,
                std::span<const ArrivalWorkspace> sourceWorkspaces) {
  std::ofstream output(path, std::ios::trunc);
  if (!output)
    throw BellhopError("unable to open ARR output: " + path.string());
  static_cast<void>(title);
  // Origin ArrMod/ReadEnvironmentBell header: '2D', frequency, then one line
  // per axis holding the count followed by the values (NSz Sz(1:NSz) here).
  output << "'2D'\n"
         << std::setprecision(std::numeric_limits<double>::max_digits10)
         << sourceWorkspaces.front().frequency() << '\n'
         << simulation.sourceCount();
  for (const Source& source : simulation.sources())
    output << ' ' << source.depth;
  output << '\n' << simulation.receivers().depthCount();
  for (double d : simulation.receivers().depths()) output << ' ' << d;
  output << '\n' << simulation.receivers().rangeCount();
  for (double r : simulation.receivers().ranges()) output << ' ' << r;
  output << '\n';
  for (const ArrivalWorkspace& workspace : sourceWorkspaces)
    writeAsciiSourceBlock(output, simulation, workspace);
  if (!output) throw BellhopError("failed while writing ARR output");
}
void store32(std::vector<std::byte>& bytes, std::size_t offset,
             std::uint32_t value) {
  for (std::size_t i = 0U; i < 4U; ++i)
    bytes.at(offset + i) = static_cast<std::byte>((value >> (8U * i)) & 0xffU);
}
void record(std::ofstream& output, const std::vector<std::byte>& payload) {
  if (payload.size() >
      static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
    throw ValidationError("ARR binary record is too large");
  const auto size = static_cast<std::uint32_t>(payload.size());
  std::vector<std::byte> marker(4U);
  store32(marker, 0U, size);
  output.write(reinterpret_cast<const char*>(marker.data()), 4);
  output.write(reinterpret_cast<const char*>(payload.data()),
               static_cast<std::streamsize>(payload.size()));
  output.write(reinterpret_cast<const char*>(marker.data()), 4);
}
void writeBinarySourceBlock(std::ofstream& output,
                            const SimulationCase& simulation,
                            const ArrivalWorkspace& workspace) {
  std::size_t maximum = 0U;
  for (std::size_t c = 0U; c < workspace.receiverCellCount(); ++c)
    maximum = std::max(maximum, workspace.cellAt(c).size());
  std::vector<std::byte> count(4U);
  store32(count, 0U, static_cast<std::uint32_t>(maximum));
  record(output, count);
  const float radiansToDegrees = static_cast<float>(180.0 / std::numbers::pi);
  for (std::size_t d = 0U; d < workspace.depthCount(); ++d)
    for (std::size_t r = 0U; r < workspace.rangeCount(); ++r) {
      const auto arrivals = workspace.arrivalsAt(d, r);
      std::vector<std::byte> cell(4U);
      store32(cell, 0U, static_cast<std::uint32_t>(arrivals.size()));
      record(output, cell);
      const float scale = pointSourceScale(simulation.receivers().ranges()[r]);
      for (const Arrival& a : arrivals) {
        std::vector<std::byte> payload(32U);
        auto put = [&](std::size_t o, float v) {
          store32(payload, o, std::bit_cast<std::uint32_t>(v));
        };
        put(0U, scale * a.amplitude);
        put(4U, radiansToDegrees * a.phaseRadians);
        put(8U, a.delaySeconds.real());
        put(12U, a.delaySeconds.imag());
        put(16U, a.sourceDeclinationDegrees);
        put(20U, a.receiverDeclinationDegrees);
        put(24U, static_cast<float>(a.topBounceCount));
        put(28U, static_cast<float>(a.bottomBounceCount));
        record(output, payload);
      }
    }
}
void writeBinary(const std::filesystem::path& path,
                 const SimulationCase& simulation,
                 std::span<const ArrivalWorkspace> sourceWorkspaces) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output)
    throw BellhopError("unable to open ARR output: " + path.string());
  record(output, {static_cast<std::byte>(0x27), static_cast<std::byte>('2'),
                  static_cast<std::byte>('D'), static_cast<std::byte>(0x27)});
  std::vector<std::byte> frequency(4U);
  store32(
      frequency, 0U,
      std::bit_cast<std::uint32_t>(
          static_cast<float>(sourceWorkspaces.front().frequency())));
  record(output, frequency);
  // F2CPP writeBinaryHeader source record: NSz followed by Sz(1:NSz) as
  // float32 values (one Fortran unformatted record).
  std::vector<std::byte> sources(4U + 4U * simulation.sourceCount());
  store32(sources, 0U, static_cast<std::uint32_t>(simulation.sourceCount()));
  for (std::size_t i = 0U; i < simulation.sourceCount(); ++i)
    store32(sources, 4U + 4U * i,
            std::bit_cast<std::uint32_t>(
                static_cast<float>(simulation.sources()[i].depth)));
  record(output, sources);
  std::vector<std::byte> depths(4U + 4U * simulation.receivers().depthCount());
  store32(depths, 0U,
          static_cast<std::uint32_t>(simulation.receivers().depthCount()));
  for (std::size_t i = 0U; i < simulation.receivers().depthCount(); ++i)
    store32(depths, 4U + 4U * i,
            std::bit_cast<std::uint32_t>(
                static_cast<float>(simulation.receivers().depths()[i])));
  record(output, depths);
  std::vector<std::byte> ranges(4U + 8U * simulation.receivers().rangeCount());
  store32(ranges, 0U,
          static_cast<std::uint32_t>(simulation.receivers().rangeCount()));
  for (std::size_t i = 0U; i < simulation.receivers().rangeCount(); ++i) {
    const auto value =
        std::bit_cast<std::uint64_t>(simulation.receivers().ranges()[i]);
    for (std::size_t b = 0U; b < 8U; ++b)
      ranges[4U + 8U * i + b] =
          static_cast<std::byte>((value >> (8U * b)) & 0xffU);
  }
  record(output, ranges);
  for (const ArrivalWorkspace& workspace : sourceWorkspaces)
    writeBinarySourceBlock(output, simulation, workspace);
  if (!output) throw BellhopError("failed while writing binary ARR output");
}
}  // namespace
void ArrivalWriter::write(const std::filesystem::path& path,
                          std::string_view title,
                          const SimulationCase& simulation,
                          const ArrivalWorkspace& workspace,
                          ArrivalEncoding encoding) {
  write(path, title, simulation,
        std::span<const ArrivalWorkspace>(&workspace, 1U), encoding);
}
void ArrivalWriter::write(const std::filesystem::path& path,
                          std::string_view title,
                          const SimulationCase& simulation,
                          std::span<const ArrivalWorkspace> sourceWorkspaces,
                          ArrivalEncoding encoding) {
  if (sourceWorkspaces.empty()) {
    throw ValidationError(
        "ARR source workspace count must match the simulation sources");
  }
  checkWorkspaces(simulation, sourceWorkspaces, encoding);
  std::filesystem::path temporary = path;
  temporary += ".tmp";
  try {
    if (encoding == ArrivalEncoding::Ascii)
      writeAscii(temporary, title, simulation, sourceWorkspaces);
    else
      writeBinary(temporary, simulation, sourceWorkspaces);
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error)
      throw BellhopError("unable to publish ARR output: " + error.message());
  } catch (...) {
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    throw;
  }
}
}  // namespace rayreuse
