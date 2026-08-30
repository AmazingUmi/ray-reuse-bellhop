#pragma once

#include <cstddef>
#include <vector>

#include "rayreuse/model/environment.hpp"
#include "rayreuse/model/sound_speed_types.hpp"
#include "rayreuse/numerics/cubic_spline_coefficients.hpp"

namespace rayreuse {

class CubicSplineSsp {
 public:
  explicit CubicSplineSsp(const SoundSpeedProfile& profile);

  [[nodiscard]] static constexpr SspGradientContinuity
  gradientContinuity() noexcept {
    return sspGradientContinuity(SspInterpolationKind::CubicSpline);
  }
  [[nodiscard]] std::size_t segmentCount() const noexcept;
  [[nodiscard]] std::size_t locateSegment(double depth,
                                          std::size_t previousSegment) const;
  [[nodiscard]] SoundSpeedSample evaluateAtSegment(
      Vec2 position, std::size_t segmentIndex) const;
  [[nodiscard]] SoundSpeedSample evaluate(Vec2 position,
                                          std::size_t previousSegment) const;

 private:
  struct DensitySegment {
    double minimumDepth{};
    double maximumDepth{};
    double densityAtMinimumDepth{};
    double densityAtMaximumDepth{};
  };

  [[nodiscard]] SoundSpeedSample evaluatePolynomial(
      Vec2 position, std::size_t segmentIndex) const;

  std::vector<double> depths_;
  std::vector<DensitySegment> densitySegments_;
  std::vector<ComplexSplinePolynomial> coefficients_;
};

}  // namespace rayreuse
