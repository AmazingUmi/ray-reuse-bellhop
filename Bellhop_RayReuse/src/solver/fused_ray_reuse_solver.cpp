#include "rayreuse/solver/fused_ray_reuse_solver.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <limits>
#include <optional>
#include <span>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "fused_influence_adapters.hpp"
#include "rayreuse/error.hpp"
#include "rayreuse/field/frequency_projector.hpp"
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
  ArrivalAccumulationStatistics arrivalStatistics;
};

enum class FusedScopeFailure {
  None,
  NotTransmissionLoss,
  // Run mode is not legal for the beam family (design §9 family legality:
  // fused eligibility is always a subset of the legal beam x run-mode
  // support matrix). Live since A06 for the coherent-only SimpleGaussian
  // family: every Cerveny/Hat/Gaussian TL mode is legal, so only Simple
  // Gaussian outside coherent can ever reach it — and SimulationCase
  // construction already rejects that combination
  // (simulation_case.cpp:404-409, the legal-matrix enforcement upstream of
  // every solver layer), which keeps this gate arm defense in depth. The
  // reachable enforcement of the same law is the intensity public entry,
  // which rejects the family before any adapter can be selected.
  RunModeIllegalForFamily,
  UnsupportedBeamFamily,
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
  // Both Cerveny coordinate systems are in fused scope since A03, the
  // Geometric Hat family (both coordinates) since A04, the Geometric
  // Gaussian family (Cartesian only) since A05, and the Simple Gaussian
  // family (coherent only) since A06 (design §9);
  // SimulationCase construction validates the closed coordinate enum, so
  // the coordinate dimension needs no rejection — the Hat kernel owns the
  // internal traversal selection.
  if (simulation.beamFamily() != BeamFamily::CervenyGaussian &&
      simulation.beamFamily() != BeamFamily::GeometricHat &&
      simulation.beamFamily() != BeamFamily::GeometricGaussian &&
      simulation.beamFamily() != BeamFamily::SimpleGaussian) {
    return FusedScopeFailure::UnsupportedBeamFamily;
  }
  // Family legality (design §9): Simple Gaussian's ONLY legal TL run mode
  // is coherent. SimulationCase construction rejects every non-coherent
  // Simple Gaussian run, so publicly this arm is defense in depth; the
  // intensity public entry carries the reachable enforcement.
  if (simulation.beamFamily() == BeamFamily::SimpleGaussian &&
      simulation.runMode() != SimulationRunMode::Coherent) {
    return FusedScopeFailure::RunModeIllegalForFamily;
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
    case FusedScopeFailure::RunModeIllegalForFamily:
      throw ValidationError(
          "fused ray-reuse solver requires a run mode that is legal for the "
          "beam family");
    case FusedScopeFailure::UnsupportedBeamFamily:
      throw ValidationError(
          "fused ray-reuse solver requires the Cerveny Gaussian, geometric "
          "hat, geometric Gaussian, or simple Gaussian beam family");
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
                              const RayPathCache& sourceCache,
                              std::size_t sourceIndex) {
  if (!sourceCache.frozen()) {
    throw ValidationError(
        "fused ray-reuse solver requires a frozen ray cache");
  }
  const Source& source = simulation.sources().at(sourceIndex);
  if (sourceCache.size() > 0U && !sourceCache.at(0U).points.empty() &&
      sourceCache.at(0U).points.front().position.depth != source.depth) {
    // Structural pairing check, same pattern as
    // SingleFrequencySolver::solveFrequencyFromSourceCache.
    throw ValidationError(
        "fused ray-reuse solver requires a ray cache traced from the "
        "requested source");
  }
}

void validateFusedArrivalScope(const SimulationCase& simulation,
                               std::size_t sourceIndex) {
  if (simulation.runMode() != SimulationRunMode::AsciiArrivals &&
      simulation.runMode() != SimulationRunMode::BinaryArrivals) {
    throw ValidationError(
        "fused arrival accumulation requires ASCII or binary arrivals mode");
  }
  if (simulation.beamFamily() != BeamFamily::GeometricHat &&
      simulation.beamFamily() != BeamFamily::GeometricGaussian) {
    throw ValidationError(
        "fused arrival accumulation supports only geometric hat and "
        "geometric Gaussian beam families");
  }
  if (sourceIndex >= simulation.sourceCount()) {
    throw ValidationError("fused arrival source index is out of range");
  }
  if (simulation.frequencies().size() < 2U) {
    throw ValidationError(
        "fused arrival accumulation requires at least two frequencies");
  }
  if (simulation.receivers().isIrregular()) {
    throw ValidationError(
        "fused arrival accumulation requires a rectilinear receiver grid");
  }
  const std::vector<double>& ranges = simulation.receivers().ranges();
  if (ranges.size() < 2U) {
    throw ValidationError(
        "fused arrival accumulation requires at least two receiver ranges");
  }
  const double rangeDelta = ranges[1U] - ranges[0U];
  for (std::size_t index = 2U; index < ranges.size(); ++index) {
    const double expected =
        ranges.front() + static_cast<double>(index) * rangeDelta;
    const double tolerance =
        32.0 * std::numeric_limits<double>::epsilon() *
        std::max({1.0, std::abs(expected), std::abs(ranges[index])});
    if (std::abs(ranges[index] - expected) > tolerance) {
      throw ValidationError(
          "fused arrival accumulation requires equally spaced receiver "
          "ranges");
    }
  }
}

}  // namespace

bool supportsFusedRayReuse(const SimulationCase& simulation) {
  return fusedScopeFailure(simulation) == FusedScopeFailure::None;
}

// Unified fused executor (design §3.1-§3.2), migrated in A02 from the former
// CC-specialized accumulateFrequencies body by pure code motion: worker loop,
// partition math, join/rethrow, and timing join are verbatim; the CC-specific
// pieces are the Adapter hooks (kernel ctor, epsilon prep, fused accumulation
// forwarding, post-scale) and the Sink policy (workspace construction, result
// assembly) frozen in A01. Since A02b the scope gate accepts every TL run
// mode of Cerveny; since A03 both Cerveny coordinate systems dispatch here
// (Cartesian and Ray-Centered adapters); since A04 the Geometric Hat family
// (both coordinates, one adapter — the kernel owns the traversal selection)
// and since A05 the Geometric Gaussian family (Cartesian) join the fused
// set; the remaining family tasks widen the beam-family dimension further.
template <typename Adapter, typename Sink>
typename Sink::Result FusedRayReuseSolver::accumulateFrequenciesImpl(
    const SimulationCase& simulation, const RayPathCache& sourceCache,
    double epsilonMultiplier, double loopRange,
    CartesianCervenySettings influenceSettings,
    FusedRayReuseExecutionSettings executionSettings,
    std::size_t sourceIndex) {
  if constexpr (std::is_same_v<Sink, ArrivalFusedSink>) {
    validateFusedArrivalScope(simulation, sourceIndex);
  } else {
    validateFusedScope(simulation);
  }
  validateFusedSourceCache(simulation, sourceCache, sourceIndex);
  if (executionSettings.requestedRangeWorkers == 0U) {
    throw ValidationError(
        "fused ray-reuse requested range worker count must be positive");
  }

  const Source& source = simulation.sources().at(sourceIndex);
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
  // One long-lived [range][depth][frequency] allocation per run, selected by
  // the sink policy. Ordinary per-frequency workspaces are materialized only
  // after accumulation.
  typename Sink::Workspace workspace =
      Sink::makeWorkspace(simulation.receivers(),
                          std::span<const double>(frequencies));

  const std::size_t rangeCount = simulation.receivers().rangeCount();
  const std::size_t activeWorkerCount =
      std::min(executionSettings.requestedRangeWorkers, rangeCount);
  std::vector<RangeWorkerResult> workerResults(activeWorkerCount);
  std::vector<std::exception_ptr> workerErrors(activeWorkerCount);

  // Loop-invariant input set of the per-ray family prep hook (design §4).
  const typename Adapter::PerRayContext context{
      simulation.beamWidthMode(), sourceSoundSpeed,
      sourceSample.soundSpeedGradient.depth,
      launchFan.launchAngleStep, loopRange, epsilonMultiplier};

  const auto runWorker = [&](std::size_t workerIndex) {
    try {
      const FrequencyProjector projector(simulation.environment());
      const typename Adapter::Kernel kernel =
          Adapter::makeKernel(simulation, influenceSettings);
      RangeWorkerResult& result = workerResults[workerIndex];
      std::vector<RayFrequencyState> frequencyStates(frequencyCount);
      typename Adapter::PerRayScratch scratch;
      Adapter::prepareScratch(scratch, frequencyCount);
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
        Adapter::preparePerRay(context, scratch, path,
                               std::span<const double>(frequencies));
        static_cast<void>(Sink::template accumulate<Adapter>(
            kernel, scratch, workspace, std::span<const double>(frequencies),
            path, std::span<const RayFrequencyState>(frequencyStates),
            rangeBegin, rangeEnd,
            influenceSettings.collectStatistics
                ? &result.influenceStatistics
                : nullptr,
            &result.arrivalStatistics));
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
  ArrivalAccumulationStatistics arrivalStatistics;
  for (const RangeWorkerResult& workerResult : workerResults) {
    projectSeconds = std::max(projectSeconds, workerResult.projectSeconds);
    influenceSeconds =
        std::max(influenceSeconds, workerResult.influenceSeconds);
    accumulateCartesianCervenyStatistics(
        influenceStatistics, workerResult.influenceStatistics);
    mergeArrivalAccumulationStatistics(arrivalStatistics,
                                       workerResult.arrivalStatistics);
  }
  std::size_t totalRayPointCount = 0U;
  for (const RayPath& path : sourceCache.paths()) {
    totalRayPointCount += path.points.size();
  }

  return Sink::makeResult(
      std::move(workspace),
      SingleFrequencyTimings{
          .traceSeconds = 0.0,
          .projectSeconds = projectSeconds,
          .influenceSeconds = influenceSeconds,
          .scaleSeconds = 0.0,
          .influenceStatistics = influenceStatistics},
      sourceCache.size(), totalRayPointCount,
      sourceCache.memoryFootprintBytes(),
      executionSettings.requestedRangeWorkers, activeWorkerCount,
      arrivalStatistics);
}

FusedAccumulationResult FusedRayReuseSolver::accumulateFrequencies(
    const SimulationCase& simulation, const RayPathCache& sourceCache,
    double epsilonMultiplier, double loopRange,
    CartesianCervenySettings influenceSettings,
    FusedRayReuseExecutionSettings executionSettings) {
  // A02/A03/A04/A05/A06 dispatchers (design §3.3): the unified executor owns
  // validation and the worker loop; the scope gate covers all TL run modes
  // of Cerveny (both coordinate systems), Geometric Hat, and Geometric
  // Gaussian, plus the coherent-only Simple Gaussian family (design §9);
  // the beam family selects the compile-time adapter, and within the
  // Cerveny family the coordinate system does (the closed enums are
  // validated by SimulationCase construction — the Hat kernel owns its
  // internal coordinate routing). The sink policy selects the complex
  // pressure payload.
  if (simulation.beamFamily() == BeamFamily::GeometricHat) {
    return accumulateFrequenciesImpl<GeometricHatFusedAdapter,
                                     CoherentFusedSink>(
        simulation, sourceCache, epsilonMultiplier, loopRange,
        influenceSettings, executionSettings);
  }
  if (simulation.beamFamily() == BeamFamily::GeometricGaussian) {
    return accumulateFrequenciesImpl<GeometricGaussianFusedAdapter,
                                     CoherentFusedSink>(
        simulation, sourceCache, epsilonMultiplier, loopRange,
        influenceSettings, executionSettings);
  }
  if (simulation.beamFamily() == BeamFamily::SimpleGaussian) {
    // SimulationCase construction guarantees the coherent run mode here
    // (the family gate's family-legality arm is the defense-in-depth
    // backstop), so the coherent sink is the family's only instantiation.
    return accumulateFrequenciesImpl<SimpleGaussianFusedAdapter,
                                     CoherentFusedSink>(
        simulation, sourceCache, epsilonMultiplier, loopRange,
        influenceSettings, executionSettings);
  }
  if (simulation.cervenyCoordinateSystem() ==
      CervenyCoordinateSystem::RayCentered) {
    return accumulateFrequenciesImpl<RayCenteredCervenyFusedAdapter,
                                     CoherentFusedSink>(
        simulation, sourceCache, epsilonMultiplier, loopRange,
        influenceSettings, executionSettings);
  }
  return accumulateFrequenciesImpl<CartesianCervenyFusedAdapter,
                                   CoherentFusedSink>(
      simulation, sourceCache, epsilonMultiplier, loopRange, influenceSettings,
      executionSettings);
}

FusedIntensityAccumulationResult
FusedRayReuseSolver::accumulateFrequenciesIntensity(
    const SimulationCase& simulation, const RayPathCache& sourceCache,
    double epsilonMultiplier, double loopRange,
    CartesianCervenySettings influenceSettings,
    FusedRayReuseExecutionSettings executionSettings) {
  // Family legality, reachable enforcement (design §9, live since A06): the
  // intensity sink exists only for families with legal incoherent /
  // semi-coherent modes. Simple Gaussian is coherent-only — its adapter
  // defines no intensity hooks (compile-time absence, design §4), so this
  // entry rejects the family BEFORE any adapter can be selected; without
  // this check the dispatch below would silently fall through to the
  // Cerveny adapters. SimulationCase construction rejects every
  // non-coherent Simple Gaussian run (simulation_case.cpp:404-409), so a
  // constructible Simple Gaussian case is always coherent and the gate's
  // RunModeIllegalForFamily arm cannot fire on this path — this entry-level
  // check states the same law where it is observable.
  if (simulation.beamFamily() == BeamFamily::SimpleGaussian) {
    throw ValidationError(
        "fused ray-reuse solver requires a run mode that is legal for the "
        "beam family");
  }
  // A02b/A03/A04/A05 sink dispatch (design §3.3/§6.2): the intensity sink
  // routes through the same executor and the family-selected kernel; the raw
  // payload is the double-lane FusedIntensityWorkspace of the result.
  if (simulation.beamFamily() == BeamFamily::GeometricHat) {
    return accumulateFrequenciesImpl<GeometricHatFusedAdapter,
                                     IntensityFusedSink>(
        simulation, sourceCache, epsilonMultiplier, loopRange,
        influenceSettings, executionSettings);
  }
  if (simulation.beamFamily() == BeamFamily::GeometricGaussian) {
    return accumulateFrequenciesImpl<GeometricGaussianFusedAdapter,
                                     IntensityFusedSink>(
        simulation, sourceCache, epsilonMultiplier, loopRange,
        influenceSettings, executionSettings);
  }
  if (simulation.cervenyCoordinateSystem() ==
      CervenyCoordinateSystem::RayCentered) {
    return accumulateFrequenciesImpl<RayCenteredCervenyFusedAdapter,
                                     IntensityFusedSink>(
        simulation, sourceCache, epsilonMultiplier, loopRange,
        influenceSettings, executionSettings);
  }
  return accumulateFrequenciesImpl<CartesianCervenyFusedAdapter,
                                   IntensityFusedSink>(
      simulation, sourceCache, epsilonMultiplier, loopRange, influenceSettings,
      executionSettings);
}

FusedArrivalAccumulationResult
FusedRayReuseSolver::accumulateArrivalFrequencies(
    const SimulationCase& simulation, const RayPathCache& sourceCache,
    std::size_t sourceIndex, CartesianCervenySettings influenceSettings,
    FusedRayReuseExecutionSettings executionSettings) {
  if (simulation.beamFamily() == BeamFamily::GeometricHat) {
    return accumulateFrequenciesImpl<GeometricHatFusedAdapter,
                                     ArrivalFusedSink>(
        simulation, sourceCache, 1.0, 0.0, influenceSettings,
        executionSettings, sourceIndex);
  }
  return accumulateFrequenciesImpl<GeometricGaussianFusedAdapter,
                                   ArrivalFusedSink>(
      simulation, sourceCache, 1.0, 0.0, influenceSettings,
      executionSettings, sourceIndex);
}

ArrivalSolverStatistics FusedRayReuseSolver::solveArrivalStreaming(
    const SimulationCase& simulation,
    const FusedArrivalSourceConsumer& consumer,
    CartesianCervenySettings influenceSettings, bool verifyCacheFingerprint,
    FusedRayReuseExecutionSettings executionSettings) {
  if (!consumer) {
    throw ValidationError("fused arrival source consumer must be callable");
  }
  validateFusedArrivalScope(simulation, 0U);

  ArrivalSolverStatistics statistics;
  statistics.frequencyCount = simulation.frequencies().size();
  statistics.cacheFingerprintVerified = verifyCacheFingerprint;
  if (verifyCacheFingerprint) {
    statistics.sourceCacheFingerprintsBefore.reserve(simulation.sourceCount());
    statistics.sourceCacheFingerprintsAfter.reserve(simulation.sourceCount());
  }

  for (std::size_t sourceIndex = 0U;
       sourceIndex < simulation.sourceCount(); ++sourceIndex) {
    // Deliberately source-local: neither frozen caches nor all-frequency
    // arrival lanes accumulate across sources.
    const RayFanTraceResult trace =
        SingleFrequencySolver::traceSourceFan(simulation, sourceIndex);
    statistics.traceSeconds += trace.traceSeconds;
    statistics.rayCount += trace.cache.size();
    statistics.totalRayPointCount += trace.totalRayPointCount;
    statistics.peakRayCacheBytes =
        std::max(statistics.peakRayCacheBytes,
                 trace.cache.memoryFootprintBytes());

    std::uint64_t fingerprintBefore = 0U;
    if (verifyCacheFingerprint) {
      fingerprintBefore = trace.cache.contentFingerprint();
      statistics.sourceCacheFingerprintsBefore.push_back(fingerprintBefore);
    }

    FusedArrivalAccumulationResult accumulated =
        accumulateArrivalFrequencies(simulation, trace.cache, sourceIndex,
                                     influenceSettings, executionSettings);
    statistics.projectSeconds += accumulated.timings.projectSeconds;
    statistics.influenceSeconds += accumulated.timings.influenceSeconds;
    statistics.projectedRayCount +=
        accumulated.rayCount * simulation.frequencies().size();
    statistics.candidateCount +=
        accumulated.arrivalStatistics.candidateCount;
    statistics.saturatedCellCount +=
        accumulated.arrivalStatistics.saturatedCellCount;
    statistics.peakArrivalWorkspaceBytes =
        std::max(statistics.peakArrivalWorkspaceBytes,
                 accumulated.rawWorkspace.storageStatistics()
                     .memoryFootprintBytes);

    const Clock::time_point consumeBegin = Clock::now();
    consumer(sourceIndex, accumulated.rawWorkspace);
    statistics.consumeSeconds += elapsedSeconds(consumeBegin, Clock::now());

    if (verifyCacheFingerprint) {
      const std::uint64_t fingerprintAfter =
          trace.cache.contentFingerprint();
      statistics.sourceCacheFingerprintsAfter.push_back(fingerprintAfter);
      if (fingerprintAfter != fingerprintBefore) {
        throw ValidationError(
            "fused arrival projection modified the frozen ray cache");
      }
    }
  }

  if (verifyCacheFingerprint) {
    statistics.cacheFingerprintBefore =
        statistics.sourceCacheFingerprintsBefore.front();
    statistics.cacheFingerprintAfter =
        statistics.sourceCacheFingerprintsAfter.front();
  }
  return statistics;
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

  // The run mode selects the sink once per run (design §3.3/§6.2): coherent
  // keeps the existing pressure chain; incoherent/semi-coherent accumulate
  // real intensity lanes through accumulateFrequenciesIntensity and convert
  // to pressure representation per frequency before delivery, so the consumer
  // receives a FrequencyWorkspace in every mode (legacy continuity:
  // single_frequency_solver.cpp:356-369).
  const bool coherentRunMode =
      simulation.runMode() == SimulationRunMode::Coherent;
  std::optional<FusedAccumulationResult> accumulated;
  std::optional<FusedIntensityAccumulationResult> accumulatedIntensity;
  if (coherentRunMode) {
    accumulated = accumulateFrequencies(
        simulation, trace.cache, epsilonMultiplier, loopRange,
        influenceSettings, executionSettings);
  } else {
    accumulatedIntensity = accumulateFrequenciesIntensity(
        simulation, trace.cache, epsilonMultiplier, loopRange,
        influenceSettings, executionSettings);
  }
  const auto absorbAccumulationStatistics = [&](const auto& result) {
    statistics.requestedRangeWorkers = result.requestedRangeWorkers;
    statistics.effectiveRangeWorkers = result.effectiveRangeWorkers;
    statistics.phaseTotals.projectSeconds += result.timings.projectSeconds;
    statistics.phaseTotals.influenceSeconds += result.timings.influenceSeconds;
    accumulateCartesianCervenyStatistics(
        statistics.phaseTotals.influenceStatistics,
        result.timings.influenceStatistics);
  };
  if (coherentRunMode) {
    absorbAccumulationStatistics(*accumulated);
  } else {
    absorbAccumulationStatistics(*accumulatedIntensity);
  }

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
    FrequencyWorkspace frequencyWorkspace = [&] {
      const bool hatFamily =
          simulation.beamFamily() == BeamFamily::GeometricHat;
      const bool gaussianFamily =
          simulation.beamFamily() == BeamFamily::GeometricGaussian;
      const bool simpleGaussianFamily =
          simulation.beamFamily() == BeamFamily::SimpleGaussian;
      const bool rayCenteredRun =
          simulation.cervenyCoordinateSystem() ==
          CervenyCoordinateSystem::RayCentered;
      if (coherentRunMode) {
        FrequencyWorkspace coherentWorkspace =
            accumulated->rawWorkspace.materializeFrequency(
                frequencyIndex, frequencies[frequencyIndex],
                simulation.receivers());
        // Adapter::scaleFrequency (design §6.1) with the family-selected
        // adapter; both Cerveny adapters apply the same family-based
        // scaleCoherentCartesianPressure call, the Hat adapter (either
        // coordinate), the Geometric Gaussian adapter, and the coherent-only
        // Simple Gaussian adapter (A06) apply
        // scaleCoherentGeometricPressure, with identical arguments.
        if (hatFamily) {
          GeometricHatFusedAdapter::scaleFrequency(
              coherentWorkspace, simulation.receivers(),
              launchFan.launchAngleStep, sourceSoundSpeed,
              simulation.sourceGeometry());
        } else if (gaussianFamily) {
          GeometricGaussianFusedAdapter::scaleFrequency(
              coherentWorkspace, simulation.receivers(),
              launchFan.launchAngleStep, sourceSoundSpeed,
              simulation.sourceGeometry());
        } else if (simpleGaussianFamily) {
          SimpleGaussianFusedAdapter::scaleFrequency(
              coherentWorkspace, simulation.receivers(),
              launchFan.launchAngleStep, sourceSoundSpeed,
              simulation.sourceGeometry());
        } else if (rayCenteredRun) {
          RayCenteredCervenyFusedAdapter::scaleFrequency(
              coherentWorkspace, simulation.receivers(),
              launchFan.launchAngleStep, sourceSoundSpeed,
              simulation.sourceGeometry());
        } else {
          CartesianCervenyFusedAdapter::scaleFrequency(
              coherentWorkspace, simulation.receivers(),
              launchFan.launchAngleStep, sourceSoundSpeed,
              simulation.sourceGeometry());
        }
        return coherentWorkspace;
      }
      // I/S sink chain (design §6.2): bitwise double-lane materialization,
      // then the family intensity-to-pressure conversion — the same calls
      // and arguments as the legacy reuse post-scale
      // (single_frequency_solver.cpp:356-369), selected by beam family:
      // Cerveny in both coordinate systems, Geometric Hat in both,
      // Geometric Gaussian.
      const IntensityWorkspace intensityWorkspace =
          accumulatedIntensity->rawIntensityWorkspace
              .materializeIntensityFrequency(
                  frequencyIndex, frequencies[frequencyIndex],
                  simulation.receivers());
      if (hatFamily) {
        return GeometricHatFusedAdapter::scaleIntensityFrequency(
            intensityWorkspace, simulation.receivers(),
            launchFan.launchAngleStep, sourceSoundSpeed,
            simulation.sourceGeometry());
      }
      if (gaussianFamily) {
        return GeometricGaussianFusedAdapter::scaleIntensityFrequency(
            intensityWorkspace, simulation.receivers(),
            launchFan.launchAngleStep, sourceSoundSpeed,
            simulation.sourceGeometry());
      }
      if (rayCenteredRun) {
        return RayCenteredCervenyFusedAdapter::scaleIntensityFrequency(
            intensityWorkspace, simulation.receivers(),
            launchFan.launchAngleStep, sourceSoundSpeed,
            simulation.sourceGeometry());
      }
      return CartesianCervenyFusedAdapter::scaleIntensityFrequency(
          intensityWorkspace, simulation.receivers(),
          launchFan.launchAngleStep, sourceSoundSpeed,
          simulation.sourceGeometry());
    }();
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
