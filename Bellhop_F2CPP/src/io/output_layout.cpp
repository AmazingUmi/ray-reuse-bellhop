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
    throw ValidationError(std::string(label) + " exceeds the SHD int32 limit");
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
