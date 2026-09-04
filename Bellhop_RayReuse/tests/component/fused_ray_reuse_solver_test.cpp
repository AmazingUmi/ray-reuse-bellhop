#include "rayreuse/solver/fused_ray_reuse_solver.hpp"

#include <algorithm>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <numbers>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "rayreuse/error.hpp"
#include "rayreuse/model/simulation_case.hpp"
#include "rayreuse/solver/serial_ray_reuse_solver.hpp"
#include "rayreuse/solver/single_frequency_solver.hpp"
#include "support/test_harness.hpp"

namespace {

using rayreuse::BeamFamily;
using rayreuse::BeamWidthMode;
using rayreuse::BoundaryCurvatureMode;
using rayreuse::BoundaryModel;
using rayreuse::CartesianCervenySettings;
using rayreuse::CervenyCoordinateSystem;
using rayreuse::Environment;
using rayreuse::FieldComponent;
using rayreuse::FrequencyGrid;
using rayreuse::FrequencyWorkspace;
using rayreuse::FusedRayReuseExecutionSettings;
using rayreuse::FusedRayReuseSolver;
using rayreuse::FusedRayReuseStatistics;
using rayreuse::IntegratorSettings;
using rayreuse::LaunchFan;
using rayreuse::ReceiverGrid;
using rayreuse::ReceiverGridLayout;
using rayreuse::SerialRayReuseResult;
using rayreuse::SerialRayReuseSolver;
using rayreuse::SimulationCase;
using rayreuse::SimulationRunMode;
using rayreuse::SingleFrequencyTimings;
using rayreuse::SoundSpeedPoint;
using rayreuse::SoundSpeedProfile;
using rayreuse::Source;
using rayreuse::SourceBeamPattern;
using rayreuse::ValidationError;
using rayreuse::supportsFusedRayReuse;
using rayreuse::test::Context;

// Small in-scope fused fixture: CC coherent, Cartesian, single source,
// rectilinear uniform-range grid, two frequencies.
SimulationCase makeSimulation(
    SimulationRunMode runMode = SimulationRunMode::Coherent,
    BeamFamily beamFamily = BeamFamily::CervenyGaussian,
    CervenyCoordinateSystem coordinates = CervenyCoordinateSystem::Cartesian,
    FrequencyGrid frequencies = FrequencyGrid({50.0, 100.0}),
    ReceiverGrid receivers = ReceiverGrid({25.0, 50.0, 75.0},
                                          {10.0, 55.0, 100.0})) {
  return SimulationCase(
      Environment(
          SoundSpeedProfile(
              {SoundSpeedPoint{
                   .depth = 0.0, .soundSpeed = 1500.0, .density = 1000.0},
               SoundSpeedPoint{
                   .depth = 100.0, .soundSpeed = 1500.0, .density = 1000.0}}),
          BoundaryModel::vacuum(0.0), BoundaryModel::rigid(100.0)),
      Source{.depth = 50.0, .amplitude = 1.0}, receivers, frequencies,
      LaunchFan{.minimumAngle = -2.0 * std::numbers::pi / 180.0,
                .maximumAngle = 2.0 * std::numbers::pi / 180.0,
                .explicitLaunchAngleCount = 300U},
      IntegratorSettings{.stepLength = 10.0,
                         .rangeLimit = 110.0,
                         .depthLimit = 110.0,
                         .maximumRayPoints = 100U},
      SourceBeamPattern::omnidirectional(), runMode, beamFamily,
      FieldComponent::Pressure, BoundaryCurvatureMode::Standard,
      BeamWidthMode::MinimumWidth, coordinates);
}

template <typename Function>
[[nodiscard]] std::optional<std::string> capturedValidationMessage(
    Function&& function) {
  try {
    function();
  } catch (const ValidationError& error) {
    return std::string(error.what());
  }
  return std::nullopt;
}

void noOpConsumer(std::size_t, std::vector<FrequencyWorkspace>&&,
                  const SingleFrequencyTimings&) {}

void testSolverScopeRejections(Context& context) {
  context.check(supportsFusedRayReuse(makeSimulation()),
                "the shared fused-support predicate accepts the production "
                "fixture");
  // IGR-3A A02b (intended behavior change): Cartesian Cerveny fused
  // eligibility covers every TL run mode of the family — incoherent and
  // semi-coherent are accepted, not rejected.
  context.check(
      supportsFusedRayReuse(makeSimulation(SimulationRunMode::Incoherent)) &&
          supportsFusedRayReuse(
              makeSimulation(SimulationRunMode::SemiCoherent)),
      "the shared fused-support predicate accepts CC incoherent and "
      "semi-coherent TL");
  // IGR-3A A03 (intended behavior change): Ray-Centered Cerveny fused
  // eligibility covers every TL run mode of the family in both coordinate
  // systems (design §9).
  context.check(
      supportsFusedRayReuse(makeSimulation(
          SimulationRunMode::Coherent, BeamFamily::CervenyGaussian,
          CervenyCoordinateSystem::RayCentered)) &&
          supportsFusedRayReuse(
              makeSimulation(SimulationRunMode::Incoherent,
                             BeamFamily::CervenyGaussian,
                             CervenyCoordinateSystem::RayCentered)) &&
          supportsFusedRayReuse(
              makeSimulation(SimulationRunMode::SemiCoherent,
                             BeamFamily::CervenyGaussian,
                             CervenyCoordinateSystem::RayCentered)),
      "the shared fused-support predicate accepts Ray-Centered Cerveny "
      "coherent, incoherent, and semi-coherent TL");
  // End-to-end wiring for one Ray-Centered mode (numerical parity itself is
  // gated in rayreuse.component.fused_rc_parity): the fused streaming path
  // must deliver every frequency through the RC adapter chain.
  {
    const SimulationCase rayCenteredSimulation = makeSimulation(
        SimulationRunMode::Coherent, BeamFamily::CervenyGaussian,
        CervenyCoordinateSystem::RayCentered);
    std::size_t rayCenteredCallbackCount = 0U;
    const FusedRayReuseStatistics rayCenteredStatistics =
        FusedRayReuseSolver::solveStreaming(
            rayCenteredSimulation, 1.0, 50.0,
            [&rayCenteredCallbackCount, &context](
                std::size_t,
                std::vector<FrequencyWorkspace>&& sourceWorkspaces,
                const SingleFrequencyTimings&) {
              ++rayCenteredCallbackCount;
              context.check(sourceWorkspaces.size() == 1U,
                            "ray-centered fused streaming delivers one source "
                            "workspace per frequency");
            },
            CartesianCervenySettings{}, true);
    context.check(
        rayCenteredCallbackCount ==
                rayCenteredSimulation.frequencies().size() &&
            rayCenteredStatistics.cacheFingerprintVerified &&
            rayCenteredStatistics.cacheFingerprintBefore ==
                rayCenteredStatistics.cacheFingerprintAfter,
        "ray-centered fused streaming delivers every frequency and leaves "
        "the frozen cache unchanged");
  }
  // IGR-3A A04 (intended behavior change): Geometric Hat fused eligibility
  // covers every TL run mode of the family in BOTH coordinate systems
  // (design §9) — one adapter; the kernel owns the internal Cartesian /
  // ray-centered traversal selection.
  context.check(
      supportsFusedRayReuse(makeSimulation(SimulationRunMode::Coherent,
                                           BeamFamily::GeometricHat)) &&
          supportsFusedRayReuse(makeSimulation(
              SimulationRunMode::Incoherent, BeamFamily::GeometricHat)) &&
          supportsFusedRayReuse(makeSimulation(
              SimulationRunMode::SemiCoherent, BeamFamily::GeometricHat)) &&
          supportsFusedRayReuse(makeSimulation(
              SimulationRunMode::Coherent, BeamFamily::GeometricHat,
              CervenyCoordinateSystem::RayCentered)) &&
          supportsFusedRayReuse(makeSimulation(
              SimulationRunMode::Incoherent, BeamFamily::GeometricHat,
              CervenyCoordinateSystem::RayCentered)) &&
          supportsFusedRayReuse(makeSimulation(
              SimulationRunMode::SemiCoherent, BeamFamily::GeometricHat,
              CervenyCoordinateSystem::RayCentered)),
      "the shared fused-support predicate accepts geometric hat coherent, "
      "incoherent, and semi-coherent TL in both coordinate systems");
  // End-to-end wiring for one Hat mode per coordinate (numerical parity
  // itself is gated in rayreuse.component.fused_hat_parity).
  for (const CervenyCoordinateSystem hatCoordinates :
       {CervenyCoordinateSystem::Cartesian,
        CervenyCoordinateSystem::RayCentered}) {
    const SimulationCase hatSimulation = makeSimulation(
        SimulationRunMode::Coherent, BeamFamily::GeometricHat,
        hatCoordinates);
    std::size_t hatCallbackCount = 0U;
    const FusedRayReuseStatistics hatStatistics =
        FusedRayReuseSolver::solveStreaming(
            hatSimulation, 1.0, 50.0,
            [&hatCallbackCount, &context](
                std::size_t,
                std::vector<FrequencyWorkspace>&& sourceWorkspaces,
                const SingleFrequencyTimings&) {
              ++hatCallbackCount;
              context.check(sourceWorkspaces.size() == 1U,
                            "geometric hat fused streaming delivers one "
                            "source workspace per frequency");
            },
            CartesianCervenySettings{}, true);
    context.check(
        hatCallbackCount == hatSimulation.frequencies().size() &&
            hatStatistics.cacheFingerprintVerified &&
            hatStatistics.cacheFingerprintBefore ==
                hatStatistics.cacheFingerprintAfter,
        "geometric hat fused streaming delivers every frequency and leaves "
        "the frozen cache unchanged");
  }
  // IGR-3A A05 (intended behavior change): Geometric Gaussian fused
  // eligibility covers every TL run mode of the family (Cartesian only —
  // the family has no ray-centered variant; design §9).
  context.check(
      supportsFusedRayReuse(makeSimulation(SimulationRunMode::Coherent,
                                           BeamFamily::GeometricGaussian)) &&
          supportsFusedRayReuse(makeSimulation(
              SimulationRunMode::Incoherent, BeamFamily::GeometricGaussian)) &&
          supportsFusedRayReuse(makeSimulation(
              SimulationRunMode::SemiCoherent,
              BeamFamily::GeometricGaussian)),
      "the shared fused-support predicate accepts geometric Gaussian "
      "coherent, incoherent, and semi-coherent TL");
  // End-to-end wiring for one Gaussian mode (numerical parity itself is
  // gated in rayreuse.component.fused_geometric_gaussian_parity).
  {
    const SimulationCase gaussianSimulation = makeSimulation(
        SimulationRunMode::Coherent, BeamFamily::GeometricGaussian);
    std::size_t gaussianCallbackCount = 0U;
    const FusedRayReuseStatistics gaussianStatistics =
        FusedRayReuseSolver::solveStreaming(
            gaussianSimulation, 1.0, 50.0,
            [&gaussianCallbackCount, &context](
                std::size_t,
                std::vector<FrequencyWorkspace>&& sourceWorkspaces,
                const SingleFrequencyTimings&) {
              ++gaussianCallbackCount;
              context.check(sourceWorkspaces.size() == 1U,
                            "geometric Gaussian fused streaming delivers one "
                            "source workspace per frequency");
            },
            CartesianCervenySettings{}, true);
    context.check(
        gaussianCallbackCount == gaussianSimulation.frequencies().size() &&
            gaussianStatistics.cacheFingerprintVerified &&
            gaussianStatistics.cacheFingerprintBefore ==
                gaussianStatistics.cacheFingerprintAfter,
        "geometric Gaussian fused streaming delivers every frequency and "
        "leaves the frozen cache unchanged");
  }
  context.check(
      !supportsFusedRayReuse(makeSimulation(
          SimulationRunMode::Coherent, BeamFamily::CervenyGaussian,
          CervenyCoordinateSystem::Cartesian,
          FrequencyGrid({50.0, 100.0}),
          ReceiverGrid({10.0, 55.0}, {25.0, 50.0, 90.0}))),
      "the shared fused-support predicate excludes a non-equally-spaced "
      "rectilinear receiver grid from legacy replacement warnings");

  std::vector<std::string> messages;
  const auto reject = [&context,
                       &messages](SimulationCase bad, const char* label) {
    const std::optional<std::string> message =
        capturedValidationMessage([&bad] {
          static_cast<void>(FusedRayReuseSolver::solveStreaming(
              bad, 1.0, 50.0, noOpConsumer));
        });
    context.check(message.has_value(), label);
    messages.push_back(message.value_or(std::string("no throw for ") + label));
  };

  // IGR-3A A06 (intended behavior change): Simple Gaussian joins the fused
  // family set in its ONLY legal product mode, coherent (design §9 — the
  // coherent-only matrix is product law, not a fused restriction).
  // Non-coherent Simple Gaussian runs are rejected upstream at SimulationCase
  // construction (simulation_case.cpp:404-409), so no SG+I/S case exists for
  // supportsFusedRayReuse to see; the constructibility assertion below pins
  // that legal-matrix fact.
  context.check(
      supportsFusedRayReuse(makeSimulation(SimulationRunMode::Coherent,
                                           BeamFamily::SimpleGaussian)),
      "the shared fused-support predicate accepts simple Gaussian coherent "
      "TL");
  for (const SimulationRunMode illegalMode :
       {SimulationRunMode::Incoherent, SimulationRunMode::SemiCoherent}) {
    const std::optional<std::string> constructionMessage =
        capturedValidationMessage([illegalMode] {
          static_cast<void>(makeSimulation(illegalMode,
                                           BeamFamily::SimpleGaussian));
        });
    context.check(
        constructionMessage.has_value() &&
            *constructionMessage ==
                "simple Gaussian beams require coherent point-source TL on a "
                "rectilinear receiver grid",
        "SimulationCase construction rejects simple Gaussian incoherent and "
        "semi-coherent runs (the legal matrix upstream of the fused gate)");
  }
  // The intensity public entry is the reachable fused enforcement of the
  // same law: it must reject the coherent-only family BEFORE any adapter
  // dispatch (the Simple Gaussian adapter defines no intensity hooks —
  // compile-time absence) with the run-mode family-legality message.
  {
    const SimulationCase simpleGaussianSimulation = makeSimulation(
        SimulationRunMode::Coherent, BeamFamily::SimpleGaussian);
    rayreuse::RayPathCache anyCache;
    const std::optional<std::string> intensityMessage =
        capturedValidationMessage([&simpleGaussianSimulation, &anyCache] {
          static_cast<void>(FusedRayReuseSolver::accumulateFrequenciesIntensity(
              simpleGaussianSimulation, anyCache, 1.0, 50.0));
        });
    context.check(
        intensityMessage.has_value() &&
            *intensityMessage ==
                "fused ray-reuse solver requires a run mode that is legal "
                "for the beam family",
        "the fused intensity entry rejects the coherent-only simple Gaussian "
        "family with the family-legality message");
  }
  // End-to-end wiring (numerical parity itself is gated in
  // rayreuse.component.fused_simple_gaussian_parity): the fused streaming
  // path must deliver every frequency through the Simple Gaussian adapter
  // chain.
  {
    const SimulationCase simpleGaussianSimulation = makeSimulation(
        SimulationRunMode::Coherent, BeamFamily::SimpleGaussian);
    std::size_t simpleGaussianCallbackCount = 0U;
    const FusedRayReuseStatistics simpleGaussianStatistics =
        FusedRayReuseSolver::solveStreaming(
            simpleGaussianSimulation, 1.0, 50.0,
            [&simpleGaussianCallbackCount, &context](
                std::size_t,
                std::vector<FrequencyWorkspace>&& sourceWorkspaces,
                const SingleFrequencyTimings&) {
              ++simpleGaussianCallbackCount;
              context.check(sourceWorkspaces.size() == 1U,
                            "simple Gaussian fused streaming delivers one "
                            "source workspace per frequency");
            },
            CartesianCervenySettings{}, true);
    context.check(
        simpleGaussianCallbackCount ==
                simpleGaussianSimulation.frequencies().size() &&
            simpleGaussianStatistics.cacheFingerprintVerified &&
            simpleGaussianStatistics.cacheFingerprintBefore ==
                simpleGaussianStatistics.cacheFingerprintAfter,
        "simple Gaussian fused streaming delivers every frequency and leaves "
        "the frozen cache unchanged");
  }
  // Non-TL products stay outside fused scope (A07: unchanged message; the
  // CLI layer rejects R/arrival/eigenray products before the solver).
  reject(makeSimulation(SimulationRunMode::RayTrace),
         "non-TL run mode is rejected by the fused solver");
  reject(makeSimulation(SimulationRunMode::Coherent,
                        BeamFamily::CervenyGaussian,
                        CervenyCoordinateSystem::Cartesian,
                        FrequencyGrid({50.0})),
         "single-frequency run is rejected by the fused solver");
  reject(makeSimulation(SimulationRunMode::Coherent,
                        BeamFamily::CervenyGaussian,
                        CervenyCoordinateSystem::Cartesian,
                        FrequencyGrid({50.0, 100.0}),
                        ReceiverGrid({10.0, 55.0}, {25.0, 75.0},
                                     ReceiverGridLayout::Irregular)),
         "irregular receiver grid is rejected by the fused solver");
  reject(makeSimulation(SimulationRunMode::Coherent,
                        BeamFamily::CervenyGaussian,
                        CervenyCoordinateSystem::Cartesian,
                        FrequencyGrid({50.0, 100.0}),
                        ReceiverGrid({10.0, 55.0}, {25.0})),
         "one receiver range is rejected by the existing CC domain");
  reject(makeSimulation(SimulationRunMode::Coherent,
                        BeamFamily::CervenyGaussian,
                        CervenyCoordinateSystem::Cartesian,
                        FrequencyGrid({50.0, 100.0}),
                        ReceiverGrid({10.0, 55.0}, {25.0, 50.0, 90.0})),
         "nonuniform receiver ranges are rejected by the fused solver");
  {
    SimulationCase multiSource(
        Environment(
            SoundSpeedProfile(
                {SoundSpeedPoint{
                     .depth = 0.0, .soundSpeed = 1500.0, .density = 1000.0},
                 SoundSpeedPoint{
                     .depth = 100.0, .soundSpeed = 1500.0, .density = 1000.0}}),
            BoundaryModel::vacuum(0.0), BoundaryModel::rigid(100.0)),
        std::vector<Source>{{.depth = 50.0, .amplitude = 1.0},
                            {.depth = 60.0, .amplitude = 1.0}},
        ReceiverGrid({25.0, 50.0, 75.0}, {10.0, 55.0, 100.0}),
        FrequencyGrid({50.0, 100.0}),
        LaunchFan{.minimumAngle = -2.0 * std::numbers::pi / 180.0,
                  .maximumAngle = 2.0 * std::numbers::pi / 180.0,
                  .explicitLaunchAngleCount = 300U},
        IntegratorSettings{.stepLength = 10.0,
                           .rangeLimit = 110.0,
                           .depthLimit = 110.0,
                           .maximumRayPoints = 100U});
    reject(multiSource, "multi-source run is rejected by the fused solver");
  }

  for (const std::string& message : messages) {
    context.check(message.starts_with("fused ray-reuse solver"),
                  "solver-level fused rejection carries the fused prefix: " +
                      message);
  }
  std::vector<std::string> sorted = messages;
  std::sort(sorted.begin(), sorted.end());
  context.check(std::adjacent_find(sorted.begin(), sorted.end()) ==
                    sorted.end(),
                "solver-level fused rejection messages are distinct");

  // Frozen-cache defense in depth on the Level-B seam: a non-frozen cache is
  // rejected before any work.
  const SimulationCase simulation = makeSimulation();
  rayreuse::RayPathCache unfrozenCache;
  const std::optional<std::string> unfrozenMessage =
      capturedValidationMessage([&simulation, &unfrozenCache] {
        static_cast<void>(FusedRayReuseSolver::accumulateFrequencies(
            simulation, unfrozenCache, 1.0, 50.0));
      });
  context.check(
      unfrozenMessage.has_value() &&
          unfrozenMessage->starts_with("fused ray-reuse solver"),
      "an unfrozen cache is rejected by the fused Level-B seam");

  const std::optional<std::string> zeroWorkerMessage =
      capturedValidationMessage([&simulation] {
        static_cast<void>(FusedRayReuseSolver::solveStreaming(
            simulation, 1.0, 50.0, noOpConsumer,
            CartesianCervenySettings{}, false,
            FusedRayReuseExecutionSettings{.requestedRangeWorkers = 0U}));
      });
  context.check(zeroWorkerMessage.has_value() &&
                    *zeroWorkerMessage ==
                        "fused ray-reuse requested range worker count must be "
                        "positive",
                "the fused solver rejects a zero range-worker count");

}

void testFusedStreamingMatchesSerialReuse(Context& context) {
  const SimulationCase simulation = makeSimulation();
  const CartesianCervenySettings settings{.collectStatistics = true};
  const SerialRayReuseResult reuse =
      SerialRayReuseSolver::solve(simulation, 1.0, 50.0, settings, true);

  std::vector<std::optional<std::vector<FrequencyWorkspace>>> streamed(
      simulation.frequencies().size());
  std::vector<std::size_t> callbackCounts(
      simulation.frequencies().size(), 0U);
  std::vector<std::size_t> callbackOrder;
  std::vector<double> callbackScaleSeconds;
  const FusedRayReuseStatistics statistics =
      FusedRayReuseSolver::solveStreaming(
      simulation, 1.0, 50.0,
      [&](std::size_t frequencyIndex,
          std::vector<FrequencyWorkspace>&& sourceWorkspaces,
          const SingleFrequencyTimings& timings) {
        ++callbackCounts.at(frequencyIndex);
        callbackOrder.push_back(frequencyIndex);
        callbackScaleSeconds.push_back(timings.scaleSeconds);
        streamed.at(frequencyIndex).emplace(std::move(sourceWorkspaces));
      },
      settings, true);

  context.check(
      callbackOrder == std::vector<std::size_t>{0U, 1U},
      "fused streaming callback preserves frequency order");
  context.check(callbackCounts == std::vector<std::size_t>{1U, 1U},
                "fused streaming consumes every frequency once");
  context.check(
      statistics.tracePassCount == 1U &&
          statistics.requestedRangeWorkers == 1U &&
          statistics.effectiveRangeWorkers == 1U &&
          statistics.rayCount ==
              simulation.launchFanPlan().launchAngleCount &&
          statistics.rayCacheBytes > 0U &&
          statistics.totalRayPointCount > statistics.rayCount,
      "fused streaming reports one trace pass and the shared cache metrics");

  for (std::size_t frequencyIndex = 0U;
       frequencyIndex < simulation.frequencies().size(); ++frequencyIndex) {
    const std::optional<std::vector<FrequencyWorkspace>>& workspaces =
        streamed[frequencyIndex];
    const FrequencyWorkspace& expected =
        reuse.frequencyResults[frequencyIndex].workspaces.front();
    context.check(
        workspaces.has_value() && workspaces->size() == 1U &&
            workspaces->front().frequency() == expected.frequency() &&
            workspaces->front().depthCount() == expected.depthCount() &&
            workspaces->front().rangeCount() == expected.rangeCount() &&
            std::equal(workspaces->front().pressure().begin(),
                       workspaces->front().pressure().end(),
                       expected.pressure().begin(), expected.pressure().end()),
        "fused frequency " + std::to_string(frequencyIndex) +
            " is bitwise equal to serial reuse");
    context.check(callbackScaleSeconds[frequencyIndex] >= 0.0,
                  "fused per-frequency consumer timings carry the scale time");
  }

  context.check(
      statistics.cacheFingerprintVerified &&
          statistics.cacheFingerprintBefore ==
              statistics.cacheFingerprintAfter &&
          statistics.cacheFingerprintBefore ==
              reuse.statistics.cacheFingerprintBefore,
      "fused streaming leaves the frozen cache unchanged and matches the "
      "reuse fingerprint");
  context.check(
      statistics.wallSeconds >= 0.0 &&
          statistics.phaseTotals.traceSeconds >= 0.0 &&
          statistics.phaseTotals.projectSeconds >= 0.0 &&
          statistics.phaseTotals.influenceSeconds >= 0.0 &&
          statistics.phaseTotals.scaleSeconds >= 0.0,
      "fused streaming exposes the block-level phase timings");
}

void testRangeWorkerResolution(Context& context) {
  // Cartesian Cerveny itself requires at least two receiver ranges. Use that
  // smallest supported grid to exercise requested > range-count clamping.
  const SimulationCase twoRanges = makeSimulation(
      SimulationRunMode::Coherent, BeamFamily::CervenyGaussian,
      CervenyCoordinateSystem::Cartesian, FrequencyGrid({50.0, 100.0}),
      ReceiverGrid({25.0, 50.0, 75.0}, {10.0, 100.0}));
  std::size_t callbackCount = 0U;
  const FusedRayReuseStatistics clamped =
      FusedRayReuseSolver::solveStreaming(
          twoRanges, 1.0, 50.0,
          [&callbackCount](std::size_t,
                           std::vector<FrequencyWorkspace>&&,
                           const SingleFrequencyTimings&) { ++callbackCount; },
          CartesianCervenySettings{}, true,
          FusedRayReuseExecutionSettings{.requestedRangeWorkers = 8U});
  context.check(clamped.requestedRangeWorkers == 8U &&
                    clamped.effectiveRangeWorkers == 2U &&
                    callbackCount == twoRanges.frequencies().size(),
                "requested range workers clamp to the receiver range count "
                "without losing frequency delivery");
}

void testFusedCounterSemantics(Context& context) {
  const SimulationCase simulation = makeSimulation();
  const CartesianCervenySettings settings{.collectStatistics = true};
  const SerialRayReuseResult reuse =
      SerialRayReuseSolver::solve(simulation, 1.0, 50.0, settings, true);
  const FusedRayReuseStatistics statistics =
      FusedRayReuseSolver::solveStreaming(simulation, 1.0, 50.0,
                                          noOpConsumer, settings, true);
  const rayreuse::CartesianCervenyStatistics& fused =
      statistics.phaseTotals.influenceStatistics;
  const rayreuse::CartesianCervenyStatistics& baseline =
      reuse.statistics.phaseTotals.influenceStatistics;

  context.check(
      fused.rayAccumulations == baseline.rayAccumulations &&
          fused.activeRayPoints == baseline.activeRayPoints,
      "fused per-frequency-kernel entry counters stay baseline-equal");
  context.check(
      fused.frequencyRangeKernelEvaluations ==
              baseline.frequencyRangeKernelEvaluations &&
          fused.frequencyImageKernelEvaluations ==
              baseline.frequencyImageKernelEvaluations &&
          fused.imageEvaluations == fused.frequencyImageKernelEvaluations,
      "fused frequency-kernel counters equal the reuse totals and keep the "
      "image identity");
  context.check(
      fused.windowRejections == baseline.windowRejections &&
          fused.taperRejections == baseline.taperRejections &&
          fused.nonzeroImageContributions ==
              baseline.nonzeroImageContributions,
      "fused image-kernel rejection counters stay baseline-equal");
  context.check(
      fused.validatedRayPoints == 0U && fused.validatedWorkspaceValues == 0U,
      "fused prevalidated route never restores full validation scans");
  context.check(
      fused.geometrySegmentEvaluations < baseline.geometrySegmentEvaluations &&
          fused.geometryRangeEvaluations < baseline.geometryRangeEvaluations &&
          fused.geometryDepthEvaluations < baseline.geometryDepthEvaluations &&
          fused.geometryImageGeometryEvaluations <
              baseline.geometryImageGeometryEvaluations &&
          fused.receiverRangeEvaluations <=
              baseline.receiverRangeEvaluations &&
          fused.receiverDepthEvaluations <= baseline.receiverDepthEvaluations,
      "fused shared-geometry counters deduplicate the per-frequency sum");
}

}  // namespace

int main() {
  Context context;
  testSolverScopeRejections(context);
  testFusedStreamingMatchesSerialReuse(context);
  testRangeWorkerResolution(context);
  testFusedCounterSemantics(context);

  if (context.failureCount() != 0) {
    std::cerr << context.failureCount()
              << " fused-ray-reuse-solver assertion(s) failed\n";
    return 1;
  }
  std::cout << "All Bellhop RayReuse fused-ray-reuse-solver tests passed\n";
  return 0;
}
