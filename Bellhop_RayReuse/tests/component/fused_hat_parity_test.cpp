#include "rayreuse/solver/fused_ray_reuse_solver.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "rayreuse/field/frequency_projector.hpp"
#include "rayreuse/field/frequency_workspace.hpp"
#include "rayreuse/field/geometric_hat_influence.hpp"
#include "rayreuse/field/pressure_scaling.hpp"
#include "rayreuse/model/simulation_case.hpp"
#include "rayreuse/model/sound_speed_evaluator.hpp"
#include "rayreuse/solver/serial_ray_reuse_solver.hpp"
#include "rayreuse/solver/single_frequency_solver.hpp"
#include "support/munk_case_fixture.hpp"
#include "support/test_harness.hpp"

// IGR-3A A04 numerical parity gates for the Geometric Hat fused kernels in
// BOTH coordinate systems (design §5/§8/§11), every fused mode gated
// separately (C, I, S):
//   Level B — raw bitwise parity per frequency: the legacy reuse
//             accumulation (Raw seam for coherent pressure; the public
//             GeometricHatInfluence entries built from the exact solver loop
//             for I/S intensity, plus the converted Raw seam) vs the fused
//             workspaces materialized per frequency (std::memcmp over the
//             payload span bytes).
//   Level C — scaled workspace bitwise parity per frequency via the two
//             production paths (SerialRayReuseSolver::solve vs
//             FusedRayReuseSolver::solveStreaming).
//   Level D — worker counts 1/2/4/8, each gated against the same serial
//             reference (transitively identical raw bytes).
//   Level A — fused fingerprint before == after and == the serial reuse
//             fingerprint on the same case.
// A04-specific risk coverage (design §8): the union-prefix traversal over
// per-frequency-diverging active prefixes. The Cartesian traversal shares
// the monotone receiver cursor and the caustic-phase accumulation across
// lanes; the ray-centered traversal evolves the per-depth persistent caustic
// phase, previous-Q state, and receiver anchors ONCE over the union prefix.
// Fixture B uses frequencies with DIFFERING active prefixes (lossy
// half-space bottom, power-law attenuation in frequency) with a runtime
// guard proving the divergence is non-vacuous, per coordinate.

namespace {

using rayreuse::AcousticMaterial;
using rayreuse::AttenuationUnit;
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
using rayreuse::GeometricHatInfluence;
using rayreuse::IntensityWorkspace;
using rayreuse::IntegratorSettings;
using rayreuse::LaunchFan;
using rayreuse::ReceiverGrid;
using rayreuse::SerialRayReuseResult;
using rayreuse::SerialRayReuseSolver;
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

// Byte-level (std::memcmp) comparison over the full intensity span; never
// operator==. Reports the first differing element and both values.
[[nodiscard]] WorkspaceByteComparison memcmpIntensitySpan(
    const IntensityWorkspace& reference, const IntensityWorkspace& fused,
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
  const std::span<const double> expected = reference.intensity();
  const std::span<const double> actual = fused.intensity();
  if (expected.size() != actual.size()) {
    return {.equal = false,
            .detail = prefix + " intensity span size mismatch (" +
                      std::to_string(expected.size()) + " vs " +
                      std::to_string(actual.size()) + ")"};
  }
  if (std::memcmp(expected.data(), actual.data(), expected.size_bytes()) ==
      0) {
    return {.equal = true, .detail = prefix};
  }
  for (std::size_t index = 0U; index < expected.size(); ++index) {
    if (std::memcmp(&expected[index], &actual[index], sizeof(double)) != 0) {
      return {.equal = false,
              .detail = prefix + " first differing element " +
                        std::to_string(index) + ": reuse (" +
                        std::to_string(expected[index]) + ") vs fused (" +
                        std::to_string(actual[index]) + ")"};
    }
  }
  return {.equal = false, .detail = prefix + " memcmp failed"};  // unreachable
}

[[nodiscard]] const char* coordinateLabel(
    CervenyCoordinateSystem coordinates) {
  return coordinates == CervenyCoordinateSystem::RayCentered ? "RC" : "CC";
}

// Fixture A geometry (the Cerveny parity tests' Munk small case) as a
// geometric hat run in the requested coordinate system: single source, real
// Munk profile, caustic-crossing q dynamics. The grid ranges are equally
// spaced, satisfying the ray-centered hat ctor validation.
[[nodiscard]] SimulationCase makeMunkHatCase(
    SimulationRunMode runMode, CervenyCoordinateSystem coordinates,
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
      SourceBeamPattern::omnidirectional(), runMode, BeamFamily::GeometricHat,
      FieldComponent::Pressure, BoundaryCurvatureMode::Standard,
      BeamWidthMode::MinimumWidth, coordinates);
}

// Fixture B (divergent per-frequency active prefixes, the A04 union-prefix
// risk): constant-speed water with a lossy reflecting acoustic half-space
// bottom whose attenuation follows a power law in frequency, so the
// cumulative projected amplitude crosses the legacy 0.005 cutoff at
// DIFFERENT points for the two frequencies.
[[nodiscard]] SimulationCase makeDivergentPrefixHatCase(
    SimulationRunMode runMode, CervenyCoordinateSystem coordinates) {
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
                         .maximumRayPoints = 4000U},
      SourceBeamPattern::omnidirectional(), runMode, BeamFamily::GeometricHat,
      FieldComponent::Pressure, BoundaryCurvatureMode::Standard,
      BeamWidthMode::MinimumWidth, coordinates);
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
// so the Level B comparisons cannot pass vacuously — for BOTH payloads (the
// coherent and intensity twins share the union-prefix traversal).
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
  std::cout << "fused-hat-parity " << label
            << ": rays=" << cache.size() << " divergent-prefix rays="
            << divergentRays << " cutoff-truncated rays=" << truncatedRays
            << '\n';
  context.check(divergentRays > 0U && truncatedRays > 0U,
                std::string(label) +
                    " Fixture B prefixes genuinely diverge across frequencies "
                    "(no vacuous pass)");
}

// Legacy reuse raw intensity reference for one frequency: the exact solver
// loop of SingleFrequencySolver::solveFrequencyFromSourceCache
// (single_frequency_solver.cpp:281-350, geometric hat intensity branch)
// built from public pieces — projection, Lloyd-mirror amplitude for S, and
// the GeometricHatInfluence kernel via its public accumulateIntensity entry
// (the exact entry the solver's reuse path takes; no shared code with the
// fused side).
[[nodiscard]] IntensityWorkspace legacyRawIntensity(
    const SimulationCase& simulation, const rayreuse::RayPathCache& cache,
    double frequency) {
  const Source& source = simulation.sources().front();
  const rayreuse::LaunchFanPlan& launchFan = simulation.launchFanPlan();
  const rayreuse::GeometrySspEvaluator soundSpeedProfile(
      simulation.environment().soundSpeedProfile());
  const rayreuse::SoundSpeedSample sourceSample = soundSpeedProfile.evaluate(
      rayreuse::Vec2{.range = 0.0, .depth = source.depth}, 0U);
  const double sourceSoundSpeed = sourceSample.soundSpeed;
  IntensityWorkspace workspace(frequency, simulation.receivers());
  const FrequencyProjector projector(simulation.environment());
  const GeometricHatInfluence kernel(simulation.receivers(),
                                     simulation.cervenyCoordinateSystem(),
                                     simulation.sourceGeometry());
  for (const rayreuse::RayPath& path : cache.paths()) {
    const double patternAmplitude =
        simulation.sourceBeamPattern().amplitudeForLaunchAngle(
            path.launchAngle);
    const double baseSourceAmplitude = source.amplitude * patternAmplitude;
    const double projectedSourceAmplitude =
        rayreuse::usesLloydMirror(simulation.runMode())
            ? rayreuse::semiCoherentProjectedSourceAmplitude(
                  baseSourceAmplitude, frequency, sourceSoundSpeed,
                  source.depth, path.launchAngle)
            : baseSourceAmplitude;
    if (!std::isfinite(projectedSourceAmplitude) ||
        projectedSourceAmplitude < 0.0) {
      throw std::runtime_error(
          "legacy intensity reference produced an invalid projected "
          "amplitude");
    }
    const rayreuse::RayFrequencyState frequencyState =
        projector.project(path, frequency, projectedSourceAmplitude);
    static_cast<void>(kernel.accumulateIntensity(
        workspace, path, frequencyState, launchFan.launchAngleStep));
  }
  return workspace;
}

// Levels B + C + A on one fixture for one run mode and coordinate system.
// `settings` flows to both sides so the comparison isolates the
// fused-vs-reuse path (the Hat kernel ignores it — no epsilon channel).
void testParityLevels(Context& context, const SimulationCase& simulation,
                      const CartesianCervenySettings& settings,
                      const char* label, std::size_t workerCount = 1U) {
  const bool coherentRunMode =
      simulation.runMode() == SimulationRunMode::Coherent;
  const std::vector<double>& frequencies = simulation.frequencies().values();
  const rayreuse::Source& source = simulation.sources().front();
  const rayreuse::LaunchFanPlan& launchFan = simulation.launchFanPlan();
  const rayreuse::GeometrySspEvaluator soundSpeedProfile(
      simulation.environment().soundSpeedProfile());
  const rayreuse::SoundSpeedSample sourceSample = soundSpeedProfile.evaluate(
      rayreuse::Vec2{.range = 0.0, .depth = source.depth}, 0U);
  const double sourceSoundSpeed = sourceSample.soundSpeed;

  // Shared frozen fan: traced once, consumed by every level below.
  const rayreuse::RayFanTraceResult trace =
      SingleFrequencySolver::traceSourceFan(simulation, 0U);

  // Level B references. Coherent: the production Raw seam returns the raw
  // pressure directly. I/S: solveFrequencyFromSourceCache(Raw) returns the
  // converted pressure workspace (the conversion is workspace construction,
  // single_frequency_solver.cpp:356-369), so the raw double payload gate
  // uses the public-kernel reference built above, plus the converted
  // end-to-end seam.
  std::vector<FrequencyWorkspace> rawReuseConverted;
  std::vector<IntensityWorkspace> rawReuseIntensity;
  rawReuseConverted.reserve(frequencies.size());
  rawReuseIntensity.reserve(frequencies.size());
  for (const double frequency : frequencies) {
    SingleFrequencyResult result =
        SingleFrequencySolver::solveFrequencyFromSourceCache(
            simulation, frequency, trace.cache, 0U, 1.0, 50.0, settings,
            WorkspaceDelivery::Raw);
    rawReuseConverted.push_back(std::move(result.workspace));
    if (!coherentRunMode) {
      rawReuseIntensity.push_back(
          legacyRawIntensity(simulation, trace.cache, frequency));
    }
  }

  // Level B fused side (also Level D: each worker count is gated against the
  // same serial reference, so raw bytes are transitively identical across
  // 1/2/4/8).
  if (coherentRunMode) {
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
        std::string(label) + " Level B result carries every frequency and "
                             "the shared cache metrics");
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
          rawReuseConverted[frequencyIndex], fusedWorkspace, frequencyIndex);
      context.check(levelB.equal,
                    std::string(label) + " Level B raw pressure bitwise "
                                        "parity: " +
                        levelB.detail);
    }
  } else {
    const rayreuse::FusedIntensityAccumulationResult fused =
        FusedRayReuseSolver::accumulateFrequenciesIntensity(
            simulation, trace.cache, 1.0, 50.0, settings,
            rayreuse::FusedRayReuseExecutionSettings{
                .requestedRangeWorkers = workerCount});
    context.check(
        fused.rawIntensityWorkspace.frequencyCount() ==
                frequencies.size() &&
            fused.rayCount == trace.cache.size() &&
            fused.rayCacheBytes == trace.cache.memoryFootprintBytes() &&
            fused.requestedRangeWorkers == workerCount &&
            fused.effectiveRangeWorkers ==
                std::min(workerCount, simulation.receivers().rangeCount()),
        std::string(label) + " Level B result carries every frequency and "
                             "the shared cache metrics");
    for (std::size_t frequencyIndex = 0U; frequencyIndex < frequencies.size();
         ++frequencyIndex) {
      const IntensityWorkspace fusedWorkspace =
          fused.rawIntensityWorkspace.materializeIntensityFrequency(
              frequencyIndex, frequencies[frequencyIndex],
              simulation.receivers());
      const WorkspaceByteComparison levelB = memcmpIntensitySpan(
          rawReuseIntensity[frequencyIndex], fusedWorkspace, frequencyIndex);
      context.check(levelB.equal,
                    std::string(label) + " Level B raw intensity bitwise "
                                        "parity: " +
                        levelB.detail);
      // Converted seam: the same scaleGeometricIntensityToPressure call the
      // fused sink chain makes (family-based selector — Hat in both
      // coordinate systems) must reproduce the production solver's raw
      // delivery byte for byte.
      const FrequencyWorkspace fusedConverted =
          rayreuse::scaleGeometricIntensityToPressure(
              fusedWorkspace, simulation.receivers(),
              launchFan.launchAngleStep, sourceSoundSpeed,
              simulation.sourceGeometry());
      const WorkspaceByteComparison levelBConverted = memcmpPressureSpan(
          rawReuseConverted[frequencyIndex], fusedConverted, frequencyIndex);
      context.check(levelBConverted.equal,
                    std::string(label) + " Level B converted seam bitwise "
                                        "parity: " +
                        levelBConverted.detail);
    }
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
        std::string(label) + " Level C captured converted fused workspace "
                             "and scale timing for frequency index " +
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

void testMode(Context& context, SimulationRunMode runMode,
              const char* modeLabel) {
  for (const CervenyCoordinateSystem coordinates :
       {CervenyCoordinateSystem::Cartesian,
        CervenyCoordinateSystem::RayCentered}) {
    const char* const coordinatesLabel = coordinateLabel(coordinates);
    // Fixture A: Munk hat, both coordinates.
    testParityLevels(
        context, makeMunkHatCase(runMode, coordinates),
        CartesianCervenySettings{},
        (std::string("fixture A (munk hat ") + coordinatesLabel + ' ' +
         modeLabel + ')')
            .c_str());
    // Worker-count gate (Level D): 16 frequencies and >= 8 real ranges,
    // every requested worker count gated against the same serial reference.
    {
      const SimulationCase parallelSimulation = makeMunkHatCase(
          runMode, coordinates,
          FrequencyGrid({50.0, 100.0, 150.0, 200.0, 250.0, 300.0, 350.0,
                         400.0, 450.0, 500.0, 550.0, 600.0, 650.0, 700.0,
                         750.0, 800.0}));
      for (const std::size_t workerCount : {1U, 2U, 4U, 8U}) {
        const std::string label =
            "fixture A parallel (16F, " + std::to_string(workerCount) +
            " workers, hat " + coordinatesLabel + ", " + modeLabel + ")";
        testParityLevels(context, parallelSimulation,
                         CartesianCervenySettings{}, label.c_str(),
                         workerCount);
      }
    }
    // Fixture B: divergent per-frequency active prefixes (highest-risk
    // union-prefix path). The divergence guard runs first so a degenerate
    // fixture fails loudly instead of passing vacuously.
    {
      const SimulationCase simulation =
          makeDivergentPrefixHatCase(runMode, coordinates);
      const rayreuse::RayFanTraceResult trace =
          SingleFrequencySolver::traceSourceFan(simulation, 0U);
      checkDivergentPrefixes(
          context, trace.cache, simulation,
          (std::string("fixture B (lossy halfspace, hat ") + coordinatesLabel +
           ' ' + modeLabel + ')')
              .c_str());
      testParityLevels(
          context, simulation, CartesianCervenySettings{},
          (std::string("fixture B parallel (lossy halfspace, 8 workers, hat ") +
           coordinatesLabel + ", " + modeLabel + ')')
              .c_str(),
          8U);
    }
  }
}

}  // namespace

int main() {
  Context context;

  // Scope acceptance is gated per family x mode x coordinate in
  // rayreuse.component.fused_solver; one assertion here keeps the parity
  // test honest about the domain it exercises.
  context.check(
      supportsFusedRayReuse(makeMunkHatCase(SimulationRunMode::Coherent,
                                            CervenyCoordinateSystem::
                                                Cartesian)) &&
          supportsFusedRayReuse(makeMunkHatCase(
              SimulationRunMode::SemiCoherent,
              CervenyCoordinateSystem::RayCentered)),
      "the fused-support predicate accepts hat fixtures in both coordinates "
      "and modes");

  // C, I, and S are gated separately (design §11): I and S differ by the
  // Lloyd-mirror projected source amplitude applied for S in the projection
  // layer; the accumulation kernels are identical.
  testMode(context, SimulationRunMode::Coherent, "C");
  testMode(context, SimulationRunMode::Incoherent, "I");
  testMode(context, SimulationRunMode::SemiCoherent, "S");

  if (context.failureCount() != 0) {
    std::cerr << context.failureCount()
              << " fused-hat-parity assertion(s) failed\n";
    return 1;
  }
  std::cout << "All Bellhop RayReuse fused-hat-parity tests passed\n";
  return 0;
}
