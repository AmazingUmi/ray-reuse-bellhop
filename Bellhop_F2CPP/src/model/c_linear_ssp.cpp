#include "bellhop/model/c_linear_ssp.hpp"

#include <algorithm>
#include <cmath>
#include <string>

#include "bellhop/error.hpp"

namespace bellhop {
namespace {

void requireFinite(double value, const std::string& name) {
  if (!std::isfinite(value)) {
    throw ValidationError(name + " must be finite");
  }
}

}  // namespace

CLinearSsp::CLinearSsp(const SoundSpeedProfile& profile) {
  const std::vector<SoundSpeedPoint>& points = profile.points();
  depths_.reserve(points.size());
  segments_.reserve(points.size() - 1U);

  for (const SoundSpeedPoint& point : points) {
    depths_.push_back(point.depth);
  }

  for (std::size_t index = 0; index + 1U < points.size(); ++index) {
    const SoundSpeedPoint& first = points[index];
    const SoundSpeedPoint& second = points[index + 1U];
    const double depthInterval = second.depth - first.depth;
    const double soundSpeedDepthGradient =
        (second.soundSpeed - first.soundSpeed) / depthInterval;
    requireFinite(soundSpeedDepthGradient,
                  "C-linear sound-speed depth gradient");

    segments_.push_back(
        Segment{.minimumDepth = first.depth,
                .maximumDepth = second.depth,
                .soundSpeedAtMinimumDepth = first.soundSpeed,
                .soundSpeedDepthGradient = soundSpeedDepthGradient,
                .densityAtMinimumDepth = first.density,
                .densityAtMaximumDepth = second.density});
  }
}

std::size_t CLinearSsp::segmentCount() const noexcept {
  return segments_.size();
}

std::size_t CLinearSsp::locateSegment(
    double depth, std::size_t previousSegment) const {
  requireFinite(depth, "SSP query depth");
  if (previousSegment >= segments_.size()) {
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
    return segments_.size() - 1U;
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
  return std::min(upperNodeIndex - 1U, segments_.size() - 1U);
}

SoundSpeedSample CLinearSsp::evaluateAtSegment(
    Vec2 position, std::size_t segmentIndex) const {
  if (!isFinite(position)) {
    throw ValidationError("SSP query position must be finite");
  }
  if (segmentIndex >= segments_.size()) {
    throw ValidationError("SSP segment index is out of range");
  }

  const Segment& segment = segments_[segmentIndex];
  if (position.depth < segment.minimumDepth ||
      position.depth > segment.maximumDepth) {
    throw ValidationError("SSP query depth is outside the selected segment");
  }
  return evaluatePolynomial(position, segmentIndex);
}

SoundSpeedSample CLinearSsp::evaluatePolynomial(
    Vec2 position, std::size_t segmentIndex) const {
  if (!isFinite(position)) {
    throw ValidationError("SSP query position must be finite");
  }
  if (segmentIndex >= segments_.size()) {
    throw ValidationError("SSP segment index is out of range");
  }
  const Segment& segment = segments_[segmentIndex];
  const double depthOffset = position.depth - segment.minimumDepth;
  const double soundSpeed =
      segment.soundSpeedAtMinimumDepth +
      depthOffset * segment.soundSpeedDepthGradient;
  const double weight =
      depthOffset / (segment.maximumDepth - segment.minimumDepth);
  const double density =
      (1.0 - weight) * segment.densityAtMinimumDepth +
      weight * segment.densityAtMaximumDepth;

  requireFinite(soundSpeed, "interpolated sound speed");
  requireFinite(density, "interpolated density");

  return SoundSpeedSample{
      .soundSpeed = soundSpeed,
      .imaginarySoundSpeed = 0.0,
      .soundSpeedGradient =
          Vec2{.range = 0.0, .depth = segment.soundSpeedDepthGradient},
      .soundSpeedHessian =
          SoundSpeedHessian{
              .rangeRange = 0.0, .rangeDepth = 0.0, .depthDepth = 0.0},
      .density = density,
      .segmentIndex = segmentIndex,
      .rangeSegmentIndex = 0U};
}

SoundSpeedSample CLinearSsp::evaluate(
    Vec2 position, std::size_t previousSegment) const {
  const std::size_t segmentIndex =
      locateSegment(position.depth, previousSegment);
  return evaluatePolynomial(position, segmentIndex);
}

}  // namespace bellhop
