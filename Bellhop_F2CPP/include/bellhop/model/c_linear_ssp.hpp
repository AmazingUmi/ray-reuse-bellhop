#pragma once

#include <cstddef>
#include <vector>

#include "bellhop/model/environment.hpp"
#include "bellhop/model/sound_speed_types.hpp"

namespace bellhop {

// C-linear interpolation of a range-independent, two-dimensional SSP.
//
// Segment indices are zero based. The locator deliberately treats both ends of
// the hinted segment as belonging to that segment. This mirrors GetSegz in the
// Fortran implementation: at a profile node the caller's current segment is
// retained, so the depth derivative is the one-sided derivative on the arrival
// side. Queries just outside the global top/bottom use first/last-segment
// extrapolation, matching the minimum-step boundary overshoot in Step2D.
class CLinearSsp {
 public:
  explicit CLinearSsp(const SoundSpeedProfile& profile);

  [[nodiscard]] static constexpr SspGradientContinuity gradientContinuity()
      noexcept {
    return sspGradientContinuity(SspInterpolationKind::CLinear);
  }

  [[nodiscard]] std::size_t segmentCount() const noexcept;

  [[nodiscard]] std::size_t locateSegment(
      double depth, std::size_t previousSegment) const;

  [[nodiscard]] SoundSpeedSample evaluateAtSegment(
      Vec2 position, std::size_t segmentIndex) const;

  [[nodiscard]] SoundSpeedSample evaluate(
      Vec2 position, std::size_t previousSegment) const;

 private:
  struct Segment {
    double minimumDepth{};
    double maximumDepth{};
    double soundSpeedAtMinimumDepth{};
    double soundSpeedDepthGradient{};
    double densityAtMinimumDepth{};
    double densityAtMaximumDepth{};
  };

  [[nodiscard]] SoundSpeedSample evaluatePolynomial(
      Vec2 position, std::size_t segmentIndex) const;

  std::vector<double> depths_;
  std::vector<Segment> segments_;
};

}  // namespace bellhop
