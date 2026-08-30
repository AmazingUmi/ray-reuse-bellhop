#include "rayreuse/model/boundary_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include "rayreuse/error.hpp"

namespace rayreuse {

BoundaryGeometry BoundaryGeometry::flat(double depth,
                                        BoundaryOrientation orientation) {
  return BoundaryGeometry(depth, orientation);
}

BoundaryGeometry BoundaryGeometry::piecewiseLinear(
    std::vector<Vec2> nodes, double referenceDepth,
    BoundaryOrientation orientation) {
  return BoundaryGeometry(std::move(nodes), referenceDepth, orientation,
                          BoundaryInterpolationKind::PiecewiseLinear);
}

BoundaryGeometry BoundaryGeometry::curvilinear(
    std::vector<Vec2> nodes, double referenceDepth,
    BoundaryOrientation orientation) {
  return BoundaryGeometry(std::move(nodes), referenceDepth, orientation,
                          BoundaryInterpolationKind::Curvilinear);
}

BoundaryGeometry::BoundaryGeometry(double depth,
                                   BoundaryOrientation orientation)
    : depth_(depth), orientation_(orientation) {
  if (!std::isfinite(depth_)) {
    throw ValidationError("boundary geometry depth must be finite");
  }
}

BoundaryGeometry::BoundaryGeometry(std::vector<Vec2> nodes,
                                   double referenceDepth,
                                   BoundaryOrientation orientation,
                                   BoundaryInterpolationKind interpolationKind)
    : depth_(referenceDepth),
      orientation_(orientation),
      interpolationKind_(interpolationKind),
      nodes_(std::move(nodes)) {
  if (!std::isfinite(depth_)) {
    throw ValidationError("boundary reference depth must be finite");
  }
  if (nodes_.size() < 2U) {
    throw ValidationError(
        "range-dependent boundary requires at least two nodes");
  }
  for (std::size_t index = 0U; index < nodes_.size(); ++index) {
    if (!isFinite(nodes_[index])) {
      throw ValidationError("boundary nodes must be finite");
    }
    if (index > 0U && nodes_[index - 1U].range >= nodes_[index].range) {
      throw ValidationError("boundary node ranges must be strictly increasing");
    }
  }

  const auto makeSegment = [orientation](double minimumRange,
                                         double maximumRange, Vec2 point,
                                         Vec2 rawTangent) {
    // Reproduce Fortran NORM2's scaled Euclidean normalization explicitly.
    // libc hypot and sqrt(dot(v, v)) can each choose the neighboring binary64
    // result and shift a nominal plane landing across the reflection sign test.
    const double scale =
        std::max(std::abs(rawTangent.range), std::abs(rawTangent.depth));
    const double scaledRange = rawTangent.range / scale;
    const double scaledDepth = rawTangent.depth / scale;
    const double length = scale * std::sqrt(scaledRange * scaledRange +
                                            scaledDepth * scaledDepth);
    if (!std::isfinite(length) || length <= 0.0) {
      throw ValidationError("boundary segment length must be positive");
    }
    const Vec2 tangent = rawTangent / length;
    const Vec2 outwardNormal =
        orientation == BoundaryOrientation::Upper
            ? Vec2{.range = tangent.depth, .depth = -tangent.range}
            : Vec2{.range = -tangent.depth, .depth = tangent.range};
    return Segment{.minimumRange = minimumRange,
                   .maximumRange = maximumRange,
                   .point = point,
                   .length = length,
                   .tangent = tangent,
                   .outwardNormal = outwardNormal,
                   .reflectionStartTangent = tangent,
                   .reflectionEndTangent = tangent,
                   .curvature = 0.0};
  };

  segments_.reserve(nodes_.size() + 1U);
  segments_.push_back(makeSegment(-std::numeric_limits<double>::infinity(),
                                  nodes_.front().range, nodes_.front(),
                                  Vec2{.range = 1.0, .depth = 0.0}));
  for (std::size_t index = 0U; index + 1U < nodes_.size(); ++index) {
    segments_.push_back(makeSegment(nodes_[index].range,
                                    nodes_[index + 1U].range, nodes_[index],
                                    nodes_[index + 1U] - nodes_[index]));
  }
  segments_.push_back(
      makeSegment(nodes_.back().range, std::numeric_limits<double>::infinity(),
                  nodes_.back(), Vec2{.range = 1.0, .depth = 0.0}));

  if (interpolationKind_ == BoundaryInterpolationKind::Curvilinear) {
    std::vector<Vec2> nodeTangents;
    nodeTangents.reserve(nodes_.size());
    for (std::size_t index = 0U; index < nodes_.size(); ++index) {
      nodeTangents.push_back(
          0.5 * (segments_[index].tangent + segments_[index + 1U].tangent));
    }
    for (std::size_t index = 0U; index + 1U < nodes_.size(); ++index) {
      Segment& segment = segments_[index + 1U];
      segment.reflectionStartTangent = nodeTangents[index];
      segment.reflectionEndTangent = nodeTangents[index + 1U];
      const Segment& nextSegment = segments_[index + 2U];
      const double slope = segment.tangent.depth / segment.tangent.range;
      const double nextSlope =
          nextSegment.tangent.depth / nextSegment.tangent.range;
      const double deltaRange = nodes_[index + 1U].range - nodes_[index].range;
      const double tangentRangeCubed =
          segment.tangent.range * segment.tangent.range * segment.tangent.range;
      segment.curvature = (nextSlope - slope) / deltaRange * tangentRangeCubed;
    }
  }
}

BoundaryOrientation BoundaryGeometry::orientation() const noexcept {
  return orientation_;
}

BoundaryInterpolationKind BoundaryGeometry::interpolationKind() const noexcept {
  return interpolationKind_;
}

bool BoundaryGeometry::isFlat() const noexcept { return segments_.empty(); }

std::size_t BoundaryGeometry::segmentCount() const noexcept {
  return isFlat() ? 1U : segments_.size();
}

double BoundaryGeometry::referenceDepth() const noexcept { return depth_; }

double BoundaryGeometry::minimumDepth() const noexcept {
  if (isFlat()) {
    return depth_;
  }
  return std::min_element(
             nodes_.begin(), nodes_.end(),
             [](Vec2 left, Vec2 right) { return left.depth < right.depth; })
      ->depth;
}

double BoundaryGeometry::maximumDepth() const noexcept {
  if (isFlat()) {
    return depth_;
  }
  return std::max_element(
             nodes_.begin(), nodes_.end(),
             [](Vec2 left, Vec2 right) { return left.depth < right.depth; })
      ->depth;
}

const std::vector<Vec2>& BoundaryGeometry::nodes() const noexcept {
  return nodes_;
}

double BoundaryGeometry::depthAt(double range,
                                 std::size_t previousSegment) const {
  const BoundaryGeometrySample sample = evaluate(range, previousSegment);
  if (isFlat()) {
    return sample.point.depth;
  }
  return sample.point.depth + (range - sample.point.range) *
                                  sample.tangent.depth / sample.tangent.range;
}

std::size_t BoundaryGeometry::locateSegment(double range,
                                            std::size_t previousSegment) const {
  if (!std::isfinite(range)) {
    throw ValidationError("boundary query range must be finite");
  }
  if (previousSegment >= segmentCount()) {
    throw ValidationError("boundary previous segment index is out of range");
  }
  if (isFlat()) {
    return 0U;
  }
  const Segment& previous = segments_[previousSegment];
  if (range >= previous.minimumRange && range <= previous.maximumRange) {
    return previousSegment;
  }
  const auto upperNode = std::lower_bound(
      nodes_.begin(), nodes_.end(), range,
      [](Vec2 node, double query) { return node.range < query; });
  if (upperNode == nodes_.begin()) {
    return 0U;
  }
  if (upperNode == nodes_.end()) {
    return segments_.size() - 1U;
  }
  return static_cast<std::size_t>(upperNode - nodes_.begin());
}

BoundaryGeometrySample BoundaryGeometry::evaluateAtSegment(
    double range, std::size_t segmentIndex) const {
  if (!std::isfinite(range)) {
    throw ValidationError("boundary query range must be finite");
  }
  if (segmentIndex >= segmentCount()) {
    throw ValidationError("boundary segment index is out of range");
  }
  if (isFlat()) {
    const bool upper = orientation_ == BoundaryOrientation::Upper;
    return {.point = Vec2{.range = 0.0, .depth = depth_},
            .tangent = Vec2{.range = 1.0, .depth = 0.0},
            .outwardNormal = Vec2{.range = 0.0, .depth = upper ? -1.0 : 1.0},
            .curvature = 0.0,
            .minimumRange = -std::numeric_limits<double>::infinity(),
            .maximumRange = std::numeric_limits<double>::infinity(),
            .segmentIndex = 0U};
  }
  const Segment& segment = segments_[segmentIndex];
  if (range < segment.minimumRange || range > segment.maximumRange) {
    throw ValidationError(
        "boundary query range is outside the selected segment");
  }
  return {.point = segment.point,
          .tangent = segment.tangent,
          .outwardNormal = segment.outwardNormal,
          .curvature = 0.0,
          .minimumRange = segment.minimumRange,
          .maximumRange = segment.maximumRange,
          .segmentIndex = segmentIndex};
}

BoundaryGeometrySample BoundaryGeometry::reflectionSampleAtSegment(
    Vec2 incidentPosition, std::size_t segmentIndex) const {
  if (!isFinite(incidentPosition)) {
    throw ValidationError("boundary reflection position must be finite");
  }
  const BoundaryGeometrySample chord =
      evaluateAtSegment(incidentPosition.range, segmentIndex);
  if (isFlat() ||
      interpolationKind_ == BoundaryInterpolationKind::PiecewiseLinear ||
      segmentIndex == 0U || segmentIndex + 1U == segments_.size()) {
    return chord;
  }

  const Segment& segment = segments_[segmentIndex];
  const double fraction =
      fortranDotProduct2D(incidentPosition - segment.point, segment.tangent) /
      segment.length;
  // The locked gfortran oracle fuses the leading start-frame product into
  // the already-rounded end-frame product for this array expression.
  const double startWeight = 1.0 - fraction;
  const Vec2 tangent{
      .range = std::fma(startWeight, segment.reflectionStartTangent.range,
                        fraction * segment.reflectionEndTangent.range),
      .depth = std::fma(startWeight, segment.reflectionStartTangent.depth,
                        fraction * segment.reflectionEndTangent.depth)};
  const Vec2 outwardNormal =
      orientation_ == BoundaryOrientation::Upper
          ? Vec2{.range = tangent.depth, .depth = -tangent.range}
          : Vec2{.range = -tangent.depth, .depth = tangent.range};
  return {.point = chord.point,
          .tangent = tangent,
          .outwardNormal = outwardNormal,
          .curvature = segment.curvature,
          .minimumRange = chord.minimumRange,
          .maximumRange = chord.maximumRange,
          .segmentIndex = chord.segmentIndex};
}

BoundaryGeometrySample BoundaryGeometry::evaluate(
    double range, std::size_t previousSegment) const {
  return evaluateAtSegment(range, locateSegment(range, previousSegment));
}

double BoundaryGeometry::interiorSignedDistance(
    Vec2 position, std::size_t previousSegment) const {
  if (!isFinite(position)) {
    throw ValidationError("boundary distance position must be finite");
  }
  const BoundaryGeometrySample sample =
      evaluate(position.range, previousSegment);
  return -fortranDotProduct2D(sample.outwardNormal, position - sample.point);
}

}  // namespace rayreuse
