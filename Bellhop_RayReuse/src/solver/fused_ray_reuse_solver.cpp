#include "rayreuse/solver/fused_ray_reuse_solver.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <limits>
#include <span>
#include <thread>
#include <utility>
#include <vector>

#include "rayreuse/error.hpp"
#include "rayreuse/field/beam_epsilon.hpp"
#include "rayreuse/field/frequency_projector.hpp"
#include "rayreuse/field/pressure_scaling.hpp"
#include "rayreuse/model/sound_speed_evaluator.hpp"

namespace rayreuse {
namespace {

using Clock = std::chrono::steady_clock;

[[nodiscard]] double elapsedSeconds(Clock::time_point begin,
                                    Clock::time_point end) {
  return std::chrono::duration<double>(end - begin).count();
}

struct RangeWorkerResult {
  double projectSeconds{};
  double influenceSeconds{};
  CartesianCervenyStatistics influenceStatistics;
};

enum class FusedScopeFailure {
  None,
  NotTransmissionLoss,
  NotCoherent,
  NotCervenyGaussian,
  NotCartesian,
  NotSingleSource,
  TooFewFrequencies,
  IrregularReceivers,
  TooFewReceiverRanges,
  UnequallySpacedReceiverRanges,
};

[[nodiscard]] FusedScopeFailure fusedScopeFailure(
    const SimulationCase& simulation) {
  if (!isTransmissionLossMode(simulation.runMode())) {
    return FusedScopeFailure::NotTransmissionLoss;
  }
  if (simulation.runMode() != SimulationRunMode::Coherent) {
    return FusedScopeFailure::NotCoherent;
  }
  if (simulation.beamFamily() != BeamFamily::CervenyGaussian) {
    return FusedScopeFailure::NotCervenyGaussian;
  }
  if (simulation.cervenyCoordinateSystem() !=
      CervenyCoordinateSystem::Cartesian) {
    return FusedScopeFailure::NotCartesian;
  }
  if (simulation.sourceCount() != 1U) {
    return FusedScopeFailure::NotSingleSource;
  }
  if (simulation.frequencies().size() < 2U) {
    return FusedScopeFailure::TooFewFrequencies;
  }
  if (simulation.receivers().isIrregular()) {
    return FusedScopeFailure::IrregularReceivers;
  }
  const std::vector<double>& ranges = simulation.receivers().ranges();
  if (ranges.size() < 2U) {
    return FusedScopeFailure::TooFewReceiverRanges;
  }
  const double rangeDelta = ranges[1U] - ranges[0U];
  for (std::size_t index = 2U; index < ranges.size(); ++index) {
    const double expected =
        ranges.front() + static_cast<double>(index) * rangeDelta;
    const double tolerance =
        32.0 * std::numeric_limits<double>::epsilon() *
        std::max({1.0, std::abs(expected), std::abs(ranges[index])});
    if (std::abs(ranges[index] - expected) > tolerance) {
      return FusedScopeFailure::UnequallySpacedReceiverRanges;
    }
  }
  return FusedScopeFailure::None;
}

// Solver-layer scope validation (design §2, defense in depth).  Distinct
// message prefix from the CLI layer so the failing layer is identifiable.
void validateFusedScope(const SimulationCase& simulation) {
  switch (fusedScopeFailure(simulation)) {
    case FusedScopeFailure::None:
      return;
    case FusedScopeFailure::NotTransmissionLoss:
      throw ValidationError(
          "fused ray-reuse solver requires a transmission-loss run mode");
    case FusedScopeFailure::NotCoherent:
      throw ValidationError(
          "fused ray-reuse solver requires the coherent run mode; incoherent "
          "and semi-coherent TL are not supported by the fused ray-reuse "
          "solver");
    case FusedScopeFailure::NotCervenyGaussian:
      throw ValidationError(
          "fused ray-reuse solver requires the Cerveny Gaussian beam family");
    case FusedScopeFailure::NotCartesian:
      throw ValidationError(
          "fused ray-reuse solver requires Cartesian Cerveny coordinates");
    case FusedScopeFailure::NotSingleSource:
      throw ValidationError(
          "fused ray-reuse solver requires exactly one source");
    case FusedScopeFailure::TooFewFrequencies:
      throw ValidationError(
          "fused ray-reuse solver requires at least two frequencies");
    case FusedScopeFailure::IrregularReceivers:
      throw ValidationError(
          "fused ray-reuse solver requires a rectilinear receiver grid");
    case FusedScopeFailure::TooFewReceiverRanges:
      throw ValidationError(
          "fused ray-reuse solver requires at least two receiver ranges");
    case FusedScopeFailure::UnequallySpacedReceiverRanges:
      throw ValidationError(
          "fused ray-reuse solver requires equally spaced receiver ranges");
  }
}

void validateFusedSourceCache(const SimulationCase& simulation,
                              const RayPathCache& sourceCache) {
  if (!sourceCache.frozen()) {
    throw ValidationError(
        "fused ray-reuse solver requires a frozen ray cache");
  }
  const Source& source = simulation.sources().front();
  if (sourceCache.size() > 0U && !sourceCache.at(0U).points.empty() &&
      sourceCache.at(0U).points.front().position.depth != source.depth) {
    // Structural pairing check, same pattern as
    // SingleFrequencySolver::solveFrequencyFromSourceCache.
    throw ValidationError(
        "fused ray-reuse solver requires a ray cache traced from the "
        "requested source");
  }
}

}  // namespace

bool supportsFusedRayReuse(const SimulationCase& simulation) {
  return fusedScopeFailure(simulation) == FusedScopeFailure::None;
}

FusedAccumulationResult FusedRayReuseSolver::accumulateFrequencies(
    const SimulationCase& simulation, const RayPathCache& sourceCache,
    double epsilonMultiplier, double loopRange,
    CartesianCervenySettings influenceSettings,
    FusedRayReuseExecutionSettings executionSettings) {
  validateFusedScope(simulation);
  validateFusedSourceCache(simulation, sourceCache);
  if (executionSettings.requestedRangeWorkers == 0U) {
    throw ValidationError(
        "fused ray-reuse requested range worker count must be positive");
  }

  const Source& source = simulation.sources().front();
  const LaunchFanPlan& launchFan = simulation.launchFanPlan();
  const std::vector<double>& frequencies = simulation.frequencies().values();
  const std::size_t frequencyCount = frequencies.size();
  // Source sample exactly as the CC branch of
  // SingleFrequencySolver::solveFrequencyFromSourceCache computes it.
  const GeometrySspEvaluator soundSpeedProfile(
      simulation.environment().soundSpeedProfile());
  const SoundSpeedSample sourceSample = soundSpeedProfile.evaluate(
      Vec2{.range = 0.0, .depth = source.depth}, 0U);
  const double sourceSoundSpeed = sourceSample.soundSpeed;
  // One long-lived [range][depth][frequency] pressure allocation. Ordinary
  // per-frequency workspaces are materialized only after accumulation.
  FusedPressureWorkspace workspace(simulation.receivers(), frequencyCount);

  const std::size_t rangeCount = simulation.receivers().rangeCount();
  const std::size_t activeWorkerCount =
      std::min(executionSettings.requestedRangeWorkers, rangeCount);
  std::vector<RangeWorkerResult> workerResults(activeWorkerCount);
  std::vector<std::exception_ptr> workerErrors(activeWorkerCount);

  const auto runWorker = [&](std::size_t workerIndex) {
    try {
      const FrequencyProjector projector(simulation.environment());
      const CartesianCervenyInfluence influence(
          simulation.environment(), simulation.receivers(), influenceSettings,
          simulation.beamWidthMode(), simulation.sourceGeometry());
      RangeWorkerResult& result = workerResults[workerIndex];
      std::vector<RayFrequencyState> frequencyStates(frequencyCount);
      std::vector<std::complex<double>> epsilons(frequencyCount);
      const std::size_t quotient = rangeCount / activeWorkerCount;
      const std::size_t remainder = rangeCount % activeWorkerCount;
      const std::size_t rangeBegin =
          workerIndex * quotient + std::min(workerIndex, remainder);
      const std::size_t rangeEnd =
          rangeBegin + quotient + (workerIndex < remainder ? 1U : 0U);
      for (const RayPath& path : sourceCache.paths()) {
        const Clock::time_point projectBegin = Clock::now();
        const double patternAmplitude =
            simulation.sourceBeamPattern().amplitudeForLaunchAngle(
                path.launchAngle);
        const double baseSourceAmplitude = source.amplitude * patternAmplitude;
        for (std::size_t frequencyIndex = 0U;
             frequencyIndex < frequencyCount; ++frequencyIndex) {
          const double projectedSourceAmplitude =
              usesLloydMirror(simulation.runMode())
                  ? semiCoherentProjectedSourceAmplitude(
                        baseSourceAmplitude, frequencies[frequencyIndex],
                        sourceSoundSpeed, source.depth, path.launchAngle)
                  : baseSourceAmplitude;
          if (!std::isfinite(projectedSourceAmplitude) ||
              projectedSourceAmplitude < 0.0) {
            throw ValidationError(
                "source beam pattern produced an invalid projected "
                "amplitude");
          }
          frequencyStates[frequencyIndex] = projector.project(
              path, frequencies[frequencyIndex], projectedSourceAmplitude);
        }
        const Clock::time_point projectEnd = Clock::now();
        for (std::size_t frequencyIndex = 0U;
             frequencyIndex < frequencyCount; ++frequencyIndex) {
          const BeamEpsilon epsilon = pickBeamEpsilon(
              simulation.beamWidthMode(), frequencies[frequencyIndex],
              sourceSoundSpeed, sourceSample.soundSpeedGradient.depth,
              path.launchAngle, launchFan.launchAngleStep, loopRange,
              epsilonMultiplier);
          epsilons[frequencyIndex] = epsilon.value;
        }
        static_cast<void>(influence.accumulateFusedPrevalidated(
            workspace, std::span<const double>(frequencies), path,
            std::span<const RayFrequencyState>(frequencyStates),
            std::span<const std::complex<double>>(epsilons), rangeBegin,
            rangeEnd,
            influenceSettings.collectStatistics
                ? &result.influenceStatistics
                : nullptr));
        const Clock::time_point influenceEnd = Clock::now();
        result.projectSeconds += elapsedSeconds(projectBegin, projectEnd);
        result.influenceSeconds += elapsedSeconds(projectEnd, influenceEnd);
      }
    } catch (...) {
      workerErrors[workerIndex] = std::current_exception();
    }
  };

  if (activeWorkerCount == 1U) {
    runWorker(0U);
  } else {
    std::vector<std::jthread> workers;
    workers.reserve(activeWorkerCount);
    for (std::size_t workerIndex = 0U; workerIndex < activeWorkerCount;
         ++workerIndex) {
      workers.emplace_back(runWorker, workerIndex);
    }
    for (std::jthread& worker : workers) {
      worker.join();
    }
  }
  for (const std::exception_ptr& workerError : workerErrors) {
    if (workerError) {
      std::rethrow_exception(workerError);
    }
  }

  double projectSeconds = 0.0;
  double influenceSeconds = 0.0;
  CartesianCervenyStatistics influenceStatistics;
  for (const RangeWorkerResult& workerResult : workerResults) {
    projectSeconds = std::max(projectSeconds, workerResult.projectSeconds);
    influenceSeconds =
        std::max(influenceSeconds, workerResult.influenceSeconds);
    accumulateCartesianCervenyStatistics(
        influenceStatistics, workerResult.influenceStatistics);
  }
  std::size_t totalRayPointCount = 0U;
  for (const RayPath& path : sourceCache.paths()) {
    totalRayPointCount += path.points.size();
  }

  return FusedAccumulationResult{
      .rawWorkspace = std::move(workspace),
      .timings =
          SingleFrequencyTimings{
              .traceSeconds = 0.0,
              .projectSeconds = projectSeconds,
              .influenceSeconds = influenceSeconds,
              .scaleSeconds = 0.0,
              .influenceStatistics = influenceStatistics},
      .rayCount = sourceCache.size(),
      .totalRayPointCount = totalRayPointCount,
      .rayCacheBytes = sourceCache.memoryFootprintBytes(),
      .requestedRangeWorkers = executionSettings.requestedRangeWorkers,
      .effectiveRangeWorkers = activeWorkerCount};
}

FusedRayReuseStatistics FusedRayReuseSolver::solveStreaming(
    const SimulationCase& simulation, double epsilonMultiplier,
    double loopRange, const RayReuseFrequencyConsumer& consumer,
    CartesianCervenySettings influenceSettings, bool verifyCacheFingerprint,
    FusedRayReuseExecutionSettings executionSettings) {
  if (!consumer) {
    throw ValidationError(
        "fused ray-reuse frequency consumer must be callable");
  }
  validateFusedScope(simulation);

  FusedRayReuseStatistics statistics;
  const Clock::time_point wallBegin = Clock::now();
  // One frozen trace pass over the validated single source; the cache is
  // owned here and handed out as const only (V2-GATE-09, D8).
  const RayFanTraceResult trace =
      SingleFrequencySolver::traceSourceFan(simulation, 0U);

  statistics.tracePassCount = 1U;
  statistics.rayCount = trace.cache.size();
  statistics.totalRayPointCount = trace.totalRayPointCount;
  statistics.rayCacheBytes = trace.cache.memoryFootprintBytes();
  statistics.phaseTotals.traceSeconds += trace.traceSeconds;
  statistics.cacheFingerprintVerified = verifyCacheFingerprint;
  if (verifyCacheFingerprint) {
    statistics.sourceCacheFingerprintsBefore.reserve(1U);
    statistics.sourceCacheFingerprintsBefore.push_back(
        trace.cache.contentFingerprint());
    statistics.cacheFingerprintBefore =
        statistics.sourceCacheFingerprintsBefore.front();
  }

  FusedAccumulationResult accumulated = accumulateFrequencies(
      simulation, trace.cache, epsilonMultiplier, loopRange, influenceSettings,
      executionSettings);
  statistics.requestedRangeWorkers = accumulated.requestedRangeWorkers;
  statistics.effectiveRangeWorkers = accumulated.effectiveRangeWorkers;
  statistics.phaseTotals.projectSeconds += accumulated.timings.projectSeconds;
  statistics.phaseTotals.influenceSeconds +=
      accumulated.timings.influenceSeconds;
  accumulateCartesianCervenyStatistics(
      statistics.phaseTotals.influenceStatistics,
      accumulated.timings.influenceStatistics);

  // Scale phase: per frequency in index order, with the source sound speed
  // computed exactly as solveFrequencyFromSourceCache computes it.
  const Source& source = simulation.sources().front();
  const GeometrySspEvaluator soundSpeedProfile(
      simulation.environment().soundSpeedProfile());
  const SoundSpeedSample sourceSample = soundSpeedProfile.evaluate(
      Vec2{.range = 0.0, .depth = source.depth}, 0U);
  const double sourceSoundSpeed = sourceSample.soundSpeed;
  const LaunchFanPlan& launchFan = simulation.launchFanPlan();
  const std::size_t frequencyCount = simulation.frequencies().size();
  const std::vector<double>& frequencies = simulation.frequencies().values();
  for (std::size_t frequencyIndex = 0U; frequencyIndex < frequencyCount;
       ++frequencyIndex) {
    const Clock::time_point scaleBegin = Clock::now();
    FrequencyWorkspace frequencyWorkspace =
        accumulated.rawWorkspace.materializeFrequency(
            frequencyIndex, frequencies[frequencyIndex],
            simulation.receivers());
    scaleCoherentCartesianPressure(
        frequencyWorkspace, simulation.receivers(), launchFan.launchAngleStep,
        sourceSoundSpeed, simulation.sourceGeometry());
    const double scaleSeconds = elapsedSeconds(scaleBegin, Clock::now());
    statistics.phaseTotals.scaleSeconds += scaleSeconds;
    // Per-frequency timings delivered to the consumer carry only that
    // frequency's scale time; block-level Project/Influence totals are not
    // fabricated per frequency (D11).
    SingleFrequencyTimings frequencyTimings;
    frequencyTimings.scaleSeconds = scaleSeconds;
    std::vector<FrequencyWorkspace> sourceWorkspaces;
    sourceWorkspaces.reserve(1U);
    sourceWorkspaces.push_back(std::move(frequencyWorkspace));
    consumer(frequencyIndex, std::move(sourceWorkspaces), frequencyTimings);
  }

  if (verifyCacheFingerprint) {
    statistics.sourceCacheFingerprintsAfter.reserve(1U);
    statistics.sourceCacheFingerprintsAfter.push_back(
        trace.cache.contentFingerprint());
    statistics.cacheFingerprintAfter =
        statistics.sourceCacheFingerprintsAfter.front();
    if (statistics.sourceCacheFingerprintsAfter !=
        statistics.sourceCacheFingerprintsBefore) {
      throw ValidationError("fused ray-reuse modified the frozen ray cache");
    }
  }
  statistics.wallSeconds = elapsedSeconds(wallBegin, Clock::now());
  return statistics;
}

}  // namespace rayreuse
