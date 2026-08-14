#pragma once

#include <cstddef>

namespace bellhop {

struct Shd2DLayout {
  std::size_t sourceDepthCount{};
  std::size_t receiverDepthCount{};
  std::size_t receiverRangeCount{};
  std::size_t recordsPerSource{};
  std::size_t recordWords{};
  std::size_t recordBytes{};
  std::size_t pressureRecordCount{};
  std::size_t totalRecordCount{};
  std::size_t fileBytes{};

  [[nodiscard]] std::size_t pressureRecordNumber1Based(
      std::size_t sourceIndex, std::size_t pressureDepthIndex) const;
};

// Checked bounds for a 2-D ARR stream.  The body has one cell per range for
// irregular grids and depth-major/range-minor cells otherwise.
struct Arrival2DLayout {
  std::size_t sourceDepthCount{};
  std::size_t receiverDepthCount{};
  std::size_t receiverRangeCount{};
  std::size_t actualCellsPerSource{};
  std::size_t perCellCapacity{};
  std::size_t maximumCellCount{};
  std::size_t maximumAsciiLineBytes{};
  std::size_t maximumBinaryRecordBytes{};
  std::size_t minimumFileBytes{};
  std::size_t maximumFileBytes{};
  bool irregular{};
};

[[nodiscard]] Shd2DLayout planShd2DLayout(
    std::size_t sourceDepthCount, std::size_t receiverDepthCount,
    std::size_t receiverRangeCount, bool irregular);

[[nodiscard]] Arrival2DLayout planArrival2DLayout(
    std::size_t sourceDepthCount, std::size_t receiverDepthCount,
    std::size_t receiverRangeCount, bool irregular,
    std::size_t perCellCapacity);

}  // namespace bellhop
