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

[[nodiscard]] double boundaryDistance(Vec2 position,
                                      ReflectionBoundary boundary,
                                      double seaSurfaceDepth,
                                      double seabedDepth) noexcept {
  switch (boundary) {
    case ReflectionBoundary::SeaSurface:
      return position.depth - seaSurfaceDepth;
    case ReflectionBoundary::Seabed:
      return seabedDepth - position.depth;
  }
  return std::numeric_limits<double>::quiet_NaN();
}

[[nodiscard]] std::optional<ReflectionBoundary> crossedBoundary(
    Vec2 initialPosition, Vec2 endPosition, double seaSurfaceDepth,
    double seabedDepth) {
  const double initialSurfaceDistance =
      boundaryDistance(initialPosition, ReflectionBoundary::SeaSurface,
                       seaSurfaceDepth, seabedDepth);
  const double endSurfaceDistance =
      boundaryDistance(endPosition, ReflectionBoundary::SeaSurface,
                       seaSurfaceDepth, seabedDepth);
  // TraceRay2D reflects only on an inside-to-outside crossing.  Position
  // integration uses fused multiply-add in the stepper so the sign of a
  // near-zero boundary residual matches the optimized Fortran oracle instead
  // of being rounded to an artificial exact zero.
  if (initialSurfaceDistance > 0.0 && endSurfaceDistance <= 0.0) {
    return ReflectionBoundary::SeaSurface;
  }

  const double initialSeabedDistance =
      boundaryDistance(initialPosition, ReflectionBoundary::Seabed,
                       seaSurfaceDepth, seabedDepth);
  const double endSeabedDistance = boundaryDistance(
      endPosition, ReflectionBoundary::Seabed, seaSurfaceDepth, seabedDepth);
  if (initialSeabedDistance > 0.0 && endSeabedDistance <= 0.0) {
    return ReflectionBoundary::Seabed;
  }
  return std::nullopt;
}

class GeometryStepLimiter {
 public:
  GeometryStepLimiter(const IntegratorSettings& integrator,
                      const std::vector<double>& profileDepths,
                      double seaSurfaceDepth, double seabedDepth)
      : integrator_(integrator),
        profileDepths_(profileDepths),
        seaSurfaceDepth_(seaSurfaceDepth),
        seabedDepth_(seabedDepth) {}

  [[nodiscard]] double operator()(const StepLimitRequest& request) const {
    double step = request.proposedStepLength;
    const Vec2 trial = request.initialPosition + step * request.unitTangent;

    reduceForSpatialCoordinate(request.initialPosition.range,
                               request.unitTangent.range, trial.range,
                               integrator_.rangeLimit, step);
    reduceForSpatialCoordinate(request.initialPosition.depth,
                               request.unitTangent.depth, trial.depth,
                               integrator_.depthLimit, step);

    reduceForDepthEvents(request, trial.depth, step);

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

    if (std::abs(tangent) <= std::numeric_limits<double>::epsilon()) {
      return;
    }
    const double crossedBoundary =
        std::copysign(absoluteLimit, trialCoordinate);
    const double boxStep = (crossedBoundary - initialCoordinate) / tangent;
    if (boxStep >= 0.0) {
      step = std::min(step, boxStep);
    }
  }

  void reduceForDepthEvents(const StepLimitRequest& request, double trialDepth,
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

    if (trialDepth < segmentTop) {
      reduceAtDepth(request.initialPosition.depth, tangent, segmentTop, step);
    } else if (trialDepth > segmentBottom) {
      reduceAtDepth(request.initialPosition.depth, tangent, segmentBottom,
                    step);
    }

    if (trialDepth < seaSurfaceDepth_) {
      reduceAtDepth(request.initialPosition.depth, tangent, seaSurfaceDepth_,
                    step);
    } else if (trialDepth > seabedDepth_) {
      reduceAtDepth(request.initialPosition.depth, tangent, seabedDepth_, step);
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

  const IntegratorSettings& integrator_;
  const std::vector<double>& profileDepths_;
  double seaSurfaceDepth_;
  double seabedDepth_;
};

}  // namespace

GeometryTracer::GeometryTracer(const Environment& environment,
                               IntegratorSettings integrator)
    : soundSpeedProfile_(environment.soundSpeedProfile()),
      integrator_(integrator),
      seaSurfaceDepth_(environment.seaSurface().depth()),
      seabedDepth_(environment.seabed().depth()) {
  validateIntegrator(integrator_);
  const std::vector<SoundSpeedPoint>& points =
      environment.soundSpeedProfile().points();
  profileDepths_.reserve(points.size());
  for (const SoundSpeedPoint& point : points) {
    profileDepths_.push_back(point.depth);
  }
}

GeometryTracer::GeometryTracer(const SimulationCase& simulation)
    : GeometryTracer(simulation.environment(), simulation.integrator()) {}

RayPath GeometryTracer::trace(const Source& source, double launchAngle) const {
  requireFinite(source.depth, "source.depth");
  requireFinite(source.amplitude, "source.amplitude");
  requireFinite(launchAngle, "launchAngle");
  if (source.depth <= seaSurfaceDepth_ || source.depth >= seabedDepth_) {
    throw ValidationError(
        "geometry-tracer source must be strictly inside the water");
  }
  if (source.amplitude < 0.0) {
    throw ValidationError("source.amplitude must be non-negative");
  }

  const Vec2 sourcePosition{.range = 0.0, .depth = source.depth};
  if (outsideSpatialBox(sourcePosition, integrator_)) {
    throw ValidationError(
        "geometry-tracer source must lie inside or on the spatial box");
  }

  const std::size_t initialSegment =
      soundSpeedProfile_.locateSegment(source.depth, 0U);
  const SoundSpeedSample sourceSample =
      soundSpeedProfile_.evaluateAtSegment(sourcePosition, initialSegment);

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
  const double firstBoundaryDistance = launchSine >= 0.0
                                           ? seabedDepth_ - source.depth
                                           : source.depth - seaSurfaceDepth_;
  const double waterDepth = seabedDepth_ - seaSurfaceDepth_;
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

  const GeometryStepLimiter geometryLimiter(integrator_, profileDepths_,
                                            seaSurfaceDepth_, seabedDepth_);
  const StepLimiter stepLimiter =
      [&geometryLimiter](const StepLimitRequest& request) {
        return geometryLimiter(request);
      };
  std::size_t segmentIndex = initialSegment;
  while (true) {
    if (path.points.size() >= integrator_.maximumRayPoints) {
      path.terminationReason = RayTerminationReason::PointLimit;
      return path;
    }

    RayStepResult result;
    try {
      result = stepRay(soundSpeedProfile_, path.points.back(), segmentIndex,
                       integrator_.stepLength, stepLimiter);
    } catch (const ValidationError&) {
      path.terminationReason = RayTerminationReason::NumericalFailure;
      return path;
    }

    const std::optional<ReflectionBoundary> boundary =
        crossedBoundary(path.points.back().position, result.endState.position,
                        seaSurfaceDepth_, seabedDepth_);

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
            result.endState.position, result.segmentIndex);
        const bool isSurface = *boundary == ReflectionBoundary::SeaSurface;
        reflection = reflectAtFlatBoundary(
            result.endState, *boundary,
            FlatBoundaryGeometry{
                .point =
                    Vec2{.range = 0.0,
                         .depth = isSurface ? seaSurfaceDepth_ : seabedDepth_},
                .tangent = Vec2{.range = 1.0, .depth = 0.0},
                .outwardNormal =
                    Vec2{.range = 0.0, .depth = isSurface ? -1.0 : 1.0},
                .soundSpeedGradient = arrivalSample.soundSpeedGradient,
                .segmentIndex = 0U,
                .maximumIncidentPlaneDistance =
                    1.0e-3 * integrator_.stepLength},
            path.points.size(), BoundaryCurvatureMode::Standard);
      } catch (const ValidationError&) {
        path.terminationReason = RayTerminationReason::NumericalFailure;
        return path;
      }
    }

    path.steps.push_back(result.quadrature);
    path.points.push_back(std::move(result.endState));
    segmentIndex = result.segmentIndex;

    if (reflection.has_value()) {
      path.events.push_back(std::move(reflection->event));
      path.points.push_back(std::move(reflection->reflectedState));
    }

    if (outsideSpatialBox(path.points.back().position, integrator_)) {
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
