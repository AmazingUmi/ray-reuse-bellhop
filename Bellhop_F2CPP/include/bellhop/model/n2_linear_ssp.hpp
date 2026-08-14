#pragma once

#include <cstddef>
#include <vector>

#include "bellhop/model/environment.hpp"
#include "bellhop/model/sound_speed_types.hpp"

namespace bellhop {

class N2LinearSsp {
 public:
  explicit N2LinearSsp(const SoundSpeedProfile& profile);

  [[nodiscard]] static constexpr SspGradientContinuity gradientContinuity()
      noexcept {
    return sspGradientContinuity(SspInterpolationKind::N2Linear);
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

}  // namespace bellhop
