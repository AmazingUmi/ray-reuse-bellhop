#include "rayreuse/solver/fused_ray_reuse_solver.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "rayreuse/field/beam_epsilon.hpp"
#include "rayreuse/field/frequency_projector.hpp"
#include "rayreuse/field/frequency_workspace.hpp"
#include "rayreuse/field/pressure_scaling.hpp"
#include "rayreuse/field/ray_centered_cerveny_influence.hpp"
#include "rayreuse/model/simulation_case.hpp"
#include "rayreuse/model/sound_speed_evaluator.hpp"
#include "rayreuse/solver/serial_ray_reuse_solver.hpp"
#include "rayreuse/solver/single_frequency_solver.hpp"
#include "support/munk_case_fixture.hpp"
#include "support/test_harness.hpp"

// IGR-3A A03 numerical parity gates for the Ray-Centered Cerveny fused
// kernels (design §5/§8/§11), every fused mode gated separately (C, I, S):
//   Level B — raw bitwise parity per frequency: the legacy reuse
//             accumulation (Raw seam for coherent pressure; the public
//             RayCenteredCervenyInfluence entries built from the exact
//             solver loop for I/S intensity, plus the converted Raw seam)
//             vs the fused workspaces materialized per frequency
//             (std::memcmp over the payload span bytes).
//   Level C — scaled workspace bitwise parity per frequency via the two
//             production paths (SerialRayReuseSolver::solve vs
//             FusedRayReuseSolver::solveStreaming).
//   Level D — worker counts 1/2/4/8, each gated against the same serial
//             reference (transitively identical raw bytes).
//   Level A — fused fingerprint before == after and == the serial reuse
//             fingerprint on the same case.
// A03-specific risk coverage (design §8): the per-frequency-lane persistent
// image-normal flip parity. Fixture B uses frequencies with DIFFERING active
// prefixes and a runtime guard proving that the per-lane flip-parity states
// genuinely diverge (gate-accepted step counts of odd difference), so the
// Level B comparisons cannot pass vacuously — for the coherent AND the
// intensity payload.

namespace {

using rayreuse::AcousticMaterial;
using rayreuse::AttenuationUnit;
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

// Fixture A geometry (the CC parity tests' Munk small case) in ray-centered
// coordinates: single source, real Munk profile, default influence settings
// (imageCount = 3). The field component and width mode are dimensions of
// the RC kernel (fixtures C/D/E vary them).
[[nodiscard]] SimulationCase makeMunkRayCenteredCase(
    SimulationRunMode runMode,
    FrequencyGrid frequencies = FrequencyGrid({50.0, 250.0}),
    BeamWidthMode widthMode = BeamWidthMode::MinimumWidth,
    FieldComponent fieldComponent = FieldComponent::Pressure) {
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
      SourceBeamPattern::omnidirectional(), runMode,
      rayreuse::BeamFamily::CervenyGaussian, fieldComponent,
      rayreuse::BoundaryCurvatureMode::Standard, widthMode,
      CervenyCoordinateSystem::RayCentered);
}

// Fixture B (divergent per-frequency active prefixes, the A03 flip-parity
// risk): constant-speed water with a lossy reflecting acoustic half-space
// bottom whose attenuation follows a power law in frequency, so the
// cumulative projected amplitude crosses the legacy 0.005 cutoff at
// DIFFERENT points for the two frequencies.
[[nodiscard]] SimulationCase makeDivergentPrefixRayCenteredCase(
    SimulationRunMode runMode) {
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
      SourceBeamPattern::omnidirectional(), runMode,
      rayreuse::BeamFamily::CervenyGaussian, FieldComponent::Pressure,
      rayreuse::BoundaryCurvatureMode::Standard,
      BeamWidthMode::MinimumWidth, CervenyCoordinateSystem::RayCentered);
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

// Number of steps [1, prefix) whose near-horizontal geometric gate passes
// (|normal.depth| >= eps; the flip never touches the depth component, so
// the gate is frequency-independent). Its parity per frequency is exactly
// the per-lane flip-parity divergence quantity of design §8.
[[nodiscard]] std::size_t gateAcceptedStepCount(const rayreuse::RayPath& path,
                                                std::size_t prefix) {
  std::size_t count = 0U;
  for (std::size_t rightIndex = 1U; rightIndex < prefix; ++rightIndex) {
    const rayreuse::RayState& point = path.points[rightIndex];
    const rayreuse::Vec2 tangent = point.soundSpeed * point.slowness;
    if (std::abs(tangent.range) >=
        std::numeric_limits<double>::epsilon()) {
      ++count;
    }
  }
  return count;
}

// Fixture B guard: the per-frequency active prefixes must genuinely diverge
// AND the per-lane flip-parity states must differ (odd difference of
// gate-accepted step counts within each lane's own prefix) so the Level B
// comparisons cannot pass vacuously — the A03-specific risk (design §8).
void checkDivergentPrefixesAndFlipParity(Context& context,
                                         const rayreuse::RayPathCache& cache,
                                         const SimulationCase& simulation,
                                         const char* label) {
  const std::vector<double>& frequencies = simulation.frequencies().values();
  const FrequencyProjector projector(simulation.environment());
  std::size_t divergentRays = 0U;
  std::size_t truncatedRays = 0U;
  std::size_t flipParityDivergentRays = 0U;
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
    if (gateAcceptedStepCount(path, prefixes.front()) % 2U !=
        gateAcceptedStepCount(path, prefixes.back()) % 2U) {
      ++flipParityDivergentRays;
    }
  }
  std::cout << "fused-rc-parity " << label
            << ": rays=" << cache.size() << " divergent-prefix rays="
            << divergentRays << " cutoff-truncated rays=" << truncatedRays
            << " flip-parity divergent rays=" << flipParityDivergentRays
            << '\n';
  context.check(divergentRays > 0U && truncatedRays > 0U,
                std::string(label) +
                    " Fixture B prefixes genuinely diverge across frequencies "
                    "(no vacuous pass)");
  context.check(flipParityDivergentRays > 0U,
                std::string(label) +
                    " Fixture B per-lane flip parities genuinely diverge "
                    "(A03 flip-parity risk is exercised)");
}

// Legacy reuse raw intensity reference for one frequency: the exact solver
// loop of SingleFrequencySolver::solveFrequencyFromSourceCache
// (single_frequency_solver.cpp:281-350, intensity branch) built from public
// pieces — projection, Lloyd-mirror amplitude for S, epsilon, and the
// RayCenteredCervenyInfluence kernel via its public accumulateIntensity
// entry (the exact entry the solver's reuse path takes).
[[nodiscard]] IntensityWorkspace legacyRawIntensity(
    const SimulationCase& simulation, const rayreuse::RayPathCache& cache,
    double frequency, const CartesianCervenySettings& settings) {
  const Source& source = simulation.sources().front();
  const rayreuse::LaunchFanPlan& launchFan = simulation.launchFanPlan();
  const rayreuse::GeometrySspEvaluator soundSpeedProfile(
      simulation.environment().soundSpeedProfile());
  const rayreuse::SoundSpeedSample sourceSample = soundSpeedProfile.evaluate(
      rayreuse::Vec2{.range = 0.0, .depth = source.depth}, 0U);
  const double sourceSoundSpeed = sourceSample.soundSpeed;
  IntensityWorkspace workspace(frequency, simulation.receivers());
  const FrequencyProjector projector(simulation.environment());
  const rayreuse::RayCenteredCervenyInfluence kernel(
      simulation.environment(), simulation.receivers(), settings,
      simulation.beamWidthMode(), simulation.runMode(),
      simulation.fieldComponent(), simulation.sourceGeometry());
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
    const rayreuse::BeamEpsilon epsilon = rayreuse::pickBeamEpsilon(
        simulation.beamWidthMode(), frequency, sourceSoundSpeed,
        sourceSample.soundSpeedGradient.depth, path.launchAngle,
        launchFan.launchAngleStep, 50.0, 1.0);
    static_cast<void>(kernel.accumulateIntensity(workspace, path,
                                                 frequencyState,
                                                 epsilon.value));
  }
  return workspace;
}

// Levels B + C + A on one fixture for one run mode (C, I, or S). `settings`
// flows to both sides so the comparison isolates the fused-vs-reuse path.
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
          legacyRawIntensity(simulation, trace.cache, frequency, settings));
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
      // Converted seam: the same scaleCartesianIntensityToPressure call the
      // fused sink chain makes (family-based selector — Cerveny in both
      // coordinate systems) must reproduce the production solver's raw
      // delivery byte for byte.
      const FrequencyWorkspace fusedConverted =
          rayreuse::scaleCartesianIntensityToPressure(
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
  // Fixture A: Munk RC, default settings (imageCount = 3).
  testParityLevels(context, makeMunkRayCenteredCase(runMode),
                   CartesianCervenySettings{},
                   (std::string("fixture A (munk RC ") + modeLabel +
                    ", 3 images)")
                       .c_str());
  // Worker-count gate (Level D): 16 frequencies and >= 8 real ranges, every
  // requested worker count gated against the same serial reference.
  {
    const SimulationCase parallelSimulation = makeMunkRayCenteredCase(
        runMode, FrequencyGrid({50.0, 100.0, 150.0, 200.0, 250.0, 300.0,
                                350.0, 400.0, 450.0, 500.0, 550.0, 600.0,
                                650.0, 700.0, 750.0, 800.0}));
    for (const std::size_t workerCount : {1U, 2U, 4U, 8U}) {
      const std::string label =
          "fixture A parallel (16F, " + std::to_string(workerCount) +
          " workers, " + modeLabel + ")";
      testParityLevels(context, parallelSimulation,
                       CartesianCervenySettings{}, label.c_str(), workerCount);
    }
  }
  // Fixture A2: kernel image-loop coverage for imageCount = 2 (flip
  // relevance requires imageCount >= 2, design §8).
  testParityLevels(
      context, makeMunkRayCenteredCase(runMode),
      CartesianCervenySettings{.imageCount = 2U},
      (std::string("fixture A2 (munk RC ") + modeLabel + ", 2 images)")
          .c_str());
  // Fixture B: divergent per-frequency active prefixes AND divergent
  // per-lane flip parities (the A03-specific risk). The divergence guards
  // run first so a degenerate fixture fails loudly instead of passing
  // vacuously.
  {
    const SimulationCase simulation =
        makeDivergentPrefixRayCenteredCase(runMode);
    const rayreuse::RayFanTraceResult trace =
        SingleFrequencySolver::traceSourceFan(simulation, 0U);
    checkDivergentPrefixesAndFlipParity(
        context, trace.cache, simulation,
        (std::string("fixture B (lossy halfspace, ") + modeLabel + ")")
            .c_str());
    testParityLevels(
        context, simulation, CartesianCervenySettings{},
        (std::string("fixture B parallel (lossy halfspace, 8 workers, ") +
         modeLabel + ")")
            .c_str(),
        8U);
  }
  // Fixture C: WKB beam width variant on the Fixture A geometry (epsilon
  // real; alternate KMAH branch rule inside updateCervenyKmah).
  testParityLevels(
      context,
      makeMunkRayCenteredCase(runMode, FrequencyGrid({50.0, 250.0}),
                              BeamWidthMode::Wkb),
      CartesianCervenySettings{},
      (std::string("fixture C (munk RC ") + modeLabel + ", WKB)").c_str());
}

}  // namespace

int main() {
  Context context;

  // Gate behavior: the shared fused-support predicate accepts Ray-Centered
  // Cerveny C/I/S (design §9).
  context.check(
      supportsFusedRayReuse(makeMunkRayCenteredCase(
          SimulationRunMode::Coherent)) &&
          supportsFusedRayReuse(makeMunkRayCenteredCase(
          SimulationRunMode::Incoherent)) &&
          supportsFusedRayReuse(makeMunkRayCenteredCase(
          SimulationRunMode::SemiCoherent)),
      "the shared fused-support predicate accepts Ray-Centered Cerveny "
      "coherent, incoherent, and semi-coherent TL");

  // The three fused modes are gated separately (design §11): they differ by
  // the sink (complex pressure vs real intensity) and, for S, by the
  // Lloyd-mirror projected amplitude in the projection layer.
  testMode(context, SimulationRunMode::Coherent, "C");
  testMode(context, SimulationRunMode::Incoherent, "I");
  testMode(context, SimulationRunMode::SemiCoherent, "S");

  // Component fixtures (coherent): the V/H factor branches of the RC
  // pressure path (ray_centered_cerveny_influence.cpp:417-435) — Fortran
  // DOT_PRODUCT conjugation for V, handwritten unconjugated form for H.
  testParityLevels(
      context,
      makeMunkRayCenteredCase(SimulationRunMode::Coherent,
                              FrequencyGrid({50.0, 250.0}),
                              BeamWidthMode::MinimumWidth,
                              FieldComponent::Vertical),
      CartesianCervenySettings{},
      "fixture D (munk RC C, Vertical component)");
  testParityLevels(
      context,
      makeMunkRayCenteredCase(SimulationRunMode::Coherent,
                              FrequencyGrid({50.0, 250.0}),
                              BeamWidthMode::MinimumWidth,
                              FieldComponent::Horizontal),
      CartesianCervenySettings{},
      "fixture E (munk RC C, Horizontal component)");

  if (context.failureCount() != 0) {
    std::cerr << context.failureCount()
              << " fused-rc-parity assertion(s) failed\n";
    return 1;
  }
  std::cout << "All Bellhop RayReuse fused-rc-parity tests passed\n";
  return 0;
}
