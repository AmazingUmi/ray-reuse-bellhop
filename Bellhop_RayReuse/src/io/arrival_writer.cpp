#include "rayreuse/io/arrival_writer.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numbers>
#include <string>
#include <system_error>
#include <vector>

#include "rayreuse/error.hpp"

namespace rayreuse {
namespace {
void checkWorkspace(const SimulationCase& simulation,
                    const ArrivalWorkspace& workspace,
                    ArrivalEncoding encoding) {
  const SimulationRunMode requiredMode =
      encoding == ArrivalEncoding::Ascii ? SimulationRunMode::AsciiArrivals
                                         : SimulationRunMode::BinaryArrivals;
  if (simulation.runMode() != requiredMode) {
    throw ValidationError(
        "ARR encoding does not match the simulation run mode");
  }
  if (workspace.frequency() <= 0.0 ||
      std::find(simulation.frequencies().values().begin(),
                simulation.frequencies().values().end(),
                workspace.frequency()) ==
          simulation.frequencies().values().end() ||
      workspace.depthCount() != simulation.receivers().depthCount() ||
      workspace.rangeCount() != simulation.receivers().rangeCount()) {
    throw ValidationError("ARR workspace metadata does not match simulation");
  }
}
float pointSourceScale(double range) {
  if (!std::isfinite(range) || range < 0.0)
    throw ValidationError("ARR receiver range must be finite and non-negative");
  return range == 0.0 ? 1.0e5F : static_cast<float>(1.0 / std::sqrt(range));
}
void writeAscii(const std::filesystem::path& path, std::string_view title,
                const SimulationCase& simulation,
                const ArrivalWorkspace& workspace) {
  std::ofstream output(path, std::ios::trunc);
  if (!output)
    throw BellhopError("unable to open ARR output: " + path.string());
  static_cast<void>(title);
  output << "'2D'\n"
         << std::setprecision(std::numeric_limits<double>::max_digits10)
         << workspace.frequency() << '\n'
         << "1 " << simulation.source().depth << '\n'
         << simulation.receivers().depthCount();
  for (double d : simulation.receivers().depths()) output << ' ' << d;
  output << '\n' << simulation.receivers().rangeCount();
  for (double r : simulation.receivers().ranges()) output << ' ' << r;
  output << '\n';
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
               << a.receiverDeclinationDegrees << ' ' << a.topBounceCount << ' '
               << a.bottomBounceCount << '\n';
    }
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
void writeBinary(const std::filesystem::path& path,
                 const SimulationCase& simulation,
                 const ArrivalWorkspace& workspace) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output)
    throw BellhopError("unable to open ARR output: " + path.string());
  record(output, {static_cast<std::byte>(0x27), static_cast<std::byte>('2'),
                  static_cast<std::byte>('D'), static_cast<std::byte>(0x27)});
  std::vector<std::byte> frequency(4U);
  store32(
      frequency, 0U,
      std::bit_cast<std::uint32_t>(static_cast<float>(workspace.frequency())));
  record(output, frequency);
  std::vector<std::byte> source(8U);
  store32(source, 0U, 1U);
  store32(source, 4U,
          std::bit_cast<std::uint32_t>(
              static_cast<float>(simulation.source().depth)));
  record(output, source);
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
  if (!output) throw BellhopError("failed while writing binary ARR output");
}
}  // namespace
void ArrivalWriter::write(const std::filesystem::path& path,
                          std::string_view title,
                          const SimulationCase& simulation,
                          const ArrivalWorkspace& workspace,
                          ArrivalEncoding encoding) {
  checkWorkspace(simulation, workspace, encoding);
  std::filesystem::path temporary = path;
  temporary += ".tmp";
  try {
    if (encoding == ArrivalEncoding::Ascii)
      writeAscii(temporary, title, simulation, workspace);
    else
      writeBinary(temporary, simulation, workspace);
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
