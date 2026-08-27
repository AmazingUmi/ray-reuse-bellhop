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

RayPathCache traceCache(const SimulationCase& simulation) {
  const LaunchFanPlan& fan = simulation.launchFanPlan();
  GeometryTracer tracer(simulation);
  RayPathCache cache;
  cache.reserve(fan.launchAngleCount);
  for (double angle : fan.launchAngles) {
    RayPath path = tracer.trace(simulation.source(), angle);
    if (path.terminationReason != RayTerminationReason::ExitedDomain)
      throw ValidationError(
          "arrival solve encountered an abnormal ray termination");
    cache.append(std::move(path));
  }
  cache.freeze();
  return cache;
}

ArrivalWorkspace projectArrivals(const SimulationCase& simulation,
                                 const RayPathCache& cache,
                                 std::size_t frequencyIndex,
                                 BeamFamily beamFamily,
                                 std::size_t& projectedCount) {
  const double frequency = simulation.frequencies().values().at(frequencyIndex);
  ArrivalWorkspace workspace(frequency, simulation.receivers());
  const FrequencyProjector projector(simulation.environment());
  GeometricHatInfluence hat(simulation.receivers(),
                            simulation.cervenyCoordinateSystem());
  GeometricGaussianInfluence gaussian(simulation.receivers());
  for (const RayPath& path : cache.paths()) {
    const double sourceAmplitude =
        simulation.source().amplitude *
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
  const LaunchFanPlan& fan = simulation.launchFanPlan();
  GeometryTracer tracer(simulation);
  RayPathCache cache;
  cache.reserve(fan.launchAngleCount);
  ArrivalSolverStatistics stats;
  const auto traceBegin = Clock::now();
  for (double angle : fan.launchAngles) {
    RayPath path = tracer.trace(simulation.source(), angle);
    if (path.terminationReason != RayTerminationReason::ExitedDomain)
      throw ValidationError(
          "arrival solve encountered an abnormal ray termination");
    stats.totalRayPointCount = checkedAdd(
        stats.totalRayPointCount, path.points.size(), "arrival point count");
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
  const double spacing = fan.launchAngleStep;
  for (std::size_t fi = 0U; fi < simulation.frequencies().size(); ++fi) {
    const double frequency = simulation.frequencies().values()[fi];
    ArrivalWorkspace workspace(frequency, simulation.receivers());
    const FrequencyProjector projector(simulation.environment());
    GeometricHatInfluence hat(simulation.receivers(),
                              simulation.cervenyCoordinateSystem());
    GeometricGaussianInfluence gaussian(simulation.receivers());
    for (const RayPath& path : cache.paths()) {
      const auto projectBegin = Clock::now();
      const double sourceAmplitude =
          simulation.source().amplitude *
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
    const auto consumeBegin = Clock::now();
    consumer(fi, cache, workspace);
    stats.consumeSeconds += elapsed(consumeBegin, Clock::now());
    ++stats.frequencyCount;
  }
  if (verifyCache) {
    stats.cacheFingerprintAfter = cache.contentFingerprint();
    if (stats.cacheFingerprintAfter != stats.cacheFingerprintBefore)
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
    const auto traceBegin = Clock::now();
    RayPathCache cache = traceCache(simulation);
    const std::uint64_t fingerprintBefore =
        verifyCache ? cache.contentFingerprint() : 0U;
    if (verifyCache) {
      if (!stats.cacheFingerprintVerified) {
        stats.cacheFingerprintVerified = true;
        stats.cacheFingerprintBefore = fingerprintBefore;
      } else if (fingerprintBefore != stats.cacheFingerprintBefore) {
        throw ValidationError(
            "arrival non-reuse traces produced inconsistent frozen caches");
      }
    }
    stats.traceSeconds += elapsed(traceBegin, Clock::now());
    stats.rayCount += cache.size();
    stats.peakRayCacheBytes =
        std::max(stats.peakRayCacheBytes, cache.memoryFootprintBytes());
    std::size_t projected = 0U;
    const auto projectBegin = Clock::now();
    ArrivalWorkspace workspace =
        projectArrivals(simulation, cache, fi, beamFamily, projected);
    const auto projectEnd = Clock::now();
    stats.projectSeconds += elapsed(projectBegin, projectEnd);
    stats.influenceSeconds += elapsed(projectBegin, projectEnd);
    stats.projectedRayCount += projected;
    stats.candidateCount =
        checkedAdd(stats.candidateCount, workspace.candidateCount(),
                   "arrival candidate count");
    stats.saturatedCellCount =
        checkedAdd(stats.saturatedCellCount, workspace.saturatedCellCount(),
                   "arrival saturated-cell count");
    stats.peakArrivalWorkspaceBytes =
        std::max(stats.peakArrivalWorkspaceBytes, workspaceBytes(workspace));
    const auto consumeBegin = Clock::now();
    consumer(fi, cache, workspace);
    if (verifyCache) {
      stats.cacheFingerprintAfter = cache.contentFingerprint();
      if (stats.cacheFingerprintAfter != fingerprintBefore)
        throw ValidationError(
            "arrival projection modified a frozen non-reuse cache");
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
  RayPathCache cache = traceCache(simulation);
  const std::uint64_t fingerprintBefore =
      verifyCache ? cache.contentFingerprint() : 0U;
  const std::size_t count = simulation.frequencies().size();
  struct Result {
    std::unique_ptr<ArrivalWorkspace> workspace;
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
          results[fi].workspace =
              std::make_unique<ArrivalWorkspace>(projectArrivals(
                  simulation, cache, fi, beamFamily, results[fi].projected));
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
  stats.rayCount = cache.size();
  stats.peakRayCacheBytes = cache.memoryFootprintBytes();
  stats.cacheFingerprintVerified = verifyCache;
  stats.cacheFingerprintBefore = fingerprintBefore;
  for (std::size_t fi = 0U; fi < count; ++fi) {
    ArrivalWorkspace& workspace = *results[fi].workspace;
    stats.projectedRayCount += results[fi].projected;
    stats.candidateCount =
        checkedAdd(stats.candidateCount, workspace.candidateCount(),
                   "arrival candidate count");
    stats.saturatedCellCount =
        checkedAdd(stats.saturatedCellCount, workspace.saturatedCellCount(),
                   "arrival saturated-cell count");
    stats.peakArrivalWorkspaceBytes =
        std::max(stats.peakArrivalWorkspaceBytes, workspaceBytes(workspace));
    consumer(fi, cache, workspace);
  }
  if (verifyCache) {
    stats.cacheFingerprintAfter = cache.contentFingerprint();
    if (stats.cacheFingerprintAfter != stats.cacheFingerprintBefore)
      throw ValidationError(
          "parallel arrival projection modified the frozen ray cache");
  }
  return stats;
}
}  // namespace rayreuse
