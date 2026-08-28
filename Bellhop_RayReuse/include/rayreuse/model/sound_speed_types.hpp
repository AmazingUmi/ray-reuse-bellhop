#pragma once

#include <cstddef>

#include "rayreuse/numerics/vec2.hpp"

namespace rayreuse {

enum class SspInterpolationKind {
  CLinear,
  Pchip,
  N2Linear,
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
      return SspGradientContinuity::DiscontinuousAtNodes;
    case SspInterpolationKind::Pchip:
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
};

}  // namespace rayreuse
