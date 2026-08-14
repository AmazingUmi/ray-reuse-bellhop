#pragma once

#include <cstddef>
#include <vector>

#include "bellhop/ray/ray_path.hpp"

namespace bellhop::ray_output_detail {

struct EncodedRayPrefix {
  std::vector<std::size_t> pointIndices;
  std::size_t topBounceCount{};
  std::size_t bottomBounceCount{};
};

// The sole point compression and prefix-bounce implementation used by R and
// E output.  prefixPointCount is exclusive and may be the complete path.
[[nodiscard]] EncodedRayPrefix encodeRayPrefix(
    const RayPath& path, std::size_t prefixPointCount, double topDepth,
    double bottomDepth);

}  // namespace bellhop::ray_output_detail
