#pragma once

#include <cstddef>

#include "rayreuse/numerics/vec2.hpp"

namespace rayreuse {

enum class SspInterpolationKind {
  CLinear,
  Pchip,
  N2Linear,
  CubicSpline,
  Quadrilateral,
};

enum class SspGradientContinuity {
  DiscontinuousAtNodes,
  ContinuousAtNodes,
};

[[nodiscard]] constexpr SspGradientContinuity sspGradientContinuity(
    SspInterpolationKind kind) noexcept {
  switch (kind) {
    case SspInterpolationKind::CLinear:
    case SspInterpolationKind::N2Linear:
    case SspInterpolationKind::Quadrilateral:
      return SspGradientContinuity::DiscontinuousAtNodes;
    case SspInterpolationKind::Pchip:
    case SspInterpolationKind::CubicSpline:
      return SspGradientContinuity::ContinuousAtNodes;
  }
  return SspGradientContinuity::DiscontinuousAtNodes;
}

struct SoundSpeedHessian {
  double rangeRange{};
  double rangeDepth{};
  double depthDepth{};

  friend constexpr bool operator==(const SoundSpeedHessian&,
                                   const SoundSpeedHessian&) = default;
};

// Range cell identity of a transient query sample. It exists only on the
// sample handed to the caller; frozen ray paths and caches never store it.
struct SoundSpeedSample {
  double soundSpeed{};
  double imaginarySoundSpeed{};
  Vec2 soundSpeedGradient{};
  SoundSpeedHessian soundSpeedHessian{};
  double density{};
  std::size_t segmentIndex{};
  std::size_t rangeSegmentIndex{};
};

}  // namespace rayreuse
