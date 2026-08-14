#pragma once

#include <cstddef>

namespace bellhop {

// A receiver contribution selected for Eigenray output.  The hit is a small
// value object; the corresponding path remains owned by the frozen source
// cache and is only valid for the duration of the consumer callback.
struct EigenrayHit final {
  const std::size_t receiverRangeIndex;
  const std::size_t receiverDepthIndex;
  // Exclusive endpoint of the ray prefix written for this hit.
  const std::size_t prefixPointCount;
};

}  // namespace bellhop
