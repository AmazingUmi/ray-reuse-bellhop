#include "bellhop/io/ray_prefix_writer.hpp"

#include <algorithm>
#include <limits>
#include <string>

#include "bellhop/error.hpp"

namespace bellhop::ray_output_detail {
namespace {

constexpr std::size_t kMaximumWrittenRayPoints = 500'000U;

}  // namespace

EncodedRayPrefix encodeRayPrefix(const RayPath& path,
                                 std::size_t prefixPointCount,
                                 double topDepth, double bottomDepth) {
  if (path.points.empty()) {
    throw ValidationError("ray writer cannot write an empty ray path");
  }
  if (prefixPointCount == 0U || prefixPointCount > path.points.size()) {
    throw ValidationError("ray writer prefix point count is out of range");
  }

  const std::size_t skip = std::max(
      prefixPointCount / kMaximumWrittenRayPoints, std::size_t{1U});
  std::vector<std::size_t> indices{0U};
  indices.reserve(std::min(prefixPointCount, kMaximumWrittenRayPoints + 2U));
  for (std::size_t index = 1U; index < prefixPointCount; ++index) {
    const double depth = path.points[index].position.depth;
    const bool nearBoundary =
        std::min(bottomDepth - depth, depth - topDepth) < 0.2;
    const bool stridePoint = ((index + 1U) % skip) == 0U;
    const bool terminal = index + 1U == prefixPointCount;
    if (nearBoundary || stridePoint || terminal) {
      indices.push_back(index);
    }
  }

  EncodedRayPrefix result{.pointIndices = std::move(indices)};
  for (const ReflectionEvent& event : path.events) {
    if (event.reflectedRayPointIndex >= prefixPointCount) {
      continue;
    }
    std::size_t* count = event.boundary == ReflectionBoundary::SeaSurface
                             ? &result.topBounceCount
                             : &result.bottomBounceCount;
    if (*count == std::numeric_limits<std::size_t>::max()) {
      throw ValidationError("ray writer bounce count exceeds size_t");
    }
    ++*count;
  }
  return result;
}

}  // namespace bellhop::ray_output_detail
