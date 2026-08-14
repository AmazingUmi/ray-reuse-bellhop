#include "bellhop/solver/ray_trace_solver.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <string>
#include <utility>

#include "bellhop/error.hpp"
#include "bellhop/ray/geometry_tracer.hpp"

namespace bellhop {
namespace {

using Clock = std::chrono::steady_clock;

std::size_t checkedAdd(std::size_t left, std::size_t right,
                       const char* label) {
  if (right > std::numeric_limits<std::size_t>::max() - left) {
    throw ValidationError(std::string(label) + " exceeds size_t capacity");
  }
  return left + right;
}

std::size_t checkedMultiply(std::size_t left, std::size_t right,
                            const char* label) {
  if (left != 0U && right > std::numeric_limits<std::size_t>::max() / left) {
    throw ValidationError(std::string(label) + " exceeds size_t capacity");
  }
  return left * right;
}

}  // namespace

RayTraceStatistics RayTraceSolver::trace(
    const SimulationCase& simulation,
    const FrozenSourceRayConsumer& consumer) {
  if (simulation.runMode() != SimulationRunMode::RayTrace) {
    throw ValidationError("ray tracer requires ray-trace run mode");
  }
  if (simulation.sourceBeamPattern().isDirectional() ||
      simulation.environment().seaSurface().kind() !=
          BoundaryKind::Vacuum ||
      simulation.environment().seabed().kind() != BoundaryKind::Rigid) {
    throw ValidationError(
        "ray tracer currently requires an omnidirectional source, vacuum "
        "surface, and rigid seabed so the written terminal prefix remains "
        "frequency independent");
  }
  if (!consumer) {
    throw ValidationError("ray tracer requires a frozen-cache consumer");
  }
  const LaunchFanPlan& fan = simulation.launchFanPlan();
  const std::size_t rayCount = checkedMultiply(
      fan.launchAngleCount, simulation.sourceCount(), "ray-trace ray count");
  if (rayCount > kMaximumRunRayCount) {
    throw ValidationError("ray-trace ray count exceeds the supported limit");
  }
  GeometryTracer tracer(simulation);
  RayTraceStatistics statistics;
  for (std::size_t sourceIndex = 0U;
       sourceIndex < simulation.sourceCount(); ++sourceIndex) {
    const Clock::time_point traceBegin = Clock::now();
    RayPathCache cache;
    cache.reserve(fan.launchAngleCount);
    for (std::size_t launchIndex = 0U;
         launchIndex < fan.launchAngles.size(); ++launchIndex) {
      RayPath path = tracer.trace(
          simulation.sources()[sourceIndex], fan.launchAngles[launchIndex]);
      if (path.terminationReason != RayTerminationReason::ExitedDomain) {
        throw ValidationError(
            "ray-trace run encountered an abnormal ray termination at "
            "source " + std::to_string(sourceIndex) + ", launch " +
            std::to_string(launchIndex));
      }
      statistics.totalRayPointCount = checkedAdd(
          statistics.totalRayPointCount, path.points.size(),
          "ray-trace point count");
      cache.append(std::move(path));
    }
    cache.freeze();
    const Clock::time_point traceEnd = Clock::now();
    statistics.traceSeconds +=
        std::chrono::duration<double>(traceEnd - traceBegin).count();
    statistics.peakRayCacheBytes = std::max(
        statistics.peakRayCacheBytes, cache.memoryFootprintBytes());
    const Clock::time_point writeBegin = Clock::now();
    consumer(sourceIndex, cache);
    statistics.writeSeconds += std::chrono::duration<double>(
        Clock::now() - writeBegin).count();
  }
  statistics.rayCount = rayCount;
  return statistics;
}

}  // namespace bellhop
