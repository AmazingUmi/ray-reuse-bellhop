#pragma once

#include <cstddef>
#include <vector>

#include "rayreuse/model/environment.hpp"
#include "rayreuse/numerics/vec2.hpp"

namespace rayreuse {

struct SoundSpeedHessian {
  double rangeRange{};
  double rangeDepth{};
  double depthDepth{};

  friend constexpr bool operator==(const SoundSpeedHessian&,
                                   const SoundSpeedHessian&) = default;
};

struct SoundSpeedSample {
  double soundSpeed{};
  // Reserved for the frequency projection implemented in M2. Geometry-only
  // C-linear evaluation returns zero.
  double imaginarySoundSpeed{};
  Vec2 soundSpeedGradient{};
  SoundSpeedHessian soundSpeedHessian{};
  double density{};
  std::size_t segmentIndex{};
};

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

}  // namespace rayreuse
