// IGR-3A src-internal unified-executor interface (design §3-§6). Not
// installed: the adapter set and the sink policy pair are compile-time
// executor plumbing; the public solver surface stays in
// rayreuse/solver/fused_ray_reuse_solver.hpp. Adapters hold no data and no
// logic beyond kernel construction, per-ray preparation, forwarding to the
// kernels' private fused entries, and the legacy post-scale selector.

#pragma once

#include <complex>
#include <cstddef>
#include <span>
#include <utility>
#include <vector>

#include "rayreuse/error.hpp"
#include "rayreuse/field/arrival_accumulator.hpp"
#include "rayreuse/field/beam_epsilon.hpp"
#include "rayreuse/field/broadband_arrival_workspace.hpp"
#include "rayreuse/field/cartesian_cerveny_influence.hpp"
#include "rayreuse/field/frequency_workspace.hpp"
#include "rayreuse/field/fused_intensity_workspace.hpp"
#include "rayreuse/field/fused_pressure_workspace.hpp"
#include "rayreuse/field/geometric_gaussian_influence.hpp"
#include "rayreuse/field/geometric_hat_influence.hpp"
#include "rayreuse/field/pressure_scaling.hpp"
#include "rayreuse/field/ray_centered_cerveny_influence.hpp"
#include "rayreuse/field/simple_gaussian_influence.hpp"
#include "rayreuse/model/simulation_case.hpp"
#include "rayreuse/ray/ray_path.hpp"
#include "rayreuse/solver/fused_ray_reuse_solver.hpp"

namespace rayreuse {

// Cartesian Cerveny adapter (design §4). makeKernel mirrors the
// single-frequency CC construction verbatim
// (single_frequency_solver.cpp:264-266); preparePerRay is the epsilon loop
// of the fused solver body (fused_ray_reuse_solver.cpp:223-231);
// accumulateFused forwards to the unchanged private fused kernel entry;
// scaleFrequency reproduces the coherent Cerveny branch of the legacy
// post-scale selector (single_frequency_solver.cpp:377-379). The intensity
// twins are the A02b construction (design §5/§6.2): accumulation forwards to
// the kernel's private fused intensity entry, and the scale hook reproduces
// the I/S Cerveny branch of the legacy post-scale selector
// (single_frequency_solver.cpp:366-369), returning the converted
// FrequencyWorkspace by value.
struct CartesianCervenyFusedAdapter {
  using Kernel = CartesianCervenyInfluence;

  // Frozen scratch shape (design §4): the per-ray epsilon lane, one value
  // per frequency, materialized by prepareScratch.
  struct PerRayScratch {
    std::vector<std::complex<double>> epsilons;
  };

  // Frozen loop-invariant input set of the per-ray epsilon loop (design
  // §4): widthMode, sourceSoundSpeed (source c0), sourceDepthGradient,
  // launchAngleStep, loopRange, epsilonMultiplier. Every adapter's context
  // is constructible from this same executor-owned argument list so the
  // unified executor stays family-agnostic; geometric families (A03+)
  // define the same constructor and ignore the Cerveny-only fields.
  struct PerRayContext {
    BeamWidthMode widthMode{};
    double sourceSoundSpeed{};
    double sourceDepthGradient{};
    double launchAngleStep{};
    double loopRange{};
    double epsilonMultiplier{};

    PerRayContext(BeamWidthMode widthModeValue, double sourceSoundSpeedValue,
                  double sourceDepthGradientValue,
                  double launchAngleStepValue, double loopRangeValue,
                  double epsilonMultiplierValue)
        : widthMode(widthModeValue),
          sourceSoundSpeed(sourceSoundSpeedValue),
          sourceDepthGradient(sourceDepthGradientValue),
          launchAngleStep(launchAngleStepValue),
          loopRange(loopRangeValue),
          epsilonMultiplier(epsilonMultiplierValue) {}
  };

  [[nodiscard]] static Kernel makeKernel(const SimulationCase& simulation,
                                         CartesianCervenySettings settings) {
    return Kernel(simulation.environment(), simulation.receivers(), settings,
                  simulation.beamWidthMode(), simulation.sourceGeometry());
  }

  static void prepareScratch(PerRayScratch& scratch,
                             std::size_t frequencyCount) {
    scratch.epsilons.assign(frequencyCount, std::complex<double>{});
  }

  static void preparePerRay(const PerRayContext& context,
                            PerRayScratch& scratch, const RayPath& path,
                            std::span<const double> frequencies) {
    for (std::size_t frequencyIndex = 0U; frequencyIndex < frequencies.size();
         ++frequencyIndex) {
      const BeamEpsilon epsilon = pickBeamEpsilon(
          context.widthMode, frequencies[frequencyIndex],
          context.sourceSoundSpeed, context.sourceDepthGradient,
          path.launchAngle, context.launchAngleStep, context.loopRange,
          context.epsilonMultiplier);
      scratch.epsilons[frequencyIndex] = epsilon.value;
    }
  }

  [[nodiscard]] static bool accumulateFused(
      const Kernel& kernel, const PerRayScratch& scratch,
      FusedPressureWorkspace& workspace,
      std::span<const double> frequencies, const RayPath& path,
      std::span<const RayFrequencyState> frequencyStates,
      std::size_t rangeBegin, std::size_t rangeEnd,
      CartesianCervenyStatistics* statistics) {
    return kernel.accumulateFusedPrevalidated(
        workspace, frequencies, path, frequencyStates,
        std::span<const std::complex<double>>(scratch.epsilons), rangeBegin,
        rangeEnd, statistics);
  }

  static void scaleFrequency(FrequencyWorkspace& workspace,
                             const ReceiverGrid& receivers,
                             double launchAngleStep, double sourceSoundSpeed,
                             SourceGeometry sourceGeometry) {
    scaleCoherentCartesianPressure(workspace, receivers, launchAngleStep,
                                   sourceSoundSpeed, sourceGeometry);
  }

  [[nodiscard]] static bool accumulateFusedIntensity(
      const Kernel& kernel, const PerRayScratch& scratch,
      FusedIntensityWorkspace& workspace,
      std::span<const double> frequencies, const RayPath& path,
      std::span<const RayFrequencyState> frequencyStates,
      std::size_t rangeBegin, std::size_t rangeEnd,
      CartesianCervenyStatistics* statistics) {
    return kernel.accumulateFusedIntensityPrevalidated(
        workspace, frequencies, path, frequencyStates,
        std::span<const std::complex<double>>(scratch.epsilons), rangeBegin,
        rangeEnd, statistics);
  }

  [[nodiscard]] static FrequencyWorkspace scaleIntensityFrequency(
      const IntensityWorkspace& workspace, const ReceiverGrid& receivers,
      double launchAngleStep, double sourceSoundSpeed,
      SourceGeometry sourceGeometry) {
    return scaleCartesianIntensityToPressure(workspace, receivers,
                                             launchAngleStep, sourceSoundSpeed,
                                             sourceGeometry);
  }
};

// Ray-Centered Cerveny adapter (IGR-3A A03, design §4). makeKernel mirrors
// the single-frequency RC construction verbatim
// (single_frequency_solver.cpp:269-272 — plus runMode() and fieldComponent(),
// which the RC ctor validates); preparePerRay is the same Cerveny epsilon
// loop as the CC adapter; the accumulation hooks forward to the private
// fused kernel entries. The scale hooks reproduce the legacy post-scale
// selector exactly per family x mode (single_frequency_solver.cpp:356-381):
// the selector is family-based, so RC coherent uses
// scaleCoherentCartesianPressure and RC I/S uses
// scaleCartesianIntensityToPressure, like CC.
struct RayCenteredCervenyFusedAdapter {
  using Kernel = RayCenteredCervenyInfluence;

  // Same frozen scratch shape as the CC adapter (design §4): the per-ray
  // epsilon lane, one value per frequency.
  struct PerRayScratch {
    std::vector<std::complex<double>> epsilons;
  };

  // Same executor-owned loop-invariant input set as the CC adapter; the
  // Cerveny-only fields are exactly the epsilon-loop inputs.
  struct PerRayContext {
    BeamWidthMode widthMode{};
    double sourceSoundSpeed{};
    double sourceDepthGradient{};
    double launchAngleStep{};
    double loopRange{};
    double epsilonMultiplier{};

    PerRayContext(BeamWidthMode widthModeValue, double sourceSoundSpeedValue,
                  double sourceDepthGradientValue,
                  double launchAngleStepValue, double loopRangeValue,
                  double epsilonMultiplierValue)
        : widthMode(widthModeValue),
          sourceSoundSpeed(sourceSoundSpeedValue),
          sourceDepthGradient(sourceDepthGradientValue),
          launchAngleStep(launchAngleStepValue),
          loopRange(loopRangeValue),
          epsilonMultiplier(epsilonMultiplierValue) {}
  };

  [[nodiscard]] static Kernel makeKernel(const SimulationCase& simulation,
                                         CartesianCervenySettings settings) {
    return Kernel(simulation.environment(), simulation.receivers(), settings,
                  simulation.beamWidthMode(), simulation.runMode(),
                  simulation.fieldComponent(), simulation.sourceGeometry());
  }

  static void prepareScratch(PerRayScratch& scratch,
                             std::size_t frequencyCount) {
    scratch.epsilons.assign(frequencyCount, std::complex<double>{});
  }

  static void preparePerRay(const PerRayContext& context,
                            PerRayScratch& scratch, const RayPath& path,
                            std::span<const double> frequencies) {
    for (std::size_t frequencyIndex = 0U; frequencyIndex < frequencies.size();
         ++frequencyIndex) {
      const BeamEpsilon epsilon = pickBeamEpsilon(
          context.widthMode, frequencies[frequencyIndex],
          context.sourceSoundSpeed, context.sourceDepthGradient,
          path.launchAngle, context.launchAngleStep, context.loopRange,
          context.epsilonMultiplier);
      scratch.epsilons[frequencyIndex] = epsilon.value;
    }
  }

  [[nodiscard]] static bool accumulateFused(
      const Kernel& kernel, const PerRayScratch& scratch,
      FusedPressureWorkspace& workspace,
      std::span<const double> frequencies, const RayPath& path,
      std::span<const RayFrequencyState> frequencyStates,
      std::size_t rangeBegin, std::size_t rangeEnd,
      CartesianCervenyStatistics* statistics) {
    return kernel.accumulateFusedPrevalidated(
        workspace, frequencies, path, frequencyStates,
        std::span<const std::complex<double>>(scratch.epsilons), rangeBegin,
        rangeEnd, statistics);
  }

  static void scaleFrequency(FrequencyWorkspace& workspace,
                             const ReceiverGrid& receivers,
                             double launchAngleStep, double sourceSoundSpeed,
                             SourceGeometry sourceGeometry) {
    scaleCoherentCartesianPressure(workspace, receivers, launchAngleStep,
                                   sourceSoundSpeed, sourceGeometry);
  }

  [[nodiscard]] static bool accumulateFusedIntensity(
      const Kernel& kernel, const PerRayScratch& scratch,
      FusedIntensityWorkspace& workspace,
      std::span<const double> frequencies, const RayPath& path,
      std::span<const RayFrequencyState> frequencyStates,
      std::size_t rangeBegin, std::size_t rangeEnd,
      CartesianCervenyStatistics* statistics) {
    return kernel.accumulateFusedIntensityPrevalidated(
        workspace, frequencies, path, frequencyStates,
        std::span<const std::complex<double>>(scratch.epsilons), rangeBegin,
        rangeEnd, statistics);
  }

  [[nodiscard]] static FrequencyWorkspace scaleIntensityFrequency(
      const IntensityWorkspace& workspace, const ReceiverGrid& receivers,
      double launchAngleStep, double sourceSoundSpeed,
      SourceGeometry sourceGeometry) {
    return scaleCartesianIntensityToPressure(workspace, receivers,
                                             launchAngleStep, sourceSoundSpeed,
                                             sourceGeometry);
  }
};

// Geometric Hat adapter (IGR-3A A04, design §4). One adapter carries BOTH
// coordinate systems: makeKernel mirrors the single-frequency Hat
// construction verbatim (single_frequency_solver.cpp:247-250) and then
// installs the run's launch-fan angle step as fused-run state — the frozen
// fused kernel entries take no spacing parameter (the Hat family has no
// epsilon channel), while the public per-frequency entries keep receiving
// it per call. The kernel owns the internal once-per-ray Cartesian /
// ray-centered traversal selection, mirroring the legacy accumulateField
// split. Hat has NO epsilon channel, so prepareScratch/preparePerRay are
// empty inline functions (compiled away, design §4); the accumulation
// hooks forward to the private fused kernel entries; the scale hooks
// reproduce the legacy post-scale selector exactly per family x mode
// (single_frequency_solver.cpp:356-381): coherent ->
// scaleCoherentGeometricPressure, I/S -> scaleGeometricIntensityToPressure
// (the selector is family-based, identical for both Hat coordinates).
struct GeometricHatFusedAdapter {
  using Kernel = GeometricHatInfluence;

  // Frozen scratch shape (design §4): the Hat family has no per-ray
  // frequency-local preparation channel — an empty struct.
  struct PerRayScratch {};

  // Same executor-owned loop-invariant input set as the Cerveny adapters
  // so the unified executor stays family-agnostic; every Cerveny-only
  // field (the epsilon-loop inputs) is ignored here.
  struct PerRayContext {
    BeamWidthMode widthMode{};
    double sourceSoundSpeed{};
    double sourceDepthGradient{};
    double launchAngleStep{};
    double loopRange{};
    double epsilonMultiplier{};

    PerRayContext(BeamWidthMode widthModeValue, double sourceSoundSpeedValue,
                  double sourceDepthGradientValue,
                  double launchAngleStepValue, double loopRangeValue,
                  double epsilonMultiplierValue)
        : widthMode(widthModeValue),
          sourceSoundSpeed(sourceSoundSpeedValue),
          sourceDepthGradient(sourceDepthGradientValue),
          launchAngleStep(launchAngleStepValue),
          loopRange(loopRangeValue),
          epsilonMultiplier(epsilonMultiplierValue) {}
  };

  [[nodiscard]] static Kernel makeKernel(const SimulationCase& simulation,
                                         CartesianCervenySettings settings) {
    static_cast<void>(settings);
    Kernel kernel(simulation.receivers(),
                  simulation.cervenyCoordinateSystem(),
                  simulation.sourceGeometry());
    kernel.setFusedLaunchAngleStep(
        simulation.launchFanPlan().launchAngleStep);
    return kernel;
  }

  static void prepareScratch(PerRayScratch& scratch,
                             std::size_t frequencyCount) {
    static_cast<void>(scratch);
    static_cast<void>(frequencyCount);
  }

  static void preparePerRay(const PerRayContext& context,
                            PerRayScratch& scratch, const RayPath& path,
                            std::span<const double> frequencies) {
    static_cast<void>(context);
    static_cast<void>(scratch);
    static_cast<void>(path);
    static_cast<void>(frequencies);
  }

  [[nodiscard]] static bool accumulateFused(
      const Kernel& kernel, const PerRayScratch& scratch,
      FusedPressureWorkspace& workspace,
      std::span<const double> frequencies, const RayPath& path,
      std::span<const RayFrequencyState> frequencyStates,
      std::size_t rangeBegin, std::size_t rangeEnd,
      CartesianCervenyStatistics* statistics) {
    static_cast<void>(scratch);
    return kernel.accumulateFusedPrevalidated(
        workspace, frequencies, path, frequencyStates, rangeBegin, rangeEnd,
        statistics);
  }

  static void scaleFrequency(FrequencyWorkspace& workspace,
                             const ReceiverGrid& receivers,
                             double launchAngleStep, double sourceSoundSpeed,
                             SourceGeometry sourceGeometry) {
    scaleCoherentGeometricPressure(workspace, receivers, launchAngleStep,
                                   sourceSoundSpeed, sourceGeometry);
  }

  [[nodiscard]] static bool accumulateFusedIntensity(
      const Kernel& kernel, const PerRayScratch& scratch,
      FusedIntensityWorkspace& workspace,
      std::span<const double> frequencies, const RayPath& path,
      std::span<const RayFrequencyState> frequencyStates,
      std::size_t rangeBegin, std::size_t rangeEnd,
      CartesianCervenyStatistics* statistics) {
    static_cast<void>(scratch);
    return kernel.accumulateFusedIntensityPrevalidated(
        workspace, frequencies, path, frequencyStates, rangeBegin, rangeEnd,
        statistics);
  }

  [[nodiscard]] static FrequencyWorkspace scaleIntensityFrequency(
      const IntensityWorkspace& workspace, const ReceiverGrid& receivers,
      double launchAngleStep, double sourceSoundSpeed,
      SourceGeometry sourceGeometry) {
    return scaleGeometricIntensityToPressure(workspace, receivers,
                                             launchAngleStep, sourceSoundSpeed,
                                             sourceGeometry);
  }

  [[nodiscard]] static bool accumulateFusedArrivals(
      const Kernel& kernel, const PerRayScratch& scratch,
      BroadbandArrivalWorkspace& workspace,
      std::span<const double> frequencies, const RayPath& path,
      std::span<const RayFrequencyState> frequencyStates,
      std::size_t rangeBegin, std::size_t rangeEnd,
      ArrivalAccumulationStatistics& statistics) {
    static_cast<void>(scratch);
    return kernel.accumulateFusedArrivalsPrevalidated(
        workspace, frequencies, path, frequencyStates, rangeBegin, rangeEnd,
        statistics);
  }
};

// Geometric Gaussian adapter (IGR-3A A05, design §4). makeKernel mirrors the
// single-frequency construction verbatim (single_frequency_solver.cpp:
// 251-253) and then installs the run's launch-fan angle step as fused-run
// state — the frozen fused kernel entries take no spacing parameter (the
// family has no epsilon channel, A04 Hat precedent), while the public
// per-frequency entries keep receiving it per call. The family is Cartesian
// only (no coordinate routing). It has NO epsilon channel, so
// prepareScratch/preparePerRay are empty inline functions (compiled away,
// design §4); the accumulation hooks forward to the private fused kernel
// entries; the scale hooks reproduce the legacy post-scale selector exactly
// per family x mode (single_frequency_solver.cpp:356-381): coherent ->
// scaleCoherentGeometricPressure, I/S -> scaleGeometricIntensityToPressure
// (the selector is family-based geometric normalization).
struct GeometricGaussianFusedAdapter {
  using Kernel = GeometricGaussianInfluence;

  // Frozen scratch shape (design §4): the Geometric Gaussian family has no
  // per-ray frequency-local preparation channel — an empty struct.
  struct PerRayScratch {};

  // Same executor-owned loop-invariant input set as the Cerveny adapters
  // so the unified executor stays family-agnostic; every Cerveny-only
  // field (the epsilon-loop inputs) is ignored here.
  struct PerRayContext {
    BeamWidthMode widthMode{};
    double sourceSoundSpeed{};
    double sourceDepthGradient{};
    double launchAngleStep{};
    double loopRange{};
    double epsilonMultiplier{};

    PerRayContext(BeamWidthMode widthModeValue, double sourceSoundSpeedValue,
                  double sourceDepthGradientValue,
                  double launchAngleStepValue, double loopRangeValue,
                  double epsilonMultiplierValue)
        : widthMode(widthModeValue),
          sourceSoundSpeed(sourceSoundSpeedValue),
          sourceDepthGradient(sourceDepthGradientValue),
          launchAngleStep(launchAngleStepValue),
          loopRange(loopRangeValue),
          epsilonMultiplier(epsilonMultiplierValue) {}
  };

  [[nodiscard]] static Kernel makeKernel(const SimulationCase& simulation,
                                         CartesianCervenySettings settings) {
    static_cast<void>(settings);
    Kernel kernel(simulation.receivers(), simulation.sourceGeometry());
    kernel.setFusedLaunchAngleStep(
        simulation.launchFanPlan().launchAngleStep);
    return kernel;
  }

  static void prepareScratch(PerRayScratch& scratch,
                             std::size_t frequencyCount) {
    static_cast<void>(scratch);
    static_cast<void>(frequencyCount);
  }

  static void preparePerRay(const PerRayContext& context,
                            PerRayScratch& scratch, const RayPath& path,
                            std::span<const double> frequencies) {
    static_cast<void>(context);
    static_cast<void>(scratch);
    static_cast<void>(path);
    static_cast<void>(frequencies);
  }

  [[nodiscard]] static bool accumulateFused(
      const Kernel& kernel, const PerRayScratch& scratch,
      FusedPressureWorkspace& workspace,
      std::span<const double> frequencies, const RayPath& path,
      std::span<const RayFrequencyState> frequencyStates,
      std::size_t rangeBegin, std::size_t rangeEnd,
      CartesianCervenyStatistics* statistics) {
    static_cast<void>(scratch);
    return kernel.accumulateFusedPrevalidated(
        workspace, frequencies, path, frequencyStates, rangeBegin, rangeEnd,
        statistics);
  }

  static void scaleFrequency(FrequencyWorkspace& workspace,
                             const ReceiverGrid& receivers,
                             double launchAngleStep, double sourceSoundSpeed,
                             SourceGeometry sourceGeometry) {
    scaleCoherentGeometricPressure(workspace, receivers, launchAngleStep,
                                   sourceSoundSpeed, sourceGeometry);
  }

  [[nodiscard]] static bool accumulateFusedIntensity(
      const Kernel& kernel, const PerRayScratch& scratch,
      FusedIntensityWorkspace& workspace,
      std::span<const double> frequencies, const RayPath& path,
      std::span<const RayFrequencyState> frequencyStates,
      std::size_t rangeBegin, std::size_t rangeEnd,
      CartesianCervenyStatistics* statistics) {
    static_cast<void>(scratch);
    return kernel.accumulateFusedIntensityPrevalidated(
        workspace, frequencies, path, frequencyStates, rangeBegin, rangeEnd,
        statistics);
  }

  [[nodiscard]] static FrequencyWorkspace scaleIntensityFrequency(
      const IntensityWorkspace& workspace, const ReceiverGrid& receivers,
      double launchAngleStep, double sourceSoundSpeed,
      SourceGeometry sourceGeometry) {
    return scaleGeometricIntensityToPressure(workspace, receivers,
                                             launchAngleStep, sourceSoundSpeed,
                                             sourceGeometry);
  }

  [[nodiscard]] static bool accumulateFusedArrivals(
      const Kernel& kernel, const PerRayScratch& scratch,
      BroadbandArrivalWorkspace& workspace,
      std::span<const double> frequencies, const RayPath& path,
      std::span<const RayFrequencyState> frequencyStates,
      std::size_t rangeBegin, std::size_t rangeEnd,
      ArrivalAccumulationStatistics& statistics) {
    static_cast<void>(scratch);
    return kernel.accumulateFusedArrivalsPrevalidated(
        workspace, frequencies, path, frequencyStates, rangeBegin, rangeEnd,
        statistics);
  }
};

// Simple Gaussian adapter (IGR-3A A06, design §4). COHERENT ONLY: the
// family's legal TL run modes are coherent-only (design §9 — its product
// gate is legal-matrix enforcement, not a fused restriction), so this is the
// one adapter without intensity twins; the compile-time absence is the
// design guarantee that the IntensityFusedSink is never instantiated with
// this adapter (instantiation would fail to compile), and the intensity
// public entry rejects the family before dispatch. makeKernel mirrors the
// single-frequency construction verbatim (single_frequency_solver.cpp:
// 255-257) and then installs the run's launch-fan angle step as fused-run
// state (A04/A05 precedent): the frozen fused kernel entry takes no spacing
// parameter, while the public per-frequency accumulate keeps receiving it
// per call — the constructor's configuredStepLengthMeters is a different
// quantity (the integrator step length of the legacy arc length) and cannot
// carry the launch spacing the beam width needs. The family has NO epsilon
// channel, so prepareScratch/preparePerRay are empty inline functions
// (compiled away, design §4); the coherent hook forwards to the private
// fused kernel entry; scaleFrequency reproduces the coherent branch of the
// legacy post-scale selector exactly (single_frequency_solver.cpp:372-375:
// geometric normalization -> scaleCoherentGeometricPressure).
struct SimpleGaussianFusedAdapter {
  using Kernel = SimpleGaussianInfluence;

  // Frozen scratch shape (design §4): the Simple Gaussian family has no
  // per-ray frequency-local preparation channel — an empty struct.
  struct PerRayScratch {};

  // Same executor-owned loop-invariant input set as the Cerveny adapters
  // so the unified executor stays family-agnostic; every Cerveny-only
  // field (the epsilon-loop inputs) is ignored here.
  struct PerRayContext {
    BeamWidthMode widthMode{};
    double sourceSoundSpeed{};
    double sourceDepthGradient{};
    double launchAngleStep{};
    double loopRange{};
    double epsilonMultiplier{};

    PerRayContext(BeamWidthMode widthModeValue, double sourceSoundSpeedValue,
                  double sourceDepthGradientValue,
                  double launchAngleStepValue, double loopRangeValue,
                  double epsilonMultiplierValue)
        : widthMode(widthModeValue),
          sourceSoundSpeed(sourceSoundSpeedValue),
          sourceDepthGradient(sourceDepthGradientValue),
          launchAngleStep(launchAngleStepValue),
          loopRange(loopRangeValue),
          epsilonMultiplier(epsilonMultiplierValue) {}
  };

  [[nodiscard]] static Kernel makeKernel(const SimulationCase& simulation,
                                         CartesianCervenySettings settings) {
    static_cast<void>(settings);
    Kernel kernel(simulation.receivers(),
                  simulation.integrator().stepLength,
                  simulation.sourceGeometry());
    kernel.setFusedLaunchAngleStep(
        simulation.launchFanPlan().launchAngleStep);
    return kernel;
  }

  static void prepareScratch(PerRayScratch& scratch,
                             std::size_t frequencyCount) {
    static_cast<void>(scratch);
    static_cast<void>(frequencyCount);
  }

  static void preparePerRay(const PerRayContext& context,
                            PerRayScratch& scratch, const RayPath& path,
                            std::span<const double> frequencies) {
    static_cast<void>(context);
    static_cast<void>(scratch);
    static_cast<void>(path);
    static_cast<void>(frequencies);
  }

  [[nodiscard]] static bool accumulateFused(
      const Kernel& kernel, const PerRayScratch& scratch,
      FusedPressureWorkspace& workspace,
      std::span<const double> frequencies, const RayPath& path,
      std::span<const RayFrequencyState> frequencyStates,
      std::size_t rangeBegin, std::size_t rangeEnd,
      CartesianCervenyStatistics* statistics) {
    static_cast<void>(scratch);
    return kernel.accumulateFusedPrevalidated(
        workspace, frequencies, path, frequencyStates, rangeBegin, rangeEnd,
        statistics);
  }

  static void scaleFrequency(FrequencyWorkspace& workspace,
                             const ReceiverGrid& receivers,
                             double launchAngleStep, double sourceSoundSpeed,
                             SourceGeometry sourceGeometry) {
    scaleCoherentGeometricPressure(workspace, receivers, launchAngleStep,
                                   sourceSoundSpeed, sourceGeometry);
  }

  // No accumulateFusedIntensity / scaleIntensityFrequency members (design
  // §4): Simple Gaussian defines only the coherent pair of hooks — its
  // legal product matrix is coherent-only (§9).
};

// Sink policies (design §3.1/§6.2): compile-time selection of everything
// mode-specific in the unified executor — the raw workspace type and its
// construction, the adapter accumulation hook, and the result type. Closed
// pair; the executor body is mode-agnostic.
struct CoherentFusedSink {
  using Workspace = FusedPressureWorkspace;
  using Result = FusedAccumulationResult;

  [[nodiscard]] static Workspace makeWorkspace(
      const ReceiverGrid& receivers, std::span<const double> frequencies) {
    return Workspace(receivers, frequencies.size());
  }

  template <typename Adapter>
  [[nodiscard]] static bool accumulate(
      const typename Adapter::Kernel& kernel,
      const typename Adapter::PerRayScratch& scratch, Workspace& workspace,
      std::span<const double> frequencies, const RayPath& path,
      std::span<const RayFrequencyState> frequencyStates,
      std::size_t rangeBegin, std::size_t rangeEnd,
      CartesianCervenyStatistics* statistics,
      ArrivalAccumulationStatistics* arrivalStatistics) {
    static_cast<void>(arrivalStatistics);
    return Adapter::accumulateFused(kernel, scratch, workspace, frequencies,
                                    path, frequencyStates, rangeBegin,
                                    rangeEnd, statistics);
  }

  [[nodiscard]] static Result makeResult(
      Workspace&& rawWorkspace, const SingleFrequencyTimings& timings,
      std::size_t rayCount, std::size_t totalRayPointCount,
      std::size_t rayCacheBytes, std::size_t requestedRangeWorkers,
      std::size_t effectiveRangeWorkers,
      const ArrivalAccumulationStatistics& arrivalStatistics) {
    static_cast<void>(arrivalStatistics);
    return Result{
        .rawWorkspace = std::move(rawWorkspace),
        .timings = timings,
        .rayCount = rayCount,
        .totalRayPointCount = totalRayPointCount,
        .rayCacheBytes = rayCacheBytes,
        .requestedRangeWorkers = requestedRangeWorkers,
        .effectiveRangeWorkers = effectiveRangeWorkers};
  }
};

struct IntensityFusedSink {
  using Workspace = FusedIntensityWorkspace;
  using Result = FusedIntensityAccumulationResult;

  [[nodiscard]] static Workspace makeWorkspace(
      const ReceiverGrid& receivers, std::span<const double> frequencies) {
    return Workspace(receivers, frequencies.size());
  }

  template <typename Adapter>
  [[nodiscard]] static bool accumulate(
      const typename Adapter::Kernel& kernel,
      const typename Adapter::PerRayScratch& scratch, Workspace& workspace,
      std::span<const double> frequencies, const RayPath& path,
      std::span<const RayFrequencyState> frequencyStates,
      std::size_t rangeBegin, std::size_t rangeEnd,
      CartesianCervenyStatistics* statistics,
      ArrivalAccumulationStatistics* arrivalStatistics) {
    static_cast<void>(arrivalStatistics);
    return Adapter::accumulateFusedIntensity(kernel, scratch, workspace,
                                             frequencies, path,
                                             frequencyStates, rangeBegin,
                                             rangeEnd, statistics);
  }

  [[nodiscard]] static Result makeResult(
      Workspace&& rawWorkspace, const SingleFrequencyTimings& timings,
      std::size_t rayCount, std::size_t totalRayPointCount,
      std::size_t rayCacheBytes, std::size_t requestedRangeWorkers,
      std::size_t effectiveRangeWorkers,
      const ArrivalAccumulationStatistics& arrivalStatistics) {
    static_cast<void>(arrivalStatistics);
    return Result{
        .rawIntensityWorkspace = std::move(rawWorkspace),
        .timings = timings,
        .rayCount = rayCount,
        .totalRayPointCount = totalRayPointCount,
        .rayCacheBytes = rayCacheBytes,
        .requestedRangeWorkers = requestedRangeWorkers,
        .effectiveRangeWorkers = effectiveRangeWorkers};
  }
};

struct ArrivalFusedSink {
  using Workspace = BroadbandArrivalWorkspace;
  using Result = FusedArrivalAccumulationResult;

  [[nodiscard]] static Workspace makeWorkspace(
      const ReceiverGrid& receivers, std::span<const double> frequencies) {
    return Workspace(frequencies, receivers);
  }

  template <typename Adapter>
  [[nodiscard]] static bool accumulate(
      const typename Adapter::Kernel& kernel,
      const typename Adapter::PerRayScratch& scratch, Workspace& workspace,
      std::span<const double> frequencies, const RayPath& path,
      std::span<const RayFrequencyState> frequencyStates,
      std::size_t rangeBegin, std::size_t rangeEnd,
      CartesianCervenyStatistics* statistics,
      ArrivalAccumulationStatistics* arrivalStatistics) {
    static_cast<void>(statistics);
    return Adapter::accumulateFusedArrivals(
        kernel, scratch, workspace, frequencies, path, frequencyStates,
        rangeBegin, rangeEnd, *arrivalStatistics);
  }

  [[nodiscard]] static Result makeResult(
      Workspace&& rawWorkspace, const SingleFrequencyTimings& timings,
      std::size_t rayCount, std::size_t totalRayPointCount,
      std::size_t rayCacheBytes, std::size_t requestedRangeWorkers,
      std::size_t effectiveRangeWorkers,
      const ArrivalAccumulationStatistics& arrivalStatistics) {
    return Result{.rawWorkspace = std::move(rawWorkspace),
                  .timings = timings,
                  .arrivalStatistics = arrivalStatistics,
                  .rayCount = rayCount,
                  .totalRayPointCount = totalRayPointCount,
                  .rayCacheBytes = rayCacheBytes,
                  .requestedRangeWorkers = requestedRangeWorkers,
                  .effectiveRangeWorkers = effectiveRangeWorkers};
  }
};

}  // namespace rayreuse
