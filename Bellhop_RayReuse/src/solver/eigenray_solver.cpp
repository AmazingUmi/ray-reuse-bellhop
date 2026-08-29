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

// Traces one source's launch fan into an independent frozen cache. The
// diagnostic carries the source and launch indices (F2CPP eigenray-solver
// error semantics).
RayPathCache traceSourceCache(const SimulationCase& simulation,
                              std::size_t sourceIndex) {
  const LaunchFanPlan& fan = simulation.launchFanPlan();
  const Source& source = simulation.sources().at(sourceIndex);
  GeometryTracer tracer(simulation);
  RayPathCache cache;
  cache.reserve(fan.launchAngleCount);
  for (std::size_t launchIndex = 0U; launchIndex < fan.launchAngles.size();
       ++launchIndex) {
    RayPath path = tracer.trace(source, fan.launchAngles[launchIndex]);
    if (path.terminationReason != RayTerminationReason::ExitedDomain)
      throw ValidationError(
          "eigenray solve encountered an abnormal ray termination at "
          "source " +
          std::to_string(sourceIndex) + ", launch " +
          std::to_string(launchIndex));
    cache.append(std::move(path));
  }
  cache.freeze();
  return cache;
}

struct EigenrayTraceBatch {
  std::vector<RayPathCache> caches;
  std::size_t totalRayPointCount{};
  double traceSeconds{};
  std::size_t peakRayCacheBytes{};
};

EigenrayTraceBatch traceAllSourceCaches(const SimulationCase& simulation) {
  EigenrayTraceBatch batch;
  batch.caches.reserve(simulation.sourceCount());
  const auto traceBegin = Clock::now();
  for (std::size_t sourceIndex = 0U; sourceIndex < simulation.sourceCount();
       ++sourceIndex) {
    RayPathCache cache = traceSourceCache(simulation, sourceIndex);
    for (const RayPath& path : cache.paths()) {
      batch.totalRayPointCount =
          checkedAdd(batch.totalRayPointCount, path.points.size(),
                     "eigenray point count");
    }
    batch.peakRayCacheBytes =
        std::max(batch.peakRayCacheBytes, cache.memoryFootprintBytes());
    batch.caches.push_back(std::move(cache));
  }
  batch.traceSeconds = elapsed(traceBegin, Clock::now());
  return batch;
}

EigenraySourceHits collectHits(const SimulationCase& simulation,
                               const RayPathCache& cache,
                               std::size_t frequencyIndex,
                               std::size_t sourceIndex,
                               BeamFamily beamFamily) {
  EigenraySourceHits hits;
  const Source& source = simulation.sources().at(sourceIndex);
  const FrequencyProjector projector(simulation.environment());
  GeometricHatInfluence hat(simulation.receivers(),
                            simulation.cervenyCoordinateSystem());
  GeometricGaussianInfluence gaussian(simulation.receivers());
  for (std::size_t li = 0U; li < cache.size(); ++li) {
    const RayPath& path = cache.at(li);
    const double sourceAmplitude =
        source.amplitude *
        simulation.sourceBeamPattern().amplitudeForLaunchAngle(
            path.launchAngle);
    if (!std::isfinite(sourceAmplitude) || sourceAmplitude < 0.0)
      throw ValidationError(
          "source beam pattern produced an invalid eigenray amplitude");
    const RayFrequencyState state = projector.project(
        path, simulation.frequencies().values().at(frequencyIndex),
        sourceAmplitude);
    const auto sink = [&](const EigenrayHit& hit) {
      hits.emplace_back(li, hit);
    };
    if (beamFamily == BeamFamily::GeometricGaussian)
      gaussian.collectEigenrayHits(sink, path, state,
                                   simulation.launchFanPlan().launchAngleStep);
    else
      hat.collectEigenrayHits(sink, path, state,
                              simulation.launchFanPlan().launchAngleStep);
  }
  return hits;
}

std::vector<EigenraySourceHits> collectAllSourceHits(
    const SimulationCase& simulation, const std::vector<RayPathCache>& caches,
    std::size_t frequencyIndex, BeamFamily beamFamily) {
  std::vector<EigenraySourceHits> sourceHits;
  sourceHits.reserve(caches.size());
  for (std::size_t sourceIndex = 0U; sourceIndex < caches.size();
       ++sourceIndex) {
    sourceHits.push_back(collectHits(simulation, caches[sourceIndex],
                                     frequencyIndex, sourceIndex, beamFamily));
  }
  return sourceHits;
}

void verifySourceFingerprints(
    const std::vector<RayPathCache>& caches,
    const std::vector<std::uint64_t>& fingerprintsBefore,
    const char* failureMessage) {
  for (std::size_t sourceIndex = 0U; sourceIndex < caches.size();
       ++sourceIndex) {
    if (caches[sourceIndex].contentFingerprint() !=
        fingerprintsBefore[sourceIndex]) {
      throw ValidationError(failureMessage);
    }
  }
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
  // One frozen cache per source (Worklist FP-2F §1.2), reused across every
  // frequency; the cache vector is owned by this solver and consumed as
  // const.
  const EigenrayTraceBatch batch = traceAllSourceCaches(simulation);
  const std::vector<RayPathCache>& caches = batch.caches;
  EigenraySolverStatistics stats;
  stats.traceSeconds = batch.traceSeconds;
  std::size_t rayCount = 0U;
  for (const RayPathCache& cache : caches) rayCount += cache.size();
  stats.rayCount = rayCount;
  stats.totalRayPointCount = batch.totalRayPointCount;
  stats.peakRayCacheBytes = batch.peakRayCacheBytes;
  if (verifyCache) {
    stats.cacheFingerprintVerified = true;
    stats.sourceCacheFingerprintsBefore.reserve(caches.size());
    for (const RayPathCache& cache : caches)
      stats.sourceCacheFingerprintsBefore.push_back(cache.contentFingerprint());
    stats.cacheFingerprintBefore = stats.sourceCacheFingerprintsBefore.front();
  }
  const double spacing = simulation.launchFanPlan().launchAngleStep;
  for (std::size_t fi = 0U; fi < simulation.frequencies().size(); ++fi) {
    const double frequency = simulation.frequencies().values()[fi];
    const FrequencyProjector projector(simulation.environment());
    GeometricHatInfluence hat(simulation.receivers(),
                              simulation.cervenyCoordinateSystem());
    GeometricGaussianInfluence gaussian(simulation.receivers());
    std::vector<EigenraySourceHits> sourceHits;
    sourceHits.reserve(caches.size());
    for (std::size_t sourceIndex = 0U; sourceIndex < caches.size();
         ++sourceIndex) {
      const RayPathCache& cache = caches[sourceIndex];
      EigenraySourceHits hits;
      for (std::size_t li = 0U; li < cache.size(); ++li) {
        const RayPath& path = cache.at(li);
        const auto projectBegin = Clock::now();
        const double sourceAmplitude =
            simulation.sources()[sourceIndex].amplitude *
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
          hits.emplace_back(li, hit);
          ++hitCount;
          prefixCount = checkedAdd(prefixCount, hit.prefixPointCount,
                                   "eigenray prefix point count");
        };
        if (beamFamily == BeamFamily::GeometricGaussian)
          gaussian.collectEigenrayHits(sink, path, state, spacing);
        else
          hat.collectEigenrayHits(sink, path, state, spacing);
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
      sourceHits.push_back(std::move(hits));
    }
    const auto consumeBegin = Clock::now();
    consumer(fi, caches, sourceHits);
    stats.consumeSeconds += elapsed(consumeBegin, Clock::now());
    ++stats.frequencyCount;
  }
  if (verifyCache) {
    stats.sourceCacheFingerprintsAfter.reserve(caches.size());
    for (const RayPathCache& cache : caches)
      stats.sourceCacheFingerprintsAfter.push_back(cache.contentFingerprint());
    stats.cacheFingerprintAfter = stats.sourceCacheFingerprintsAfter.front();
    if (stats.sourceCacheFingerprintsAfter !=
        stats.sourceCacheFingerprintsBefore)
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
    // Non-reuse: every frequency re-traces every source's fan
    // (Worklist FP-2F §1.5: Nfreq x NSz trace passes).
    const EigenrayTraceBatch batch = traceAllSourceCaches(simulation);
    const std::vector<RayPathCache>& caches = batch.caches;
    std::vector<std::uint64_t> fingerprintsBefore;
    if (verifyCache) {
      fingerprintsBefore.reserve(caches.size());
      for (const RayPathCache& cache : caches)
        fingerprintsBefore.push_back(cache.contentFingerprint());
      if (!stats.cacheFingerprintVerified) {
        stats.cacheFingerprintVerified = true;
        stats.sourceCacheFingerprintsBefore = fingerprintsBefore;
        stats.cacheFingerprintBefore = fingerprintsBefore.front();
      } else if (fingerprintsBefore != stats.sourceCacheFingerprintsBefore) {
        throw ValidationError(
            "eigenray non-reuse traces produced inconsistent frozen caches");
      }
    }
    for (const RayPathCache& cache : caches) {
      stats.rayCount += cache.size();
      stats.peakRayCacheBytes =
          std::max(stats.peakRayCacheBytes, cache.memoryFootprintBytes());
    }
    stats.totalRayPointCount = checkedAdd(
        stats.totalRayPointCount, batch.totalRayPointCount,
        "eigenray point count");
    stats.traceSeconds += batch.traceSeconds;
    const std::vector<EigenraySourceHits> sourceHits =
        collectAllSourceHits(simulation, caches, fi, beamFamily);
    for (const EigenraySourceHits& hits : sourceHits) {
      for (const auto& [launchIndex, hit] : hits) {
        static_cast<void>(launchIndex);
        ++stats.totalHitCount;
        stats.totalPrefixPointCount =
            checkedAdd(stats.totalPrefixPointCount, hit.prefixPointCount,
                       "eigenray prefix point count");
      }
    }
    const auto consumeBegin = Clock::now();
    consumer(fi, caches, sourceHits);
    if (verifyCache) {
      verifySourceFingerprints(caches, fingerprintsBefore,
                               "eigenray projection modified a frozen "
                               "non-reuse cache");
      stats.sourceCacheFingerprintsAfter = fingerprintsBefore;
      stats.cacheFingerprintAfter = fingerprintsBefore.front();
    }
    stats.consumeSeconds += elapsed(consumeBegin, Clock::now());
    stats.projectedRayCount += [&]() {
      std::size_t projected = 0U;
      for (const RayPathCache& cache : caches) projected += cache.size();
      return projected;
    }();
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
  const EigenrayTraceBatch batch = traceAllSourceCaches(simulation);
  const std::vector<RayPathCache>& caches = batch.caches;
  const std::vector<std::uint64_t> fingerprintsBefore = [&]() {
    std::vector<std::uint64_t> fingerprints;
    if (verifyCache) {
      fingerprints.reserve(caches.size());
      for (const RayPathCache& cache : caches)
        fingerprints.push_back(cache.contentFingerprint());
    }
    return fingerprints;
  }();
  const std::size_t count = simulation.frequencies().size();
  std::vector<std::vector<EigenraySourceHits>> results(count);
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
          results[fi] = collectAllSourceHits(simulation, caches, fi, beamFamily);
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
  std::size_t rayCount = 0U;
  for (const RayPathCache& cache : caches) rayCount += cache.size();
  stats.rayCount = rayCount;
  stats.totalRayPointCount = batch.totalRayPointCount;
  stats.peakRayCacheBytes = batch.peakRayCacheBytes;
  stats.traceSeconds = batch.traceSeconds;
  stats.cacheFingerprintVerified = verifyCache;
  stats.sourceCacheFingerprintsBefore = fingerprintsBefore;
  if (verifyCache) stats.cacheFingerprintBefore = fingerprintsBefore.front();
  for (std::size_t fi = 0U; fi < count; ++fi) {
    stats.projectedRayCount += stats.rayCount;
    for (const EigenraySourceHits& hits : results[fi]) {
      for (const auto& [launchIndex, hit] : hits) {
        static_cast<void>(launchIndex);
        ++stats.totalHitCount;
        stats.totalPrefixPointCount =
            checkedAdd(stats.totalPrefixPointCount, hit.prefixPointCount,
                       "eigenray prefix point count");
      }
    }
    const auto consumeBegin = Clock::now();
    consumer(fi, caches, results[fi]);
    stats.consumeSeconds += elapsed(consumeBegin, Clock::now());
  }
  if (verifyCache) {
    verifySourceFingerprints(caches, fingerprintsBefore,
                             "parallel eigenray projection modified the "
                             "frozen ray cache");
    stats.sourceCacheFingerprintsAfter = fingerprintsBefore;
    stats.cacheFingerprintAfter = fingerprintsBefore.front();
  }
  return stats;
}
}  // namespace rayreuse
