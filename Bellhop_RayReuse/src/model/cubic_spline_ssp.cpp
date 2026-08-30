#include "rayreuse/model/cubic_spline_ssp.hpp"

#include <algorithm>
#include <cmath>
#include <complex>

#include "rayreuse/error.hpp"

namespace rayreuse {
namespace {

// splinec.f90 forms SIXTH from default-real literals before assigning it to
// a real(8) PARAMETER, so the binary32-rounded value is part of the oracle.
constexpr double kFortranSixth = static_cast<double>(1.0F / 6.0F);

}  // namespace

CubicSplineSsp::CubicSplineSsp(const SoundSpeedProfile& profile) {
  const std::vector<SoundSpeedPoint>& points = profile.points();
  std::vector<std::complex<double>> soundSpeeds;
  depths_.reserve(points.size());
  soundSpeeds.reserve(points.size());
  densitySegments_.reserve(points.size() - 1U);
  for (const SoundSpeedPoint& point : points) {
    depths_.push_back(point.depth);
    soundSpeeds.emplace_back(point.soundSpeed, 0.0);
  }
  for (std::size_t index = 0U; index + 1U < points.size(); ++index) {
    densitySegments_.push_back(
        {.minimumDepth = points[index].depth,
         .maximumDepth = points[index + 1U].depth,
         .densityAtMinimumDepth = points[index].density,
         .densityAtMaximumDepth = points[index + 1U].density});
  }
  coefficients_ = computeCubicSplineCoefficients(depths_, soundSpeeds);
}

std::size_t CubicSplineSsp::segmentCount() const noexcept {
  return coefficients_.size();
}

std::size_t CubicSplineSsp::locateSegment(double depth,
                                          std::size_t previousSegment) const {
  if (!std::isfinite(depth)) {
    throw ValidationError("SSP query depth must be finite");
  }
  if (previousSegment >= segmentCount()) {
    throw ValidationError("SSP previous segment index is out of range");
  }
  const DensitySegment& previous = densitySegments_[previousSegment];
  if (depth >= previous.minimumDepth && depth <= previous.maximumDepth) {
    return previousSegment;
  }
  if (depth < depths_.front()) {
    return 0U;
  }
  if (depth > depths_.back()) {
    return segmentCount() - 1U;
  }
  const auto upperNode =
      std::lower_bound(depths_.begin(), depths_.end(), depth);
  if (upperNode == depths_.begin()) {
    return 0U;
  }
  const std::size_t upperNodeIndex =
      static_cast<std::size_t>(upperNode - depths_.begin());
  return std::min(upperNodeIndex - 1U, segmentCount() - 1U);
}

SoundSpeedSample CubicSplineSsp::evaluatePolynomial(
    Vec2 position, std::size_t segmentIndex) const {
  if (!isFinite(position)) {
    throw ValidationError("SSP query position must be finite");
  }
  if (segmentIndex >= segmentCount()) {
    throw ValidationError("SSP segment index is out of range");
  }
  const DensitySegment& densitySegment = densitySegments_[segmentIndex];
  const ComplexSplinePolynomial& polynomial = coefficients_[segmentIndex];
  const double offset = position.depth - densitySegment.minimumDepth;
  const double soundSpeed = std::real(
      polynomial.value + offset * (polynomial.derivative +
                                   offset * (0.5 * polynomial.curvature +
                                             kFortranSixth * offset *
                                                 polynomial.thirdDerivative)));
  const double gradient =
      std::real(polynomial.derivative +
                offset * (polynomial.curvature +
                          0.5 * offset * polynomial.thirdDerivative));
  const double curvature =
      std::real(polynomial.curvature + offset * polynomial.thirdDerivative);
  const double densityWeight =
      offset / (densitySegment.maximumDepth - densitySegment.minimumDepth);
  const double density =
      (1.0 - densityWeight) * densitySegment.densityAtMinimumDepth +
      densityWeight * densitySegment.densityAtMaximumDepth;
  if (!std::isfinite(soundSpeed) || soundSpeed <= 0.0 ||
      !std::isfinite(gradient) || !std::isfinite(curvature) ||
      !std::isfinite(density)) {
    throw ValidationError(
        "cubic-spline SSP evaluation produced an invalid value");
  }
  return {.soundSpeed = soundSpeed,
          .imaginarySoundSpeed = 0.0,
          .soundSpeedGradient = {.range = 0.0, .depth = gradient},
          .soundSpeedHessian = {.rangeRange = 0.0,
                                .rangeDepth = 0.0,
                                .depthDepth = curvature},
          .density = density,
          .segmentIndex = segmentIndex};
}

SoundSpeedSample CubicSplineSsp::evaluateAtSegment(
    Vec2 position, std::size_t segmentIndex) const {
  if (segmentIndex >= segmentCount()) {
    throw ValidationError("SSP segment index is out of range");
  }
  const DensitySegment& segment = densitySegments_[segmentIndex];
  if (position.depth < segment.minimumDepth ||
      position.depth > segment.maximumDepth) {
    throw ValidationError("SSP query depth is outside the selected segment");
  }
  return evaluatePolynomial(position, segmentIndex);
}

SoundSpeedSample CubicSplineSsp::evaluate(Vec2 position,
                                          std::size_t previousSegment) const {
  return evaluatePolynomial(position,
                            locateSegment(position.depth, previousSegment));
}

}  // namespace rayreuse
