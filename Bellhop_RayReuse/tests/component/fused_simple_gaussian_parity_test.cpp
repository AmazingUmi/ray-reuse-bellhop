#include "rayreuse/solver/fused_ray_reuse_solver.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstring>
#include <functional>
#include <iostream>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "rayreuse/field/frequency_projector.hpp"
#include "rayreuse/field/frequency_workspace.hpp"
#include "rayreuse/field/pressure_scaling.hpp"
#include "rayreuse/field/simple_gaussian_influence.hpp"
#include "rayreuse/model/simulation_case.hpp"
#include "rayreuse/solver/serial_ray_reuse_solver.hpp"
#include "rayreuse/solver/single_frequency_solver.hpp"
#include "support/munk_case_fixture.hpp"
#include "support/test_harness.hpp"

// IGR-3A A06 numerical parity gates for the Simple Gaussian fused kernel
// (design §5/§8/§11), COHERENT ONLY — the family's ONLY legal product mode
// (design §9: the coherent-only matrix is product law enforced upstream at
// SimulationCase construction, not a fused restriction; the intensity sink
// is never instantiated with this family's adapter):
//   Level B — raw bitwise parity per frequency: the legacy reuse
//             accumulation (Raw seam) vs the fused workspace materialized
//             per frequency (std::memcmp over the payload span bytes).
//   Level C — scaled workspace bitwise parity per frequency via the two
//             production paths (SerialRayReuseSolver::solve vs
//             FusedRayReuseSolver::solveStreaming).
//   Level D — worker counts 1/2/4/8, each gated against the same serial
//             reference (transitively identical raw bytes).
//   Level A — fused fingerprint before == after and == the serial reuse
//             fingerprint on the same case.
// A06-specific risk coverage (design §8): the beam width is
// FREQUENCY-INDEPENDENT (gaussianA from the launch spacing), so the whole
// traversal state — monotone range cursor, previousQ hand-off, and the
// persistent caustic phase accumulated at BOTH legacy update points
// (segment entry and per matched receiver) — is shared across lanes, while
// each lane's own active-prefix bound gates its stores. Fixture A uses Munk
// q dynamics with a runtime guard proving caustic-phase accumulation
// genuinely fires; fixture B covers the union-prefix traversal over
// per-frequency-diverging active prefixes.

namespace {

using rayreuse::BeamFamily;
using rayreuse::BeamWidthMode;
using rayreuse::BoundaryCurvatureMode;
using rayreuse::CartesianCervenySettings;
using rayreuse::CervenyCoordinateSystem;
using rayreuse::Environment;
using rayreuse::FieldComponent;
using rayreuse::FrequencyGrid;
using rayreuse::FrequencyProjector;
using rayreuse::FrequencyWorkspace;
using rayreuse::FusedRayReuseSolver;
using rayreuse::FusedRayReuseStatistics;
using rayreuse::IntegratorSettings;
using rayreuse::LaunchFan;
using rayreuse::ReceiverGrid;
using rayreuse::SerialRayReuseResult;
using rayreuse::SerialRayReuseSolver;
using rayreuse::SimpleGaussianDiagnosticRequest;
using rayreuse::SimpleGaussianInfluence;
using rayreuse::SimulationCase;
using rayreuse::SimulationRunMode;
using rayreuse::SingleFrequencyResult;
using rayreuse::SingleFrequencySolver;
using rayreuse::SingleFrequencyTimings;
using rayreuse::SoundSpeedPoint;
using rayreuse::SoundSpeedProfile;
using rayreuse::Source;
using rayreuse::SourceBeamPattern;
using rayreuse::WorkspaceDelivery;
using rayreuse::supportsFusedRayReuse;
using rayreuse::test::Context;

struct WorkspaceByteComparison {
  bool equal{};
  std::string detail;
};

// Byte-level (std::memcmp) comparison over the full pressure span; never
// operator==. Reports the first differing element and both values.
[[nodiscard]] WorkspaceByteComparison memcmpPressureSpan(
    const FrequencyWorkspace& reference, const FrequencyWorkspace& fused,
    std::size_t frequencyIndex) {
  const std::string prefix =
      "frequency index " + std::to_string(frequencyIndex);
  if (reference.frequency() != fused.frequency() ||
      reference.depthCount() != fused.depthCount() ||
      reference.rangeCount() != fused.rangeCount()) {
    return {.equal = false,
            .detail = prefix + " workspace shape/frequency mismatch (" +
                      std::to_string(reference.depthCount()) + "x" +
                      std::to_string(reference.rangeCount()) + " @ " +
                      std::to_string(reference.frequency()) + " vs " +
                      std::to_string(fused.depthCount()) + "x" +
                      std::to_string(fused.rangeCount()) + " @ " +
                      std::to_string(fused.frequency()) + ")"};
  }
  const std::span<const std::complex<double>> expected = reference.pressure();
  const std::span<const std::complex<double>> actual = fused.pressure();
  if (expected.size() != actual.size()) {
    return {.equal = false,
            .detail = prefix + " pressure span size mismatch (" +
                      std::to_string(expected.size()) + " vs " +
                      std::to_string(actual.size()) + ")"};
  }
  if (std::memcmp(expected.data(), actual.data(), expected.size_bytes()) ==
      0) {
    return {.equal = true, .detail = prefix};
  }
  for (std::size_t index = 0U; index < expected.size(); ++index) {
    if (std::memcmp(&expected[index], &actual[index],
                    sizeof(std::complex<double>)) != 0) {
      return {.equal = false,
              .detail = prefix + " first differing element " +
                        std::to_string(index) + ": reuse (" +
                        std::to_string(expected[index].real()) + ", " +
                        std::to_string(expected[index].imag()) +
                        ") vs fused (" + std::to_string(actual[index].real()) +
                        ", " + std::to_string(actual[index].imag()) + ")"};
    }
  }
  return {.equal = false, .detail = prefix + " memcmp failed"};  // unreachable
}

// Fixture A geometry (the Cerveny parity tests' Munk small case) as a simple
// Gaussian run: single point source, real Munk profile, caustic-crossing q
// dynamics. The family is coherent-only (design §9).
[[nodiscard]] SimulationCase makeMunkSimpleGaussianCase(
    FrequencyGrid frequencies = FrequencyGrid({50.0, 250.0})) {
  constexpr double kRadiansPerDegree = std::numbers::pi / 180.0;
  return SimulationCase(
      rayreuse::test::makeMunkEnvironment(
          rayreuse::SspInterpolationKind::CLinear),
      Source{.depth = 1000.0, .amplitude = 1.0},
      ReceiverGrid({0.0, 500.0, 1000.0, 1500.0, 2000.0, 2500.0, 3000.0},
                   {0.0, 1250.0, 2500.0, 3750.0, 5000.0, 6250.0, 7500.0,
                    8750.0, 10000.0}),
      frequencies,
      LaunchFan{.minimumAngle = -12.0 * kRadiansPerDegree,
                .maximumAngle = 12.0 * kRadiansPerDegree,
                .explicitLaunchAngleCount = 61U},
      IntegratorSettings{.stepLength = 100.0,
                         .rangeLimit = 12000.0,
                         .depthLimit = 5500.0,
                         .maximumRayPoints = 2000U},
      SourceBeamPattern::omnidirectional(), SimulationRunMode::Coherent,
      BeamFamily::SimpleGaussian, FieldComponent::Pressure,
      BoundaryCurvatureMode::Standard, BeamWidthMode::MinimumWidth,
      CervenyCoordinateSystem::Cartesian);
}

// Fixture B (divergent per-frequency active prefixes, the union-prefix
// risk): constant-speed water with a lossy reflecting acoustic half-space
// bottom whose attenuation follows a power law in frequency, so the
// cumulative projected amplitude crosses the legacy 0.005 cutoff at
// DIFFERENT points for the two frequencies.
[[nodiscard]] SimulationCase makeDivergentPrefixSimpleGaussianCase() {
  constexpr double kRadiansPerDegree = std::numbers::pi / 180.0;
  return SimulationCase(
      Environment(
          SoundSpeedProfile(
              {SoundSpeedPoint{
                   .depth = 0.0, .soundSpeed = 1500.0, .density = 1000.0},
               SoundSpeedPoint{
                   .depth = 100.0, .soundSpeed = 1500.0, .density = 1000.0}}),
          rayreuse::BoundaryModel::vacuum(0.0),
          rayreuse::BoundaryModel::acousticHalfSpace(
              100.0, rayreuse::AcousticMaterial{
                         .compressionalSoundSpeed = 1700.0,
                         .shearSoundSpeed = 0.0,
                         .density = 1800.0,
                         .compressionalAttenuation = {
                             .value = 10.0,
                             .unit = rayreuse::AttenuationUnit::
                                 DecibelsPerMeterPowerLaw,
                             .referenceFrequency = 1000.0,
                             .powerLawExponent = 2.0,
                             .transitionFrequency = 1.0e9}})),
      Source{.depth = 50.0, .amplitude = 1.0},
      ReceiverGrid({10.0, 55.0, 100.0},
                   {10.0, 21.25, 32.5, 43.75, 55.0, 66.25, 77.5, 88.75,
                    100.0}),
      FrequencyGrid({100.0, 1000.0}),
      LaunchFan{.minimumAngle = -60.0 * kRadiansPerDegree,
                .maximumAngle = 60.0 * kRadiansPerDegree,
                .explicitLaunchAngleCount = 121U},
      IntegratorSettings{.stepLength = 5.0,
                         .rangeLimit = 1500.0,
                         .depthLimit = 200.0,
                         .maximumRayPoints = 4000U},
      SourceBeamPattern::omnidirectional(), SimulationRunMode::Coherent,
      BeamFamily::SimpleGaussian, FieldComponent::Pressure,
      BoundaryCurvatureMode::Standard, BeamWidthMode::MinimumWidth,
      CervenyCoordinateSystem::Cartesian);
}

// First inactive point index of a projected state (points.size() when every
// point stays active) — the active prefix length the kernels derive via the
// same scan.
[[nodiscard]] std::size_t activePrefixLength(
    const rayreuse::RayFrequencyState& state) {
  for (std::size_t index = 0U; index < state.points.size(); ++index) {
    if (!state.points[index].active) {
      return index;
    }
  }
  return state.points.size();
}

// Fixture B guard: the per-frequency active prefixes must genuinely diverge
// so the Level B comparisons cannot pass vacuously — a lane with a shorter
// prefix must not receive stores from segments beyond its own legacy loop
// bound.
void checkDivergentPrefixes(Context& context,
                            const rayreuse::RayPathCache& cache,
                            const SimulationCase& simulation,
                            const char* label) {
  const std::vector<double>& frequencies = simulation.frequencies().values();
  const FrequencyProjector projector(simulation.environment());
  std::size_t divergentRays = 0U;
  std::size_t truncatedRays = 0U;
  for (const rayreuse::RayPath& path : cache.paths()) {
    std::vector<std::size_t> prefixes(frequencies.size());
    for (std::size_t frequencyIndex = 0U;
         frequencyIndex < frequencies.size(); ++frequencyIndex) {
      prefixes[frequencyIndex] = activePrefixLength(
          projector.project(path, frequencies[frequencyIndex], 1.0));
    }
    if (std::adjacent_find(prefixes.begin(), prefixes.end(),
                           std::not_equal_to<std::size_t>{}) !=
        prefixes.end()) {
      ++divergentRays;
    }
    if (std::any_of(prefixes.begin(), prefixes.end(),
                    [&path](std::size_t prefix) {
                      return prefix < path.points.size();
                    })) {
      ++truncatedRays;
    }
  }
  std::cout << "fused-simple-gaussian-parity " << label
            << ": rays=" << cache.size() << " divergent-prefix rays="
            << divergentRays << " cutoff-truncated rays=" << truncatedRays
            << '\n';
  context.check(divergentRays > 0U && truncatedRays > 0U,
                std::string(label) +
                    " Fixture B prefixes genuinely diverge across frequencies "
                    "(no vacuous pass)");
}

// Fixture A guard (the A06-specific risk, design §8): the shared caustic
// phase evolution must genuinely fire — a ray counts as caustic-crossing
// when the PUBLIC legacy kernel's diagnostic reports a strictly positive
// causticPhase at some probed receiver (the phase only ever accumulates at
// the two legacy update points), proving both the segment-entry and the
// per-matched-receiver update paths shape the fixture's pressure field.
void checkCausticPhaseActivity(Context& context,
                               const rayreuse::RayPathCache& cache,
                               const SimulationCase& simulation,
                               const char* label) {
  const std::vector<double>& frequencies = simulation.frequencies().values();
  const FrequencyProjector projector(simulation.environment());
  const SimpleGaussianInfluence kernel(
      simulation.receivers(), simulation.integrator().stepLength,
      simulation.sourceGeometry());
  const double launchAngleStep = simulation.launchFanPlan().launchAngleStep;
  // The caustic evolution is frequency-independent (design §8), so one lane
  // suffices for the probe; its stores are discarded.
  FrequencyWorkspace scratch(frequencies.front(), simulation.receivers());
  std::vector<SimpleGaussianDiagnosticRequest> requests;
  for (const std::size_t rangeIndex : {1U, 2U, 3U, 4U, 5U}) {
    for (const std::size_t depthIndex : {1U, 2U, 3U}) {
      requests.push_back(SimpleGaussianDiagnosticRequest{
          .receiverRangeIndex = rangeIndex,
          .receiverDepthIndex = depthIndex});
    }
  }
  std::size_t causticRays = 0U;
  for (const rayreuse::RayPath& path : cache.paths()) {
    bool causticCrossed = false;
    for (const SimpleGaussianDiagnosticRequest& request : requests) {
      const rayreuse::RayFrequencyState state = projector.project(
          path, frequencies.front(), 1.0);
      const std::optional<rayreuse::SimpleGaussianDiagnostic> diagnostic =
          kernel.accumulate(scratch, path, state, launchAngleStep, request);
      if (diagnostic.has_value() && diagnostic->evaluated &&
          diagnostic->causticPhase > 0.0) {
        causticCrossed = true;
      }
    }
    if (causticCrossed) {
      ++causticRays;
    }
  }
  std::cout << "fused-simple-gaussian-parity " << label
            << ": rays=" << cache.size()
            << " caustic-phase-active rays=" << causticRays << '\n';
  context.check(
      causticRays > 0U,
      std::string(label) +
          " caustic-phase accumulation genuinely fires (no vacuous pass)");
}

// Levels B + C + A on one fixture (coherent only — the family's single
// legal mode). `settings` flows to both sides so the comparison isolates
// the fused-vs-reuse path (the simple Gaussian kernel ignores it — no
// epsilon channel).
void testParityLevels(Context& context, const SimulationCase& simulation,
                      const CartesianCervenySettings& settings,
                      const char* label, std::size_t workerCount = 1U) {
  const std::vector<double>& frequencies = simulation.frequencies().values();

  // Shared frozen fan: traced once, consumed by every level below.
  const rayreuse::RayFanTraceResult trace =
      SingleFrequencySolver::traceSourceFan(simulation, 0U);

  // Level B reference: the production Raw seam returns the raw pressure
  // directly (coherent delivery).
  std::vector<FrequencyWorkspace> rawReuse;
  rawReuse.reserve(frequencies.size());
  for (const double frequency : frequencies) {
    SingleFrequencyResult result =
        SingleFrequencySolver::solveFrequencyFromSourceCache(
            simulation, frequency, trace.cache, 0U, 1.0, 50.0, settings,
            WorkspaceDelivery::Raw);
    rawReuse.push_back(std::move(result.workspace));
  }

  // Level B fused side (also Level D: each worker count is gated against
  // the same serial reference, so raw bytes are transitively identical
  // across 1/2/4/8).
  const rayreuse::FusedAccumulationResult fused =
      FusedRayReuseSolver::accumulateFrequencies(
          simulation, trace.cache, 1.0, 50.0, settings,
          rayreuse::FusedRayReuseExecutionSettings{
              .requestedRangeWorkers = workerCount});
  context.check(
      fused.rawWorkspace.frequencyCount() == frequencies.size() &&
          fused.rayCount == trace.cache.size() &&
          fused.rayCacheBytes == trace.cache.memoryFootprintBytes() &&
          fused.requestedRangeWorkers == workerCount &&
          fused.effectiveRangeWorkers ==
              std::min(workerCount, simulation.receivers().rangeCount()),
      std::string(label) + " Level B result carries every frequency and the "
                           "shared cache metrics");
  context.check(fused.timings.traceSeconds == 0.0 &&
                    fused.timings.scaleSeconds == 0.0 &&
                    fused.timings.projectSeconds >= 0.0 &&
                    fused.timings.influenceSeconds >= 0.0,
                std::string(label) + " Level B timings follow the raw seam "
                                     "contract (no scale time, block "
                                     "phases)");
  for (std::size_t frequencyIndex = 0U; frequencyIndex < frequencies.size();
       ++frequencyIndex) {
    const FrequencyWorkspace fusedWorkspace =
        fused.rawWorkspace.materializeFrequency(
            frequencyIndex, frequencies[frequencyIndex],
            simulation.receivers());
    const WorkspaceByteComparison levelB = memcmpPressureSpan(
        rawReuse[frequencyIndex], fusedWorkspace, frequencyIndex);
    context.check(levelB.equal,
                  std::string(label) +
                      " Level B raw pressure bitwise parity: " + levelB.detail);
  }

  // Level C: production paths on the same SimulationCase.
  const SerialRayReuseResult serial =
      SerialRayReuseSolver::solve(simulation, 1.0, 50.0, settings, true);
  std::vector<std::optional<std::vector<FrequencyWorkspace>>> streamed(
      frequencies.size());
  std::vector<double> streamedScaleSeconds(frequencies.size(), -1.0);
  std::vector<std::size_t> callbackOrder;
  const FusedRayReuseStatistics fusedStatistics =
      FusedRayReuseSolver::solveStreaming(
          simulation, 1.0, 50.0,
          [&](std::size_t frequencyIndex,
              std::vector<FrequencyWorkspace>&& sourceWorkspaces,
              const SingleFrequencyTimings& timings) {
            callbackOrder.push_back(frequencyIndex);
            streamedScaleSeconds.at(frequencyIndex) = timings.scaleSeconds;
            streamed.at(frequencyIndex).emplace(std::move(sourceWorkspaces));
          },
          settings, true,
          rayreuse::FusedRayReuseExecutionSettings{
              .requestedRangeWorkers = workerCount});

  context.check(
      fusedStatistics.requestedRangeWorkers == workerCount &&
          fusedStatistics.effectiveRangeWorkers ==
              std::min(workerCount, simulation.receivers().rangeCount()),
      std::string(label) + " Level C reports requested/effective workers");

  context.check(
      callbackOrder.size() == frequencies.size() &&
          std::is_sorted(callbackOrder.begin(), callbackOrder.end()),
      std::string(label) + " Level C fused consumer visits every frequency "
                           "in index order");
  for (std::size_t frequencyIndex = 0U; frequencyIndex < frequencies.size();
       ++frequencyIndex) {
    context.check(
        streamed[frequencyIndex].has_value() &&
            streamed[frequencyIndex]->size() == 1U &&
            streamedScaleSeconds[frequencyIndex] >= 0.0,
        std::string(label) + " Level C captured scaled fused workspace and "
                             "scale timing for frequency index " +
            std::to_string(frequencyIndex));
    const WorkspaceByteComparison levelC = memcmpPressureSpan(
        serial.frequencyResults[frequencyIndex].workspaces.front(),
        streamed[frequencyIndex]->front(), frequencyIndex);
    context.check(levelC.equal,
                  std::string(label) + " Level C scaled workspace bitwise "
                                      "parity: " +
                      levelC.detail);
  }

  // Level A: the fused fingerprint is stable and equals the serial reuse
  // fingerprint.
  context.check(
      fusedStatistics.cacheFingerprintVerified &&
          fusedStatistics.cacheFingerprintBefore ==
              fusedStatistics.cacheFingerprintAfter &&
          fusedStatistics.cacheFingerprintBefore ==
              serial.statistics.cacheFingerprintBefore,
      std::string(label) + " Level A fused cache fingerprint is stable and "
                           "matches serial reuse");
}

}  // namespace

int main() {
  Context context;

  // Scope acceptance is gated per family x mode in
  // rayreuse.component.fused_solver; assertions here keep the parity test
  // honest about the domain it exercises — coherent is the family's ONLY
  // legal mode (design §9).
  context.check(
      supportsFusedRayReuse(makeMunkSimpleGaussianCase()),
      "the fused-support predicate accepts the simple Gaussian fixture in "
      "its only legal mode, coherent");

  // Fixture A: Munk simple Gaussian.
  testParityLevels(context, makeMunkSimpleGaussianCase(),
                   CartesianCervenySettings{},
                   "fixture A (munk simple gaussian C)");
  // Worker-count gate (Level D): 16 frequencies and >= 8 real ranges, every
  // requested worker count gated against the same serial reference.
  {
    const SimulationCase parallelSimulation = makeMunkSimpleGaussianCase(
        FrequencyGrid({50.0, 100.0, 150.0, 200.0, 250.0, 300.0, 350.0, 400.0,
                       450.0, 500.0, 550.0, 600.0, 650.0, 700.0, 750.0,
                       800.0}));
    for (const std::size_t workerCount : {1U, 2U, 4U, 8U}) {
      const std::string label =
          "fixture A parallel (16F, " + std::to_string(workerCount) +
          " workers, simple gaussian, C)";
      testParityLevels(context, parallelSimulation,
                       CartesianCervenySettings{}, label.c_str(), workerCount);
    }
  }
  // Fixture A caustic guard (the A06-specific shared-state risk): runs
  // first-adjacent to the parity gates on the same 2F fixture so the
  // bitwise comparisons cannot pass vacuously without caustic activity.
  {
    const SimulationCase simulation = makeMunkSimpleGaussianCase();
    const rayreuse::RayFanTraceResult trace =
        SingleFrequencySolver::traceSourceFan(simulation, 0U);
    checkCausticPhaseActivity(
        context, trace.cache, simulation,
        "fixture A (munk simple gaussian C, caustic guard)");
  }
  // Fixture B: divergent per-frequency active prefixes (union-prefix path).
  // The divergence guard runs first so a degenerate fixture fails loudly
  // instead of passing vacuously.
  {
    const SimulationCase simulation = makeDivergentPrefixSimpleGaussianCase();
    const rayreuse::RayFanTraceResult trace =
        SingleFrequencySolver::traceSourceFan(simulation, 0U);
    checkDivergentPrefixes(
        context, trace.cache, simulation,
        "fixture B (lossy halfspace, simple gaussian C)");
    testParityLevels(
        context, simulation, CartesianCervenySettings{},
        "fixture B parallel (lossy halfspace, 8 workers, simple gaussian, C)",
        8U);
  }

  if (context.failureCount() != 0) {
    std::cerr << context.failureCount()
              << " fused-simple-gaussian-parity assertion(s) failed\n";
    return 1;
  }
  std::cout << "All Bellhop RayReuse fused-simple-gaussian-parity tests "
               "passed\n";
  return 0;
}
