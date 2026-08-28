#include "rayreuse/ray/geometry_tracer.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <utility>

#include "rayreuse/error.hpp"
#include "rayreuse/ray/flat_boundary_reflection.hpp"
#include "rayreuse/ray/ray_stepper.hpp"

namespace rayreuse {
namespace {

void requireFinite(double value, const std::string& name) {
  if (!std::isfinite(value)) {
    throw ValidationError(name + " must be finite");
  }
}

[[nodiscard]]
#if defined(__clang__) || defined(__GNUC__)
__attribute__((noinline))
#endif
double legacyCosine(double angle) {
  return std::cos(angle);
}

[[nodiscard]]
#if defined(__clang__) || defined(__GNUC__)
__attribute__((noinline))
#endif
double legacySine(double angle) {
  return std::sin(angle);
}

void validateIntegrator(const IntegratorSettings& integrator) {
  requireFinite(integrator.stepLength, "integrator.stepLength");
  requireFinite(integrator.rangeLimit, "integrator.rangeLimit");
  requireFinite(integrator.depthLimit, "integrator.depthLimit");
  if (integrator.stepLength <= 0.0) {
    throw ValidationError("integrator.stepLength must be positive");
  }
  if (integrator.rangeLimit <= 0.0) {
    throw ValidationError("integrator.rangeLimit must be positive");
  }
  if (integrator.depthLimit <= 0.0) {
    throw ValidationError("integrator.depthLimit must be positive");
  }
  if (integrator.maximumRayPoints < 2U) {
    throw ValidationError("integrator.maximumRayPoints must be at least two");
  }
}

[[nodiscard]] bool outsideSpatialBox(Vec2 position,
                                     const IntegratorSettings& integrator) {
  // TraceRay2D uses strict comparisons here. A point exactly on the box is
  // retained and the next ReduceStep2D call advances it by the minimum step.
  return std::abs(position.range) > integrator.rangeLimit ||
         std::abs(position.depth) > integrator.depthLimit;
}

[[nodiscard]] double crossingDistance(double initialCoordinate, double tangent,
                                      double boundaryCoordinate) {
  return (boundaryCoordinate - initialCoordinate) / tangent;
}

[[nodiscard]] double boundaryDistance(
    Vec2 position, ReflectionBoundary boundary,
    const BoundaryGeometry& seaSurfaceGeometry,
    const BoundaryGeometry& seabedGeometry, std::size_t boundarySegmentIndex) {
  switch (boundary) {
    case ReflectionBoundary::SeaSurface:
      return seaSurfaceGeometry.interiorSignedDistance(position,
                                                       boundarySegmentIndex);
    case ReflectionBoundary::Seabed:
      return seabedGeometry.interiorSignedDistance(position,
                                                   boundarySegmentIndex);
  }
  return std::numeric_limits<double>::quiet_NaN();
}

[[nodiscard]] std::optional<ReflectionBoundary> crossedBoundary(
    Vec2 initialPosition, Vec2 endPosition,
    const BoundaryGeometry& seaSurfaceGeometry,
    const BoundaryGeometry& seabedGeometry, std::size_t initialTopSegment,
    std::size_t endTopSegment, std::size_t initialBottomSegment,
    std::size_t endBottomSegment) {
  const double initialSurfaceDistance =
      boundaryDistance(initialPosition, ReflectionBoundary::SeaSurface,
                       seaSurfaceGeometry, seabedGeometry, initialTopSegment);
  const double endSurfaceDistance =
      boundaryDistance(endPosition, ReflectionBoundary::SeaSurface,
                       seaSurfaceGeometry, seabedGeometry, endTopSegment);
  if (initialSurfaceDistance > 0.0 && endSurfaceDistance <= 0.0) {
    return ReflectionBoundary::SeaSurface;
  }

  const double initialSeabedDistance = boundaryDistance(
      initialPosition, ReflectionBoundary::Seabed, seaSurfaceGeometry,
      seabedGeometry, initialBottomSegment);
  const double endSeabedDistance =
      boundaryDistance(endPosition, ReflectionBoundary::Seabed,
                       seaSurfaceGeometry, seabedGeometry, endBottomSegment);
  if (initialSeabedDistance > 0.0 && endSeabedDistance <= 0.0) {
    return ReflectionBoundary::Seabed;
  }
  return std::nullopt;
}

class GeometryStepLimiter {
 public:
  GeometryStepLimiter(const IntegratorSettings& integrator,
                      const std::vector<double>& profileDepths,
                      const GeometrySspEvaluator& soundSpeedProfile,
                      const BoundaryGeometry& seaSurfaceGeometry,
                      const BoundaryGeometry& seabedGeometry)
      : integrator_(integrator),
        profileDepths_(profileDepths),
        soundSpeedProfile_(soundSpeedProfile),
        seaSurfaceGeometry_(seaSurfaceGeometry),
        seabedGeometry_(seabedGeometry) {}

  [[nodiscard]] double operator()(const StepLimitRequest& request,
                                  std::size_t topSegmentIndex,
                                  std::size_t bottomSegmentIndex) const {
    double step = request.proposedStepLength;
    const Vec2 trial = request.initialPosition + step * request.unitTangent;

    reduceForSpatialCoordinate(request.initialPosition.range,
                               request.unitTangent.range, trial.range,
                               integrator_.rangeLimit, step);
    reduceForSpatialCoordinate(request.initialPosition.depth,
                               request.unitTangent.depth, trial.depth,
                               integrator_.depthLimit, step);

    reduceForDepthInterface(request, trial, step);
    reduceForBoundary(request, trial, seaSurfaceGeometry_, topSegmentIndex,
                      step);
    reduceForBoundary(request, trial, seabedGeometry_, bottomSegmentIndex,
                      step);
    reduceForBoundarySegment(request, trial, topSegmentIndex,
                             bottomSegmentIndex, step);

    // This is the final clause in ReduceStep2D. It intentionally may replace
    // an exact zero-distance event with a small forward step.
    const double minimumStep = 1.0e-3 * request.nominalStepLength;
    if (step < minimumStep) {
      step = minimumStep;
    }
    return step;
  }

 private:
  static void reduceForSpatialCoordinate(double initialCoordinate,
                                         double tangent, double trialCoordinate,
                                         double absoluteLimit, double& step) {
    if (std::abs(trialCoordinate) <= absoluteLimit) {
      return;
    }

    // ReduceStep2D uses the remaining absolute distance to the symmetric box,
    // even when a long trial crosses through the box and exits on the opposite
    // side.  Preserve that legacy subdivision rather than solving directly
    // for the signed exit plane.
    const double boxStep =
        (absoluteLimit - std::abs(initialCoordinate)) / std::abs(tangent);
    step = std::min(step, boxStep);
  }

  void reduceForDepthInterface(const StepLimitRequest& request, Vec2 trial,
                               double& step) const {
    if (request.initialSegmentIndex + 1U >= profileDepths_.size()) {
      return;
    }

    const double tangent = request.unitTangent.depth;
    if (std::abs(tangent) <= std::numeric_limits<double>::epsilon()) {
      return;
    }

    const double segmentTop = profileDepths_[request.initialSegmentIndex];
    const double segmentBottom =
        profileDepths_[request.initialSegmentIndex + 1U];

    if (trial.depth < segmentTop) {
      reduceAtDepth(request.initialPosition.depth, tangent, segmentTop, step);
    } else if (trial.depth > segmentBottom) {
      reduceAtDepth(request.initialPosition.depth, tangent, segmentBottom,
                    step);
    }
  }

  static void reduceForBoundary(const StepLimitRequest& request, Vec2 trial,
                                const BoundaryGeometry& geometry,
                                std::size_t segmentIndex, double& step) {
    const BoundaryGeometrySample sample =
        geometry.evaluateAtSegment(request.initialPosition.range, segmentIndex);
    if (geometry.isFlat()) {
      const bool crosses = geometry.orientation() == BoundaryOrientation::Upper
                               ? trial.depth < sample.point.depth
                               : trial.depth > sample.point.depth;
      if (crosses) {
        reduceAtDepth(request.initialPosition.depth, request.unitTangent.depth,
                      sample.point.depth, step);
      }
      return;
    }

    const double trialExteriorDistance =
        fortranDotProduct2D(sample.outwardNormal, trial - sample.point);
    if (trialExteriorDistance <= std::numeric_limits<double>::epsilon()) {
      return;
    }
    const double denominator =
        fortranDotProduct2D(request.unitTangent, sample.outwardNormal);
    if (std::abs(denominator) <= std::numeric_limits<double>::epsilon()) {
      return;
    }
    const double eventStep =
        -fortranDotProduct2D(request.initialPosition - sample.point,
                             sample.outwardNormal) /
        denominator;
    // ReduceStep2D includes a slightly negative boundary-plane distance in
    // the MIN reduction, then replaces it with the minimum forward step.
    // This is how a point rounded a few ulps outside the plane progresses to
    // the actual reflection test instead of skipping the boundary.
    if (eventStep <= step) {
      step = eventStep;
    }
  }

  void reduceForBoundarySegment(const StepLimitRequest& request, Vec2 trial,
                                std::size_t topSegmentIndex,
                                std::size_t bottomSegmentIndex,
                                double& step) const {
    const BoundaryGeometrySample top = seaSurfaceGeometry_.evaluateAtSegment(
        request.initialPosition.range, topSegmentIndex);
    const BoundaryGeometrySample bottom = seabedGeometry_.evaluateAtSegment(
        request.initialPosition.range, bottomSegmentIndex);
    const double minimumRange =
        std::max({top.minimumRange, bottom.minimumRange,
                  soundSpeedProfile_.minimumRangeForSegment(
                      request.initialRangeSegmentIndex)});
    const double maximumRange =
        std::min({top.maximumRange, bottom.maximumRange,
                  soundSpeedProfile_.maximumRangeForSegment(
                      request.initialRangeSegmentIndex)});
    if (trial.range < minimumRange) {
      reduceAtRange(request.initialPosition.range, request.unitTangent.range,
                    minimumRange, step);
    } else if (trial.range > maximumRange) {
      reduceAtRange(request.initialPosition.range, request.unitTangent.range,
                    maximumRange, step);
    }
  }

  static void reduceAtDepth(double initialDepth, double tangent,
                            double eventDepth, double& step) {
    const double eventStep =
        crossingDistance(initialDepth, tangent, eventDepth);
    if (eventStep >= 0.0 && eventStep <= step) {
      step = eventStep;
    }
  }

  static void reduceAtRange(double initialRange, double tangent,
                            double eventRange, double& step) {
    if (std::abs(tangent) <= std::numeric_limits<double>::epsilon()) {
      return;
    }
    const double eventStep =
        crossingDistance(initialRange, tangent, eventRange);
    if (eventStep >= 0.0 && eventStep <= step) {
      step = eventStep;
    }
  }

  const IntegratorSettings& integrator_;
  const std::vector<double>& profileDepths_;
  const GeometrySspEvaluator& soundSpeedProfile_;
  const BoundaryGeometry& seaSurfaceGeometry_;
  const BoundaryGeometry& seabedGeometry_;
};

}  // namespace

GeometryTracer::GeometryTracer(const Environment& environment,
                               IntegratorSettings integrator,
                               BoundaryCurvatureMode curvatureMode)
    : soundSpeedProfile_(environment.soundSpeedProfile()),
      integrator_(integrator),
      seaSurfaceBoundary_(environment.seaSurface()),
      seabedBoundary_(environment.seabed()),
      curvatureMode_(curvatureMode) {
  validateIntegrator(integrator_);
  switch (curvatureMode_) {
    case BoundaryCurvatureMode::Standard:
    case BoundaryCurvatureMode::Double:
    case BoundaryCurvatureMode::Zero:
      break;
    default:
      throw ValidationError("unknown boundary curvature mode");
  }
  const std::vector<SoundSpeedPoint>& points =
      environment.soundSpeedProfile().points();
  profileDepths_.reserve(points.size());
  for (const SoundSpeedPoint& point : points) {
    profileDepths_.push_back(point.depth);
  }
}

GeometryTracer::GeometryTracer(const SimulationCase& simulation)
    : GeometryTracer(simulation.environment(), simulation.integrator(),
                     simulation.curvatureMode()) {}

RayPath GeometryTracer::trace(const Source& source, double launchAngle) const {
  const BoundaryGeometry& seaSurfaceGeometry = seaSurfaceBoundary_.geometry();
  const BoundaryGeometry& seabedGeometry = seabedBoundary_.geometry();
  requireFinite(source.depth, "source.depth");
  requireFinite(source.amplitude, "source.amplitude");
  requireFinite(launchAngle, "launchAngle");
  const Vec2 sourcePosition{.range = 0.0, .depth = source.depth};
  std::size_t topSegmentIndex =
      seaSurfaceGeometry.locateSegment(sourcePosition.range, 0U);
  std::size_t bottomSegmentIndex =
      seabedGeometry.locateSegment(sourcePosition.range, 0U);
  const double sourceTopDistance = seaSurfaceGeometry.interiorSignedDistance(
      sourcePosition, topSegmentIndex);
  const double sourceBottomDistance =
      seabedGeometry.interiorSignedDistance(sourcePosition, bottomSegmentIndex);
  if (sourceTopDistance <= 0.0 || sourceBottomDistance <= 0.0) {
    throw ValidationError(
        "geometry-tracer source must be strictly inside the water");
  }
  if (source.amplitude < 0.0) {
    throw ValidationError("source.amplitude must be non-negative");
  }

  if (outsideSpatialBox(sourcePosition, integrator_)) {
    throw ValidationError(
        "geometry-tracer source must lie inside or on the spatial box");
  }

  const std::size_t initialSegment =
      soundSpeedProfile_.locateSegment(source.depth, 0U);
  const std::size_t initialRangeSegment =
      soundSpeedProfile_.locateRangeSegment(sourcePosition.range, 0U);
  const SoundSpeedSample sourceSample = soundSpeedProfile_.evaluateAtSegments(
      sourcePosition, initialSegment, initialRangeSegment);

  // Keep these as independent calls. Clang otherwise contracts adjacent
  // std::sin/std::cos calls to sincos, whose last bit can differ from the
  // separate libm calls made by the Fortran reference. Near a flat boundary,
  // that one bit can change whether a nominal step lands just inside or just
  // outside and therefore change the reflected point sequence.
  const double launchCosine = legacyCosine(launchAngle);
  const double launchSine = legacySine(launchAngle);

  RayPath path;
  path.launchAngle = launchAngle;
  const double horizontalAdvancePerStep =
      integrator_.stepLength * std::abs(launchCosine);
  const double nominalStepEstimate =
      horizontalAdvancePerStep > std::numeric_limits<double>::min()
          ? std::ceil(integrator_.rangeLimit / horizontalAdvancePerStep)
          : static_cast<double>(integrator_.maximumRayPoints);
  const double verticalTravelEstimate =
      nominalStepEstimate * integrator_.stepLength * std::abs(launchSine);
  const double firstBoundaryDistance =
      launchSine >= 0.0 ? sourceBottomDistance : sourceTopDistance;
  const double waterDepth =
      seabedGeometry.referenceDepth() - seaSurfaceGeometry.referenceDepth();
  const double reflectionEstimate =
      verticalTravelEstimate < firstBoundaryDistance
          ? 0.0
          : 1.0 + std::floor((verticalTravelEstimate - firstBoundaryDistance) /
                             waterDepth);
  // Each reflection adds a derived point and usually induces one minimum
  // continuation step. Unlike the former fixed 2*range estimate, this leaves
  // direct narrow-angle fans compact and reserves event storage only for
  // reflections that can actually occur.
  const double pointEstimate =
      std::min(static_cast<double>(integrator_.maximumRayPoints),
               nominalStepEstimate + 2.0 * reflectionEstimate + 8.0);
  const std::size_t initialPointCapacity = std::min(
      integrator_.maximumRayPoints,
      std::max(std::size_t{64U}, static_cast<std::size_t>(pointEstimate)));
  const std::size_t initialEventCapacity =
      std::min(initialPointCapacity / 2U,
               static_cast<std::size_t>(std::min(
                   reflectionEstimate + 2.0,
                   static_cast<double>(integrator_.maximumRayPoints))));
  path.points.reserve(initialPointCapacity);
  path.steps.reserve(initialPointCapacity - 1U);
  path.events.reserve(initialEventCapacity);
  path.points.push_back(
      RayState{.position = sourcePosition,
               .slowness = Vec2{.range = launchCosine / sourceSample.soundSpeed,
                                .depth = launchSine / sourceSample.soundSpeed},
               .dynamicP = {1.0, 0.0},
               .dynamicQ = {0.0, 1.0},
               .soundSpeed = sourceSample.soundSpeed,
               .realTravelTime = 0.0});

  const GeometryStepLimiter geometryLimiter(
      integrator_, profileDepths_, soundSpeedProfile_, seaSurfaceGeometry,
      seabedGeometry);
  const StepLimiter stepLimiter =
      [&geometryLimiter, &topSegmentIndex,
       &bottomSegmentIndex](const StepLimitRequest& request) {
        return geometryLimiter(request, topSegmentIndex, bottomSegmentIndex);
      };
  std::size_t segmentIndex = initialSegment;
  std::size_t rangeSegmentIndex = initialRangeSegment;
  while (true) {
    if (path.points.size() >= integrator_.maximumRayPoints) {
      path.terminationReason = RayTerminationReason::PointLimit;
      return path;
    }

    RayStepResult result;
    try {
      result = stepRay(soundSpeedProfile_, path.points.back(), segmentIndex,
                       rangeSegmentIndex, integrator_.stepLength, stepLimiter);
    } catch (const ValidationError&) {
      path.terminationReason = RayTerminationReason::NumericalFailure;
      return path;
    }

    const std::size_t endTopSegment = seaSurfaceGeometry.locateSegment(
        result.endState.position.range, topSegmentIndex);
    const std::size_t endBottomSegment = seabedGeometry.locateSegment(
        result.endState.position.range, bottomSegmentIndex);
    const double beginTopDistance = boundaryDistance(
        path.points.back().position, ReflectionBoundary::SeaSurface,
        seaSurfaceGeometry, seabedGeometry, topSegmentIndex);
    const double endTopDistance = boundaryDistance(
        result.endState.position, ReflectionBoundary::SeaSurface,
        seaSurfaceGeometry, seabedGeometry, endTopSegment);
    const double beginBottomDistance = boundaryDistance(
        path.points.back().position, ReflectionBoundary::Seabed,
        seaSurfaceGeometry, seabedGeometry, bottomSegmentIndex);
    const double endBottomDistance =
        boundaryDistance(result.endState.position, ReflectionBoundary::Seabed,
                         seaSurfaceGeometry, seabedGeometry, endBottomSegment);
    const std::optional<ReflectionBoundary> boundary =
        crossedBoundary(path.points.back().position, result.endState.position,
                        seaSurfaceGeometry, seabedGeometry, topSegmentIndex,
                        endTopSegment, bottomSegmentIndex, endBottomSegment);

    std::optional<FlatBoundaryReflection> reflection;
    if (boundary.has_value()) {
      // The integrated incident point and its same-position reflected point
      // form one indivisible D-10 event. Do not leave a half-reflection when
      // the caller's point budget has room for only one of them.
      if (integrator_.maximumRayPoints - path.points.size() < 2U) {
        path.terminationReason = RayTerminationReason::PointLimit;
        return path;
      }

      try {
        const SoundSpeedSample arrivalSample = soundSpeedProfile_.evaluate(
            result.endState.position, result.segmentIndex,
            result.rangeSegmentIndex);
        const bool isSurface = *boundary == ReflectionBoundary::SeaSurface;
        const std::size_t boundarySegmentIndex =
            isSurface ? endTopSegment : endBottomSegment;
        const BoundaryGeometry& boundaryGeometry =
            isSurface ? seaSurfaceGeometry : seabedGeometry;
        // ReduceStep2D and Distances2D always use the piecewise-linear chord,
        // including for a curvilinear boundary.  Only Reflect2D substitutes
        // the interpolated legacy node frame and its segment curvature.
        const BoundaryGeometrySample collisionSample =
            boundaryGeometry.evaluateAtSegment(result.endState.position.range,
                                               boundarySegmentIndex);
        const BoundaryGeometrySample reflectionSample =
            boundaryGeometry.reflectionSampleAtSegment(result.endState.position,
                                                       boundarySegmentIndex);
        reflection = reflectAtBoundary(
            result.endState, *boundary,
            BoundaryReflectionGeometry{
                .collisionPlanePoint = collisionSample.point,
                .collisionPlaneOutwardNormal = collisionSample.outwardNormal,
                .reflectionTangent = reflectionSample.tangent,
                .reflectionOutwardNormal = reflectionSample.outwardNormal,
                .soundSpeedGradient = arrivalSample.soundSpeedGradient,
                .segmentIndex = reflectionSample.segmentIndex,
                .curvature = reflectionSample.curvature,
                .maximumIncidentPlaneDistance =
                    1.0e-3 * integrator_.stepLength},
            path.points.size(), curvatureMode_);
        const BoundaryModel& boundaryModel =
            isSurface ? seaSurfaceBoundary_ : seabedBoundary_;
        if (boundaryModel.kind() == BoundaryKind::AcousticHalfSpace &&
            boundaryModel.hasRangeDependentMaterials()) {
          reflection->event.longMaterialOverride = FrozenBoundaryMaterial{
              .material = boundaryModel.materialAtSegment(
                  reflectionSample.segmentIndex),
              .attenuationEvaluationDepth =
                  boundaryModel.materialAttenuationDepthAtSegment(
                      reflectionSample.segmentIndex)};
        }
      } catch (const ValidationError&) {
        path.terminationReason = RayTerminationReason::NumericalFailure;
        return path;
      }
    }

    path.steps.push_back(result.quadrature);
    path.points.push_back(std::move(result.endState));
    segmentIndex = result.segmentIndex;
    rangeSegmentIndex = result.rangeSegmentIndex;
    topSegmentIndex = endTopSegment;
    bottomSegmentIndex = endBottomSegment;

    if (reflection.has_value()) {
      path.events.push_back(std::move(reflection->event));
      path.points.push_back(std::move(reflection->reflectedState));
    }

    if (outsideSpatialBox(path.points.back().position, integrator_)) {
      path.terminationReason = RayTerminationReason::ExitedDomain;
      return path;
    }
    // A curvilinear legacy frame is not unit-normalized, so its reflected
    // point can remain outside the collision chord for another step. The
    // original 2-D tracer stops after two consecutive outside points.
    if (beginTopDistance < 0.0 && endTopDistance < 0.0) {
      path.terminationReason = RayTerminationReason::ExitedDomain;
      return path;
    }
    if (beginBottomDistance < 0.0 && endBottomDistance < 0.0) {
      path.terminationReason = RayTerminationReason::ExitedDomain;
      return path;
    }
    if (std::abs(path.points.back().dynamicQ[0]) > 1.0e100 ||
        std::abs(path.points.back().dynamicQ[1]) > 1.0e100) {
      path.terminationReason = RayTerminationReason::NumericalFailure;
      return path;
    }
  }
}

}  // namespace rayreuse
