#include "rayreuse/solver/fused_ray_reuse_solver.hpp"

#include <algorithm>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <numbers>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "rayreuse/field/frequency_projector.hpp"
#include "rayreuse/field/frequency_workspace.hpp"
#include "rayreuse/model/simulation_case.hpp"
#include "rayreuse/solver/serial_ray_reuse_solver.hpp"
#include "rayreuse/solver/single_frequency_solver.hpp"
#include "support/munk_case_fixture.hpp"
#include "support/test_harness.hpp"

// IGR-1 R05 numerical parity gates A/B/C at component level (design §10.1,
// worklist R05 / V2-GATE-07):
//   Level B — raw (unscaled) workspace bitwise parity per frequency, reuse
//             accumulation via the frozen WorkspaceDelivery::Raw seam vs
//             FusedRayReuseSolver::accumulateFrequencies (std::memcmp over
//             the pressure span bytes).
//   Level C — scaled workspace bitwise parity per frequency via the two
//             production paths (SerialRayReuseSolver::solve vs
//             FusedRayReuseSolver::solveStreaming) on the same SimulationCase.
//   Level A — fused fingerprint before == after and == the serial reuse
//             fingerprint on the same case.

namespace {

using rayreuse::AcousticMaterial;
using rayreuse::AttenuationUnit;
using rayreuse::BeamWidthMode;
using rayreuse::BoundaryModel;
using rayreuse::CartesianCervenySettings;
using rayreuse::Environment;
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
using rayreuse::SimulationCase;
using rayreuse::SingleFrequencyResult;
using rayreuse::SingleFrequencySolver;
using rayreuse::SingleFrequencyTimings;
using rayreuse::SoundSpeedPoint;
using rayreuse::SoundSpeedProfile;
using rayreuse::Source;
using rayreuse::WorkspaceDelivery;
using rayreuse::test::Context;

static_assert(std::is_trivially_copyable_v<std::complex<double>>);

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

// Fixture A: small-grid Munk CC coherent case (real Munk profile via the
// shared fixture machinery), single source, default influence settings
// (imageCount = 3). Fixture C reuses this geometry with the WKB width mode.
[[nodiscard]] SimulationCase makeMunkSmallCase(
    FrequencyGrid frequencies = FrequencyGrid({50.0, 250.0}),
    BeamWidthMode widthMode = BeamWidthMode::MinimumWidth) {
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
      rayreuse::SourceBeamPattern::omnidirectional(),
      rayreuse::SimulationRunMode::Coherent, rayreuse::BeamFamily::CervenyGaussian,
      rayreuse::FieldComponent::Pressure, rayreuse::BoundaryCurvatureMode::Standard,
      widthMode, rayreuse::CervenyCoordinateSystem::Cartesian);
}

// Fixture B (worklist R05 D5 / design §13 risk 3): constant-speed water with
// a lossy reflecting acoustic half-space bottom whose attenuation follows a
// power law in frequency, so the cumulative projected amplitude crosses the
// legacy 0.005 cutoff at DIFFERENT points for the two frequencies. This is
// the divergent per-frequency active prefix path of the fused kernel
// (union-prefix traversal + per-frequency left-endpoint active checks +
// terminal retention).
[[nodiscard]] SimulationCase makeDivergentPrefixCase() {
  constexpr double kRadiansPerDegree = std::numbers::pi / 180.0;
  return SimulationCase(
      Environment(
          SoundSpeedProfile(
              {SoundSpeedPoint{
                   .depth = 0.0, .soundSpeed = 1500.0, .density = 1000.0},
               SoundSpeedPoint{
                   .depth = 100.0, .soundSpeed = 1500.0, .density = 1000.0}}),
          BoundaryModel::vacuum(0.0),
          BoundaryModel::acousticHalfSpace(
              100.0, AcousticMaterial{
                         .compressionalSoundSpeed = 1700.0,
                         .shearSoundSpeed = 0.0,
                         .density = 1800.0,
                         .compressionalAttenuation = {
                             .value = 10.0,
                             .unit = AttenuationUnit::DecibelsPerMeterPowerLaw,
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
                         .maximumRayPoints = 4000U});
}

// First inactive point index of a projected state (points.size() when every
// point stays active) — the active prefix length the CC kernel derives via
// the same scan.
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
// so the Level B/C comparison cannot pass vacuously. Prints the observed
// prefix indices for the report.
void checkDivergentPrefixes(Context& context,
                            const rayreuse::RayPathCache& cache,
                            const SimulationCase& simulation,
                            const char* label) {
  const std::vector<double>& frequencies = simulation.frequencies().values();
  const FrequencyProjector projector(simulation.environment());
  std::size_t divergentRays = 0U;
  std::size_t truncatedRays = 0U;
  std::size_t exampleRay = 0U;
  std::size_t exampleLowPrefix = 0U;
  std::size_t exampleHighPrefix = 0U;
  std::size_t rayIndex = 0U;
  for (const rayreuse::RayPath& path : cache.paths()) {
    const std::size_t low =
        activePrefixLength(projector.project(path, frequencies.front(), 1.0));
    const std::size_t high =
        activePrefixLength(projector.project(path, frequencies.back(), 1.0));
    if (low != high) {
      ++divergentRays;
      exampleRay = rayIndex;
      exampleLowPrefix = low;
      exampleHighPrefix = high;
    }
    if (low < path.points.size() || high < path.points.size()) {
      ++truncatedRays;
    }
    ++rayIndex;
  }
  std::cout << "fused-cc-parity " << label
            << ": rays=" << cache.size() << " divergent-prefix rays="
            << divergentRays << " cutoff-truncated rays=" << truncatedRays
            << " example ray " << exampleRay << " prefix(f="
            << frequencies.front() << ")=" << exampleLowPrefix
            << " prefix(f=" << frequencies.back() << ")=" << exampleHighPrefix
            << '\n';
  context.check(divergentRays > 0U && truncatedRays > 0U,
                std::string(label) +
                    " Fixture B prefixes genuinely diverge across frequencies "
                    "(no vacuous pass)");
}

// Levels B + C + A on one fixture. `settings` flows to both sides so the
// comparison isolates the fused-vs-reuse path, not the influence options.
void testParityLevels(Context& context, const SimulationCase& simulation,
                      const CartesianCervenySettings& settings,
                      const char* label, std::size_t workerCount = 1U) {
  const std::vector<double>& frequencies = simulation.frequencies().values();

  // Shared frozen fan: traced once, consumed by every level below.
  const rayreuse::RayFanTraceResult trace =
      SingleFrequencySolver::traceSourceFan(simulation, 0U);

  // Level B reference: the exact reuse accumulation path (project + fused CC
  // accumulation) minus scaling, via the R02-frozen Raw seam.
  std::vector<FrequencyWorkspace> rawReuse;
  rawReuse.reserve(frequencies.size());
  for (const double frequency : frequencies) {
    SingleFrequencyResult result =
        SingleFrequencySolver::solveFrequencyFromSourceCache(
            simulation, frequency, trace.cache, 0U, 1.0, 50.0, settings,
            WorkspaceDelivery::Raw);
    rawReuse.push_back(std::move(result.workspace));
  }

  // Level B fused side.
  const rayreuse::FusedAccumulationResult fused =
      FusedRayReuseSolver::accumulateFrequencies(
          simulation, trace.cache, 1.0,
          50.0, settings,
          rayreuse::FusedRayReuseExecutionSettings{
              .requestedRangeWorkers = workerCount});
  context.check(fused.rawWorkspace.frequencyCount() == frequencies.size() &&
                    fused.rayCount == trace.cache.size() &&
                    fused.rayCacheBytes == trace.cache.memoryFootprintBytes() &&
                    fused.requestedRangeWorkers == workerCount &&
                    fused.effectiveRangeWorkers ==
                        std::min(workerCount,
                                 simulation.receivers().rangeCount()),
                std::string(label) + " Level B result carries every frequency "
                                     "and the shared cache metrics");
  // Timing fields exist and follow the frozen seam contract; no timing values
  // are asserted (they are wall-clock dependent).
  context.check(fused.timings.traceSeconds == 0.0 &&
                    fused.timings.scaleSeconds == 0.0 &&
                    fused.timings.projectSeconds >= 0.0 &&
                    fused.timings.influenceSeconds >= 0.0,
                std::string(label) + " Level B timings follow the raw seam "
                                     "contract (no scale time, block phases)");
  for (std::size_t frequencyIndex = 0U; frequencyIndex < frequencies.size();
       ++frequencyIndex) {
    const FrequencyWorkspace fusedWorkspace =
        fused.rawWorkspace.materializeFrequency(
            frequencyIndex, frequencies[frequencyIndex],
            simulation.receivers());
    const WorkspaceByteComparison levelB = memcmpPressureSpan(
        rawReuse[frequencyIndex], fusedWorkspace, frequencyIndex);
    context.check(levelB.equal,
                  std::string(label) + " Level B raw workspace bitwise "
                                      "parity: " +
                                      levelB.detail);
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
      std::string(label) +
          " Level C reports requested/effective range workers");

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

  // Level A: no throw happened implicitly (solveStreaming returned); the
  // fused fingerprint is stable and equals the serial reuse fingerprint.
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

  // Fixture A: Munk CC coherent, default settings (imageCount = 3).
  testParityLevels(context, makeMunkSmallCase(), CartesianCervenySettings{},
                   "fixture A (munk CC, 3 images)");
  // Receiver-range parallel gate: 16 frequencies and >= 8 real ranges, with
  // every requested worker count used by the performance screen.
  {
    const SimulationCase parallelSimulation = makeMunkSmallCase(FrequencyGrid(
        {50.0, 100.0, 150.0, 200.0, 250.0, 300.0, 350.0, 400.0, 450.0,
         500.0, 550.0, 600.0, 650.0, 700.0, 750.0, 800.0}));
    for (const std::size_t workerCount : {1U, 2U, 4U, 8U}) {
      const std::string label =
          "fixture A parallel (16F, " + std::to_string(workerCount) +
          " workers)";
      testParityLevels(context, parallelSimulation,
                       CartesianCervenySettings{}, label.c_str(), workerCount);
    }
  }
  // Fixture A2: kernel dispatch coverage for imageCount = 2.
  testParityLevels(context, makeMunkSmallCase(),
                   CartesianCervenySettings{.imageCount = 2U},
                   "fixture A2 (munk CC, 2 images)");
  // Fixture B: divergent per-frequency active prefixes (highest-risk D5
  // path). The divergence guard runs first so a degenerate fixture fails
  // loudly instead of passing vacuously.
  {
    const SimulationCase simulation = makeDivergentPrefixCase();
    const rayreuse::RayFanTraceResult trace =
        SingleFrequencySolver::traceSourceFan(simulation, 0U);
    checkDivergentPrefixes(context, trace.cache, simulation,
                           "fixture B (lossy halfspace)");
    testParityLevels(context, simulation, CartesianCervenySettings{},
                     "fixture B parallel (lossy halfspace, 8 workers)", 8U);
  }
  // Fixture C: WKB beam width variant on the Fixture A geometry (epsilon
  // real; alternate KMAH branch rule inside updateCervenyKmah).
  testParityLevels(context,
                   makeMunkSmallCase(FrequencyGrid({50.0, 250.0}),
                                     BeamWidthMode::Wkb),
                   CartesianCervenySettings{}, "fixture C (munk CC, WKB)");

  if (context.failureCount() != 0) {
    std::cerr << context.failureCount()
              << " fused-cc-parity assertion(s) failed\n";
    return 1;
  }
  std::cout << "All Bellhop RayReuse fused-cc-parity tests passed\n";
  return 0;
}
