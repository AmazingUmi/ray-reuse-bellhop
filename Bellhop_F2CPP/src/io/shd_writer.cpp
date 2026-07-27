#include "bellhop/io/shd_writer.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "bellhop/error.hpp"

namespace bellhop {
namespace {

constexpr std::size_t kMinimumRecordWords = 41U;

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
  const double frequency =
      simulation.frequencies().values().front();
  if (workspace.frequency() != frequency) {
    throw ValidationError(
        "SHD workspace frequency must match the simulation");
  }
  if (workspace.depthCount() != receivers.depthCount() ||
      workspace.rangeCount() != receivers.rangeCount()) {
    throw ValidationError(
        "SHD workspace dimensions must match the receiver grid");
  }

  for (const double depth : receivers.depths()) {
    static_cast<void>(
        checkedFloat32(depth, "SHD receiver depth"));
  }
  for (const double range : receivers.ranges()) {
    requireFinite(range, "SHD receiver range");
  }
  static_cast<void>(checkedFloat32(
      simulation.source().depth, "SHD source depth"));
  for (const std::complex<double> pressure :
       workspace.pressure()) {
    requireFiniteComplex(pressure, "SHD pressure");
    static_cast<void>(
        checkedFloat32(pressure.real(), "SHD pressure real part"));
    static_cast<void>(
        checkedFloat32(pressure.imag(), "SHD pressure imaginary part"));
  }

  constexpr std::size_t frequencyCount = 1U;
  constexpr std::size_t bearingCount = 1U;
  constexpr std::size_t sourceXCount = 1U;
  constexpr std::size_t sourceYCount = 1U;
  constexpr std::size_t sourceDepthCount = 1U;
  const std::size_t receiverDepthCount =
      receivers.depthCount();
  const std::size_t receiverRangeCount =
      receivers.rangeCount();
  if (receiverRangeCount >
      std::numeric_limits<std::size_t>::max() / 2U) {
    throw ValidationError(
        "SHD receiver-range count overflow");
  }

  const std::size_t recordWords = std::max(
      {kMinimumRecordWords,
       2U * frequencyCount,
       2U * bearingCount,
       2U * sourceXCount,
       2U * sourceYCount,
       sourceDepthCount,
       receiverDepthCount,
       2U * receiverRangeCount});
  if (recordWords >
      std::numeric_limits<std::size_t>::max() / 4U) {
    throw ValidationError("SHD record byte count overflow");
  }
  const std::size_t recordBytes = 4U * recordWords;
  if (recordBytes >
      static_cast<std::size_t>(
          std::numeric_limits<std::streamsize>::max())) {
    throw ValidationError(
        "SHD record exceeds streamsize capacity");
  }

  const std::int32_t recordWords32 =
      checkedInt32(recordWords, "SHD record word count");
  const std::int32_t receiverDepthCount32 =
      checkedInt32(
          receiverDepthCount, "SHD receiver-depth count");
  const std::int32_t receiverRangeCount32 =
      checkedInt32(
          receiverRangeCount, "SHD receiver-range count");

  std::ofstream output(
      path, std::ios::binary | std::ios::trunc);
  if (!output.is_open()) {
    throw BellhopError(
        "unable to open SHD output: " + path.string());
  }

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
  storeText(record, 0U, 10U, "rectilin  ");
  writeRecord(output, record);

  clearRecord();
  storeInt32(record, 0U, 1);
  storeInt32(record, 4U, 1);
  storeInt32(record, 8U, 1);
  storeInt32(record, 12U, 1);
  storeInt32(record, 16U, 1);
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
  storeFloat32(
      record, 0U,
      checkedFloat32(
          simulation.source().depth, "SHD source depth"));
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

  for (std::size_t depthIndex = 0U;
       depthIndex < receiverDepthCount; ++depthIndex) {
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

  output.close();
  if (!output) {
    throw BellhopError(
        "failed to finalize SHD output: " + path.string());
  }
}

}  // namespace bellhop
