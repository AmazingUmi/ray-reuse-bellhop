#pragma once

#include <cstddef>
#include <vector>

#include "broadband/field/frequency_workspace.hpp"
#include "broadband/ray/ray_path.hpp"

namespace broadband::ray_output_detail {

struct EncodedRayPrefix {
  std::vector<std::size_t> pointIndices;
  std::size_t topBounceCount{};
  std::size_t bottomBounceCount{};
};

[[nodiscard]] std::size_t terminalPrefixPointCount(
    const RayPath& path, const RayFrequencyState& frequencyState);

// Encodes the Origin RAY point prefix. prefixPointCount is exclusive and may
// include the first inactive terminal point, but never the inactive suffix.
[[nodiscard]] EncodedRayPrefix encodeRayPrefix(const RayPath& path,
                                               std::size_t prefixPointCount,
                                               double topDepth,
                                               double bottomDepth);

}  // namespace broadband::ray_output_detail
