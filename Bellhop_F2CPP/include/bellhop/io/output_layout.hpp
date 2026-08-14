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

[[nodiscard]] Shd2DLayout planShd2DLayout(
    std::size_t sourceDepthCount, std::size_t receiverDepthCount,
    std::size_t receiverRangeCount, bool irregular);

}  // namespace bellhop
