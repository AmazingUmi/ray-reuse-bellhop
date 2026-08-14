#pragma once

#include <cstddef>
#include <vector>

#include "bellhop/model/environment.hpp"
#include "bellhop/model/sound_speed_types.hpp"

namespace bellhop {

// Origin Bellhop's two-dimensional `Q` interpolation. The real sound-speed
// matrix is bilinear in one depth/range cell, while density remains the
// vertically interpolated reference profile carried by the ENV file.
class QuadrilateralSsp {
 public:
  explicit QuadrilateralSsp(const SoundSpeedProfile& profile);

  [[nodiscard]] static constexpr SspGradientContinuity gradientContinuity()
      noexcept {
    return sspGradientContinuity(SspInterpolationKind::Quadrilateral);
  }

  [[nodiscard]] std::size_t segmentCount() const noexcept;
  [[nodiscard]] std::size_t rangeSegmentCount() const noexcept;

  [[nodiscard]] std::size_t locateSegment(
      double depth, std::size_t previousSegment) const;
  [[nodiscard]] std::size_t locateRangeSegment(
      double range, std::size_t previousRangeSegment) const;
  [[nodiscard]] double minimumRangeForSegment(
      std::size_t rangeSegmentIndex) const;
  [[nodiscard]] double maximumRangeForSegment(
      std::size_t rangeSegmentIndex) const;

  [[nodiscard]] SoundSpeedSample evaluateAtSegment(
      Vec2 position, std::size_t segmentIndex) const;
  [[nodiscard]] SoundSpeedSample evaluateAtSegments(
      Vec2 position, std::size_t segmentIndex,
      std::size_t rangeSegmentIndex) const;

  [[nodiscard]] SoundSpeedSample evaluate(
      Vec2 position, std::size_t previousSegment) const;
  [[nodiscard]] SoundSpeedSample evaluate(
      Vec2 position, std::size_t previousSegment,
      std::size_t previousRangeSegment) const;

 private:
  struct DepthSegment {
    double minimumDepth{};
    double maximumDepth{};
    double densityAtMinimumDepth{};
    double densityAtMaximumDepth{};
  };

  [[nodiscard]] SoundSpeedSample evaluatePolynomial(
      Vec2 position, std::size_t depthSegmentIndex,
      std::size_t rangeSegmentIndex) const;
  [[nodiscard]] double speedAt(std::size_t depthIndex,
                               std::size_t rangeIndex) const noexcept;
  [[nodiscard]] double depthGradientAt(
      std::size_t depthSegmentIndex,
      std::size_t rangeIndex) const noexcept;

  SharedQuadrilateralSspGrid grid_;
  std::vector<double> depths_;
  std::vector<DepthSegment> depthSegments_;
  std::vector<double> depthGradients_;
};

}  // namespace bellhop
