#pragma once

#include <cstddef>

#include "bellhop/numerics/vec2.hpp"

namespace bellhop {

enum class SspInterpolationKind {
  N2Linear,
  CLinear,
  Pchip,
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
    case SspInterpolationKind::N2Linear:
    case SspInterpolationKind::CLinear:
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

struct SoundSpeedSample {
  double soundSpeed{};
  double imaginarySoundSpeed{};
  Vec2 soundSpeedGradient{};
  SoundSpeedHessian soundSpeedHessian{};
  double density{};
  std::size_t segmentIndex{};
  std::size_t rangeSegmentIndex{};
};

}  // namespace bellhop
