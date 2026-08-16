#pragma once

#include <cstddef>
#include <vector>

#include "rayreuse/numerics/vec2.hpp"

namespace rayreuse {

enum class BoundaryOrientation {
  Upper,
  Lower,
};

struct BoundaryGeometrySample {
  Vec2 point;
  Vec2 tangent;
  Vec2 outwardNormal;
  double curvature{};
  double minimumRange{};
  double maximumRange{};
  std::size_t segmentIndex{};
};

// Value-owned flat or piecewise-linear range/depth boundary geometry.
class BoundaryGeometry {
 public:
  [[nodiscard]] static BoundaryGeometry flat(double depth,
                                             BoundaryOrientation orientation);
  [[nodiscard]] static BoundaryGeometry piecewiseLinear(
      std::vector<Vec2> nodes, double referenceDepth,
      BoundaryOrientation orientation);
  [[nodiscard]] BoundaryOrientation orientation() const noexcept;
  [[nodiscard]] bool isFlat() const noexcept;
  [[nodiscard]] std::size_t segmentCount() const noexcept;
  [[nodiscard]] double referenceDepth() const noexcept;
  [[nodiscard]] double minimumDepth() const noexcept;
  [[nodiscard]] double maximumDepth() const noexcept;
  [[nodiscard]] const std::vector<Vec2>& nodes() const noexcept;
  [[nodiscard]] double depthAt(double range, std::size_t previousSegment) const;
  [[nodiscard]] std::size_t locateSegment(double range,
                                          std::size_t previousSegment) const;
  [[nodiscard]] BoundaryGeometrySample evaluateAtSegment(
      double range, std::size_t segmentIndex) const;
  [[nodiscard]] BoundaryGeometrySample reflectionSampleAtSegment(
      Vec2 incidentPosition, std::size_t segmentIndex) const;
  [[nodiscard]] BoundaryGeometrySample evaluate(
      double range, std::size_t previousSegment) const;
  [[nodiscard]] double interiorSignedDistance(
      Vec2 position, std::size_t previousSegment) const;

 private:
  struct Segment {
    double minimumRange{};
    double maximumRange{};
    Vec2 point;
    double length{};
    Vec2 tangent;
    Vec2 outwardNormal;
  };

  BoundaryGeometry(double depth, BoundaryOrientation orientation);
  BoundaryGeometry(std::vector<Vec2> nodes, double referenceDepth,
                   BoundaryOrientation orientation);

  double depth_{};
  BoundaryOrientation orientation_{BoundaryOrientation::Upper};
  std::vector<Vec2> nodes_;
  std::vector<Segment> segments_;
};

}  // namespace rayreuse
