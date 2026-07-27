#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <utility>
#include <vector>

#include "bellhop/cache/ray_path_cache.hpp"
#include "bellhop/error.hpp"
#include "bellhop/model/environment.hpp"
#include "bellhop/model/simulation_case.hpp"
#include "bellhop/ray/geometry_tracer.hpp"
#include "support/test_harness.hpp"

namespace {

using bellhop::BoundaryModel;
using bellhop::Environment;
using bellhop::GeometryTracer;
using bellhop::IntegratorSettings;
using bellhop::RayPath;
using bellhop::RayPathCache;
using bellhop::RayTerminationReason;
using bellhop::SoundSpeedPoint;
using bellhop::SoundSpeedProfile;
using bellhop::Source;
using bellhop::ValidationError;
using bellhop::Vec2;
using bellhop::test::Context;

constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr double kSoundSpeed = 1500.0;

Environment makeConstantEnvironment(double surfaceDepth = 0.0,
                                    double bottomDepth = 1000.0) {
  return Environment(
      SoundSpeedProfile(
          {{.depth = surfaceDepth,
            .soundSpeed = kSoundSpeed,
            .density = 1000.0},
           {.depth = bottomDepth,
            .soundSpeed = kSoundSpeed,
            .density = 1000.0}}),
      BoundaryModel::vacuum(surfaceDepth),
      BoundaryModel::rigid(bottomDepth));
}

Environment makeLinearGradientEnvironment() {
  return Environment(
      SoundSpeedProfile(
          {{.depth = -5000.0,
            .soundSpeed = 1400.0,
            .density = 1000.0},
           {.depth = 5000.0,
            .soundSpeed = 1600.0,
            .density = 1000.0}}),
      BoundaryModel::vacuum(-5000.0),
      BoundaryModel::rigid(5000.0));
}

IntegratorSettings makeSettings(double rangeLimit = 5100.0,
                                double depthLimit = 1100.0,
                                std::size_t maximumRayPoints = 10000U) {
  return IntegratorSettings{.stepLength = 10.0,
                            .rangeLimit = rangeLimit,
                            .depthLimit = depthLimit,
                            .maximumRayPoints = maximumRayPoints};
}

void checkPathIndexInvariant(Context& context, const RayPath& path,
                             const char* message) {
  context.check(path.points.size() == path.steps.size() + 1U, message);
}

void testHorizontalRayAndMinimumBoxStep(Context& context) {
  const GeometryTracer tracer(makeConstantEnvironment(),
                              makeSettings(25.0, 1100.0));
  const RayPath path = tracer.trace(Source{.depth = 500.0}, 0.0);

  context.check(path.terminationReason == RayTerminationReason::ExitedDomain,
                "horizontal ray exits the strict spatial box");
  context.check(path.points.size() == 5U,
                "box-aligned ray retains boundary and overshoot points");
  context.check(path.steps.size() == 4U,
                "box-aligned ray stores every integrated step");
  checkPathIndexInvariant(
      context, path,
      "horizontal trace has exactly one more point than quadrature record");
  context.checkNear(path.points[3U].position.range, 25.0, 0.0,
                    "reduced step lands exactly on the range box");
  context.checkNear(path.steps[3U].stepLength, 0.01, 1.0e-15,
                    "point on box advances by ReduceStep2D minimum step");
  context.check(path.points.back().position.range > 25.0,
                "TraceRay2D strict comparison terminates after overshoot");
}

void testBackwardRayUsesSignedBoxIntersection(Context& context) {
  const GeometryTracer tracer(makeConstantEnvironment(),
                              makeSettings(25.0, 1100.0));
  const RayPath path =
      tracer.trace(Source{.depth = 500.0}, kPi);

  context.check(path.terminationReason == RayTerminationReason::ExitedDomain,
                "backward horizontal ray exits the strict spatial box");
  context.checkNear(path.points[path.points.size() - 2U].position.range,
                    -25.0, 1.0e-14,
                    "backward ray lands on the signed negative range box");
  context.check(path.points.back().position.range < -25.0,
                "backward ray terminates after a negative-range overshoot");
}

void testSmallAngleAnalyticState(Context& context) {
  constexpr double angle = 0.5 * kPi / 180.0;
  const GeometryTracer tracer(makeConstantEnvironment(),
                              makeSettings(45.0, 1100.0));
  const RayPath path = tracer.trace(Source{.depth = 500.0}, angle);
  const Vec2 direction{.range = std::cos(angle),
                       .depth = std::sin(angle)};

  double arcLength = 0.0;
  for (std::size_t index = 0; index < path.points.size(); ++index) {
    if (index > 0U) {
      arcLength += path.steps[index - 1U].stepLength;
    }
    const auto& point = path.points[index];
    context.checkNear(point.position.range, arcLength * direction.range,
                      2.0e-13,
                      "constant-speed small-angle range is a straight line");
    context.checkNear(point.position.depth,
                      500.0 + arcLength * direction.depth, 2.0e-13,
                      "constant-speed small-angle depth is a straight line");
    context.checkNear(point.slowness.range,
                      direction.range / kSoundSpeed, 0.0,
                      "constant-speed range slowness is invariant");
    context.checkNear(point.slowness.depth,
                      direction.depth / kSoundSpeed, 0.0,
                      "constant-speed depth slowness is invariant");
    context.checkNear(point.dynamicP[0], 1.0, 0.0,
                      "first dynamic p is constant");
    context.checkNear(point.dynamicP[1], 0.0, 0.0,
                      "second dynamic p is constant");
    context.checkNear(point.dynamicQ[0], arcLength * kSoundSpeed, 1.0e-10,
                      "first dynamic q follows its analytic solution");
    context.checkNear(point.dynamicQ[1], 1.0, 0.0,
                      "second dynamic q follows its analytic solution");
    context.checkNear(point.realTravelTime, arcLength / kSoundSpeed, 1.0e-15,
                      "real travel time accumulates actual step lengths");
  }
  checkPathIndexInvariant(
      context, path,
      "small-angle trace has exactly one more point than quadrature record");
}

void testSnellInvariantInLinearGradient(Context& context) {
  const GeometryTracer tracer(
      makeLinearGradientEnvironment(),
      IntegratorSettings{.stepLength = 10.0,
                         .rangeLimit = 10000.0,
                         .depthLimit = 6000.0,
                         .maximumRayPoints = 100U});
  const RayPath path =
      tracer.trace(Source{.depth = 0.0}, 0.2);
  const double initialHorizontalSlowness =
      path.points.front().slowness.range;

  context.check(path.terminationReason == RayTerminationReason::PointLimit,
                "linear-gradient Snell trace reaches its point limit");
  for (const auto& point : path.points) {
    context.checkNear(
        point.slowness.range, initialHorizontalSlowness, 1.0e-15,
        "range-independent C-linear SSP preserves horizontal slowness");
  }
}

void testDynamicQMatchesLaunchAngleFiniteDifference(Context& context) {
  constexpr double baseAngle = 0.1;
  constexpr double angleIncrement = 1.0e-6;
  const IntegratorSettings settings{
      .stepLength = 10.0,
      .rangeLimit = 10000.0,
      .depthLimit = 1100.0,
      .maximumRayPoints = 52U};
  const GeometryTracer tracer(makeConstantEnvironment(), settings);
  const RayPath base =
      tracer.trace(Source{.depth = 500.0}, baseAngle);
  const RayPath minus =
      tracer.trace(Source{.depth = 500.0}, baseAngle - angleIncrement);
  const RayPath plus =
      tracer.trace(Source{.depth = 500.0}, baseAngle + angleIncrement);
  const Vec2 positionDerivative =
      (plus.points.back().position - minus.points.back().position) /
      (2.0 * angleIncrement);
  const Vec2 rayNormal{.range = -std::sin(baseAngle),
                       .depth = std::cos(baseAngle)};
  const double finiteDifferenceNormal =
      bellhop::dot(positionDerivative, rayNormal);
  const double dynamicNormal =
      base.points.back().dynamicQ[0] / kSoundSpeed;

  context.checkNear(
      dynamicNormal, finiteDifferenceNormal, 1.0e-6,
      "dynamic q agrees with a neighboring-launch-angle finite difference");
}

RayPath traceLinearGradient(double stepLength) {
  constexpr double totalArcLength = 1000.0;
  const std::size_t stepCount =
      static_cast<std::size_t>(totalArcLength / stepLength);
  const GeometryTracer tracer(
      makeLinearGradientEnvironment(),
      IntegratorSettings{.stepLength = stepLength,
                         .rangeLimit = 10000.0,
                         .depthLimit = 6000.0,
                         .maximumRayPoints = stepCount + 1U});
  return tracer.trace(Source{.depth = 0.0}, 0.3);
}

void testSecondOrderStepConvergence(Context& context) {
  const RayPath coarse = traceLinearGradient(20.0);
  const RayPath fine = traceLinearGradient(10.0);
  const RayPath reference = traceLinearGradient(0.3125);
  const double coarseError =
      bellhop::norm(coarse.points.back().position -
                    reference.points.back().position);
  const double fineError =
      bellhop::norm(fine.points.back().position -
                    reference.points.back().position);

  context.check(coarse.terminationReason == RayTerminationReason::PointLimit &&
                    fine.terminationReason == RayTerminationReason::PointLimit &&
                    reference.terminationReason ==
                        RayTerminationReason::PointLimit,
                "convergence traces cover the same arc length");
  context.check(fineError > 0.0 && coarseError / fineError > 3.8,
                "modified Heun geometry converges at second order");
}

void testDepthBoxUsesMinimumStep(Context& context) {
  const GeometryTracer tracer(
      makeConstantEnvironment(-1000.0, 2000.0),
      makeSettings(1000.0, 505.0));
  const RayPath path =
      tracer.trace(Source{.depth = 500.0}, 0.5 * kPi);

  context.check(path.terminationReason == RayTerminationReason::ExitedDomain,
                "vertical ray exits the strict depth box");
  context.checkNear(path.points[path.points.size() - 2U].position.depth,
                    505.0, 0.0,
                    "reduced step lands exactly on the depth box");
  context.checkNear(path.steps.back().stepLength, 0.01, 1.0e-15,
                    "depth box also uses the minimum forward step");
  context.check(path.points.back().position.depth > 505.0,
                "depth box terminates only after a strict overshoot");
}

void testPointLimit(Context& context) {
  const GeometryTracer tracer(makeConstantEnvironment(),
                              makeSettings(1000.0, 1100.0, 4U));
  const RayPath path = tracer.trace(Source{.depth = 500.0}, 0.0);

  context.check(path.terminationReason == RayTerminationReason::PointLimit,
                "maximumRayPoints produces an explicit PointLimit");
  context.check(path.points.size() == 4U,
                "PointLimit retains exactly the configured number of points");
  context.check(path.steps.size() == 3U,
                "PointLimit retains matching quadrature records");
  checkPathIndexInvariant(
      context, path,
      "PointLimit trace has exactly one more point than quadrature record");
}

void testSspCrossingContinuesToBoundary(Context& context) {
  const Environment environment(
      SoundSpeedProfile(
          {{.depth = 0.0, .soundSpeed = 1500.0, .density = 1000.0},
           {.depth = 100.0, .soundSpeed = 1500.0, .density = 1000.0},
           {.depth = 1000.0, .soundSpeed = 1500.0, .density = 1000.0}}),
      BoundaryModel::vacuum(0.0), BoundaryModel::rigid(1000.0));
  GeometryTracer tracer(
      environment,
      IntegratorSettings{.stepLength = 60.0,
                         .rangeLimit = 1000.0,
                         .depthLimit = 1100.0,
                         .maximumRayPoints = 6U});
  const RayPath path =
      tracer.trace(Source{.depth = 50.0}, 0.5 * kPi);

  context.check(
      path.terminationReason == RayTerminationReason::PointLimit,
      "continued SSP trace reaches its configured point limit");
  std::size_t interfaceCount = 0U;
  std::size_t interfaceIndex = path.points.size();
  for (std::size_t index = 0U; index < path.points.size(); ++index) {
    if (std::abs(path.points[index].position.depth - 100.0) <= 1.0e-14) {
      interfaceIndex = index;
      ++interfaceCount;
    }
  }
  context.check(interfaceCount == 1U,
                "SSP interface is aligned and stored exactly once");
  context.check(interfaceIndex + 1U < path.points.size() &&
                    path.points[interfaceIndex + 1U].position.depth > 100.0,
                "trace continues beyond the internal SSP interface");
  context.check(path.events.empty(),
                "short SSP-interface trace does not reach a sea boundary");
  checkPathIndexInvariant(
      context, path,
      "SSP-interface trace retains the point/step index invariant");
}

void testStandardDirectOracleShape(Context& context) {
  constexpr std::size_t oneBasedAlphaIndex = 150U;
  constexpr std::size_t angleCount = 300U;
  constexpr double minimumDegrees = -5.0;
  constexpr double maximumDegrees = 5.0;
  const double degrees =
      minimumDegrees +
      static_cast<double>(oneBasedAlphaIndex - 1U) *
          (maximumDegrees - minimumDegrees) /
          static_cast<double>(angleCount - 1U);
  const double angle = degrees * kPi / 180.0;

  const GeometryTracer tracer(makeConstantEnvironment(), makeSettings());
  const RayPath path = tracer.trace(Source{.depth = 500.0}, angle);

  context.check(path.points.size() == 512U,
                "standard direct alpha 150 has 512 oracle-shaped points");
  context.check(path.steps.size() == 511U,
                "standard direct alpha 150 has 511 integrated steps");
  context.check(path.terminationReason == RayTerminationReason::ExitedDomain,
                "standard direct alpha 150 exits through the range box");
  context.check(path.points.back().position.range > 5100.0,
                "standard direct endpoint strictly exceeds RBOX");
}

void testPathCanFreezeInCache(Context& context) {
  RayPathCache cache;
  {
    const GeometryTracer tracer(makeConstantEnvironment(),
                                makeSettings(25.0, 1100.0));
    cache.append(tracer.trace(Source{.depth = 500.0}, 0.0));
  }
  cache.freeze();

  context.check(cache.frozen(),
                "GeometryTracer output freezes in RayPathCache");
  context.check(cache.at(0U).points.size() == 5U,
                "frozen cache owns the complete traced path");
  context.check(cache.at(0U).events.empty(),
                "unreflected GeometryTracer emits no reflection events");
}

void testInputValidation(Context& context) {
  const GeometryTracer tracer(makeConstantEnvironment(), makeSettings());
  context.expectThrows<ValidationError>(
      [&tracer] {
        static_cast<void>(tracer.trace(
            Source{.depth = 500.0},
            std::numeric_limits<double>::quiet_NaN()));
      },
      "non-finite launch angle is rejected");
  context.expectThrows<ValidationError>(
      [&tracer] {
        static_cast<void>(tracer.trace(Source{.depth = 0.0}, 0.0));
      },
      "source on the sea surface is rejected");
}

}  // namespace

int main() {
  Context context;
  testHorizontalRayAndMinimumBoxStep(context);
  testBackwardRayUsesSignedBoxIntersection(context);
  testSmallAngleAnalyticState(context);
  testSnellInvariantInLinearGradient(context);
  testDynamicQMatchesLaunchAngleFiniteDifference(context);
  testSecondOrderStepConvergence(context);
  testDepthBoxUsesMinimumStep(context);
  testPointLimit(context);
  testSspCrossingContinuesToBoundary(context);
  testStandardDirectOracleShape(context);
  testPathCanFreezeInCache(context);
  testInputValidation(context);

  if (context.failureCount() != 0) {
    std::cerr << context.failureCount()
              << " geometry-tracer test assertion(s) failed\n";
    return 1;
  }

  std::cout << "All Bellhop F2CPP geometry-tracer tests passed\n";
  return 0;
}
