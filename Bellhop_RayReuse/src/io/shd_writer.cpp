#include "rayreuse/io/shd_writer.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "rayreuse/error.hpp"

namespace rayreuse {
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

class ShdFrequencyWriter::Impl {
 public:
  Impl(const std::filesystem::path& path,
       std::string_view title,
       const SimulationCase& simulation)
      : frequencies_(simulation.frequencies().values()),
        receiverDepthCount_(
            simulation.receivers().depthCount()),
        receiverRangeCount_(
            simulation.receivers().rangeCount()),
        written_(frequencies_.size(), false),
        path_(path) {
    validateTitle(title);
    validateStaticData(simulation);
    calculateLayout();

    output_.open(path_, std::ios::binary | std::ios::trunc);
    if (!output_.is_open()) {
      throw BellhopError(
          "unable to open SHD output: " + path_.string());
    }

    writeHeader(title, simulation);
    preallocate();
  }

  ~Impl() noexcept = default;

  void writeFrequency(
      std::size_t index,
      const FrequencyWorkspace& workspace) {
    if (finalized_) {
      throw ValidationError(
          "cannot write a frequency after finalizing SHD output");
    }
    if (index >= frequencies_.size()) {
      throw ValidationError(
          "SHD frequency index is out of range");
    }
    if (written_[index]) {
      throw ValidationError(
          "SHD frequency index has already been written");
    }
    if (workspace.frequency() != frequencies_[index]) {
      throw ValidationError(
          "SHD workspace frequency does not match its index");
    }
    if (workspace.depthCount() != receiverDepthCount_ ||
        workspace.rangeCount() != receiverRangeCount_) {
      throw ValidationError(
          "SHD workspace dimensions must match the receiver grid");
    }

    // Validate the complete workspace before modifying its file slot.
    for (const std::complex<double> pressure :
         workspace.pressure()) {
      requireFiniteComplex(pressure, "SHD pressure");
      static_cast<void>(checkedFloat32(
          pressure.real(), "SHD pressure real part"));
      static_cast<void>(checkedFloat32(
          pressure.imag(), "SHD pressure imaginary part"));
    }

    std::vector<std::byte> record(recordBytes_);
    for (std::size_t depthIndex = 0U;
         depthIndex < receiverDepthCount_; ++depthIndex) {
      const std::size_t recordIndex =
          kHeaderRecordCount +
          index * receiverDepthCount_ + depthIndex;
      seekToRecord(recordIndex);
      for (std::size_t rangeIndex = 0U;
           rangeIndex < receiverRangeCount_; ++rangeIndex) {
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
      writeRecord(output_, record);
    }

    written_[index] = true;
    ++writtenCount_;
  }

  void finalize() {
    if (finalized_) {
      return;
    }
    if (writtenCount_ != written_.size()) {
      throw ValidationError(
          "cannot finalize SHD output before every frequency is written");
    }
    output_.flush();
    output_.close();
    if (!output_) {
      throw BellhopError(
          "failed to finalize SHD output: " + path_.string());
    }
    finalized_ = true;
  }

 private:
  static constexpr std::size_t kHeaderRecordCount = 10U;

  static void validateTitle(std::string_view title) {
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
  }

  void validateStaticData(const SimulationCase& simulation) const {
    if (frequencies_.empty()) {
      throw ValidationError(
          "SHD output requires at least one frequency");
    }
    for (const double frequency : frequencies_) {
      requireFinite(frequency, "SHD frequency");
    }
    static_cast<void>(checkedFloat32(
        simulation.source().depth, "SHD source depth"));
    for (const double depth :
         simulation.receivers().depths()) {
      static_cast<void>(
          checkedFloat32(depth, "SHD receiver depth"));
    }
    for (const double range :
         simulation.receivers().ranges()) {
      requireFinite(range, "SHD receiver range");
    }
  }

  void calculateLayout() {
    constexpr std::size_t bearingCount = 1U;
    constexpr std::size_t sourceXCount = 1U;
    constexpr std::size_t sourceYCount = 1U;
    constexpr std::size_t sourceDepthCount = 1U;

    if (frequencies_.size() >
        std::numeric_limits<std::size_t>::max() / 2U) {
      throw ValidationError("SHD frequency count overflow");
    }
    if (receiverRangeCount_ >
        std::numeric_limits<std::size_t>::max() / 2U) {
      throw ValidationError(
          "SHD receiver-range count overflow");
    }

    const std::size_t recordWords = std::max(
        {kMinimumRecordWords,
         2U * frequencies_.size(),
         2U * bearingCount,
         2U * sourceXCount,
         2U * sourceYCount,
         sourceDepthCount,
         receiverDepthCount_,
         2U * receiverRangeCount_});
    if (recordWords >
        std::numeric_limits<std::size_t>::max() / 4U) {
      throw ValidationError("SHD record byte count overflow");
    }
    recordBytes_ = 4U * recordWords;
    if (recordBytes_ >
        static_cast<std::size_t>(
            std::numeric_limits<std::streamsize>::max())) {
      throw ValidationError(
          "SHD record exceeds streamsize capacity");
    }
    recordWords32_ =
        checkedInt32(recordWords, "SHD record word count");

    if (frequencies_.size() >
        (std::numeric_limits<std::size_t>::max() -
         kHeaderRecordCount) /
            receiverDepthCount_) {
      throw ValidationError("SHD record count overflow");
    }
    totalRecordCount_ =
        kHeaderRecordCount +
        frequencies_.size() * receiverDepthCount_;
    if (totalRecordCount_ >
        std::numeric_limits<std::size_t>::max() /
            recordBytes_) {
      throw ValidationError("SHD file size overflow");
    }
    totalBytes_ = totalRecordCount_ * recordBytes_;
    if (totalBytes_ >
        static_cast<std::size_t>(
            std::numeric_limits<std::streamoff>::max())) {
      throw ValidationError(
          "SHD file exceeds stream offset capacity");
    }
  }

  void writeHeader(
      std::string_view title,
      const SimulationCase& simulation) {
    const ReceiverGrid& receivers = simulation.receivers();
    const std::int32_t frequencyCount32 =
        checkedInt32(
            frequencies_.size(), "SHD frequency count");
    const std::int32_t receiverDepthCount32 =
        checkedInt32(
            receiverDepthCount_,
            "SHD receiver-depth count");
    const std::int32_t receiverRangeCount32 =
        checkedInt32(
            receiverRangeCount_,
            "SHD receiver-range count");

    std::vector<std::byte> record(recordBytes_);
    const auto writeClearedRecord = [&] {
      writeRecord(output_, record);
      std::fill(
          record.begin(), record.end(), std::byte{});
    };

    storeInt32(record, 0U, recordWords32_);
    storeText(record, 4U, 80U, title);
    writeClearedRecord();

    storeText(record, 0U, 10U, "rectilin  ");
    writeClearedRecord();

    storeInt32(record, 0U, frequencyCount32);
    storeInt32(record, 4U, 1);
    storeInt32(record, 8U, 1);
    storeInt32(record, 12U, 1);
    storeInt32(record, 16U, 1);
    storeInt32(record, 20U, receiverDepthCount32);
    storeInt32(record, 24U, receiverRangeCount32);
    storeFloat64(record, 28U, frequencies_.front());
    storeFloat64(record, 36U, 0.0);
    writeClearedRecord();

    for (std::size_t index = 0U;
         index < frequencies_.size(); ++index) {
      storeFloat64(
          record, 8U * index, frequencies_[index]);
    }
    writeClearedRecord();

    storeFloat64(record, 0U, 0.0);
    writeClearedRecord();
    storeFloat64(record, 0U, 0.0);
    writeClearedRecord();
    storeFloat64(record, 0U, 0.0);
    writeClearedRecord();

    storeFloat32(
        record, 0U,
        checkedFloat32(
            simulation.source().depth, "SHD source depth"));
    writeClearedRecord();

    for (std::size_t index = 0U;
         index < receiverDepthCount_; ++index) {
      storeFloat32(
          record, 4U * index,
          checkedFloat32(
              receivers.depths()[index],
              "SHD receiver depth"));
    }
    writeClearedRecord();

    for (std::size_t index = 0U;
         index < receiverRangeCount_; ++index) {
      storeFloat64(
          record, 8U * index, receivers.ranges()[index]);
    }
    writeRecord(output_, record);
  }

  void preallocate() {
    output_.seekp(
        static_cast<std::streamoff>(totalBytes_ - 1U),
        std::ios::beg);
    output_.put('\0');
    output_.flush();
    if (!output_) {
      throw BellhopError(
          "failed to preallocate SHD output: " +
          path_.string());
    }
  }

  void seekToRecord(std::size_t recordIndex) {
    const std::size_t byteOffset =
        recordIndex * recordBytes_;
    output_.seekp(
        static_cast<std::streamoff>(byteOffset),
        std::ios::beg);
    if (!output_) {
      throw BellhopError(
          "failed to seek within SHD output: " +
          path_.string());
    }
  }

  std::vector<double> frequencies_;
  std::size_t receiverDepthCount_{};
  std::size_t receiverRangeCount_{};
  std::size_t recordBytes_{};
  std::int32_t recordWords32_{};
  std::size_t totalRecordCount_{};
  std::size_t totalBytes_{};
  std::vector<bool> written_;
  std::size_t writtenCount_{};
  std::filesystem::path path_;
  std::ofstream output_;
  bool finalized_{};
};

ShdFrequencyWriter::ShdFrequencyWriter(
    const std::filesystem::path& path,
    std::string_view title,
    const SimulationCase& simulation)
    : impl_(std::make_unique<Impl>(
          path, title, simulation)) {}

ShdFrequencyWriter::~ShdFrequencyWriter() noexcept = default;

ShdFrequencyWriter::ShdFrequencyWriter(
    ShdFrequencyWriter&&) noexcept = default;

ShdFrequencyWriter& ShdFrequencyWriter::operator=(
    ShdFrequencyWriter&&) noexcept = default;

void ShdFrequencyWriter::writeFrequency(
    std::size_t index,
    const FrequencyWorkspace& workspace) {
  if (!impl_) {
    throw ValidationError(
        "cannot write with a moved-from SHD writer");
  }
  impl_->writeFrequency(index, workspace);
}

void ShdFrequencyWriter::finalize() {
  if (!impl_) {
    throw ValidationError(
        "cannot finalize a moved-from SHD writer");
  }
  impl_->finalize();
}

void ShdWriter::writeSingleFrequency(
    const std::filesystem::path& path,
    std::string_view title,
    const SimulationCase& simulation,
    const FrequencyWorkspace& workspace) {
  writeFrequencies(
      path, title, simulation,
      std::span<const FrequencyWorkspace>(&workspace, 1U));
}

void ShdWriter::writeFrequencies(
    const std::filesystem::path& path,
    std::string_view title,
    const SimulationCase& simulation,
    std::span<const FrequencyWorkspace> workspaces) {
  const std::vector<double>& frequencies =
      simulation.frequencies().values();
  if (workspaces.size() != frequencies.size()) {
    throw ValidationError(
        "SHD workspace count must match the simulation frequency count");
  }

  ShdFrequencyWriter writer(path, title, simulation);
  for (std::size_t index = 0U;
       index < workspaces.size(); ++index) {
    writer.writeFrequency(index, workspaces[index]);
  }
  writer.finalize();
}

}  // namespace rayreuse
