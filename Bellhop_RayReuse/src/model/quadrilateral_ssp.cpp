#include "rayreuse/model/quadrilateral_ssp.hpp"

#include <algorithm>
#include <cmath>
#include <string>

#include "rayreuse/error.hpp"

namespace rayreuse {
namespace {

void requireFinite(double value, const std::string& name) {
  if (!std::isfinite(value)) {
    throw ValidationError(name + " must be finite");
  }
}

}  // namespace

QuadrilateralSsp::QuadrilateralSsp(const SoundSpeedProfile& profile)
    : grid_(profile.quadrilateralGrid()) {
  if (profile.interpolationKind() != SspInterpolationKind::Quadrilateral ||
      !grid_) {
    throw ValidationError(
        "quadrilateral SSP evaluator requires a quadrilateral profile");
  }

  const std::vector<SoundSpeedPoint>& points = profile.points();
  depths_.reserve(points.size());
  depthSegments_.reserve(points.size() - 1U);
  depthGradients_.reserve((points.size() - 1U) * grid_->rangeCount);

  for (const SoundSpeedPoint& point : points) {
    depths_.push_back(point.depth);
  }
  for (std::size_t depthIndex = 0U; depthIndex + 1U < points.size();
       ++depthIndex) {
    const SoundSpeedPoint& first = points[depthIndex];
    const SoundSpeedPoint& second = points[depthIndex + 1U];
    const double depthInterval = second.depth - first.depth;
    depthSegments_.push_back(
        DepthSegment{.minimumDepth = first.depth,
                     .maximumDepth = second.depth,
                     .densityAtMinimumDepth = first.density,
                     .densityAtMaximumDepth = second.density});

    for (std::size_t rangeIndex = 0U; rangeIndex < grid_->rangeCount;
         ++rangeIndex) {
      // Preserve Quad's initialization order: one vertical slope is formed
      // for every matrix column before any query is evaluated.
      const double gradient = (speedAt(depthIndex + 1U, rangeIndex) -
                               speedAt(depthIndex, rangeIndex)) /
                              depthInterval;
      requireFinite(gradient, "quadrilateral SSP depth gradient");
      depthGradients_.push_back(gradient);
    }
  }
}

std::size_t QuadrilateralSsp::segmentCount() const noexcept {
  return depthSegments_.size();
}

std::size_t QuadrilateralSsp::rangeSegmentCount() const noexcept {
  return grid_->rangeCount - 1U;
}

std::size_t QuadrilateralSsp::locateSegment(double depth,
                                            std::size_t previousSegment) const {
  requireFinite(depth, "SSP query depth");
  if (previousSegment >= depthSegments_.size()) {
    throw ValidationError("SSP previous segment index is out of range");
  }

  const DepthSegment& previous = depthSegments_[previousSegment];
  if (depth >= previous.minimumDepth && depth <= previous.maximumDepth) {
    return previousSegment;
  }
  if (depth < depths_.front()) {
    return 0U;
  }
  if (depth > depths_.back()) {
    return depthSegments_.size() - 1U;
  }

  // Quad searches with a strict `depth < node` comparison after the hinted
  // cell misses. Thus an exact internal node selects its right-hand depth
  // cell unless either adjacent hinted cell already retained the node.
  const auto upperNode =
      std::upper_bound(depths_.begin(), depths_.end(), depth);
  if (upperNode == depths_.begin()) {
    return 0U;
  }
  const std::size_t upperNodeIndex =
      static_cast<std::size_t>(upperNode - depths_.begin());
  return std::min(upperNodeIndex - 1U, depthSegments_.size() - 1U);
}

std::size_t QuadrilateralSsp::locateRangeSegment(
    double range, std::size_t previousRangeSegment) const {
  requireFinite(range, "quadrilateral SSP query range");
  if (previousRangeSegment >= rangeSegmentCount()) {
    throw ValidationError("SSP previous range segment index is out of range");
  }
  if (range < grid_->rangesMeters.front() ||
      range > grid_->rangesMeters.back()) {
    throw ValidationError("quadrilateral SSP query range is outside its grid");
  }

  const double previousMinimum = grid_->rangesMeters[previousRangeSegment];
  const double previousMaximum = grid_->rangesMeters[previousRangeSegment + 1U];
  const bool isLast = previousRangeSegment + 1U == rangeSegmentCount();
  if (range >= previousMinimum &&
      (range < previousMaximum || (isLast && range <= previousMaximum))) {
    return previousRangeSegment;
  }

  // Quad's range cells are left-closed/right-open. An internal node therefore
  // selects the cell to its right; the accepted final node uses the last cell.
  const auto upper = std::upper_bound(grid_->rangesMeters.begin(),
                                      grid_->rangesMeters.end(), range);
  if (upper == grid_->rangesMeters.end()) {
    return grid_->rangeCount - 2U;
  }
  return static_cast<std::size_t>(upper - grid_->rangesMeters.begin() - 1);
}

double QuadrilateralSsp::minimumRangeForSegment(
    std::size_t rangeSegmentIndex) const {
  if (rangeSegmentIndex >= rangeSegmentCount()) {
    throw ValidationError("SSP range segment index is out of range");
  }
  return grid_->rangesMeters[rangeSegmentIndex];
}

double QuadrilateralSsp::maximumRangeForSegment(
    std::size_t rangeSegmentIndex) const {
  if (rangeSegmentIndex >= rangeSegmentCount()) {
    throw ValidationError("SSP range segment index is out of range");
  }
  return grid_->rangesMeters[rangeSegmentIndex + 1U];
}

double QuadrilateralSsp::speedAt(std::size_t depthIndex,
                                 std::size_t rangeIndex) const noexcept {
  return grid_->speedsDepthMajor[depthIndex * grid_->rangeCount + rangeIndex];
}

double QuadrilateralSsp::depthGradientAt(
    std::size_t depthSegmentIndex, std::size_t rangeIndex) const noexcept {
  return depthGradients_[depthSegmentIndex * grid_->rangeCount + rangeIndex];
}

SoundSpeedSample QuadrilateralSsp::evaluateAtSegment(
    Vec2 position, std::size_t segmentIndex) const {
  if (!isFinite(position)) {
    throw ValidationError("SSP query position must be finite");
  }
  if (segmentIndex >= depthSegments_.size()) {
    throw ValidationError("SSP segment index is out of range");
  }
  const DepthSegment& segment = depthSegments_[segmentIndex];
  if (position.depth < segment.minimumDepth ||
      position.depth > segment.maximumDepth) {
    throw ValidationError("SSP query depth is outside the selected segment");
  }
  return evaluatePolynomial(position, segmentIndex,
                            locateRangeSegment(position.range, 0U));
}

SoundSpeedSample QuadrilateralSsp::evaluateAtSegments(
    Vec2 position, std::size_t segmentIndex,
    std::size_t rangeSegmentIndex) const {
  if (!isFinite(position)) {
    throw ValidationError("SSP query position must be finite");
  }
  if (segmentIndex >= depthSegments_.size()) {
    throw ValidationError("SSP segment index is out of range");
  }
  const DepthSegment& depthSegment = depthSegments_[segmentIndex];
  if (position.depth < depthSegment.minimumDepth ||
      position.depth > depthSegment.maximumDepth) {
    throw ValidationError("SSP query depth is outside the selected segment");
  }
  const std::size_t locatedRangeSegment =
      locateRangeSegment(position.range, rangeSegmentIndex);
  if (locatedRangeSegment != rangeSegmentIndex) {
    throw ValidationError(
        "SSP query range is outside the selected range segment");
  }
  return evaluatePolynomial(position, segmentIndex, rangeSegmentIndex);
}

SoundSpeedSample QuadrilateralSsp::evaluatePolynomial(
    Vec2 position, std::size_t depthSegmentIndex,
    std::size_t rangeSegmentIndex) const {
  if (!isFinite(position)) {
    throw ValidationError("SSP query position must be finite");
  }
  if (depthSegmentIndex >= depthSegments_.size()) {
    throw ValidationError("SSP segment index is out of range");
  }

  if (rangeSegmentIndex >= rangeSegmentCount()) {
    throw ValidationError("SSP range segment index is out of range");
  }
  const DepthSegment& depthSegment = depthSegments_[depthSegmentIndex];
  const double cz1 = depthGradientAt(depthSegmentIndex, rangeSegmentIndex);
  const double cz2 = depthGradientAt(depthSegmentIndex, rangeSegmentIndex + 1U);

  double s2 = position.depth - depthSegment.minimumDepth;
  const double deltaZ = depthSegment.maximumDepth - depthSegment.minimumDepth;
  const double c1 = speedAt(depthSegmentIndex, rangeSegmentIndex) + s2 * cz1;
  const double c2 =
      speedAt(depthSegmentIndex, rangeSegmentIndex + 1U) + s2 * cz2;

  const double deltaR = grid_->rangesMeters[rangeSegmentIndex + 1U] -
                        grid_->rangesMeters[rangeSegmentIndex];
  double s1 =
      (position.range - grid_->rangesMeters[rangeSegmentIndex]) / deltaR;
  s1 = std::min(s1, 1.0);
  s1 = std::max(s1, 0.0);

  const double soundSpeed = (1.0 - s1) * c1 + s1 * c2;
  s2 = s2 / deltaZ;
  const double cz = (1.0 - s1) * cz1 + s1 * cz2;
  const double cr = (c2 - c1) / deltaR;
  const double crz = (cz2 - cz1) / deltaR;
  const double density = (1.0 - s2) * depthSegment.densityAtMinimumDepth +
                         s2 * depthSegment.densityAtMaximumDepth;

  requireFinite(soundSpeed, "interpolated sound speed");
  requireFinite(cr, "quadrilateral SSP range gradient");
  requireFinite(cz, "quadrilateral SSP depth gradient");
  requireFinite(crz, "quadrilateral SSP cross derivative");
  requireFinite(density, "interpolated density");

  return SoundSpeedSample{
      .soundSpeed = soundSpeed,
      .imaginarySoundSpeed = 0.0,
      .soundSpeedGradient = Vec2{.range = cr, .depth = cz},
      .soundSpeedHessian =
          SoundSpeedHessian{
              .rangeRange = 0.0, .rangeDepth = crz, .depthDepth = 0.0},
      .density = density,
      .segmentIndex = depthSegmentIndex,
      .rangeSegmentIndex = rangeSegmentIndex};
}

SoundSpeedSample QuadrilateralSsp::evaluate(Vec2 position,
                                            std::size_t previousSegment) const {
  return evaluate(position, previousSegment, 0U);
}

SoundSpeedSample QuadrilateralSsp::evaluate(
    Vec2 position, std::size_t previousSegment,
    std::size_t previousRangeSegment) const {
  const std::size_t segmentIndex =
      locateSegment(position.depth, previousSegment);
  const std::size_t rangeSegmentIndex =
      locateRangeSegment(position.range, previousRangeSegment);
  return evaluatePolynomial(position, segmentIndex, rangeSegmentIndex);
}

}  // namespace rayreuse
