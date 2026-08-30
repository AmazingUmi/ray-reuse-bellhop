#pragma once

#include <cstddef>
#include <vector>

#include "rayreuse/model/environment.hpp"
#include "rayreuse/model/sound_speed_types.hpp"
#include "rayreuse/numerics/vec2.hpp"

namespace rayreuse {

// N²-linear interpolation of a range-independent, two-dimensional SSP,
// migrated line by line from the Bellhop F2CPP production implementation.
//
// Each depth segment stores the real N² value at its shallower node and the
// constant N² depth gradient; the sound speed is recovered as 1/sqrt(N²(z)).
// The segment locator mirrors the C-linear hinted behavior: both ends of the
// hinted segment belong to that segment, and queries outside the global
// top/bottom extrapolate the first/last segment.
class N2LinearSsp {
 public:
  explicit N2LinearSsp(const SoundSpeedProfile& profile);

  [[nodiscard]] static constexpr SspGradientContinuity
  gradientContinuity() noexcept {
    return sspGradientContinuity(SspInterpolationKind::N2Linear);
  }
  [[nodiscard]] std::size_t segmentCount() const noexcept;
  [[nodiscard]] std::size_t locateSegment(double depth,
                                          std::size_t previousSegment) const;
  [[nodiscard]] SoundSpeedSample evaluateAtSegment(
      Vec2 position, std::size_t segmentIndex) const;
  [[nodiscard]] SoundSpeedSample evaluate(Vec2 position,
                                          std::size_t previousSegment) const;

 private:
  struct Segment {
    double minimumDepth{};
    double maximumDepth{};
    double n2AtMinimumDepth{};
    double n2DepthGradient{};
    double densityAtMinimumDepth{};
    double densityAtMaximumDepth{};
  };

  [[nodiscard]] SoundSpeedSample evaluatePolynomial(
      Vec2 position, std::size_t segmentIndex) const;

  std::vector<double> depths_;
  std::vector<Segment> segments_;
};

}  // namespace rayreuse
