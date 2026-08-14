#include "bellhop/io/output_layout.hpp"

#include <algorithm>
#include <cstdint>
#include <ios>
#include <limits>
#include <string>

#include "bellhop/error.hpp"

namespace bellhop {
namespace {

constexpr std::size_t kShdHeaderRecordCount = 10U;
constexpr std::size_t kMinimumRecordWords = 41U;

std::size_t checkedAdd(std::size_t left, std::size_t right,
                       const char* label) {
  if (right > std::numeric_limits<std::size_t>::max() - left) {
    throw ValidationError(std::string(label) + " exceeds size_t capacity");
  }
  return left + right;
}

std::size_t checkedMultiply(std::size_t left, std::size_t right,
                            const char* label) {
  if (left != 0U && right > std::numeric_limits<std::size_t>::max() / left) {
    throw ValidationError(std::string(label) + " exceeds size_t capacity");
  }
  return left * right;
}

void requireOriginInt32(std::size_t value, const char* label) {
  if (value > static_cast<std::size_t>(
                  std::numeric_limits<std::int32_t>::max())) {
    throw ValidationError(std::string(label) + " exceeds the int32 limit");
  }
}

void requireStreamSize(std::size_t value, const char* label) {
  if (value > static_cast<std::size_t>(
                  std::numeric_limits<std::streamsize>::max())) {
    throw ValidationError(std::string(label) + " exceeds streamsize capacity");
  }
  if (value > static_cast<std::size_t>(
                  std::numeric_limits<std::streamoff>::max())) {
    throw ValidationError(std::string(label) + " exceeds streamoff capacity");
  }
}

}  // namespace

Shd2DLayout planShd2DLayout(std::size_t sourceDepthCount,
                            std::size_t receiverDepthCount,
                            std::size_t receiverRangeCount,
                            bool irregular) {
  if (sourceDepthCount == 0U || receiverDepthCount == 0U ||
      receiverRangeCount == 0U) {
    throw ValidationError("SHD dimensions must all be positive");
  }
  requireOriginInt32(sourceDepthCount, "SHD source-depth count");
  requireOriginInt32(receiverDepthCount, "SHD receiver-depth count");
  requireOriginInt32(receiverRangeCount, "SHD receiver-range count");

  const std::size_t recordsPerSource = irregular ? 1U : receiverDepthCount;
  const std::size_t doubledRangeCount = checkedMultiply(
      2U, receiverRangeCount, "SHD complex receiver-range words");
  const std::size_t recordWords = std::max(
      {kMinimumRecordWords, sourceDepthCount, receiverDepthCount,
       doubledRangeCount});
  requireOriginInt32(recordWords, "SHD record word count");
  const std::size_t recordBytes =
      checkedMultiply(4U, recordWords, "SHD record byte count");
  if (recordBytes > static_cast<std::size_t>(
                        std::numeric_limits<std::streamsize>::max())) {
    throw ValidationError("SHD record exceeds streamsize capacity");
  }

  const std::size_t pressureRecordCount = checkedMultiply(
      sourceDepthCount, recordsPerSource, "SHD pressure record count");
  const std::size_t totalRecordCount = checkedAdd(
      kShdHeaderRecordCount, pressureRecordCount, "SHD total record count");
  requireOriginInt32(totalRecordCount, "SHD final record number");
  const std::size_t fileBytes = checkedMultiply(
      totalRecordCount, recordBytes, "SHD total file byte count");
  if (fileBytes > static_cast<std::size_t>(
                      std::numeric_limits<std::streamoff>::max())) {
    throw ValidationError("SHD file exceeds streamoff capacity");
  }

  return Shd2DLayout{
      .sourceDepthCount = sourceDepthCount,
      .receiverDepthCount = receiverDepthCount,
      .receiverRangeCount = receiverRangeCount,
      .recordsPerSource = recordsPerSource,
      .recordWords = recordWords,
      .recordBytes = recordBytes,
      .pressureRecordCount = pressureRecordCount,
      .totalRecordCount = totalRecordCount,
      .fileBytes = fileBytes};
}

Arrival2DLayout planArrival2DLayout(std::size_t sourceDepthCount,
                                    std::size_t receiverDepthCount,
                                    std::size_t receiverRangeCount,
                                    bool irregular,
                                    std::size_t perCellCapacity) {
  if (sourceDepthCount == 0U || receiverDepthCount == 0U ||
      receiverRangeCount == 0U || perCellCapacity == 0U) {
    throw ValidationError("ARR dimensions and capacity must be positive");
  }
  requireOriginInt32(sourceDepthCount, "ARR source-depth count");
  requireOriginInt32(receiverDepthCount, "ARR receiver-depth count");
  requireOriginInt32(receiverRangeCount, "ARR receiver-range count");
  requireOriginInt32(perCellCapacity, "ARR per-cell capacity");

  const std::size_t actualCellsPerSource =
      irregular ? receiverRangeCount
                : checkedMultiply(receiverDepthCount, receiverRangeCount,
                                  "ARR cells per source");
  const std::size_t maximumCellCount = perCellCapacity;
  // Eight float fields, separators, and a newline.  This is deliberately a
  // conservative bound used before the temporary file is opened.
  constexpr std::size_t kAsciiFieldBytes = 32U;
  constexpr std::size_t kAsciiFieldsPerArrival = 8U;
  const std::size_t maximumAsciiLineBytes = checkedAdd(
      checkedAdd(checkedMultiply(kAsciiFieldBytes, kAsciiFieldsPerArrival,
                                 "ARR ASCII record bound"),
                 7U, "ARR ASCII record separators"),
      1U, "ARR ASCII record newline");
  constexpr std::size_t kBinaryArrivalPayloadBytes = 32U;

  const std::size_t headerAsciiBytes = checkedAdd(
      checkedMultiply(4U, kAsciiFieldBytes, "ARR ASCII header bound"),
      checkedMultiply(
          checkedAdd(sourceDepthCount,
                     checkedAdd(receiverDepthCount, receiverRangeCount,
                                "ARR ASCII header vector count"),
                     "ARR ASCII header vector count"),
          kAsciiFieldBytes, "ARR ASCII header vector bytes"),
      "ARR ASCII header bytes");
  const std::size_t bodyAsciiPerCell =
      checkedAdd(kAsciiFieldBytes, maximumAsciiLineBytes,
                 "ARR ASCII cell bound");
  const std::size_t maximumAsciiBody = checkedMultiply(
      checkedMultiply(sourceDepthCount, actualCellsPerSource,
                      "ARR ASCII body cells"),
      checkedAdd(bodyAsciiPerCell,
                 checkedMultiply(perCellCapacity, maximumAsciiLineBytes,
                                 "ARR ASCII arrival bound"),
                 "ARR ASCII cell bound"),
      "ARR ASCII body bytes");
  const std::size_t minimumAsciiBody = checkedMultiply(
      checkedMultiply(sourceDepthCount, actualCellsPerSource,
                      "ARR ASCII minimum body cells"),
      bodyAsciiPerCell, "ARR ASCII minimum body bytes");

  constexpr std::size_t kBinaryRecordOverhead = 8U;
  const auto binaryRecordBytes = [&](std::size_t payload,
                                     const char* label) {
    return checkedAdd(payload, kBinaryRecordOverhead, label);
  };
  const std::size_t binaryHeaderBytes = checkedAdd(
      binaryRecordBytes(4U, "ARR binary tag record"),
      binaryRecordBytes(4U, "ARR binary frequency record"),
      "ARR binary header bytes");
  const std::size_t binaryHeaderWithSources = checkedAdd(
      binaryHeaderBytes,
      binaryRecordBytes(
          checkedAdd(4U, checkedMultiply(sourceDepthCount, 4U,
                                         "ARR binary source depths"),
                     "ARR binary source-depth payload"),
          "ARR binary source-depth record"),
      "ARR binary header bytes");
  const std::size_t binaryHeaderWithReceivers = checkedAdd(
      checkedAdd(
          binaryHeaderWithSources,
          binaryRecordBytes(
              checkedAdd(4U, checkedMultiply(receiverDepthCount, 4U,
                                             "ARR binary receiver depths"),
                         "ARR binary receiver-depth payload"),
              "ARR binary receiver-depth record"),
          "ARR binary header bytes"),
      binaryRecordBytes(
          checkedAdd(4U, checkedMultiply(receiverRangeCount, 8U,
                                         "ARR binary receiver ranges"),
                     "ARR binary receiver-range payload"),
          "ARR binary receiver-range record"),
      "ARR binary header bytes");
  const std::size_t binaryCellBytes = checkedAdd(
      binaryRecordBytes(4U, "ARR binary cell-count record"),
      checkedMultiply(
          perCellCapacity,
          binaryRecordBytes(kBinaryArrivalPayloadBytes,
                            "ARR binary arrival record"),
          "ARR binary arrival payload"),
      "ARR binary cell bytes");
  const std::size_t binaryMinimumBody = checkedMultiply(
      sourceDepthCount,
      checkedAdd(binaryRecordBytes(4U, "ARR binary source maximum record"),
                 checkedMultiply(actualCellsPerSource,
                                 binaryRecordBytes(4U,
                                                   "ARR binary cell count"),
                                 "ARR binary minimum cell counts"),
                 "ARR binary source body"),
      "ARR binary minimum body");
  const std::size_t binaryMaximumBody = checkedMultiply(
      sourceDepthCount,
      checkedAdd(binaryRecordBytes(4U, "ARR binary source maximum record"),
                 checkedMultiply(actualCellsPerSource, binaryCellBytes,
                                     "ARR binary source body"),
                 "ARR binary source body"),
      "ARR binary maximum body");
  const std::size_t minimumFileBytes = checkedAdd(
      binaryHeaderWithReceivers, binaryMinimumBody,
      "ARR binary minimum file bound");
  const std::size_t maximumFileBytes = std::max(
      checkedAdd(headerAsciiBytes, checkedAdd(minimumAsciiBody,
                                               maximumAsciiBody,
                                               "ARR ASCII file bound"),
                 "ARR ASCII file bound"),
      checkedAdd(binaryHeaderWithReceivers, binaryMaximumBody,
                 "ARR binary file bound"));
  requireStreamSize(maximumFileBytes, "ARR file bound");

  return Arrival2DLayout{
      .sourceDepthCount = sourceDepthCount,
      .receiverDepthCount = receiverDepthCount,
      .receiverRangeCount = receiverRangeCount,
      .actualCellsPerSource = actualCellsPerSource,
      .perCellCapacity = perCellCapacity,
      .maximumCellCount = maximumCellCount,
      .maximumAsciiLineBytes = maximumAsciiLineBytes,
      .maximumBinaryRecordBytes = kBinaryArrivalPayloadBytes,
      .minimumFileBytes = minimumFileBytes,
      .maximumFileBytes = maximumFileBytes,
      .irregular = irregular};
}

std::size_t Shd2DLayout::pressureRecordNumber1Based(
    std::size_t sourceIndex, std::size_t pressureDepthIndex) const {
  if (sourceIndex >= sourceDepthCount ||
      pressureDepthIndex >= recordsPerSource) {
    throw ValidationError("SHD pressure record index is out of range");
  }
  const std::size_t zeroBasedPressure =
      sourceIndex * recordsPerSource + pressureDepthIndex;
  return 11U + zeroBasedPressure;
}

}  // namespace bellhop
