#pragma once

#include <cstddef>

namespace broadband {

struct EigenrayHit final {
  std::size_t receiverRangeIndex{};
  std::size_t receiverDepthIndex{};
  // Exclusive endpoint of the frozen ray prefix for this hit.
  std::size_t prefixPointCount{};
};

}  // namespace broadband
