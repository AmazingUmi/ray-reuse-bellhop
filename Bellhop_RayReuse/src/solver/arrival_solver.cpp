#include "rayreuse/solver/arrival_solver.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#include "rayreuse/error.hpp"
#include "rayreuse/field/frequency_projector.hpp"
#include "rayreuse/field/geometric_gaussian_influence.hpp"
#include "rayreuse/field/geometric_hat_influence.hpp"
#include "rayreuse/ray/geometry_tracer.hpp"
#include "rayreuse/solver/single_frequency_solver.hpp"

namespace rayreuse {
namespace {
using Clock = std::chrono::steady_clock;
void validateArrivalSimulation(const SimulationCase& simulation) {
  if (simulation.runMode() != SimulationRunMode::AsciiArrivals &&
      simulation.runMode() != SimulationRunMode::BinaryArrivals) {
    throw ValidationError(
        "arrival solver requires ASCII or binary arrivals mode");
  }
}
double elapsed(Clock::time_point begin, Clock::time_point end) {
  return std::chrono::duration<double>(end - begin).count();
}
std::size_t checkedAdd(std::size_t a, std::size_t b, const char* label) {
  if (b > std::numeric_limits<std::size_t>::max() - a)
    throw ValidationError(label);
  return a + b;
}
std::size_t workspaceBytes(const ArrivalWorkspace& workspace) {
  std::size_t bytes = sizeof(ArrivalWorkspace);
  for (std::size_t i = 0U; i < workspace.receiverCellCount(); ++i)
    bytes = checkedAdd(bytes, workspace.cellAt(i).size() * sizeof(Arrival),
                       "arrival workspace bytes");
  return bytes;
}

// Traces one source's launch fan into an independent frozen cache. The
// diagnostic carries the source and launch indices (F2CPP arrival-solver
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
          "arrival solve encountered an abnormal ray termination at source " +
          std::to_string(sourceIndex) + ", launch " +
          std::to_string(launchIndex));
    cache.append(std::move(path));
  }
  cache.freeze();
  return cache;
}

struct ArrivalTraceBatch {
  std::vector<RayPathCache> caches;
  std::size_t totalRayPointCount{};
  double traceSeconds{};
  std::size_t peakRayCacheBytes{};
};

ArrivalTraceBatch traceAllSourceCaches(const SimulationCase& simulation) {
  ArrivalTraceBatch batch;
  batch.caches.reserve(simulation.sourceCount());
  const auto traceBegin = Clock::now();
  for (std::size_t sourceIndex = 0U; sourceIndex < simulation.sourceCount();
       ++sourceIndex) {
    RayPathCache cache = traceSourceCache(simulation, sourceIndex);
    for (const RayPath& path : cache.paths()) {
      batch.totalRayPointCount = checkedAdd(
          batch.totalRayPointCount, path.points.size(), "arrival point count");
    }
    batch.peakRayCacheBytes =
        std::max(batch.peakRayCacheBytes, cache.memoryFootprintBytes());
    batch.caches.push_back(std::move(cache));
  }
  batch.traceSeconds = elapsed(traceBegin, Clock::now());
  return batch;
}

ArrivalWorkspace projectArrivals(const SimulationCase& simulation,
                                 const RayPathCache& cache,
                                 std::size_t frequencyIndex,
                                 std::size_t sourceIndex, BeamFamily beamFamily,
                                 std::size_t& projectedCount) {
  const double frequency = simulation.frequencies().values().at(frequencyIndex);
  const Source& source = simulation.sources().at(sourceIndex);
  ArrivalWorkspace workspace(frequency, simulation.receivers());
  const FrequencyProjector projector(simulation.environment());
  GeometricHatInfluence hat(simulation.receivers(),
                            simulation.cervenyCoordinateSystem(),
                            simulation.sourceGeometry());
  GeometricGaussianInfluence gaussian(simulation.receivers(),
                                      simulation.sourceGeometry());
  for (const RayPath& path : cache.paths()) {
    const double sourceAmplitude =
        source.amplitude *
        simulation.sourceBeamPattern().amplitudeForLaunchAngle(
            path.launchAngle);
    if (!std::isfinite(sourceAmplitude) || sourceAmplitude < 0.0)
      throw ValidationError(
          "source beam pattern produced an invalid arrival amplitude");
    const RayFrequencyState state =
        projector.project(path, frequency, sourceAmplitude);
    if (beamFamily == BeamFamily::GeometricGaussian)
      gaussian.accumulateArrivals(workspace, path, state,
                                  simulation.launchFanPlan().launchAngleStep);
    else if (beamFamily == BeamFamily::GeometricHat)
      hat.accumulateArrivals(workspace, path, state,
                             simulation.launchFanPlan().launchAngleStep);
    else
      throw ValidationError(
          "arrival solver supports only geometric beam families");
    ++projectedCount;
  }
  return workspace;
}

std::vector<ArrivalWorkspace> projectAllSourceArrivals(
    const SimulationCase& simulation, const std::vector<RayPathCache>& caches,
    std::size_t frequencyIndex, BeamFamily beamFamily,
    std::size_t& projectedCount) {
  std::vector<ArrivalWorkspace> workspaces;
  workspaces.reserve(caches.size());
  for (std::size_t sourceIndex = 0U; sourceIndex < caches.size();
       ++sourceIndex) {
    workspaces.push_back(projectArrivals(simulation, caches[sourceIndex],
                                         frequencyIndex, sourceIndex,
                                         beamFamily, projectedCount));
  }
  return workspaces;
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

ArrivalSolverStatistics ArrivalSolver::solve(
    const SimulationCase& simulation,
    const FrozenFrequencyArrivalConsumer& consumer, bool verifyCache) {
  validateArrivalSimulation(simulation);
  const BeamFamily beamFamily = simulation.beamFamily();
  if (!consumer)
    throw ValidationError("arrival solver requires a frequency consumer");
  if (beamFamily != BeamFamily::GeometricHat &&
      beamFamily != BeamFamily::GeometricGaussian)
    throw ValidationError(
        "arrival solver supports only geometric beam families");
  // One frozen cache per source (Worklist FP-2F §1.2), reused across every
  // frequency; the cache vector is owned by this solver and consumed as
  // const.
  const ArrivalTraceBatch batch = traceAllSourceCaches(simulation);
  const std::vector<RayPathCache>& caches = batch.caches;
  ArrivalSolverStatistics stats;
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
                              simulation.cervenyCoordinateSystem(),
                              simulation.sourceGeometry());
    GeometricGaussianInfluence gaussian(simulation.receivers(),
                                        simulation.sourceGeometry());
    std::vector<ArrivalWorkspace> workspaces;
    workspaces.reserve(caches.size());
    for (std::size_t sourceIndex = 0U; sourceIndex < caches.size();
         ++sourceIndex) {
      const Source& source = simulation.sources()[sourceIndex];
      const RayPathCache& cache = caches[sourceIndex];
      ArrivalWorkspace workspace(frequency, simulation.receivers());
      for (const RayPath& path : cache.paths()) {
        const auto projectBegin = Clock::now();
        const double sourceAmplitude =
            source.amplitude *
            simulation.sourceBeamPattern().amplitudeForLaunchAngle(
                path.launchAngle);
        if (!std::isfinite(sourceAmplitude) || sourceAmplitude < 0.0)
          throw ValidationError(
              "source beam pattern produced an invalid arrival amplitude");
        const RayFrequencyState state =
            projector.project(path, frequency, sourceAmplitude);
        const auto projectEnd = Clock::now();
        if (beamFamily == BeamFamily::GeometricGaussian)
          gaussian.accumulateArrivals(workspace, path, state, spacing);
        else
          hat.accumulateArrivals(workspace, path, state, spacing);
        stats.projectSeconds += elapsed(projectBegin, projectEnd);
        stats.influenceSeconds += elapsed(projectEnd, Clock::now());
        ++stats.projectedRayCount;
      }
      stats.candidateCount =
          checkedAdd(stats.candidateCount, workspace.candidateCount(),
                     "arrival candidate count");
      stats.saturatedCellCount =
          checkedAdd(stats.saturatedCellCount, workspace.saturatedCellCount(),
                     "arrival saturated-cell count");
      stats.peakArrivalWorkspaceBytes =
          std::max(stats.peakArrivalWorkspaceBytes, workspaceBytes(workspace));
      workspaces.push_back(std::move(workspace));
    }
    const auto consumeBegin = Clock::now();
    consumer(fi, caches, workspaces);
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
      throw ValidationError("arrival projection modified the frozen ray cache");
  }
  return stats;
}

ArrivalSolverStatistics ArrivalSolver::solveNonReuse(
    const SimulationCase& simulation,
    const FrozenFrequencyArrivalConsumer& consumer, bool verifyCache) {
  validateArrivalSimulation(simulation);
  const BeamFamily beamFamily = simulation.beamFamily();
  if (!consumer)
    throw ValidationError("arrival solver requires a frequency consumer");
  if (beamFamily != BeamFamily::GeometricHat &&
      beamFamily != BeamFamily::GeometricGaussian)
    throw ValidationError(
        "arrival solver supports only geometric beam families");
  ArrivalSolverStatistics stats;
  for (std::size_t fi = 0U; fi < simulation.frequencies().size(); ++fi) {
    // Non-reuse: every frequency re-traces every source's fan
    // (Worklist FP-2F §1.5: Nfreq x NSz trace passes).
    const auto traceBegin = Clock::now();
    const ArrivalTraceBatch batch = traceAllSourceCaches(simulation);
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
            "arrival non-reuse traces produced inconsistent frozen caches");
      }
    }
    stats.traceSeconds += elapsed(traceBegin, Clock::now());
    for (const RayPathCache& cache : caches) {
      stats.rayCount += cache.size();
      stats.peakRayCacheBytes =
          std::max(stats.peakRayCacheBytes, cache.memoryFootprintBytes());
    }
    stats.totalRayPointCount =
        checkedAdd(stats.totalRayPointCount, batch.totalRayPointCount,
                   "arrival point count");
    std::size_t projected = 0U;
    const auto projectBegin = Clock::now();
    std::vector<ArrivalWorkspace> workspaces =
        projectAllSourceArrivals(simulation, caches, fi, beamFamily, projected);
    const auto projectEnd = Clock::now();
    stats.projectSeconds += elapsed(projectBegin, projectEnd);
    stats.influenceSeconds += elapsed(projectBegin, projectEnd);
    stats.projectedRayCount += projected;
    for (const ArrivalWorkspace& workspace : workspaces) {
      stats.candidateCount =
          checkedAdd(stats.candidateCount, workspace.candidateCount(),
                     "arrival candidate count");
      stats.saturatedCellCount =
          checkedAdd(stats.saturatedCellCount, workspace.saturatedCellCount(),
                     "arrival saturated-cell count");
      stats.peakArrivalWorkspaceBytes =
          std::max(stats.peakArrivalWorkspaceBytes, workspaceBytes(workspace));
    }
    const auto consumeBegin = Clock::now();
    consumer(fi, caches, workspaces);
    if (verifyCache) {
      verifySourceFingerprints(caches, fingerprintsBefore,
                               "arrival projection modified a frozen "
                               "non-reuse cache");
      stats.sourceCacheFingerprintsAfter = fingerprintsBefore;
      stats.cacheFingerprintAfter = fingerprintsBefore.front();
    }
    stats.consumeSeconds += elapsed(consumeBegin, Clock::now());
    ++stats.frequencyCount;
  }
  return stats;
}

ArrivalSolverStatistics ArrivalSolver::solveParallel(
    const SimulationCase& simulation,
    const FrozenFrequencyArrivalConsumer& consumer, std::size_t workerCount,
    bool verifyCache) {
  validateArrivalSimulation(simulation);
  const BeamFamily beamFamily = simulation.beamFamily();
  if (!consumer)
    throw ValidationError("arrival solver requires a frequency consumer");
  if (beamFamily != BeamFamily::GeometricHat &&
      beamFamily != BeamFamily::GeometricGaussian)
    throw ValidationError(
        "arrival solver supports only geometric beam families");
  if (workerCount == 0U)
    throw ValidationError("arrival worker count must be positive");
  const ArrivalTraceBatch batch = traceAllSourceCaches(simulation);
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
  struct Result {
    std::vector<ArrivalWorkspace> workspaces;
    std::size_t projected{};
  };
  std::vector<Result> results(count);
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
          results[fi].workspaces = projectAllSourceArrivals(
              simulation, caches, fi, beamFamily, results[fi].projected);
        }
      } catch (...) {
        const std::lock_guard lock(errorMutex);
        if (!workerError) workerError = std::current_exception();
      }
    });
  }
  pool.clear();
  if (workerError) std::rethrow_exception(workerError);
  ArrivalSolverStatistics stats;
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
    stats.projectedRayCount += results[fi].projected;
    for (const ArrivalWorkspace& workspace : results[fi].workspaces) {
      stats.candidateCount =
          checkedAdd(stats.candidateCount, workspace.candidateCount(),
                     "arrival candidate count");
      stats.saturatedCellCount =
          checkedAdd(stats.saturatedCellCount, workspace.saturatedCellCount(),
                     "arrival saturated-cell count");
      stats.peakArrivalWorkspaceBytes =
          std::max(stats.peakArrivalWorkspaceBytes, workspaceBytes(workspace));
    }
    const auto consumeBegin = Clock::now();
    consumer(fi, caches, results[fi].workspaces);
    stats.consumeSeconds += elapsed(consumeBegin, Clock::now());
  }
  if (verifyCache) {
    verifySourceFingerprints(caches, fingerprintsBefore,
                             "parallel arrival projection modified the frozen "
                             "ray cache");
    stats.sourceCacheFingerprintsAfter = fingerprintsBefore;
    stats.cacheFingerprintAfter = fingerprintsBefore.front();
  }
  return stats;
}
}  // namespace rayreuse
