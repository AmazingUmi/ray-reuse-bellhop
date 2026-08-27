#include "rayreuse/solver/eigenray_solver.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include "rayreuse/error.hpp"
#include "rayreuse/field/frequency_projector.hpp"
#include "rayreuse/field/geometric_gaussian_influence.hpp"
#include "rayreuse/field/geometric_hat_influence.hpp"
#include "rayreuse/ray/geometry_tracer.hpp"

namespace rayreuse {
namespace {
using Clock = std::chrono::steady_clock;
void validateEigenraySimulation(const SimulationCase& simulation) {
  if (simulation.runMode() != SimulationRunMode::Eigenray)
    throw ValidationError("eigenray solver requires Eigenray mode");
}
double elapsed(Clock::time_point begin, Clock::time_point end) {
  return std::chrono::duration<double>(end - begin).count();
}
std::size_t checkedAdd(std::size_t a, std::size_t b, const char* label) {
  if (b > std::numeric_limits<std::size_t>::max() - a)
    throw ValidationError(label);
  return a + b;
}

RayPathCache traceCache(const SimulationCase& simulation) {
  const LaunchFanPlan& fan = simulation.launchFanPlan();
  GeometryTracer tracer(simulation);
  RayPathCache cache;
  cache.reserve(fan.launchAngleCount);
  for (double angle : fan.launchAngles) {
    RayPath path = tracer.trace(simulation.source(), angle);
    if (path.terminationReason != RayTerminationReason::ExitedDomain)
      throw ValidationError(
          "eigenray solve encountered an abnormal ray termination");
    cache.append(std::move(path));
  }
  cache.freeze();
  return cache;
}

struct FrequencyHits {
  std::vector<std::pair<std::size_t, EigenrayHit>> hits;
};
FrequencyHits collectHits(const SimulationCase& simulation,
                          const RayPathCache& cache, std::size_t frequencyIndex,
                          BeamFamily beamFamily) {
  FrequencyHits result;
  const FrequencyProjector projector(simulation.environment());
  GeometricHatInfluence hat(simulation.receivers(),
                            simulation.cervenyCoordinateSystem());
  GeometricGaussianInfluence gaussian(simulation.receivers());
  for (std::size_t li = 0U; li < cache.size(); ++li) {
    const RayPath& path = cache.at(li);
    const double sourceAmplitude =
        simulation.source().amplitude *
        simulation.sourceBeamPattern().amplitudeForLaunchAngle(
            path.launchAngle);
    if (!std::isfinite(sourceAmplitude) || sourceAmplitude < 0.0)
      throw ValidationError(
          "source beam pattern produced an invalid eigenray amplitude");
    const RayFrequencyState state = projector.project(
        path, simulation.frequencies().values().at(frequencyIndex),
        sourceAmplitude);
    const auto sink = [&](const EigenrayHit& hit) {
      result.hits.emplace_back(li, hit);
    };
    if (beamFamily == BeamFamily::GeometricGaussian)
      gaussian.collectEigenrayHits(sink, path, state,
                                   simulation.launchFanPlan().launchAngleStep);
    else
      hat.collectEigenrayHits(sink, path, state,
                              simulation.launchFanPlan().launchAngleStep);
  }
  return result;
}
}  // namespace

EigenraySolverStatistics EigenraySolver::solve(
    const SimulationCase& simulation,
    const FrozenFrequencyEigenrayConsumer& consumer, bool verifyCache) {
  validateEigenraySimulation(simulation);
  const BeamFamily beamFamily = simulation.beamFamily();
  if (!consumer)
    throw ValidationError("eigenray solver requires a frequency hit consumer");
  if (beamFamily != BeamFamily::GeometricHat &&
      beamFamily != BeamFamily::GeometricGaussian)
    throw ValidationError(
        "eigenray solver supports only geometric beam families");
  const LaunchFanPlan& fan = simulation.launchFanPlan();
  GeometryTracer tracer(simulation);
  RayPathCache cache;
  cache.reserve(fan.launchAngleCount);
  EigenraySolverStatistics stats;
  const auto traceBegin = Clock::now();
  for (double angle : fan.launchAngles) {
    RayPath path = tracer.trace(simulation.source(), angle);
    if (path.terminationReason != RayTerminationReason::ExitedDomain)
      throw ValidationError(
          "eigenray solve encountered an abnormal ray termination");
    stats.totalRayPointCount = checkedAdd(
        stats.totalRayPointCount, path.points.size(), "eigenray point count");
    cache.append(std::move(path));
  }
  cache.freeze();
  stats.traceSeconds = elapsed(traceBegin, Clock::now());
  stats.rayCount = cache.size();
  stats.peakRayCacheBytes = cache.memoryFootprintBytes();
  if (verifyCache) {
    stats.cacheFingerprintVerified = true;
    stats.cacheFingerprintBefore = cache.contentFingerprint();
  }
  for (std::size_t fi = 0U; fi < simulation.frequencies().size(); ++fi) {
    const double frequency = simulation.frequencies().values()[fi];
    const FrequencyProjector projector(simulation.environment());
    GeometricHatInfluence hat(simulation.receivers(),
                              simulation.cervenyCoordinateSystem());
    GeometricGaussianInfluence gaussian(simulation.receivers());
    FrequencyHits frequencyHits;
    for (std::size_t li = 0U; li < cache.size(); ++li) {
      const RayPath& path = cache.at(li);
      const auto projectBegin = Clock::now();
      const double sourceAmplitude =
          simulation.source().amplitude *
          simulation.sourceBeamPattern().amplitudeForLaunchAngle(
              path.launchAngle);
      if (!std::isfinite(sourceAmplitude) || sourceAmplitude < 0.0)
        throw ValidationError(
            "source beam pattern produced an invalid eigenray amplitude");
      const RayFrequencyState state =
          projector.project(path, frequency, sourceAmplitude);
      const auto projectEnd = Clock::now();
      std::size_t hitCount = 0U;
      std::size_t prefixCount = 0U;
      const auto sink = [&](const EigenrayHit& hit) {
        frequencyHits.hits.emplace_back(li, hit);
        ++hitCount;
        prefixCount = checkedAdd(prefixCount, hit.prefixPointCount,
                                 "eigenray prefix point count");
      };
      if (beamFamily == BeamFamily::GeometricGaussian)
        gaussian.collectEigenrayHits(sink, path, state, fan.launchAngleStep);
      else
        hat.collectEigenrayHits(sink, path, state, fan.launchAngleStep);
      stats.projectSeconds += elapsed(projectBegin, projectEnd);
      stats.influenceSeconds += elapsed(projectEnd, Clock::now());
      stats.projectedRayCount = checkedAdd(stats.projectedRayCount, 1U,
                                           "eigenray projected ray count");
      stats.totalHitCount =
          checkedAdd(stats.totalHitCount, hitCount, "eigenray hit count");
      stats.totalPrefixPointCount =
          checkedAdd(stats.totalPrefixPointCount, prefixCount,
                     "eigenray prefix point count");
    }
    const auto consumeBegin = Clock::now();
    consumer(fi, cache, frequencyHits.hits);
    stats.consumeSeconds += elapsed(consumeBegin, Clock::now());
    ++stats.frequencyCount;
  }
  if (verifyCache) {
    stats.cacheFingerprintAfter = cache.contentFingerprint();
    if (stats.cacheFingerprintAfter != stats.cacheFingerprintBefore)
      throw ValidationError(
          "eigenray projection modified the frozen ray cache");
  }
  return stats;
}

EigenraySolverStatistics EigenraySolver::solveNonReuse(
    const SimulationCase& simulation,
    const FrozenFrequencyEigenrayConsumer& consumer, bool verifyCache) {
  validateEigenraySimulation(simulation);
  const BeamFamily beamFamily = simulation.beamFamily();
  if (!consumer)
    throw ValidationError("eigenray solver requires a frequency hit consumer");
  if (beamFamily != BeamFamily::GeometricHat &&
      beamFamily != BeamFamily::GeometricGaussian)
    throw ValidationError(
        "eigenray solver supports only geometric beam families");
  EigenraySolverStatistics stats;
  for (std::size_t fi = 0U; fi < simulation.frequencies().size(); ++fi) {
    RayPathCache cache = traceCache(simulation);
    const std::uint64_t fingerprintBefore =
        verifyCache ? cache.contentFingerprint() : 0U;
    if (verifyCache) {
      if (!stats.cacheFingerprintVerified) {
        stats.cacheFingerprintVerified = true;
        stats.cacheFingerprintBefore = fingerprintBefore;
      } else if (fingerprintBefore != stats.cacheFingerprintBefore) {
        throw ValidationError(
            "eigenray non-reuse traces produced inconsistent frozen caches");
      }
    }
    stats.rayCount += cache.size();
    stats.peakRayCacheBytes =
        std::max(stats.peakRayCacheBytes, cache.memoryFootprintBytes());
    FrequencyHits hits = collectHits(simulation, cache, fi, beamFamily);
    for (const auto& [launchIndex, hit] : hits.hits) {
      static_cast<void>(launchIndex);
      ++stats.totalHitCount;
      stats.totalPrefixPointCount =
          checkedAdd(stats.totalPrefixPointCount, hit.prefixPointCount,
                     "eigenray prefix point count");
    }
    const auto consumeBegin = Clock::now();
    consumer(fi, cache, hits.hits);
    if (verifyCache) {
      stats.cacheFingerprintAfter = cache.contentFingerprint();
      if (stats.cacheFingerprintAfter != fingerprintBefore)
        throw ValidationError(
            "eigenray projection modified a frozen non-reuse cache");
    }
    stats.consumeSeconds += elapsed(consumeBegin, Clock::now());
    stats.projectedRayCount += cache.size();
    ++stats.frequencyCount;
  }
  return stats;
}

EigenraySolverStatistics EigenraySolver::solveParallel(
    const SimulationCase& simulation,
    const FrozenFrequencyEigenrayConsumer& consumer, std::size_t workerCount,
    bool verifyCache) {
  validateEigenraySimulation(simulation);
  const BeamFamily beamFamily = simulation.beamFamily();
  if (!consumer)
    throw ValidationError("eigenray solver requires a frequency hit consumer");
  if (beamFamily != BeamFamily::GeometricHat &&
      beamFamily != BeamFamily::GeometricGaussian)
    throw ValidationError(
        "eigenray solver supports only geometric beam families");
  if (workerCount == 0U)
    throw ValidationError("eigenray worker count must be positive");
  RayPathCache cache = traceCache(simulation);
  const std::uint64_t fingerprintBefore =
      verifyCache ? cache.contentFingerprint() : 0U;
  const std::size_t count = simulation.frequencies().size();
  std::vector<FrequencyHits> results(count);
  std::atomic<std::size_t> next{0U};
  std::mutex errorMutex;
  std::exception_ptr workerError;
  const std::size_t workers = std::min(workerCount, count);
  std::vector<std::jthread> pool;
  pool.reserve(workers);
  for (std::size_t wi = 0U; wi < workers; ++wi) {
    pool.emplace_back([&] {
      try {
        while (true) {
          const std::size_t fi = next.fetch_add(1U);
          if (fi >= count) break;
          results[fi] = collectHits(simulation, cache, fi, beamFamily);
        }
      } catch (...) {
        const std::lock_guard lock(errorMutex);
        if (!workerError) workerError = std::current_exception();
      }
    });
  }
  pool.clear();
  if (workerError) std::rethrow_exception(workerError);
  EigenraySolverStatistics stats;
  stats.frequencyCount = count;
  stats.rayCount = cache.size();
  stats.peakRayCacheBytes = cache.memoryFootprintBytes();
  stats.cacheFingerprintVerified = verifyCache;
  stats.cacheFingerprintBefore = fingerprintBefore;
  for (std::size_t fi = 0U; fi < count; ++fi) {
    stats.projectedRayCount += cache.size();
    for (const auto& [launchIndex, hit] : results[fi].hits) {
      static_cast<void>(launchIndex);
      ++stats.totalHitCount;
      stats.totalPrefixPointCount =
          checkedAdd(stats.totalPrefixPointCount, hit.prefixPointCount,
                     "eigenray prefix point count");
    }
    const auto consumeBegin = Clock::now();
    consumer(fi, cache, results[fi].hits);
    stats.consumeSeconds += elapsed(consumeBegin, Clock::now());
  }
  if (verifyCache) {
    stats.cacheFingerprintAfter = cache.contentFingerprint();
    if (stats.cacheFingerprintAfter != stats.cacheFingerprintBefore)
      throw ValidationError(
          "parallel eigenray projection modified the frozen ray cache");
  }
  return stats;
}
}  // namespace rayreuse
