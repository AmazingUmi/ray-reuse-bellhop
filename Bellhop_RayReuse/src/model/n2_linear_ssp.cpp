#include "rayreuse/model/n2_linear_ssp.hpp"

#include <algorithm>
#include <cmath>

#include "rayreuse/error.hpp"

namespace rayreuse {

N2LinearSsp::N2LinearSsp(const SoundSpeedProfile& profile) {
  const std::vector<SoundSpeedPoint>& points = profile.points();
  depths_.reserve(points.size());
  segments_.reserve(points.size() - 1U);
  for (const SoundSpeedPoint& point : points) {
    depths_.push_back(point.depth);
  }
  for (std::size_t index = 0U; index + 1U < points.size(); ++index) {
    const double interval = points[index + 1U].depth - points[index].depth;
    const double firstN2 =
        1.0 / (points[index].soundSpeed * points[index].soundSpeed);
    const double secondN2 =
        1.0 /
        (points[index + 1U].soundSpeed * points[index + 1U].soundSpeed);
    const double gradient = (secondN2 - firstN2) / interval;
    if (!std::isfinite(gradient)) {
      throw ValidationError("N2-linear coefficient must be finite");
    }
    segments_.push_back(
        Segment{.minimumDepth = points[index].depth,
                .maximumDepth = points[index + 1U].depth,
                .n2AtMinimumDepth = firstN2,
                .n2DepthGradient = gradient,
                .densityAtMinimumDepth = points[index].density,
                .densityAtMaximumDepth = points[index + 1U].density});
  }
}

std::size_t N2LinearSsp::segmentCount() const noexcept {
  return segments_.size();
}

std::size_t N2LinearSsp::locateSegment(
    double depth, std::size_t previousSegment) const {
  if (!std::isfinite(depth)) {
    throw ValidationError("SSP query depth must be finite");
  }
  if (previousSegment >= segmentCount()) {
    throw ValidationError("SSP previous segment index is out of range");
  }
  const Segment& previous = segments_[previousSegment];
  if (depth >= previous.minimumDepth && depth <= previous.maximumDepth) {
    return previousSegment;
  }
  if (depth < depths_.front()) {
    return 0U;
  }
  if (depth > depths_.back()) {
    return segmentCount() - 1U;
  }
  // lower_bound produces the segment immediately to the left at an exact
  // node. The hinted segment was tested first, preserving the Fortran GetSegz
  // state semantics when either adjacent segment is current.
  const auto upperNode =
      std::lower_bound(depths_.begin(), depths_.end(), depth);
  if (upperNode == depths_.begin()) {
    return 0U;
  }
  const std::size_t upperNodeIndex =
      static_cast<std::size_t>(upperNode - depths_.begin());
  return std::min(upperNodeIndex - 1U, segmentCount() - 1U);
}

SoundSpeedSample N2LinearSsp::evaluatePolynomial(
    Vec2 position, std::size_t segmentIndex) const {
  if (!isFinite(position)) {
    throw ValidationError("SSP query position must be finite");
  }
  if (segmentIndex >= segmentCount()) {
    throw ValidationError("SSP segment index is out of range");
  }
  const Segment& segment = segments_[segmentIndex];
  const double offset = position.depth - segment.minimumDepth;
  const double n2 =
      segment.n2AtMinimumDepth + offset * segment.n2DepthGradient;
  if (!std::isfinite(n2) || n2 <= 0.0) {
    throw ValidationError("interpolated N2 must be finite and positive");
  }
  const double soundSpeed = 1.0 / std::sqrt(n2);
  const double gradient =
      -0.5 * soundSpeed * soundSpeed * soundSpeed *
      segment.n2DepthGradient;
  const double curvature = 3.0 * gradient * gradient / soundSpeed;
  const double densityWeight =
      offset / (segment.maximumDepth - segment.minimumDepth);
  const double density =
      (1.0 - densityWeight) * segment.densityAtMinimumDepth +
      densityWeight * segment.densityAtMaximumDepth;
  if (!std::isfinite(soundSpeed) || !std::isfinite(gradient) ||
      !std::isfinite(curvature) || !std::isfinite(density)) {
    throw ValidationError("N2-linear SSP evaluation produced an invalid value");
  }
  return SoundSpeedSample{
      .soundSpeed = soundSpeed,
      .imaginarySoundSpeed = 0.0,
      .soundSpeedGradient = Vec2{.range = 0.0, .depth = gradient},
      .soundSpeedHessian =
          SoundSpeedHessian{
              .rangeRange = 0.0, .rangeDepth = 0.0, .depthDepth = curvature},
      .density = density,
      .segmentIndex = segmentIndex};
}

SoundSpeedSample N2LinearSsp::evaluateAtSegment(
    Vec2 position, std::size_t segmentIndex) const {
  if (segmentIndex >= segmentCount()) {
    throw ValidationError("SSP segment index is out of range");
  }
  const Segment& segment = segments_[segmentIndex];
  if (position.depth < segment.minimumDepth ||
      position.depth > segment.maximumDepth) {
    throw ValidationError("SSP query depth is outside the selected segment");
  }
  return evaluatePolynomial(position, segmentIndex);
}

SoundSpeedSample N2LinearSsp::evaluate(
    Vec2 position, std::size_t previousSegment) const {
  return evaluatePolynomial(
      position, locateSegment(position.depth, previousSegment));
}

}  // namespace rayreuse
