#include "rayreuse/model/boundary_geometry.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <vector>

#include "rayreuse/error.hpp"
#include "support/test_harness.hpp"

namespace {

using rayreuse::BoundaryGeometry;
using rayreuse::BoundaryGeometrySample;
using rayreuse::BoundaryInterpolationKind;
using rayreuse::BoundaryOrientation;
using rayreuse::Vec2;
using rayreuse::test::Context;

// Asymmetric four-node ladder (SI units). Every chord slope and every node
// kink is distinct, so any mix-up of segment order or curvature sign flips
// the anchors below.
constexpr std::array<Vec2, 4> kNodes{{
    Vec2{.range = 0.0, .depth = 5.0},
    Vec2{.range = 1000.0, .depth = 10.0},
    Vec2{.range = 2000.0, .depth = 4.0},
    Vec2{.range = 3000.0, .depth = 8.0},
}};

// Independent re-derivation of the locked Origin bdryMod.f90
// ComputeBdryTangentNormal formulas (chord part): Fortran NORM2 length,
// unit tangent, and first derivative Dx = t2/t1.
struct ChordFrame {
  double length{};
  Vec2 tangent{};
  double slope{};
};

[[nodiscard]] ChordFrame chordFrame(Vec2 delta) {
  const double length =
      std::sqrt(delta.range * delta.range + delta.depth * delta.depth);
  return ChordFrame{.length = length,
                    .tangent = delta / length,
                    .slope = delta.depth / delta.range};
}

[[nodiscard]] double curvatureFromOriginFormula(const ChordFrame& segment,
                                                const ChordFrame& next,
                                                double deltaRange) {
  // Origin overrides kappa with Dss = Dxx * t1^3 where
  // Dxx = (Dx[next] - Dx[segment]) / (x[next] - x[segment]).
  const double secondDerivative = (next.slope - segment.slope) / deltaRange;
  return secondDerivative * segment.tangent.range * segment.tangent.range *
         segment.tangent.range;
}

void testCurvilinearNodeFrameAndCurvature(Context& context) {
  const BoundaryGeometry bottom = BoundaryGeometry::curvilinear(
      std::vector<Vec2>(kNodes.begin(), kNodes.end()), 130.0,
      BoundaryOrientation::Lower);
  context.check(
      bottom.interpolationKind() == BoundaryInterpolationKind::Curvilinear,
      "curvilinear factory records the interpolation kind");
  context.check(!bottom.isFlat() && bottom.segmentCount() == 5U,
                "curvilinear geometry extends four nodes with two outside "
                "chords");

  const ChordFrame first = chordFrame(kNodes[1U] - kNodes[0U]);
  const ChordFrame second = chordFrame(kNodes[2U] - kNodes[1U]);
  const ChordFrame third = chordFrame(kNodes[3U] - kNodes[2U]);

  // Origin node tangent: average of the adjacent chord tangents (the legacy
  // sss = 0.5 overwrite in ComputeBdryTangentNormal).
  const Vec2 nodeTangentAtFirstNode =
      0.5 * (Vec2{.range = 1.0, .depth = 0.0} + first.tangent);
  const Vec2 nodeTangentAtSecondNode = 0.5 * (first.tangent + second.tangent);

  // Reflection frame at the segment-1 chord midpoint. The stored frame must
  // be the interpolated (non-unit) node tangent pair, not the chord tangent.
  const Vec2 midpoint{.range = 500.0, .depth = 7.5};
  const BoundaryGeometrySample sample =
      bottom.reflectionSampleAtSegment(midpoint, 1U);
  const double fraction =
      rayreuse::dot(midpoint - kNodes[0U], first.tangent) / first.length;
  const Vec2 expectedTangent = (1.0 - fraction) * nodeTangentAtFirstNode +
                               fraction * nodeTangentAtSecondNode;
  context.checkNear(sample.tangent.range, expectedTangent.range, 1.0e-12,
                    "curvilinear midpoint tangent range matches the Origin "
                    "0.5-average node interpolation");
  context.checkNear(sample.tangent.depth, expectedTangent.depth, 1.0e-12,
                    "curvilinear midpoint tangent depth matches the Origin "
                    "0.5-average node interpolation");
  context.check(std::abs(rayreuse::norm(sample.tangent) - 1.0) > 1.0e-6,
                "interpolated curvilinear reflection frame is not unit "
                "length");
  context.checkNear(rayreuse::dot(sample.tangent, sample.outwardNormal), 0.0,
                    1.0e-12, "lower interpolated frame stays orthogonal");
  context.checkNear(sample.outwardNormal.range, -sample.tangent.depth, 0.0,
                    "lower interpolated outward normal flips tangent depth");

  // Curvature anchors: Dss = (Dx_next - Dx)/dx * t1^3 per interior segment,
  // including the trailing segment whose next chord is the flat extension
  // (slope zero by Origin Bdry(NPts)%Nodet = [1, 0]).
  const ChordFrame flatExtension{
      .length = 1.0, .tangent = Vec2{.range = 1.0, .depth = 0.0}, .slope = 0.0};
  context.checkNear(bottom.reflectionSampleAtSegment(midpoint, 1U).curvature,
                    curvatureFromOriginFormula(first, second, 1000.0), 1.0e-15,
                    "segment 1 curvature matches the Origin Dss formula");
  context.checkNear(
      bottom.reflectionSampleAtSegment(Vec2{.range = 1500.0, .depth = 7.0}, 2U)
          .curvature,
      curvatureFromOriginFormula(second, third, 1000.0), 1.0e-15,
      "segment 2 curvature matches the Origin Dss formula");
  context.checkNear(
      bottom.reflectionSampleAtSegment(Vec2{.range = 2500.0, .depth = 6.0}, 3U)
          .curvature,
      curvatureFromOriginFormula(third, flatExtension, 1000.0), 1.0e-15,
      "segment 3 curvature uses the flat extension chord slope zero");
  context.check(
      bottom.reflectionSampleAtSegment(midpoint, 1U).curvature != 0.0 &&
          bottom.reflectionSampleAtSegment(Vec2{.range = 1500.0, .depth = 7.0},
                                           2U)
                  .curvature != 0.0,
      "interior curvilinear segments report non-zero curvature");

  // Collision samples stay on the piecewise-linear chord with zero
  // curvature regardless of the interpolation kind (Origin ReduceStep2D /
  // Distances2D semantics).
  const BoundaryGeometrySample chord = bottom.evaluateAtSegment(500.0, 1U);
  context.checkNear(chord.tangent.range, first.tangent.range, 1.0e-12,
                    "collision chord tangent range is unaffected");
  context.checkNear(chord.tangent.depth, first.tangent.depth, 1.0e-12,
                    "collision chord tangent depth is unaffected");
  context.check(chord.curvature == 0.0 &&
                    std::abs(rayreuse::norm(chord.tangent) - 1.0) <= 1.0e-15,
                "collision chord sample keeps unit tangent and zero "
                "curvature");

  // First and last (infinite) extension segments fall back to the chord
  // frame with zero curvature, mirroring Origin Bdry(1)/Bdry(NPts) node
  // tangents [1, 0].
  const BoundaryGeometrySample leadIn =
      bottom.reflectionSampleAtSegment(Vec2{.range = -10.0, .depth = 5.0}, 0U);
  context.check(leadIn.tangent == Vec2{.range = 1.0, .depth = 0.0} &&
                    leadIn.curvature == 0.0,
                "leading extension segment reflects with the flat chord");
  const BoundaryGeometrySample tail =
      bottom.reflectionSampleAtSegment(Vec2{.range = 3100.0, .depth = 8.0}, 4U);
  context.check(
      tail.tangent == Vec2{.range = 1.0, .depth = 0.0} && tail.curvature == 0.0,
      "trailing extension segment reflects with the flat chord");

  // Upper boundaries rotate the interpolated outward normal the other way.
  const BoundaryGeometry top = BoundaryGeometry::curvilinear(
      std::vector<Vec2>(kNodes.begin(), kNodes.end()), 0.0,
      BoundaryOrientation::Upper);
  const BoundaryGeometrySample topSample =
      top.reflectionSampleAtSegment(midpoint, 1U);
  context.checkNear(topSample.outwardNormal.range, topSample.tangent.depth, 0.0,
                    "upper interpolated outward normal keeps tangent depth");
  context.checkNear(topSample.outwardNormal.depth, -topSample.tangent.range,
                    0.0,
                    "upper interpolated outward normal flips tangent range");
  context.checkNear(topSample.curvature,
                    bottom.reflectionSampleAtSegment(midpoint, 1U).curvature,
                    0.0, "curvature is orientation independent");
}

void testPiecewiseAndFlatReflectionFramesUnchanged(Context& context) {
  const BoundaryGeometry piecewise = BoundaryGeometry::piecewiseLinear(
      std::vector<Vec2>(kNodes.begin(), kNodes.end()), 130.0,
      BoundaryOrientation::Lower);
  context.check(piecewise.interpolationKind() ==
                    BoundaryInterpolationKind::PiecewiseLinear,
                "piecewise factory records the interpolation kind");
  const Vec2 midpoint{.range = 500.0, .depth = 7.5};
  const BoundaryGeometrySample reflection =
      piecewise.reflectionSampleAtSegment(midpoint, 1U);
  const BoundaryGeometrySample chord =
      piecewise.evaluateAtSegment(midpoint.range, 1U);
  context.check(reflection.tangent == chord.tangent &&
                    reflection.outwardNormal == chord.outwardNormal &&
                    reflection.curvature == 0.0,
                "piecewise-linear reflection sample stays the chord sample");

  const BoundaryGeometry flat =
      BoundaryGeometry::flat(100.0, BoundaryOrientation::Lower);
  context.check(
      flat.interpolationKind() == BoundaryInterpolationKind::PiecewiseLinear,
      "flat geometry defaults to the piecewise kind");
  const BoundaryGeometrySample flatReflection =
      flat.reflectionSampleAtSegment(Vec2{.range = 250.0, .depth = 101.0}, 0U);
  context.check(flatReflection.curvature == 0.0 &&
                    flatReflection.tangent == Vec2{.range = 1.0, .depth = 0.0},
                "flat reflection sample keeps zero curvature");

  context.expectThrows<rayreuse::ValidationError>(
      [&] {
        static_cast<void>(BoundaryGeometry::curvilinear(
            std::vector<Vec2>{Vec2{.range = 0.0, .depth = 1.0},
                              Vec2{.range = 0.0, .depth = 2.0}},
            10.0, BoundaryOrientation::Lower));
      },
      "curvilinear geometry still rejects non-increasing node ranges");
}

}  // namespace

int main() {
  Context context;
  testCurvilinearNodeFrameAndCurvature(context);
  testPiecewiseAndFlatReflectionFramesUnchanged(context);

  if (context.failureCount() != 0) {
    std::cerr << context.failureCount()
              << " boundary-geometry assertion(s) failed\n";
    return 1;
  }
  std::cout << "All Bellhop RayReuse boundary-geometry tests passed\n";
  return 0;
}
