#include "bellhop/io/arrival_writer.hpp"

#include <bit>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <numbers>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

#include "bellhop/error.hpp"

namespace bellhop {
namespace {

constexpr float kRadiansToDegrees =
    static_cast<float>(180.0 / std::numbers::pi);

std::int32_t checkedInt32(std::size_t value, const char* label) {
  if (value > static_cast<std::size_t>(
                  std::numeric_limits<std::int32_t>::max())) {
    throw ValidationError(std::string(label) + " exceeds the ARR int32 limit");
  }
  return static_cast<std::int32_t>(value);
}

float checkedFloat(double value, const char* label) {
  if (!std::isfinite(value)) {
    throw ValidationError(std::string(label) + " must be finite");
  }
  const float converted = static_cast<float>(value);
  if (!std::isfinite(converted)) {
    throw ValidationError(std::string(label) + " exceeds the ARR float32 range");
  }
  return converted;
}

float checkedBounce(std::int32_t value, const char* label) {
  const float converted = static_cast<float>(value);
  if (!std::isfinite(converted) ||
      static_cast<double>(converted) != static_cast<double>(value)) {
    throw ValidationError(std::string(label) +
                          " cannot be represented exactly as ARR float32");
  }
  return converted;
}

template <typename Unsigned>
void storeLittleEndian(std::vector<std::byte>& bytes, std::size_t offset,
                       Unsigned value) {
  static_assert(std::is_unsigned_v<Unsigned>);
  if (offset > bytes.size() || sizeof(Unsigned) > bytes.size() - offset) {
    throw ValidationError("ARR binary payload packing exceeded its bounds");
  }
  for (std::size_t index = 0U; index < sizeof(Unsigned); ++index) {
    bytes[offset + index] = static_cast<std::byte>(value & 0xffU);
    value >>= 8U;
  }
}

void storeInt32(std::vector<std::byte>& bytes, std::size_t offset,
                std::int32_t value) {
  storeLittleEndian(bytes, offset, std::bit_cast<std::uint32_t>(value));
}

void storeFloat32(std::vector<std::byte>& bytes, std::size_t offset,
                  float value) {
  storeLittleEndian(bytes, offset, std::bit_cast<std::uint32_t>(value));
}

void storeFloat64(std::vector<std::byte>& bytes, std::size_t offset,
                  double value) {
  storeLittleEndian(bytes, offset, std::bit_cast<std::uint64_t>(value));
}

void storeText(std::vector<std::byte>& bytes, std::size_t offset,
               std::string_view value) {
  if (offset > bytes.size() || value.size() > bytes.size() - offset) {
    throw ValidationError("ARR binary text payload exceeded its bounds");
  }
  for (std::size_t index = 0U; index < value.size(); ++index) {
    bytes[offset + index] =
        static_cast<std::byte>(static_cast<unsigned char>(value[index]));
  }
}

void writeBytes(std::ofstream& output, std::span<const std::byte> bytes) {
  output.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  if (!output) {
    throw BellhopError("failed while writing temporary ARR output");
  }
}

void writeBinaryRecord(std::ofstream& output,
                       const std::vector<std::byte>& payload) {
  if (payload.size() > static_cast<std::size_t>(
                          std::numeric_limits<std::int32_t>::max())) {
    throw ValidationError("ARR binary record exceeds the int32 limit");
  }
  const std::int32_t length = static_cast<std::int32_t>(payload.size());
  std::vector<std::byte> marker(sizeof(length));
  storeInt32(marker, 0U, length);
  writeBytes(output, marker);
  writeBytes(output, payload);
  writeBytes(output, marker);
}

float sourceScale(SourceGeometry geometry, double range) {
  if (!std::isfinite(range) || range < 0.0) {
    throw ValidationError("ARR receiver range must be finite and non-negative");
  }
  if (geometry == SourceGeometry::Line) {
    return static_cast<float>(4.0 * std::sqrt(std::numbers::pi));
  }
  return range == 0.0 ? 1.0e5F
                      : checkedFloat(1.0 / std::sqrt(range),
                                     "ARR point-source scale");
}

std::size_t workspaceCellCount(const ReceiverGrid& receivers) {
  if (receivers.rangeCount() != 0U &&
      receivers.receiversPerRange() >
          std::numeric_limits<std::size_t>::max() /
              receivers.rangeCount()) {
    throw ValidationError("ARR receiver cell count exceeds size_t capacity");
  }
  return receivers.receiversPerRange() * receivers.rangeCount();
}

}  // namespace

ArrivalWriter::ArrivalWriter(std::filesystem::path outputPath,
                             const SimulationCase& simulation)
    : ArrivalWriter(
          std::move(outputPath), simulation,
          simulation.runMode() == SimulationRunMode::AsciiArrivals
              ? ArrivalEncoding::Ascii
              : ArrivalEncoding::Binary) {}

ArrivalWriter::ArrivalWriter(std::filesystem::path outputPath,
                             const SimulationCase& simulation,
                             ArrivalEncoding encoding)
    : outputPath_(std::move(outputPath)),
      temporaryPath_(outputPath_.string() + ".tmp"),
      simulation_(simulation),
      encoding_(encoding),
      layout_(planArrival2DLayout(
          simulation.sourceCount(), simulation.receivers().depthCount(),
          simulation.receivers().rangeCount(), simulation.receivers().isIrregular(),
          planArrivalCapacity(workspaceCellCount(simulation.receivers()))
              .arrivalsPerCell)) {
  const bool modeMatches =
      (encoding_ == ArrivalEncoding::Ascii &&
       simulation_.runMode() == SimulationRunMode::AsciiArrivals) ||
      (encoding_ == ArrivalEncoding::Binary &&
       simulation_.runMode() == SimulationRunMode::BinaryArrivals);
  if (!modeMatches) {
    throw ValidationError("ARR writer encoding does not match run mode");
  }
  output_.open(temporaryPath_, encoding_ == ArrivalEncoding::Binary
                                  ? std::ios::binary | std::ios::trunc
                                  : std::ios::out | std::ios::trunc);
  if (!output_.is_open()) {
    throw BellhopError("unable to open temporary ARR output: " +
                       temporaryPath_.string());
  }
  try {
    if (encoding_ == ArrivalEncoding::Ascii) {
      writeAsciiHeader();
    } else {
      writeBinaryHeader();
    }
  } catch (...) {
    output_.close();
    std::error_code ignored;
    std::filesystem::remove(temporaryPath_, ignored);
    throw;
  }
}

ArrivalWriter::~ArrivalWriter() {
  if (!finalized_) {
    output_.close();
    std::error_code ignored;
    std::filesystem::remove(temporaryPath_, ignored);
  }
}

void ArrivalWriter::writeAsciiHeader() {
  output_ << std::setprecision(std::numeric_limits<double>::max_digits10)
          << "'2D'\n"
          << simulation_.frequencies().values().front() << '\n'
          << simulation_.sourceCount();
  for (const Source& source : simulation_.sources()) {
    output_ << ' ' << source.depth;
  }
  output_ << '\n' << simulation_.receivers().depthCount();
  for (const double depth : simulation_.receivers().depths()) {
    output_ << ' ' << depth;
  }
  output_ << '\n' << simulation_.receivers().rangeCount();
  for (const double range : simulation_.receivers().ranges()) {
    output_ << ' ' << range;
  }
  output_ << '\n';
  if (!output_) {
    throw BellhopError("failed while writing temporary ARR header");
  }
}

void ArrivalWriter::writeBinaryHeader() {
  std::vector<std::byte> tag(4U);
  storeText(tag, 0U, "'2D'");
  writeBinaryRecord(output_, tag);

  std::vector<std::byte> frequency(4U);
  storeFloat32(frequency, 0U,
               checkedFloat(simulation_.frequencies().values().front(),
                            "ARR frequency"));
  writeBinaryRecord(output_, frequency);

  std::vector<std::byte> sourceDepths(
      4U + 4U * simulation_.sourceCount());
  storeInt32(sourceDepths, 0U,
             checkedInt32(simulation_.sourceCount(), "ARR source count"));
  for (std::size_t index = 0U; index < simulation_.sourceCount(); ++index) {
    storeFloat32(sourceDepths, 4U + 4U * index,
                 checkedFloat(simulation_.sources()[index].depth,
                              "ARR source depth"));
  }
  writeBinaryRecord(output_, sourceDepths);

  std::vector<std::byte> receiverDepths(
      4U + 4U * simulation_.receivers().depthCount());
  storeInt32(receiverDepths, 0U,
             checkedInt32(simulation_.receivers().depthCount(),
                          "ARR receiver-depth count"));
  for (std::size_t index = 0U;
       index < simulation_.receivers().depthCount(); ++index) {
    storeFloat32(receiverDepths, 4U + 4U * index,
                 checkedFloat(simulation_.receivers().depths()[index],
                              "ARR receiver depth"));
  }
  writeBinaryRecord(output_, receiverDepths);

  std::vector<std::byte> receiverRanges(
      4U + 8U * simulation_.receivers().rangeCount());
  storeInt32(receiverRanges, 0U,
             checkedInt32(simulation_.receivers().rangeCount(),
                          "ARR receiver-range count"));
  for (std::size_t index = 0U;
       index < simulation_.receivers().rangeCount(); ++index) {
    storeFloat64(receiverRanges, 4U + 8U * index,
                 simulation_.receivers().ranges()[index]);
  }
  writeBinaryRecord(output_, receiverRanges);
}

void ArrivalWriter::appendSource(std::size_t sourceIndex,
                                 const ArrivalWorkspace& workspace) {
  if (finalized_ || sourceIndex != nextSourceIndex_) {
    throw ValidationError("ARR writer source order is invalid");
  }
  if (workspace.frequency() != simulation_.frequencies().values().front() ||
      workspace.depthCount() != simulation_.receivers().receiversPerRange() ||
      workspace.rangeCount() != simulation_.receivers().rangeCount() ||
      workspace.capacity().receiverCellCount != layout_.actualCellsPerSource ||
      workspace.capacity().arrivalsPerCell != layout_.perCellCapacity) {
    throw ValidationError("ARR workspace metadata does not match simulation");
  }
  if (encoding_ == ArrivalEncoding::Ascii) {
    writeAsciiSource(workspace);
  } else {
    writeBinarySource(workspace);
  }
  ++nextSourceIndex_;
}

void ArrivalWriter::writeAsciiSource(const ArrivalWorkspace& workspace) {
  std::size_t maximum = 0U;
  for (std::size_t cell = 0U; cell < workspace.receiverCellCount(); ++cell) {
    maximum = std::max(maximum, workspace.cellAt(cell).size());
  }
  output_ << std::setprecision(std::numeric_limits<float>::max_digits10)
          << maximum << '\n';
  for (std::size_t depthIndex = 0U;
       depthIndex < workspace.depthCount(); ++depthIndex) {
    for (std::size_t rangeIndex = 0U;
         rangeIndex < workspace.rangeCount(); ++rangeIndex) {
      const auto arrivals = workspace.arrivalsAt(depthIndex, rangeIndex);
      output_ << arrivals.size() << '\n';
      const float factor = sourceScale(
          simulation_.sourceGeometry(),
          simulation_.receivers().ranges()[rangeIndex]);
      for (const Arrival& arrival : arrivals) {
        output_ << factor * arrival.amplitude << ' '
                << kRadiansToDegrees * arrival.phaseRadians << ' '
                << arrival.delaySeconds.real() << ' '
                << arrival.delaySeconds.imag() << ' '
                << arrival.sourceDeclinationDegrees << ' '
                << arrival.receiverDeclinationDegrees << ' '
                << arrival.topBounceCount << ' '
                << arrival.bottomBounceCount << '\n';
      }
    }
  }
  if (!output_) {
    throw BellhopError("failed while writing temporary ARR source");
  }
}

void ArrivalWriter::writeBinarySource(const ArrivalWorkspace& workspace) {
  std::size_t maximum = 0U;
  for (std::size_t cell = 0U; cell < workspace.receiverCellCount(); ++cell) {
    maximum = std::max(maximum, workspace.cellAt(cell).size());
  }
  std::vector<std::byte> count(4U);
  storeInt32(count, 0U, checkedInt32(maximum, "ARR maximum cell count"));
  writeBinaryRecord(output_, count);

  for (std::size_t depthIndex = 0U;
       depthIndex < workspace.depthCount(); ++depthIndex) {
    for (std::size_t rangeIndex = 0U;
         rangeIndex < workspace.rangeCount(); ++rangeIndex) {
      const auto arrivals = workspace.arrivalsAt(depthIndex, rangeIndex);
      std::vector<std::byte> cellCount(4U);
      storeInt32(cellCount, 0U,
                 checkedInt32(arrivals.size(), "ARR cell count"));
      writeBinaryRecord(output_, cellCount);
      const float factor = sourceScale(
          simulation_.sourceGeometry(),
          simulation_.receivers().ranges()[rangeIndex]);
      for (const Arrival& arrival : arrivals) {
        std::vector<std::byte> payload(32U);
        storeFloat32(payload, 0U, factor * arrival.amplitude);
        storeFloat32(payload, 4U,
                     kRadiansToDegrees * arrival.phaseRadians);
        storeFloat32(payload, 8U, arrival.delaySeconds.real());
        storeFloat32(payload, 12U, arrival.delaySeconds.imag());
        storeFloat32(payload, 16U, arrival.sourceDeclinationDegrees);
        storeFloat32(payload, 20U, arrival.receiverDeclinationDegrees);
        storeFloat32(payload, 24U,
                     checkedBounce(arrival.topBounceCount,
                                   "ARR top bounce count"));
        storeFloat32(payload, 28U,
                     checkedBounce(arrival.bottomBounceCount,
                                   "ARR bottom bounce count"));
        writeBinaryRecord(output_, payload);
      }
    }
  }
}

void ArrivalWriter::finalize() {
  if (finalized_ || nextSourceIndex_ != simulation_.sourceCount()) {
    throw ValidationError("ARR writer cannot finalize an incomplete run");
  }
  output_.close();
  if (!output_) {
    throw BellhopError("failed to finalize temporary ARR output");
  }
  std::error_code error;
  std::filesystem::rename(temporaryPath_, outputPath_, error);
  if (error) {
    throw BellhopError("unable to publish ARR output: " + error.message());
  }
  finalized_ = true;
}

}  // namespace bellhop
