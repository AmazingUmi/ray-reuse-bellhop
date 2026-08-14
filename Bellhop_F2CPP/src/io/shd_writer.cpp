#include "bellhop/io/shd_writer.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <vector>

#include "bellhop/error.hpp"
#include "bellhop/io/output_layout.hpp"

namespace bellhop {
namespace {

class AtomicBinaryOutput {
 public:
  explicit AtomicBinaryOutput(std::filesystem::path finalPath)
      : finalPath_(std::move(finalPath)),
        temporaryPath_(finalPath_.string() + ".tmp"),
        output_(temporaryPath_, std::ios::binary | std::ios::trunc) {
    if (!output_.is_open()) {
      throw BellhopError(
          "unable to open temporary SHD output: " +
          temporaryPath_.string());
    }
  }

  AtomicBinaryOutput(const AtomicBinaryOutput&) = delete;
  AtomicBinaryOutput& operator=(const AtomicBinaryOutput&) = delete;

  ~AtomicBinaryOutput() {
    if (!committed_) {
      output_.close();
      std::error_code ignored;
      std::filesystem::remove(temporaryPath_, ignored);
    }
  }

  [[nodiscard]] std::ofstream& stream() noexcept { return output_; }

  void commit() {
    output_.close();
    if (!output_) {
      throw BellhopError(
          "failed to finalize temporary SHD output: " +
          temporaryPath_.string());
    }
    std::error_code error;
    std::filesystem::rename(temporaryPath_, finalPath_, error);
    if (error) {
      throw BellhopError(
          "unable to publish SHD output: " + error.message());
    }
    committed_ = true;
  }

 private:
  std::filesystem::path finalPath_;
  std::filesystem::path temporaryPath_;
  std::ofstream output_;
  bool committed_{};
};

void requireFinite(double value, std::string_view name) {
  if (!std::isfinite(value)) {
    throw ValidationError(
        std::string(name) + " must be finite");
  }
}

void requireFiniteComplex(std::complex<double> value,
                          std::string_view name) {
  if (!std::isfinite(value.real()) ||
      !std::isfinite(value.imag())) {
    throw ValidationError(
        std::string(name) + " must be finite");
  }
}

template <typename Unsigned>
void storeLittleEndian(std::vector<std::byte>& record,
                       std::size_t offset, Unsigned value) {
  static_assert(std::is_unsigned_v<Unsigned>);
  if (offset > record.size() ||
      sizeof(Unsigned) > record.size() - offset) {
    throw ValidationError(
        "SHD record packing exceeded record size");
  }
  for (std::size_t byteIndex = 0U;
       byteIndex < sizeof(Unsigned); ++byteIndex) {
    record[offset + byteIndex] = static_cast<std::byte>(
        value & static_cast<Unsigned>(0xffU));
    value >>= 8U;
  }
}

void storeInt32(std::vector<std::byte>& record,
                std::size_t offset, std::int32_t value) {
  storeLittleEndian(
      record, offset,
      std::bit_cast<std::uint32_t>(value));
}

void storeFloat32(std::vector<std::byte>& record,
                  std::size_t offset, float value) {
  storeLittleEndian(
      record, offset,
      std::bit_cast<std::uint32_t>(value));
}

void storeFloat64(std::vector<std::byte>& record,
                  std::size_t offset, double value) {
  storeLittleEndian(
      record, offset,
      std::bit_cast<std::uint64_t>(value));
}

void storeText(std::vector<std::byte>& record,
               std::size_t offset, std::size_t width,
               std::string_view text) {
  if (offset > record.size() ||
      width > record.size() - offset ||
      text.size() > width) {
    throw ValidationError(
        "SHD text field exceeded record size");
  }
  for (std::size_t index = 0U; index < text.size(); ++index) {
    record[offset + index] =
        static_cast<std::byte>(
            static_cast<unsigned char>(text[index]));
  }
  for (std::size_t index = text.size(); index < width; ++index) {
    record[offset + index] = static_cast<std::byte>(' ');
  }
}

[[nodiscard]] std::int32_t checkedInt32(
    std::size_t value, std::string_view name) {
  if (value >
      static_cast<std::size_t>(
          std::numeric_limits<std::int32_t>::max())) {
    throw ValidationError(
        std::string(name) + " exceeds the SHD int32 limit");
  }
  return static_cast<std::int32_t>(value);
}

void writeRecord(std::ofstream& output,
                 const std::vector<std::byte>& record) {
  output.write(
      reinterpret_cast<const char*>(record.data()),
      static_cast<std::streamsize>(record.size()));
  if (!output) {
    throw BellhopError("failed while writing SHD record");
  }
}

[[nodiscard]] float checkedFloat32(
    double value, std::string_view name) {
  requireFinite(value, name);
  const float converted = static_cast<float>(value);
  if (!std::isfinite(converted)) {
    throw ValidationError(
        std::string(name) +
        " is outside the SHD float32 range");
  }
  return converted;
}

}  // namespace

void ShdWriter::writeSingleFrequency(
    const std::filesystem::path& path,
    std::string_view title,
    const SimulationCase& simulation,
    const FrequencyWorkspace& workspace) {
  writeSingleFrequency(path, title, simulation, workspace, {});
}

void ShdWriter::writeSingleFrequency(
    const std::filesystem::path& path,
    std::string_view title,
    const SimulationCase& simulation,
    const FrequencyWorkspace& firstSourceWorkspace,
    std::span<const FrequencyWorkspace> additionalSourceWorkspaces) {
  if (title.empty() || title.size() > 80U) {
    throw ValidationError(
        "SHD title must contain between 1 and 80 characters");
  }
  for (const char character : title) {
    const unsigned char byte =
        static_cast<unsigned char>(character);
    if (byte < 0x20U || byte > 0x7eU) {
      throw ValidationError(
          "SHD title must contain printable ASCII characters");
    }
  }

  const ReceiverGrid& receivers = simulation.receivers();
  if (!isTransmissionLossMode(simulation.runMode())) {
    throw ValidationError("SHD writer requires a transmission-loss run mode");
  }
  const double frequency =
      simulation.frequencies().values().front();
  if (additionalSourceWorkspaces.size() != simulation.sourceCount() - 1U) {
    throw ValidationError(
        "SHD source workspace count must match the simulation sources");
  }
  const auto workspaceForSource =
      [&](std::size_t sourceIndex) -> const FrequencyWorkspace& {
    return sourceIndex == 0U
               ? firstSourceWorkspace
               : additionalSourceWorkspaces[sourceIndex - 1U];
  };
  for (std::size_t sourceIndex = 0U;
       sourceIndex < simulation.sourceCount(); ++sourceIndex) {
    const FrequencyWorkspace& workspace = workspaceForSource(sourceIndex);
    if (workspace.frequency() != frequency) {
      throw ValidationError(
          "SHD workspace frequency must match the simulation");
    }
    if (workspace.depthCount() != receivers.receiversPerRange() ||
        workspace.rangeCount() != receivers.rangeCount()) {
      throw ValidationError(
          "SHD workspace dimensions must match the receiver grid");
    }
    for (const std::complex<double> pressure : workspace.pressure()) {
      requireFiniteComplex(pressure, "SHD pressure");
      static_cast<void>(
          checkedFloat32(pressure.real(), "SHD pressure real part"));
      static_cast<void>(
          checkedFloat32(pressure.imag(), "SHD pressure imaginary part"));
    }
  }

  for (const double depth : receivers.depths()) {
    static_cast<void>(
        checkedFloat32(depth, "SHD receiver depth"));
  }
  for (const double range : receivers.ranges()) {
    requireFinite(range, "SHD receiver range");
  }
  for (const Source& source : simulation.sources()) {
    static_cast<void>(
        checkedFloat32(source.depth, "SHD source depth"));
  }

  const std::size_t sourceDepthCount = simulation.sourceCount();
  const std::size_t receiverDepthCount =
      receivers.depthCount();
  const std::size_t receiverRecordsPerRange =
      receivers.receiversPerRange();
  const std::size_t receiverRangeCount =
      receivers.rangeCount();
  const Shd2DLayout layout = planShd2DLayout(
      sourceDepthCount, receiverDepthCount, receiverRangeCount,
      receivers.isIrregular());
  const std::size_t recordWords = layout.recordWords;
  const std::size_t recordBytes = layout.recordBytes;

  const std::int32_t recordWords32 =
      checkedInt32(recordWords, "SHD record word count");
  const std::int32_t receiverDepthCount32 =
      checkedInt32(
          receiverDepthCount, "SHD receiver-depth count");
  const std::int32_t receiverRangeCount32 =
      checkedInt32(
          receiverRangeCount, "SHD receiver-range count");
  const std::int32_t sourceDepthCount32 =
      checkedInt32(sourceDepthCount, "SHD source-depth count");

  AtomicBinaryOutput atomicOutput(path);
  std::ofstream& output = atomicOutput.stream();

  std::vector<std::byte> record(recordBytes);
  const auto clearRecord = [&] {
    std::fill(
        record.begin(), record.end(), std::byte{});
  };

  clearRecord();
  storeInt32(record, 0U, recordWords32);
  storeText(record, 4U, 80U, title);
  writeRecord(output, record);

  clearRecord();
  storeText(record, 0U, 10U,
            receivers.isIrregular() ? "irregular " : "rectilin  ");
  writeRecord(output, record);

  clearRecord();
  storeInt32(record, 0U, 1);
  storeInt32(record, 4U, 1);
  storeInt32(record, 8U, 1);
  storeInt32(record, 12U, 1);
  storeInt32(record, 16U, sourceDepthCount32);
  storeInt32(record, 20U, receiverDepthCount32);
  storeInt32(record, 24U, receiverRangeCount32);
  storeFloat64(record, 28U, frequency);
  storeFloat64(record, 36U, 0.0);
  writeRecord(output, record);

  clearRecord();
  storeFloat64(record, 0U, frequency);
  writeRecord(output, record);

  clearRecord();
  storeFloat64(record, 0U, 0.0);
  writeRecord(output, record);

  clearRecord();
  storeFloat64(record, 0U, 0.0);
  writeRecord(output, record);

  clearRecord();
  storeFloat64(record, 0U, 0.0);
  writeRecord(output, record);

  clearRecord();
  for (std::size_t sourceIndex = 0U;
       sourceIndex < sourceDepthCount; ++sourceIndex) {
    storeFloat32(
        record, 4U * sourceIndex,
        checkedFloat32(
            simulation.sources()[sourceIndex].depth, "SHD source depth"));
  }
  writeRecord(output, record);

  clearRecord();
  for (std::size_t depthIndex = 0U;
       depthIndex < receiverDepthCount; ++depthIndex) {
    storeFloat32(
        record, 4U * depthIndex,
        checkedFloat32(
            receivers.depths()[depthIndex],
            "SHD receiver depth"));
  }
  writeRecord(output, record);

  clearRecord();
  for (std::size_t rangeIndex = 0U;
       rangeIndex < receiverRangeCount; ++rangeIndex) {
    storeFloat64(
        record, 8U * rangeIndex,
        receivers.ranges()[rangeIndex]);
  }
  writeRecord(output, record);

  for (std::size_t sourceIndex = 0U;
       sourceIndex < sourceDepthCount; ++sourceIndex) {
    const FrequencyWorkspace& workspace = workspaceForSource(sourceIndex);
    for (std::size_t depthIndex = 0U;
         depthIndex < receiverRecordsPerRange; ++depthIndex) {
      clearRecord();
      for (std::size_t rangeIndex = 0U;
           rangeIndex < receiverRangeCount; ++rangeIndex) {
        const std::complex<double> pressure =
            workspace.at(depthIndex, rangeIndex);
        storeFloat32(
            record, 8U * rangeIndex,
            checkedFloat32(
                pressure.real(), "SHD pressure real part"));
        storeFloat32(
            record, 8U * rangeIndex + 4U,
            checkedFloat32(
                pressure.imag(), "SHD pressure imaginary part"));
      }
      writeRecord(output, record);
    }
  }

  atomicOutput.commit();
}

}  // namespace bellhop
