#include <algorithm>
#include <array>
#include <cstddef>
#include <utility>

#include "bellhop/cache/ray_path_cache.hpp"
#include "bellhop/ray/geometry_tracer.hpp"
#include "support/munk_case_fixture.hpp"
#include "support/test_harness.hpp"

namespace {

using bellhop::GeometryTracer;
using bellhop::RayPathCache;
using bellhop::RayTerminationReason;
using bellhop::ReflectionBoundary;
using bellhop::Source;
using bellhop::test::Context;

void testMunkOracleRegression(Context& context) {
  const GeometryTracer tracer(
      bellhop::test::makeMunkEnvironment(),
      bellhop::test::makeMunkIntegratorSettings());
  const auto path =
      tracer.trace(Source{.depth = 1000.0},
                   bellhop::test::kMunkOracleLaunchAngle);

  context.check(path.terminationReason == RayTerminationReason::ExitedDomain,
                "Munk ray exits through the strict range box");
  context.check(path.points.size() == 299U,
                "Munk oracle regression retains all 299 points");
  context.check(path.steps.size() == 298U,
                "Munk oracle regression retains all 298 steps");
  context.check(path.events.empty(),
                "selected refracted Munk ray has no boundary reflection");
  context.check(path.points.size() == path.steps.size() + 1U,
                "unreflected Munk path satisfies P = 1 + S");
  if (path.points.size() != 299U || path.steps.size() != 298U) {
    return;
  }

  const auto [minimumDepth, maximumDepth] = std::minmax_element(
      path.points.begin(), path.points.end(),
      [](const auto& left, const auto& right) {
        return left.position.depth < right.position.depth;
      });
  context.checkNear(minimumDepth->position.depth, 999.9935394381022,
                    2.0e-7,
                    "Munk ray minimum depth matches the Fortran oracle");
  context.checkNear(maximumDepth->position.depth, 1646.3104064731708,
                    2.0e-7,
                    "Munk ray crosses multiple C-linear SSP layers");

  const std::size_t reducedStepCount = static_cast<std::size_t>(
      std::count_if(path.steps.begin(), path.steps.end(),
                    [](const auto& step) {
                      return step.stepLength < 500.0;
                    }));
  const std::size_t subMeterStepCount = static_cast<std::size_t>(
      std::count_if(path.steps.begin(), path.steps.end(),
                    [](const auto& step) {
                      return step.stepLength < 1.0;
                    }));
  context.check(reducedStepCount == 108U,
                "Munk path records every SSP and range reduced step");
  context.check(subMeterStepCount == 59U,
                "source-on-node reversals retain Fortran minimum steps");

  const auto& final = path.points.back();
  context.checkNear(final.position.range, 101000.49999960621, 1.1e-5,
                    "Munk final range matches the oracle");
  context.checkNear(final.position.depth, 1000.1751801959769, 2.0e-7,
                    "Munk final depth matches the oracle");
  context.checkNear(final.dynamicP[0], -1.9953737039592374, 2.1e-9,
                    "Munk final dynamic p matches the oracle");
  context.checkNear(final.dynamicQ[0], 986208.1028032822, 1.0e-3,
                    "Munk final dynamic q matches the oracle");
  context.checkNear(final.realTravelTime, 67.32698260883494, 7.0e-9,
                    "Munk final travel time matches the oracle");
}

void testMunkBoundaryReflectionRegression(Context& context) {
  struct ExpectedTrace {
    double launchAngle{};
    std::size_t pointCount{};
    std::size_t stepCount{};
    std::size_t topBounceCount{};
    std::size_t bottomBounceCount{};
  };
  constexpr std::array expectedTraces{
      ExpectedTrace{
          .launchAngle = -bellhop::test::kMunkExtremeLaunchAngle,
          .pointCount = 547U,
          .stepCount = 539U,
          .topBounceCount = 4U,
          .bottomBounceCount = 3U},
      ExpectedTrace{
          .launchAngle = bellhop::test::kMunkExtremeLaunchAngle,
          .pointCount = 543U,
          .stepCount = 536U,
          .topBounceCount = 3U,
          .bottomBounceCount = 3U}};

  const GeometryTracer tracer(
      bellhop::test::makeMunkEnvironment(),
      bellhop::test::makeMunkIntegratorSettings());
  RayPathCache cache;
  for (const ExpectedTrace& expected : expectedTraces) {
    auto path =
        tracer.trace(Source{.depth = 1000.0}, expected.launchAngle);
    const std::size_t topBounceCount = static_cast<std::size_t>(
        std::count_if(path.events.begin(), path.events.end(),
                      [](const auto& event) {
                        return event.boundary ==
                               ReflectionBoundary::SeaSurface;
                      }));
    const std::size_t bottomBounceCount = path.events.size() -
                                          topBounceCount;

    context.check(
        path.terminationReason == RayTerminationReason::ExitedDomain,
        "extreme Munk ray exits through the range box");
    context.check(path.points.size() == expected.pointCount,
                  "extreme Munk point count matches the Fortran oracle");
    context.check(path.steps.size() == expected.stepCount,
                  "extreme Munk step count matches the Fortran oracle");
    context.check(topBounceCount == expected.topBounceCount,
                  "extreme Munk sea-surface bounce count matches");
    context.check(bottomBounceCount == expected.bottomBounceCount,
                  "extreme Munk seabed bounce count matches");
    context.check(path.points.size() ==
                      1U + path.steps.size() + path.events.size(),
                  "extreme Munk path satisfies P = 1 + S + E");
    cache.append(std::move(path));
  }

  cache.freeze();
  context.check(cache.frozen() && cache.size() == expectedTraces.size(),
                "long reflected Munk paths freeze in RayPathCache");
}

}  // namespace

int main() {
  Context context;
  testMunkOracleRegression(context);
  testMunkBoundaryReflectionRegression(context);
  return context.failureCount() == 0 ? 0 : 1;
}
