#include <cmath>
#include <iostream>
#include <limits>

#include "bellhop/error.hpp"
#include "bellhop/model/boundary_geometry.hpp"
#include "support/test_harness.hpp"

namespace {

using bellhop::BoundaryGeometry;
using bellhop::BoundaryInterpolationKind;
using bellhop::BoundaryOrientation;
using bellhop::ValidationError;
using bellhop::Vec2;
using bellhop::test::Context;

void testFlatUpperGeometry(Context& context) {
  const BoundaryGeometry geometry =
      BoundaryGeometry::flat(10.0, BoundaryOrientation::Upper);
  const auto sample = geometry.evaluate(2500.0, 0U);
  context.check(geometry.isFlat(), "upper geometry retains flat specialization");
  context.check(geometry.segmentCount() == 1U,
                "flat upper geometry has one segment");
  context.check(sample.point == Vec2{.range = 0.0, .depth = 10.0},
                "flat geometry retains the legacy zero-range anchor");
  context.check(sample.tangent == Vec2{.range = 1.0, .depth = 0.0},
                "flat tangent points toward increasing range");
  context.check(sample.outwardNormal ==
                    Vec2{.range = 0.0, .depth = -1.0},
                "upper outward normal points upward");
  context.check(sample.curvature == 0.0 && sample.segmentIndex == 0U,
                "flat geometry reports zero curvature and segment zero");
  context.checkNear(
      geometry.interiorSignedDistance(
          Vec2{.range = -1000.0, .depth = 25.0}, 0U),
      15.0, 0.0, "upper signed distance is positive inside the water");
}

void testFlatLowerGeometry(Context& context) {
  const BoundaryGeometry geometry =
      BoundaryGeometry::flat(100.0, BoundaryOrientation::Lower);
  const auto sample = geometry.evaluateAtSegment(-500.0, 0U);
  context.check(sample.outwardNormal ==
                    Vec2{.range = 0.0, .depth = 1.0},
                "lower outward normal points downward");
  context.checkNear(
      geometry.interiorSignedDistance(
          Vec2{.range = 500.0, .depth = 75.0}, 0U),
      25.0, 0.0, "lower signed distance is positive inside the water");
  context.checkNear(geometry.referenceDepth(), 100.0, 0.0,
                    "flat reference depth remains exact");
  context.checkNear(geometry.minimumDepth(), 100.0, 0.0,
                    "flat minimum depth remains exact");
  context.checkNear(geometry.maximumDepth(), 100.0, 0.0,
                    "flat maximum depth remains exact");
}

void testValidation(Context& context) {
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(BoundaryGeometry::flat(
            std::numeric_limits<double>::quiet_NaN(),
            BoundaryOrientation::Upper));
      },
      "flat boundary rejects non-finite depth");
  const BoundaryGeometry geometry =
      BoundaryGeometry::flat(0.0, BoundaryOrientation::Upper);
  context.expectThrows<ValidationError>(
      [&geometry] {
        static_cast<void>(geometry.evaluate(0.0, 1U));
      },
      "flat boundary rejects an invalid segment hint");
  context.expectThrows<ValidationError>(
      [&geometry] {
        static_cast<void>(geometry.evaluate(
            std::numeric_limits<double>::infinity(), 0U));
      },
      "flat boundary rejects a non-finite query range");
}

void testPiecewiseLinearGeometry(Context& context) {
  const BoundaryGeometry upper = BoundaryGeometry::piecewiseLinear(
      {{.range = 0.0, .depth = 0.0},
       {.range = 1000.0, .depth = 20.0},
       {.range = 2000.0, .depth = 0.0}},
      0.0, BoundaryOrientation::Upper);
  context.check(!upper.isFlat(), "piecewise boundary is not flat");
  context.check(upper.segmentCount() == 4U,
                "three user nodes form two slopes and two extensions");
  context.check(
      upper.interpolationKind() ==
          BoundaryInterpolationKind::PiecewiseLinear,
      "piecewise boundary retains its interpolation kind");

  const auto left = upper.evaluate(-500.0, 2U);
  context.check(left.segmentIndex == 0U &&
                    left.point == Vec2{.range = 0.0, .depth = 0.0} &&
                    left.tangent == Vec2{.range = 1.0, .depth = 0.0},
                "left extension is horizontal at the first depth");
  const auto right = upper.evaluate(2500.0, 0U);
  context.check(right.segmentIndex == 3U &&
                    right.point == Vec2{.range = 2000.0, .depth = 0.0} &&
                    right.tangent == Vec2{.range = 1.0, .depth = 0.0},
                "right extension is horizontal at the last depth");

  const auto rising = upper.evaluate(500.0, 0U);
  const double length = std::hypot(1000.0, 20.0);
  context.check(rising.segmentIndex == 1U,
                "interior range selects the first user segment");
  context.checkNear(rising.tangent.range, 1000.0 / length, 1.0e-15,
                    "piecewise tangent has normalized range component");
  context.checkNear(rising.tangent.depth, 20.0 / length, 1.0e-15,
                    "piecewise tangent has normalized depth component");
  context.checkNear(rising.outwardNormal.range, 20.0 / length, 1.0e-15,
                    "upper normal uses the original orientation");
  context.checkNear(rising.outwardNormal.depth, -1000.0 / length, 1.0e-15,
                    "upper normal points out of the water");
  context.checkNear(
      upper.interiorSignedDistance(
          Vec2{.range = 500.0, .depth = 30.0}, rising.segmentIndex),
      20.0 * 1000.0 / length, 1.0e-14,
      "sloping upper signed distance is positive below the line");

  context.check(upper.evaluate(1000.0, 1U).segmentIndex == 1U,
                "node retains a cached left segment");
  context.check(upper.evaluate(1000.0, 2U).segmentIndex == 2U,
                "node retains a cached right segment");
  context.check(upper.evaluate(1000.0, 0U).segmentIndex == 1U,
                "nonadjacent query at a node selects the left segment");

  const BoundaryGeometry lower = BoundaryGeometry::piecewiseLinear(
      {{.range = 0.0, .depth = 100.0},
       {.range = 1000.0, .depth = 120.0}},
      100.0, BoundaryOrientation::Lower);
  const auto lowerSlope = lower.evaluate(500.0, 0U);
  context.checkNear(lowerSlope.outwardNormal.range, -20.0 / length,
                    1.0e-15,
                    "lower normal has opposite range orientation");
  context.checkNear(lowerSlope.outwardNormal.depth, 1000.0 / length,
                    1.0e-15, "lower normal points below the seabed");

  const auto reflection = upper.reflectionSampleAtSegment(
      Vec2{.range = 500.0, .depth = 10.0}, 1U);
  context.check(reflection.tangent == rising.tangent &&
                    reflection.outwardNormal == rising.outwardNormal &&
                    reflection.curvature == 0.0,
                "piecewise reflection sampling is identical to the chord");
}

void testCurvilinearReflectionGeometry(Context& context) {
  const BoundaryGeometry upper = BoundaryGeometry::curvilinear(
      {{.range = 0.0, .depth = 0.0},
       {.range = 100.0, .depth = 100.0},
       {.range = 200.0, .depth = 0.0}},
      0.0, BoundaryOrientation::Upper);
  context.check(
      upper.interpolationKind() == BoundaryInterpolationKind::Curvilinear,
      "curvilinear boundary retains its interpolation kind");

  const double segmentLength = 100.0 * std::sqrt(2.0);
  const double diagonal = 100.0 / segmentLength;
  const auto chord = upper.evaluateAtSegment(25.0, 1U);
  context.checkNear(chord.tangent.range, diagonal, 0.0,
                    "curvilinear collision sampling keeps the chord tangent");
  context.checkNear(chord.tangent.depth, diagonal, 0.0,
                    "curvilinear collision sampling keeps the chord slope");
  context.checkNear(chord.curvature, 0.0, 0.0,
                    "collision sampling does not expose reflection curvature");

  const Vec2 startFrame =
      0.5 * (Vec2{.range = 1.0, .depth = 0.0} +
             Vec2{.range = diagonal, .depth = diagonal});
  const Vec2 endFrame = Vec2{.range = diagonal, .depth = 0.0};
  const Vec2 expectedTangent = 0.75 * startFrame + 0.25 * endFrame;
  const auto reflection = upper.reflectionSampleAtSegment(
      Vec2{.range = 25.0, .depth = 25.0}, 1U);
  context.checkNear(reflection.tangent.range, expectedTangent.range, 1.0e-15,
                    "curvilinear reflection linearly interpolates node frames");
  context.checkNear(reflection.tangent.depth, expectedTangent.depth, 1.0e-15,
                    "curvilinear reflection preserves frame depth component");
  context.checkNear(reflection.outwardNormal.range,
                    expectedTangent.depth, 1.0e-15,
                    "curvilinear upper normal rotates the interpolated frame");
  context.checkNear(reflection.outwardNormal.depth,
                    -expectedTangent.range, 1.0e-15,
                    "curvilinear upper normal is not independently normalized");
  context.checkNear(reflection.curvature,
                    -0.02 * chord.tangent.range * chord.tangent.range *
                        chord.tangent.range,
                    0.0,
                    "segment curvature uses Dxx times tangent-range cubed");

  const auto nodeFrame = upper.reflectionSampleAtSegment(
      Vec2{.range = 100.0, .depth = 100.0}, 1U);
  context.checkNear(nodeFrame.tangent.range, diagonal, 2.0e-16,
                    "interior node frame averages adjacent unit tangents");
  context.checkNear(nodeFrame.tangent.depth, 0.0, 2.0e-16,
                    "opposite node-frame depth components cancel");
  context.checkNear(bellhop::norm(nodeFrame.tangent), diagonal, 2.0e-16,
                    "curvilinear node frames remain unnormalized");

  const Vec2 overshoot{.range = 25.0, .depth = 26.0};
  const double overshootFraction =
      bellhop::fortranDotProduct2D(overshoot, chord.tangent) /
      segmentLength;
  const Vec2 overshootExpected{
      .range = std::fma(1.0 - overshootFraction, startFrame.range,
                        overshootFraction * endFrame.range),
      .depth = std::fma(1.0 - overshootFraction, startFrame.depth,
                        overshootFraction * endFrame.depth)};
  const auto overshootReflection =
      upper.reflectionSampleAtSegment(overshoot, 1U);
  context.checkNear(overshootReflection.tangent.depth,
                    overshootExpected.depth, 0.0,
                    "reflection interpolation uses projected incident position");
  context.check(
      overshootReflection.tangent.depth != reflection.tangent.depth,
      "normal overshoot is not collapsed to a range-only fraction");

  const auto extension = upper.reflectionSampleAtSegment(
      Vec2{.range = 250.0, .depth = 0.0}, 3U);
  context.check(extension.tangent == Vec2{.range = 1.0, .depth = 0.0} &&
                    extension.curvature == 0.0,
                "curvilinear extension sampling is deterministic and flat");
}

}  // namespace

int main() {
  Context context;
  testFlatUpperGeometry(context);
  testFlatLowerGeometry(context);
  testPiecewiseLinearGeometry(context);
  testCurvilinearReflectionGeometry(context);
  testValidation(context);
  if (context.failureCount() != 0) {
    std::cerr << context.failureCount()
              << " boundary-geometry test assertion(s) failed\n";
    return 1;
  }
  std::cout << "All Bellhop F2CPP boundary-geometry tests passed\n";
  return 0;
}
