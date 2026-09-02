#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "rayreuse/error.hpp"
#include "rayreuse/io/arrival_writer.hpp"
#include "rayreuse/io/command_line.hpp"
#include "rayreuse/io/eigenray_writer.hpp"
#include "rayreuse/io/environment_parser.hpp"
#include "rayreuse/io/ray_writer.hpp"
#include "rayreuse/io/shd_writer.hpp"
#include "rayreuse/solver/arrival_solver.hpp"
#include "rayreuse/solver/broadband_nonreuse_solver.hpp"
#include "rayreuse/solver/eigenray_solver.hpp"
#include "rayreuse/solver/fused_ray_reuse_solver.hpp"
#include "rayreuse/solver/parallel_ray_reuse_solver.hpp"
#include "rayreuse/solver/ray_trace_product.hpp"
#include "rayreuse/solver/serial_ray_reuse_solver.hpp"
#include "rayreuse/solver/single_frequency_solver.hpp"

namespace {

using Clock = std::chrono::steady_clock;

void printUsage(std::ostream& stream) {
  stream << "Usage: bellhop_rayreuse --version\n"
         << "       bellhop_rayreuse <file-root> "
            "[--frequencies-hz <f0,f1,...>] "
            "[--execution-mode <nonreuse|reuse|parallel|fused>] "
            "[--verify-cache] [--profile-influence] "
            "[--profile-frequency-tasks] "
            "[--workers <count>] "
            "[--output-queue-capacity <count>] "
            "[--memory-budget-mib <MiB>]\n"
         << "\n"
         << "Reads <file-root>.env and writes <file-root>.prt plus the "
            "mode-specific product.\n"
         << "Without --frequencies-hz, the scalar or strictly increasing "
            "frequency list in the .env file is used.\n"
         << "With --frequencies-hz, the strictly increasing list "
            "overrides the .env frequency.\n"
         << "Broadband execution defaults to nonreuse; use "
            "--execution-mode reuse for serial trace-once or parallel "
            "for bounded frequency concurrency.\n"
         << "--verify-cache hashes the complete frozen ray cache before "
            "and after projection and is intended for validation.\n"
         << "--profile-influence records detailed Influence work counts "
            "and sub-phase timings; it is disabled by default.\n"
         << "--profile-frequency-tasks writes existing per-frequency "
            "Project/Influence/Scale timings for parallel reuse; it is "
            "disabled by default.\n"
         << "Parallel tuning options require --execution-mode parallel; "
            "worker count defaults to hardware concurrency, the output "
            "queue defaults to 2, and a zero/unset memory budget means "
            "no explicit budget.\n";
}

[[nodiscard]] std::string runModeName(rayreuse::SimulationRunMode mode) {
  switch (mode) {
    case rayreuse::SimulationRunMode::Coherent:
      return "coherent TL (SHD)";
    case rayreuse::SimulationRunMode::Incoherent:
      return "incoherent TL (SHD)";
    case rayreuse::SimulationRunMode::SemiCoherent:
      return "semi-coherent TL (SHD)";
    case rayreuse::SimulationRunMode::RayTrace:
      return "ray trace (RAY)";
    case rayreuse::SimulationRunMode::AsciiArrivals:
      return "ASCII arrivals (ARR)";
    case rayreuse::SimulationRunMode::BinaryArrivals:
      return "binary arrivals (ARR)";
    case rayreuse::SimulationRunMode::Eigenray:
      return "eigenray (RAY)";
  }
  throw rayreuse::ValidationError("unknown simulation run mode");
}

[[nodiscard]] char fieldComponentToken(rayreuse::FieldComponent component) {
  switch (component) {
    case rayreuse::FieldComponent::Pressure:
      return 'P';
    case rayreuse::FieldComponent::Vertical:
      return 'V';
    case rayreuse::FieldComponent::Horizontal:
      return 'H';
  }
  throw rayreuse::ValidationError("field component is invalid");
}

[[nodiscard]] std::string_view curvatureModeLabel(
    rayreuse::BoundaryCurvatureMode mode) {
  switch (mode) {
    case rayreuse::BoundaryCurvatureMode::Double:
      return "Curvature doubling invoked";
    case rayreuse::BoundaryCurvatureMode::Standard:
      return "Standard curvature condition";
    case rayreuse::BoundaryCurvatureMode::Zero:
      return "Curvature zeroing invoked";
  }
  throw rayreuse::ValidationError("boundary curvature mode is invalid");
}

[[nodiscard]] std::string_view beamWidthModeLabel(
    rayreuse::BeamWidthMode mode) {
  switch (mode) {
    case rayreuse::BeamWidthMode::SpaceFilling:
      return "Space filling beams";
    case rayreuse::BeamWidthMode::MinimumWidth:
      return "Minimum width beams";
    case rayreuse::BeamWidthMode::Wkb:
      return "WKB beams";
  }
  throw rayreuse::ValidationError("beam width mode is invalid");
}

[[nodiscard]] std::string_view attenuationUnitLabel(
    rayreuse::AttenuationUnit unit) {
  switch (unit) {
    case rayreuse::AttenuationUnit::NepersPerMeter:
      return "nepers/m";
    case rayreuse::AttenuationUnit::DecibelsPerMeter:
      return "dB/m";
    case rayreuse::AttenuationUnit::DecibelsPerMeterPowerLaw:
      return "dB/m with power law";
    case rayreuse::AttenuationUnit::DecibelsPerMeterKilohertz:
      return "dB/mkHz";
    case rayreuse::AttenuationUnit::DecibelsPerWavelength:
      return "dB/wavelength";
    case rayreuse::AttenuationUnit::QualityFactor:
      return "Q";
    case rayreuse::AttenuationUnit::LossParameter:
      return "Loss parameter";
  }
  throw rayreuse::ValidationError("attenuation unit is invalid");
}

[[nodiscard]] std::string frequencyToken(double frequency) {
  std::ostringstream stream;
  stream << std::setprecision(12) << std::defaultfloat << frequency;
  std::string token = stream.str();
  for (char& character : token) {
    if (character == '.') {
      character = 'p';
    } else if (character == '+') {
      character = 'p';
    } else if (character == '-') {
      character = 'm';
    }
  }
  return token;
}

[[nodiscard]] std::filesystem::path productPath(const std::string& fileRoot,
                                                std::size_t frequencyIndex,
                                                std::size_t frequencyCount,
                                                double frequency,
                                                std::string_view extension) {
  if (frequencyCount == 1U) {
    return std::filesystem::path(fileRoot + std::string(extension));
  }
  std::ostringstream suffix;
  suffix << "_f" << std::setw(3) << std::setfill('0') << frequencyIndex << '_'
         << frequencyToken(frequency) << "Hz" << extension;
  return std::filesystem::path(fileRoot + suffix.str());
}

void removeArtifact(const std::filesystem::path& path) {
  std::error_code error;
  const bool removed = std::filesystem::remove(path, error);
  if (error) {
    throw rayreuse::BellhopError("unable to remove stale product " +
                                 path.string() + ": " + error.message());
  }
  static_cast<void>(removed);
}

void removeProductArtifacts(const std::string& fileRoot) {
  const std::filesystem::path root(fileRoot);
  const std::filesystem::path directory =
      root.has_parent_path() ? root.parent_path() : std::filesystem::path(".");
  const std::string underscorePrefix = root.filename().string() + "_f";
  const std::string legacyDotPrefix = root.filename().string() + ".f";
  removeArtifact(std::filesystem::path(fileRoot + ".shd"));
  removeArtifact(std::filesystem::path(fileRoot + ".ray"));
  removeArtifact(std::filesystem::path(fileRoot + ".arr"));
  removeArtifact(std::filesystem::path(fileRoot + ".shd.tmp"));
  removeArtifact(std::filesystem::path(fileRoot + ".ray.tmp"));
  removeArtifact(std::filesystem::path(fileRoot + ".arr.tmp"));

  std::error_code iteratorError;
  for (const std::filesystem::directory_entry& entry :
       std::filesystem::directory_iterator(directory, iteratorError)) {
    if (iteratorError) {
      break;
    }
    const std::string name = entry.path().filename().string();
    const bool usesUnderscorePrefix = name.starts_with(underscorePrefix);
    const bool usesLegacyDotPrefix = name.starts_with(legacyDotPrefix);
    if (!usesUnderscorePrefix && !usesLegacyDotPrefix) {
      continue;
    }
    const std::size_t prefixLength =
        usesUnderscorePrefix ? underscorePrefix.size() : legacyDotPrefix.size();
    const std::size_t separator = name.find('_', prefixLength);
    if (separator == std::string::npos) {
      continue;
    }
    const std::string index =
        name.substr(prefixLength, separator - prefixLength);
    if (index.empty() ||
        !std::all_of(index.begin(), index.end(),
                     [](char value) { return value >= '0' && value <= '9'; })) {
      continue;
    }
    const std::string suffix = name.substr(separator);
    if ((suffix.size() > 5U && suffix.ends_with(".arr")) ||
        (suffix.size() > 5U && suffix.ends_with(".ray")) ||
        (suffix.size() > 9U && suffix.ends_with(".arr.tmp")) ||
        (suffix.size() > 9U && suffix.ends_with(".ray.tmp"))) {
      removeArtifact(entry.path());
    }
  }
  if (iteratorError) {
    throw rayreuse::BellhopError("unable to scan stale products for " +
                                 fileRoot + ": " + iteratorError.message());
  }
}

void removeProductArtifactsNoThrow(const std::string& fileRoot) noexcept {
  try {
    removeProductArtifacts(fileRoot);
  } catch (...) {
  }
}

void validateProductOptions(const rayreuse::ParsedEnvironment& parsed,
                            const rayreuse::CommandLineOptions& options) {
  const rayreuse::SimulationRunMode mode = parsed.simulationCase.runMode();
  const bool unsupportedParallelTuning =
      options.outputQueueCapacitySpecified || options.memoryBudgetSpecified;
  if (mode == rayreuse::SimulationRunMode::RayTrace) {
    if (parsed.simulationCase.frequencies().size() != 1U) {
      throw rayreuse::ValidationError(
          "multi-frequency R products are not supported by the executable");
    }
    if (options.executionModeSpecified &&
        (options.executionMode == rayreuse::BroadbandExecutionMode::Reuse ||
         options.executionMode == rayreuse::BroadbandExecutionMode::Parallel)) {
      throw rayreuse::ValidationError(
          "--execution-mode reuse/parallel is not defined for R products");
    }
    if (options.executionModeSpecified &&
        options.executionMode == rayreuse::BroadbandExecutionMode::Fused) {
      throw rayreuse::ValidationError(
          "--execution-mode fused is not defined for R products");
    }
    if (options.profileInfluence || options.profileFrequencyTasks ||
        options.workerCountSpecified || unsupportedParallelTuning) {
      throw rayreuse::ValidationError(
          "profiling and parallel tuning options are only supported for TL");
    }
    return;
  }
  if (rayreuse::isTransmissionLossMode(mode) &&
      parsed.simulationCase.frequencies().size() == 1U &&
      options.executionModeSpecified &&
      (options.executionMode == rayreuse::BroadbandExecutionMode::Reuse ||
       options.executionMode == rayreuse::BroadbandExecutionMode::Parallel)) {
    throw rayreuse::ValidationError(
        "--execution-mode reuse/parallel requires a multi-frequency TL run");
  }
  if (rayreuse::isTransmissionLossMode(mode) &&
      parsed.simulationCase.frequencies().size() == 1U &&
      options.executionModeSpecified &&
      options.executionMode == rayreuse::BroadbandExecutionMode::Fused) {
    throw rayreuse::ValidationError(
        "--execution-mode fused requires a multi-frequency TL run");
  }
  if (rayreuse::isTransmissionLossMode(mode) &&
      options.executionMode == rayreuse::BroadbandExecutionMode::Fused) {
    if (parsed.simulationCase.beamFamily() !=
            rayreuse::BeamFamily::CervenyGaussian ||
        parsed.simulationCase.cervenyCoordinateSystem() !=
            rayreuse::CervenyCoordinateSystem::Cartesian ||
        mode != rayreuse::SimulationRunMode::Coherent) {
      throw rayreuse::ValidationError(
          "--execution-mode fused requires coherent Cartesian Cerveny TL");
    }
    if (parsed.simulationCase.sourceCount() != 1U) {
      throw rayreuse::ValidationError(
          "--execution-mode fused requires a single source");
    }
    if (parsed.simulationCase.receivers().isIrregular()) {
      throw rayreuse::ValidationError(
          "--execution-mode fused requires a rectilinear receiver grid");
    }
  }
  if (rayreuse::isTransmissionLossMode(mode) && options.profileInfluence &&
      (parsed.simulationCase.beamFamily() !=
           rayreuse::BeamFamily::CervenyGaussian ||
       parsed.simulationCase.cervenyCoordinateSystem() !=
           rayreuse::CervenyCoordinateSystem::Cartesian)) {
    throw rayreuse::ValidationError(
        "--profile-influence is currently defined only for Cartesian "
        "Cerveny TL");
  }
  if (mode == rayreuse::SimulationRunMode::AsciiArrivals ||
      mode == rayreuse::SimulationRunMode::BinaryArrivals ||
      mode == rayreuse::SimulationRunMode::Eigenray) {
    if (options.profileInfluence || options.profileFrequencyTasks ||
        unsupportedParallelTuning) {
      throw rayreuse::ValidationError(
          "Influence profiling and parallel tuning options are not supported "
          "for arrival/eigenray products");
    }
    // The arrivals/eigenray dispatch chains end in an else -> parallel solve;
    // fused must be rejected here so it cannot silently run the parallel
    // arrivals/eigenray solver (design §2 R2/R3).
    if (options.executionModeSpecified &&
        options.executionMode == rayreuse::BroadbandExecutionMode::Fused) {
      throw rayreuse::ValidationError(
          "--execution-mode fused is not defined for arrival/eigenray "
          "products");
    }
  }
}

void printVersion(std::ostream& stream) {
  stream << "Bellhop RayReuse " << RAYREUSE_VERSION << '\n';
}

void writeBoundarySummary(std::ostream& stream,
                          const rayreuse::BoundaryModel& boundary,
                          std::string_view name) {
  switch (boundary.kind()) {
    case rayreuse::BoundaryKind::Vacuum:
      stream << "VACUUM " << name << '\n';
      break;
    case rayreuse::BoundaryKind::Rigid:
      stream << "Perfectly RIGID " << name << '\n';
      break;
    case rayreuse::BoundaryKind::AcousticHalfSpace:
      stream << "ACOUSTO-ELASTIC half-space " << name << '\n';
      break;
    case rayreuse::BoundaryKind::GrainSizeHalfSpace:
      stream << "Grain size to define half-space " << name << '\n'
             << "Grain size = " << boundary.grainSizeMaterial()->meanGrainSize
             << '\n';
      break;
    case rayreuse::BoundaryKind::TabulatedReflection:
      stream << "FILE used for reflection loss\n"
             << "Using tabulated " << name << " reflection coef.\n"
             << "Number of points in " << name << " reflection coefficient = "
             << boundary.reflectionTable()->size() << '\n';
      break;
  }
  if (!boundary.geometry().isFlat()) {
    if (boundary.geometry().interpolationKind() ==
        rayreuse::BoundaryInterpolationKind::Curvilinear) {
      stream << "Curvilinear Interpolation\n";
    } else {
      stream << "Piecewise linear interpolation\n";
    }
    if (boundary.hasRangeDependentMaterials()) {
      stream << "Long format (bathymetry and geoacoustics)\n";
    }
  }
}

void writeConfigurationSummary(std::ostream& stream,
                               const rayreuse::ParsedEnvironment& parsed) {
  const rayreuse::SimulationCase& simulation = parsed.simulationCase;
  const rayreuse::Environment& environment = simulation.environment();
  stream << "BELLHOP RAYREUSE\n"
         << "program version = " << RAYREUSE_VERSION << "\n\n"
         << parsed.title << '\n';
  if (simulation.frequencies().size() == 1U) {
    stream << "frequency = " << simulation.frequencies().values().front()
           << " Hz\n";
  } else {
    stream << "frequency count = " << simulation.frequencies().size() << '\n'
           << "frequencies Hz =";
    for (const double frequency : simulation.frequencies().values()) {
      stream << ' ' << frequency;
    }
    stream << '\n';
  }
  stream << "design frequency = " << simulation.frequencies().designFrequency()
         << " Hz\n"
         << "run mode = " << runModeName(simulation.runMode()) << '\n'
         << "beam family = ";
  switch (simulation.beamFamily()) {
    case rayreuse::BeamFamily::CervenyGaussian:
      stream << "Cerveny Gaussian\n";
      break;
    case rayreuse::BeamFamily::GeometricHat:
      stream << "geometric hat\n";
      break;
    case rayreuse::BeamFamily::GeometricGaussian:
      stream << "geometric Gaussian\n";
      break;
    case rayreuse::BeamFamily::SimpleGaussian:
      stream << "simple Gaussian\n";
      break;
  }
  stream << "source beam pattern = "
         << (simulation.sourceBeamPattern().isDirectional() ? "directional"
                                                            : "omnidirectional")
         << '\n';
  if (simulation.beamFamily() == rayreuse::BeamFamily::CervenyGaussian ||
      !rayreuse::isTransmissionLossMode(simulation.runMode())) {
    stream << (simulation.cervenyCoordinateSystem() ==
                       rayreuse::CervenyCoordinateSystem::RayCentered
                   ? "Ray centered beams\n"
                   : "Cartesian beams\n");
  }
  if (simulation.beamFamily() == rayreuse::BeamFamily::CervenyGaussian &&
      rayreuse::isTransmissionLossMode(simulation.runMode())) {
    stream << "Component = " << fieldComponentToken(simulation.fieldComponent())
           << '\n'
           << beamWidthModeLabel(simulation.beamWidthMode()) << '\n'
           << curvatureModeLabel(simulation.curvatureMode()) << '\n';
  }
  switch (simulation.runMode()) {
    case rayreuse::SimulationRunMode::Coherent:
      stream << "Coherent TL calculation\n";
      break;
    case rayreuse::SimulationRunMode::Incoherent:
      stream << "Incoherent TL calculation\n";
      break;
    case rayreuse::SimulationRunMode::SemiCoherent:
      stream << "Semi-coherent TL calculation\n";
      break;
    case rayreuse::SimulationRunMode::RayTrace:
      stream << "Ray trace run\n";
      break;
    case rayreuse::SimulationRunMode::AsciiArrivals:
      stream << "Arrivals calculation\n";
      stream << "Arrivals calculation, ASCII  file output\n";
      break;
    case rayreuse::SimulationRunMode::BinaryArrivals:
      stream << "Arrivals calculation\n"
             << "Arrivals calculation, binary file output\n";
      break;
    case rayreuse::SimulationRunMode::Eigenray:
      stream << "Eigenray trace run\n";
      break;
  }
  switch (simulation.beamFamily()) {
    case rayreuse::BeamFamily::CervenyGaussian:
      if (simulation.cervenyCoordinateSystem() ==
          rayreuse::CervenyCoordinateSystem::RayCentered) {
        stream << "Cerveny beams in ray-centered coordinates\n";
      } else {
        stream << "Cerveny beams in Cartesian coordinates\n";
      }
      break;
    case rayreuse::BeamFamily::GeometricHat:
      stream << (simulation.cervenyCoordinateSystem() ==
                         rayreuse::CervenyCoordinateSystem::RayCentered
                     ? "Geometric hat beams in ray-centered coordinates\n"
                     : "Geometric hat beams in Cartesian coordinates\n")
             << "Geometric hat beams\n";
      break;
    case rayreuse::BeamFamily::GeometricGaussian:
      stream << "Geometric gaussian beams in Cartesian coordinates\n"
             << "Geometric Gaussian beams\n";
      break;
    case rayreuse::BeamFamily::SimpleGaussian:
      stream << "Simple gaussian beams\n"
             << "Simple Gaussian beams\n";
      break;
  }
  if (simulation.sourceBeamPattern().isDirectional()) {
    stream << "Using source beam pattern file\n"
           << "Number of source beam pattern points = "
           << simulation.sourceBeamPattern().size() << '\n';
  }
  if (simulation.sourceGeometry() == rayreuse::SourceGeometry::Line) {
    stream << "Line source (Cartesian coordinates)\n";
  } else {
    stream << "Point source (cylindrical coordinates)\n";
  }
  if (simulation.receivers().isIrregular()) {
    stream << "Irregular grid: paired receiver ranges and depths\n";
  } else {
    stream << "Rectilinear receiver grid\n";
  }
  writeBoundarySummary(stream, environment.seaSurface(), "top");
  stream
      << "Attenuation units: "
      << attenuationUnitLabel(
             environment.soundSpeedProfile().points().front().attenuation.unit)
      << '\n';
  if (environment.soundSpeedProfile().interpolationKind() ==
      rayreuse::SspInterpolationKind::Quadrilateral) {
    stream << "Using range-dependent sound speed\n"
           << "Number of SSP ranges = "
           << environment.soundSpeedProfile().quadrilateralGrid()->rangeCount
           << '\n';
  }
  writeBoundarySummary(stream, environment.seabed(), "bottom");
  const rayreuse::VolumeAttenuation& volumeAttenuation =
      environment.volumeAttenuation();
  switch (volumeAttenuation.model) {
    case rayreuse::VolumeAttenuationModel::None:
      break;
    case rayreuse::VolumeAttenuationModel::Thorp:
      stream << "THORP volume attenuation added\n";
      break;
    case rayreuse::VolumeAttenuationModel::FrancoisGarrison:
      stream << "Francois-Garrison volume attenuation added\n";
      break;
    case rayreuse::VolumeAttenuationModel::Biological: {
      stream << "Biological attenaution\n";
      const auto& layers =
          *std::get<rayreuse::SharedBiologicalAttenuationLayers>(
              volumeAttenuation.parameters);
      stream << "Number of Bio Layers = " << layers.size() << '\n';
      break;
    }
  }
  stream << "launch angles = " << simulation.launchFanPlan().launchAngleCount
         << '\n'
         << "phase criterion angles = "
         << simulation.launchFanPlan().phaseCriterionCount << '\n'
         << "depth criterion angles = "
         << simulation.launchFanPlan().depthCriterionCount << '\n'
         << "sufficiency check angles = "
         << simulation.launchFanPlan().minimumRecommendedAngleCount << '\n';
  if (simulation.sourceCount() > 1U) {
    // Multi-source runs list every source depth in ascending order (the
    // model sorts sources by depth), echoing the Origin/F2CPP PRT summary.
    stream << "source depths = " << simulation.sourceCount() << '\n';
    for (const rayreuse::Source& source : simulation.sources()) {
      stream << "source depth = " << source.depth << '\n';
    }
  }
  stream << "receiver depths = " << simulation.receivers().depthCount() << '\n'
         << "receiver ranges = " << simulation.receivers().rangeCount() << '\n'
         << "step length = " << simulation.integrator().stepLength << " m\n"
         << "range limit = " << simulation.integrator().rangeLimit << " m\n"
         << "depth limit = " << simulation.integrator().depthLimit << " m\n\n";
}

void writeSingleFrequencySummary(
    std::ostream& stream, const rayreuse::SingleFrequencyResult& result) {
  stream << "frequency result = " << result.workspace.frequency() << " Hz\n"
         << "ray count = " << result.rayCount << '\n'
         << "ray point count = " << result.totalRayPointCount << '\n'
         << "ray cache bytes = " << result.rayCacheBytes << '\n'
         << "Trace seconds = " << result.timings.traceSeconds << '\n'
         << "Project seconds = " << result.timings.projectSeconds << '\n'
         << "Influence seconds = " << result.timings.influenceSeconds << '\n'
         << "Scale seconds = " << result.timings.scaleSeconds << '\n';
}

void writeInfluenceStatistics(
    std::ostream& stream,
    const rayreuse::CartesianCervenyStatistics& statistics) {
  stream
      << "Influence ray accumulations = " << statistics.rayAccumulations << '\n'
      << "Influence validated ray points = " << statistics.validatedRayPoints
      << '\n'
      << "Influence validated workspace values = "
      << statistics.validatedWorkspaceValues << '\n'
      << "Influence active ray points = " << statistics.activeRayPoints << '\n'
      << "Influence segment candidates = " << statistics.segmentCandidates
      << '\n'
      << "Influence eligible segments = " << statistics.eligibleSegments << '\n'
      << "Influence receiver range evaluations = "
      << statistics.receiverRangeEvaluations << '\n'
      << "Influence receiver depth evaluations = "
      << statistics.receiverDepthEvaluations << '\n'
      << "Influence image evaluations = " << statistics.imageEvaluations << '\n'
      << "Influence window rejections = " << statistics.windowRejections << '\n'
      << "Influence taper rejections = " << statistics.taperRejections << '\n'
      << "Influence nonzero image contributions = "
      << statistics.nonzeroImageContributions << '\n'
      << "Influence geometry segment evaluations = "
      << statistics.geometrySegmentEvaluations << '\n'
      << "Influence geometry range evaluations = "
      << statistics.geometryRangeEvaluations << '\n'
      << "Influence geometry depth evaluations = "
      << statistics.geometryDepthEvaluations << '\n'
      << "Influence geometry image geometry evaluations = "
      << statistics.geometryImageGeometryEvaluations << '\n'
      << "Influence frequency range kernel evaluations = "
      << statistics.frequencyRangeKernelEvaluations << '\n'
      << "Influence frequency image kernel evaluations = "
      << statistics.frequencyImageKernelEvaluations << '\n'
      << "Influence validation seconds = " << statistics.validationSeconds
      << '\n'
      << "Influence precompute seconds = " << statistics.precomputeSeconds
      << '\n'
      << "Influence hot loop seconds = " << statistics.hotLoopSeconds << '\n';
}

void writeFrequencyTaskTimings(
    std::ostream& stream, const rayreuse::FrequencyGrid& frequencies,
    const std::vector<rayreuse::SingleFrequencyTimings>& timings) {
  if (timings.size() != frequencies.size()) {
    throw rayreuse::ValidationError(
        "frequency-task timing count must match frequency count");
  }
  stream << "frequency task count = " << timings.size() << '\n';
  for (std::size_t index = 0U; index < timings.size(); ++index) {
    const rayreuse::SingleFrequencyTimings& timing = timings[index];
    const double totalSeconds =
        timing.projectSeconds + timing.influenceSeconds + timing.scaleSeconds;
    stream << "frequency task " << index
           << " frequency Hz = " << frequencies.values()[index] << '\n'
           << "frequency task " << index
           << " Project seconds = " << timing.projectSeconds << '\n'
           << "frequency task " << index
           << " Influence seconds = " << timing.influenceSeconds << '\n'
           << "frequency task " << index
           << " Scale seconds = " << timing.scaleSeconds << '\n'
           << "frequency task " << index << " total seconds = " << totalSeconds
           << '\n';
  }
}

[[nodiscard]] std::size_t resolvedWorkerCount(
    std::size_t requestedWorkerCount) {
  if (requestedWorkerCount != 0U) {
    return requestedWorkerCount;
  }
  const unsigned int hardwareCount = std::thread::hardware_concurrency();
  return hardwareCount == 0U ? 1U : static_cast<std::size_t>(hardwareCount);
}

[[nodiscard]] std::size_t memoryBudgetBytes(std::size_t memoryBudgetMiB) {
  constexpr std::size_t bytesPerMiB = 1024U * 1024U;
  if (memoryBudgetMiB > std::numeric_limits<std::size_t>::max() / bytesPerMiB) {
    throw rayreuse::ValidationError(
        "--memory-budget-mib exceeds the platform size limit");
  }
  return memoryBudgetMiB * bytesPerMiB;
}

void writeProductExecutionMode(std::ostream& stream,
                               rayreuse::BroadbandExecutionMode mode,
                               std::size_t frequencyCount,
                               std::size_t sourceCount = 1U) {
  if (frequencyCount == 1U) {
    stream << "execution mode = single-frequency ";
  } else {
    stream << "execution mode = broadband ";
  }
  switch (mode) {
    case rayreuse::BroadbandExecutionMode::NonReuse:
      stream << "non-reuse\n";
      break;
    case rayreuse::BroadbandExecutionMode::Reuse:
      stream << "reuse\n";
      break;
    case rayreuse::BroadbandExecutionMode::Parallel:
      stream << "parallel reuse\n";
      break;
    case rayreuse::BroadbandExecutionMode::Fused:
      // Unreachable in practice (fused is rejected for every product that
      // calls this helper); kept for switch exhaustiveness (design §1.8).
      stream << "fused reuse\n";
      break;
  }
  // Frozen semantics (Worklist FP-2F §1.5): trace passes count per-source
  // fan traces (non-reuse = Nfreq x NSz, reuse/parallel = NSz).
  stream << "Trace passes = "
         << (mode == rayreuse::BroadbandExecutionMode::NonReuse
                 ? frequencyCount * sourceCount
                 : sourceCount)
         << '\n';
}

void writePerSourceCacheFingerprints(
    std::ostream& stream, const std::vector<std::uint64_t>& fingerprintsBefore,
    const std::vector<std::uint64_t>& fingerprintsAfter,
    std::string_view label) {
  // Multi-source runs list one fingerprint pair per source; single-source
  // output stays byte-identical (no extra lines).
  if (fingerprintsBefore.size() <= 1U) {
    return;
  }
  for (std::size_t sourceIndex = 0U; sourceIndex < fingerprintsBefore.size();
       ++sourceIndex) {
    stream << "source index " << sourceIndex << ' ' << label
           << " before = " << fingerprintsBefore[sourceIndex] << '\n'
           << "source index " << sourceIndex << ' ' << label
           << " after = " << fingerprintsAfter[sourceIndex] << '\n';
  }
}

}  // namespace

int main(int argumentCount, char* arguments[]) {
  std::vector<std::string_view> argumentViews;
  argumentViews.reserve(
      argumentCount > 0 ? static_cast<std::size_t>(argumentCount - 1) : 0U);
  for (int index = 1; index < argumentCount; ++index) {
    argumentViews.emplace_back(arguments[index]);
  }

  rayreuse::CommandLineOptions options;
  try {
    options = rayreuse::parseCommandLine(argumentViews);
  } catch (const std::exception& error) {
    printUsage(std::cerr);
    std::cerr << "bellhop_rayreuse: " << error.what() << '\n';
    return 2;
  }
  if (options.showHelp) {
    printUsage(std::cout);
    return 0;
  }
  if (options.showVersion) {
    printVersion(std::cout);
    return 0;
  }

  const std::string& fileRoot = options.fileRoot;
  const std::filesystem::path environmentPath(fileRoot + ".env");
  const std::filesystem::path printPath(fileRoot + ".prt");
  const std::filesystem::path shadePath(fileRoot + ".shd");

  std::ofstream printLog(printPath, std::ios::out | std::ios::trunc);
  if (!printLog.is_open()) {
    std::cerr << "bellhop_rayreuse: unable to open print output: " << printPath
              << '\n';
    return 1;
  }
  printLog << std::setprecision(17);

  bool productsPrepared = false;
  try {
    const rayreuse::ParsedEnvironment parsed =
        rayreuse::EnvironmentParser::parseFile(
            environmentPath, std::move(options.frequencyOverrideHz));
    validateProductOptions(parsed, options);
    // Product files are mode-owned.  Remove every known product for this
    // root before solving so switching modes cannot expose stale output.
    removeProductArtifacts(fileRoot);
    productsPrepared = true;
    writeConfigurationSummary(printLog, parsed);
    rayreuse::CartesianCervenySettings influenceSettings =
        parsed.beam.influence;
    influenceSettings.collectStatistics = options.profileInfluence;

    const Clock::time_point solveBegin = Clock::now();
    const rayreuse::SimulationRunMode runMode = parsed.simulationCase.runMode();
    if (runMode == rayreuse::SimulationRunMode::RayTrace) {
      const double frequency =
          parsed.simulationCase.frequencies().values().front();
      // One frozen per-source cache per SimulationCase::sources() entry; the
      // R product carries every source's fan in depth-ascending order.
      const std::vector<rayreuse::RayPathCache> caches =
          rayreuse::traceRayProducts(parsed.simulationCase);
      std::vector<std::uint64_t> fingerprintsBefore;
      fingerprintsBefore.reserve(caches.size());
      std::size_t rayCount = 0U;
      std::size_t rayCacheBytes = 0U;
      for (const rayreuse::RayPathCache& cache : caches) {
        fingerprintsBefore.push_back(cache.contentFingerprint());
        rayCount += cache.size();
        rayCacheBytes += cache.memoryFootprintBytes();
      }
      const std::filesystem::path output =
          productPath(fileRoot, 0U, 1U, frequency, ".ray");
      rayreuse::RayWriter writer(output, parsed.title, parsed.simulationCase,
                                 frequency);
      // One fan block per source in SimulationCase::sources() order (depth
      // ascending) with the `1 1 NSz` ray-file header (Origin WriteRay).
      for (std::size_t sourceIndex = 0U; sourceIndex < caches.size();
           ++sourceIndex) {
        writer.appendSource(sourceIndex, caches[sourceIndex]);
      }
      writer.finalize();
      std::vector<std::uint64_t> fingerprintsAfter;
      fingerprintsAfter.reserve(caches.size());
      for (const rayreuse::RayPathCache& cache : caches) {
        fingerprintsAfter.push_back(cache.contentFingerprint());
      }
      if (options.verifyCache && fingerprintsAfter != fingerprintsBefore) {
        throw rayreuse::ValidationError(
            "R product modified the frozen ray cache");
      }
      printLog << "product = " << output << '\n'
               << "ray count = " << rayCount << '\n'
               << "ray cache bytes = " << rayCacheBytes << '\n'
               << "cache fingerprint verification = "
               << (options.verifyCache ? "enabled\n" : "disabled\n");
      if (options.verifyCache) {
        printLog << "cache fingerprint before = " << fingerprintsBefore.front()
                 << '\n'
                 << "cache fingerprint after = " << fingerprintsAfter.front()
                 << '\n';
        writePerSourceCacheFingerprints(printLog, fingerprintsBefore,
                                        fingerprintsAfter, "cache fingerprint");
      }
    } else if (runMode == rayreuse::SimulationRunMode::AsciiArrivals ||
               runMode == rayreuse::SimulationRunMode::BinaryArrivals) {
      const rayreuse::ArrivalEncoding encoding =
          runMode == rayreuse::SimulationRunMode::AsciiArrivals
              ? rayreuse::ArrivalEncoding::Ascii
              : rayreuse::ArrivalEncoding::Binary;
      const std::string_view extension = ".arr";
      const auto consumer =
          [&](std::size_t frequencyIndex,
              const std::vector<rayreuse::RayPathCache>& caches,
              const std::vector<rayreuse::ArrivalWorkspace>& workspaces) {
            std::vector<std::uint64_t> before;
            before.reserve(caches.size());
            for (const rayreuse::RayPathCache& cache : caches) {
              before.push_back(cache.contentFingerprint());
            }
            const double frequency = workspaces.front().frequency();
            const std::filesystem::path output =
                productPath(fileRoot, frequencyIndex,
                            parsed.simulationCase.frequencies().size(),
                            frequency, extension);
            // One per-source block in depth-ascending order; the ARR header
            // carries the source count and every source depth (Origin ArrMod).
            rayreuse::ArrivalWriter::write(output, parsed.title,
                                           parsed.simulationCase, workspaces,
                                           encoding);
            std::vector<std::uint64_t> after;
            after.reserve(caches.size());
            for (const rayreuse::RayPathCache& cache : caches) {
              after.push_back(cache.contentFingerprint());
            }
            if (options.verifyCache && after != before) {
              throw rayreuse::ValidationError(
                  "arrival product modified the frozen ray cache");
            }
            printLog << "frequency product index = " << frequencyIndex
                     << " frequency Hz = " << frequency << '\n'
                     << "product = " << output << '\n';
            if (options.verifyCache) {
              printLog << "cache fingerprint before = " << before.front()
                       << '\n'
                       << "cache fingerprint after = " << after.front() << '\n';
              writePerSourceCacheFingerprints(printLog, before, after,
                                              "cache fingerprint");
            }
          };
      const std::size_t workers = resolvedWorkerCount(options.workerCount);
      rayreuse::ArrivalSolverStatistics statistics;
      if (options.executionMode == rayreuse::BroadbandExecutionMode::NonReuse) {
        statistics = rayreuse::ArrivalSolver::solveNonReuse(
            parsed.simulationCase, consumer, options.verifyCache);
      } else if (options.executionMode ==
                 rayreuse::BroadbandExecutionMode::Reuse) {
        statistics = rayreuse::ArrivalSolver::solve(
            parsed.simulationCase, consumer, options.verifyCache);
      } else {
        statistics = rayreuse::ArrivalSolver::solveParallel(
            parsed.simulationCase, consumer, workers, options.verifyCache);
      }
      writeProductExecutionMode(printLog, options.executionMode,
                                statistics.frequencyCount,
                                parsed.simulationCase.sourceCount());
      printLog << "frequency count = " << statistics.frequencyCount << '\n'
               << "ray count = " << statistics.rayCount << '\n'
               << "arrival candidate count = " << statistics.candidateCount
               << '\n'
               << "arrival consume seconds = " << statistics.consumeSeconds
               << '\n';
      if (statistics.cacheFingerprintVerified) {
        printLog << "cache fingerprint verification = enabled\n"
                 << "solver cache fingerprint before = "
                 << statistics.cacheFingerprintBefore << '\n'
                 << "solver cache fingerprint after = "
                 << statistics.cacheFingerprintAfter << '\n';
        writePerSourceCacheFingerprints(
            printLog, statistics.sourceCacheFingerprintsBefore,
            statistics.sourceCacheFingerprintsAfter,
            "solver cache fingerprint");
      } else {
        printLog << "cache fingerprint verification = disabled\n";
      }
    } else if (runMode == rayreuse::SimulationRunMode::Eigenray) {
      const auto consumer =
          [&](std::size_t frequencyIndex,
              const std::vector<rayreuse::RayPathCache>& caches,
              const std::vector<rayreuse::EigenraySourceHits>& sourceHits) {
            std::vector<std::uint64_t> before;
            before.reserve(caches.size());
            for (const rayreuse::RayPathCache& cache : caches) {
              before.push_back(cache.contentFingerprint());
            }
            const double frequency =
                parsed.simulationCase.frequencies().values().at(frequencyIndex);
            const std::filesystem::path output = productPath(
                fileRoot, frequencyIndex,
                parsed.simulationCase.frequencies().size(), frequency, ".ray");
            // One hit section per source in depth-ascending order under the
            // `1 1 NSz` ray-file header (Origin WriteRay / F2CPP
            // EigenrayWriter).
            rayreuse::EigenrayWriter::write(output, parsed.title,
                                            parsed.simulationCase, frequency,
                                            caches, sourceHits);
            std::size_t frequencyHitCount = 0U;
            for (const rayreuse::EigenraySourceHits& hits : sourceHits) {
              frequencyHitCount += hits.size();
            }
            std::vector<std::uint64_t> after;
            after.reserve(caches.size());
            for (const rayreuse::RayPathCache& cache : caches) {
              after.push_back(cache.contentFingerprint());
            }
            if (options.verifyCache && after != before) {
              throw rayreuse::ValidationError(
                  "eigenray product modified the frozen ray cache");
            }
            printLog << "frequency product index = " << frequencyIndex
                     << " frequency Hz = " << frequency << '\n'
                     << "product = " << output << '\n'
                     << "eigenray hit count = " << frequencyHitCount << '\n';
            if (options.verifyCache) {
              printLog << "cache fingerprint before = " << before.front()
                       << '\n'
                       << "cache fingerprint after = " << after.front() << '\n';
              writePerSourceCacheFingerprints(printLog, before, after,
                                              "cache fingerprint");
            }
          };
      const std::size_t workers = resolvedWorkerCount(options.workerCount);
      rayreuse::EigenraySolverStatistics statistics;
      if (options.executionMode == rayreuse::BroadbandExecutionMode::NonReuse) {
        statistics = rayreuse::EigenraySolver::solveNonReuse(
            parsed.simulationCase, consumer, options.verifyCache);
      } else if (options.executionMode ==
                 rayreuse::BroadbandExecutionMode::Reuse) {
        statistics = rayreuse::EigenraySolver::solve(
            parsed.simulationCase, consumer, options.verifyCache);
      } else {
        statistics = rayreuse::EigenraySolver::solveParallel(
            parsed.simulationCase, consumer, workers, options.verifyCache);
      }
      writeProductExecutionMode(printLog, options.executionMode,
                                statistics.frequencyCount,
                                parsed.simulationCase.sourceCount());
      printLog << "frequency count = " << statistics.frequencyCount << '\n'
               << "ray count = " << statistics.rayCount << '\n'
               << "eigenray hit count = " << statistics.totalHitCount << '\n'
               << "eigenray consume seconds = " << statistics.consumeSeconds
               << '\n';
      if (statistics.cacheFingerprintVerified) {
        printLog << "cache fingerprint verification = enabled\n"
                 << "solver cache fingerprint before = "
                 << statistics.cacheFingerprintBefore << '\n'
                 << "solver cache fingerprint after = "
                 << statistics.cacheFingerprintAfter << '\n';
        writePerSourceCacheFingerprints(
            printLog, statistics.sourceCacheFingerprintsBefore,
            statistics.sourceCacheFingerprintsAfter,
            "solver cache fingerprint");
      } else {
        printLog << "cache fingerprint verification = disabled\n";
      }
    } else if (parsed.simulationCase.frequencies().size() == 1U) {
      const rayreuse::SingleFrequencyResult result =
          rayreuse::SingleFrequencySolver::solve(
              parsed.simulationCase, parsed.beam.epsilonMultiplier,
              parsed.beam.loopRange, influenceSettings);

      const Clock::time_point writeBegin = Clock::now();
      // Per-source SHD records: one receiversPerRange-record block per source
      // under the NSz-carrying header (Origin: IRec = 10 + NSz per source).
      rayreuse::ShdWriter::writeSingleFrequency(
          shadePath, parsed.title, parsed.simulationCase, result.workspace,
          result.additionalSourceWorkspaces);
      const double writeSeconds =
          std::chrono::duration<double>(Clock::now() - writeBegin).count();

      printLog << "execution mode = single-frequency\n"
               << "Trace passes = " << result.sourceCount() << '\n';
      writeSingleFrequencySummary(printLog, result);
      if (options.profileInfluence) {
        writeInfluenceStatistics(printLog, result.timings.influenceStatistics);
      }
      printLog << "SHD seconds = " << writeSeconds << '\n';
    } else if (options.executionMode ==
               rayreuse::BroadbandExecutionMode::NonReuse) {
      rayreuse::BroadbandNonReuseResult result =
          rayreuse::BroadbandNonReuseSolver::solve(
              parsed.simulationCase, parsed.beam.epsilonMultiplier,
              parsed.beam.loopRange, influenceSettings);

      // One source-major workspace vector per frequency (first source in
      // `workspace`, the rest in `additionalSourceWorkspaces`).
      std::vector<std::vector<rayreuse::FrequencyWorkspace>>
          sourceWorkspacesPerFrequency;
      sourceWorkspacesPerFrequency.reserve(result.frequencyResults.size());
      for (rayreuse::SingleFrequencyResult& frequencyResult :
           result.frequencyResults) {
        std::vector<rayreuse::FrequencyWorkspace> sourceWorkspaces;
        sourceWorkspaces.reserve(frequencyResult.sourceCount());
        sourceWorkspaces.push_back(std::move(frequencyResult.workspace));
        for (rayreuse::FrequencyWorkspace& additional :
             frequencyResult.additionalSourceWorkspaces) {
          sourceWorkspaces.push_back(std::move(additional));
        }
        sourceWorkspacesPerFrequency.push_back(std::move(sourceWorkspaces));
      }

      const Clock::time_point writeBegin = Clock::now();
      rayreuse::ShdWriter::writeFrequencies(shadePath, parsed.title,
                                            parsed.simulationCase,
                                            sourceWorkspacesPerFrequency);
      const double writeSeconds =
          std::chrono::duration<double>(Clock::now() - writeBegin).count();

      printLog << "execution mode = broadband non-reuse\n"
               << "Trace passes = " << result.statistics.tracePassCount << '\n'
               << "total ray count = " << result.statistics.totalRayCount
               << '\n'
               << "total ray point count = "
               << result.statistics.totalRayPointCount << '\n'
               << "cumulative ray cache bytes = "
               << result.statistics.cumulativeRayCacheBytes << '\n'
               << "peak ray cache bytes = "
               << result.statistics.peakRayCacheBytes << '\n'
               << "Trace seconds = "
               << result.statistics.phaseTotals.traceSeconds << '\n'
               << "Project seconds = "
               << result.statistics.phaseTotals.projectSeconds << '\n'
               << "Influence seconds = "
               << result.statistics.phaseTotals.influenceSeconds << '\n'
               << "Scale seconds = "
               << result.statistics.phaseTotals.scaleSeconds << '\n'
               << "non-reuse wall seconds = " << result.statistics.wallSeconds
               << '\n'
               << "SHD seconds = " << writeSeconds << '\n';
      if (options.profileInfluence) {
        writeInfluenceStatistics(
            printLog, result.statistics.phaseTotals.influenceStatistics);
      }
    } else if (options.executionMode ==
               rayreuse::BroadbandExecutionMode::Reuse) {
      double writeSeconds = 0.0;
      const Clock::time_point writerSetupBegin = Clock::now();
      rayreuse::ShdFrequencyWriter writer(shadePath, parsed.title,
                                          parsed.simulationCase);
      writeSeconds +=
          std::chrono::duration<double>(Clock::now() - writerSetupBegin)
              .count();
      const rayreuse::RayReuseFrequencyConsumer consumer =
          [&](std::size_t frequencyIndex,
              std::vector<rayreuse::FrequencyWorkspace>&& sourceWorkspaces,
              const rayreuse::SingleFrequencyTimings&) {
            const Clock::time_point writeBegin = Clock::now();
            // One receiversPerRange-record block per source in the frequency
            // slot (source-major, Origin IRec addressing).
            writer.writeFrequency(frequencyIndex, sourceWorkspaces);
            writeSeconds +=
                std::chrono::duration<double>(Clock::now() - writeBegin)
                    .count();
          };
      const rayreuse::SerialRayReuseStatistics statistics =
          rayreuse::SerialRayReuseSolver::solveStreaming(
              parsed.simulationCase, parsed.beam.epsilonMultiplier,
              parsed.beam.loopRange, consumer, influenceSettings,
              options.verifyCache);
      const Clock::time_point finalizeBegin = Clock::now();
      writer.finalize();
      writeSeconds +=
          std::chrono::duration<double>(Clock::now() - finalizeBegin).count();

      printLog << "execution mode = broadband reuse\n"
               << "Trace passes = " << statistics.tracePassCount << '\n'
               << "ray count = " << statistics.rayCount << '\n'
               << "ray point count = " << statistics.totalRayPointCount << '\n'
               << "ray cache bytes = " << statistics.rayCacheBytes << '\n'
               << "Trace seconds = " << statistics.phaseTotals.traceSeconds
               << '\n'
               << "Project seconds = " << statistics.phaseTotals.projectSeconds
               << '\n'
               << "Influence seconds = "
               << statistics.phaseTotals.influenceSeconds << '\n'
               << "Scale seconds = " << statistics.phaseTotals.scaleSeconds
               << '\n'
               << "reuse wall seconds = " << statistics.wallSeconds << '\n'
               << "SHD seconds = " << writeSeconds << '\n';
      if (statistics.cacheFingerprintVerified) {
        printLog << "cache fingerprint verification = enabled\n"
                 << "cache fingerprint before = "
                 << statistics.cacheFingerprintBefore << '\n'
                 << "cache fingerprint after = "
                 << statistics.cacheFingerprintAfter << '\n';
        writePerSourceCacheFingerprints(
            printLog, statistics.sourceCacheFingerprintsBefore,
            statistics.sourceCacheFingerprintsAfter, "cache fingerprint");
      } else {
        printLog << "cache fingerprint verification = disabled\n";
      }
      if (options.profileInfluence) {
        writeInfluenceStatistics(printLog,
                                 statistics.phaseTotals.influenceStatistics);
      }
    } else if (options.executionMode ==
               rayreuse::BroadbandExecutionMode::Fused) {
      double writeSeconds = 0.0;
      const Clock::time_point writerSetupBegin = Clock::now();
      rayreuse::ShdFrequencyWriter writer(shadePath, parsed.title,
                                          parsed.simulationCase);
      writeSeconds +=
          std::chrono::duration<double>(Clock::now() - writerSetupBegin)
              .count();
      const rayreuse::RayReuseFrequencyConsumer consumer =
          [&](std::size_t frequencyIndex,
              std::vector<rayreuse::FrequencyWorkspace>&& sourceWorkspaces,
              const rayreuse::SingleFrequencyTimings&) {
            const Clock::time_point writeBegin = Clock::now();
            // One receiversPerRange-record block per source in the frequency
            // slot (source-major, Origin IRec addressing).
            writer.writeFrequency(frequencyIndex, sourceWorkspaces);
            writeSeconds +=
                std::chrono::duration<double>(Clock::now() - writeBegin)
                    .count();
          };
      const rayreuse::FusedRayReuseStatistics statistics =
          rayreuse::FusedRayReuseSolver::solveStreaming(
              parsed.simulationCase, parsed.beam.epsilonMultiplier,
              parsed.beam.loopRange, consumer, influenceSettings,
              options.verifyCache);
      const Clock::time_point finalizeBegin = Clock::now();
      writer.finalize();
      writeSeconds +=
          std::chrono::duration<double>(Clock::now() - finalizeBegin)
              .count();

      printLog << "execution mode = broadband fused reuse\n"
               << "Trace passes = " << statistics.tracePassCount << '\n'
               << "ray count = " << statistics.rayCount << '\n'
               << "ray point count = " << statistics.totalRayPointCount << '\n'
               << "ray cache bytes = " << statistics.rayCacheBytes << '\n'
               << "Trace seconds = " << statistics.phaseTotals.traceSeconds
               << '\n'
               << "Project seconds = "
               << statistics.phaseTotals.projectSeconds << '\n'
               << "Influence seconds = "
               << statistics.phaseTotals.influenceSeconds << '\n'
               << "Scale seconds = " << statistics.phaseTotals.scaleSeconds
               << '\n'
               << "fused reuse wall seconds = " << statistics.wallSeconds
               << '\n'
               << "SHD seconds = " << writeSeconds << '\n';
      if (statistics.cacheFingerprintVerified) {
        printLog << "cache fingerprint verification = enabled\n"
                 << "cache fingerprint before = "
                 << statistics.cacheFingerprintBefore << '\n'
                 << "cache fingerprint after = "
                 << statistics.cacheFingerprintAfter << '\n';
        writePerSourceCacheFingerprints(
            printLog, statistics.sourceCacheFingerprintsBefore,
            statistics.sourceCacheFingerprintsAfter, "cache fingerprint");
      } else {
        printLog << "cache fingerprint verification = disabled\n";
      }
      if (options.profileInfluence) {
        writeInfluenceStatistics(printLog,
                                 statistics.phaseTotals.influenceStatistics);
      }
    } else {
      double writeSeconds = 0.0;
      const Clock::time_point writerSetupBegin = Clock::now();
      rayreuse::ShdFrequencyWriter writer(shadePath, parsed.title,
                                          parsed.simulationCase);
      writeSeconds +=
          std::chrono::duration<double>(Clock::now() - writerSetupBegin)
              .count();
      const rayreuse::RayReuseFrequencyConsumer consumer =
          [&](std::size_t frequencyIndex,
              std::vector<rayreuse::FrequencyWorkspace>&& sourceWorkspaces,
              const rayreuse::SingleFrequencyTimings&) {
            const Clock::time_point writeBegin = Clock::now();
            // One receiversPerRange-record block per source in the frequency
            // slot (source-major, Origin IRec addressing).
            writer.writeFrequency(frequencyIndex, sourceWorkspaces);
            writeSeconds +=
                std::chrono::duration<double>(Clock::now() - writeBegin)
                    .count();
          };
      const rayreuse::ParallelRayReuseSettings settings{
          .workerCount = resolvedWorkerCount(options.workerCount),
          .outputQueueCapacity = options.outputQueueCapacity,
          .memoryBudgetBytes = memoryBudgetBytes(options.memoryBudgetMiB),
      };
      const rayreuse::ParallelRayReuseStatistics statistics =
          rayreuse::ParallelRayReuseSolver::solveStreaming(
              parsed.simulationCase, parsed.beam.epsilonMultiplier,
              parsed.beam.loopRange, consumer, settings, influenceSettings,
              options.verifyCache);
      const Clock::time_point finalizeBegin = Clock::now();
      writer.finalize();
      writeSeconds +=
          std::chrono::duration<double>(Clock::now() - finalizeBegin).count();

      printLog << "execution mode = broadband parallel reuse\n"
               << "Trace passes = " << statistics.tracePassCount << '\n'
               << "ray count = " << statistics.rayCount << '\n'
               << "ray point count = " << statistics.totalRayPointCount << '\n'
               << "ray cache bytes = " << statistics.rayCacheBytes << '\n'
               << "requested worker count = " << statistics.requestedWorkerCount
               << '\n'
               << "active frequency limit = " << statistics.activeFrequencyLimit
               << '\n'
               << "output queue capacity = " << statistics.outputQueueCapacity
               << '\n'
               << "peak queued results = " << statistics.peakQueuedResults
               << '\n'
               << "estimated workspace bytes = "
               << statistics.estimatedWorkspaceBytes << '\n'
               << "estimated peak memory bytes = "
               << statistics.estimatedPeakMemoryBytes << '\n'
               << "memory budget bytes = " << statistics.memoryBudgetBytes
               << '\n'
               << "Trace seconds = " << statistics.phaseTotals.traceSeconds
               << '\n'
               << "Project seconds = " << statistics.phaseTotals.projectSeconds
               << '\n'
               << "Influence seconds = "
               << statistics.phaseTotals.influenceSeconds << '\n'
               << "Scale seconds = " << statistics.phaseTotals.scaleSeconds
               << '\n'
               << "parallel reuse wall seconds = " << statistics.wallSeconds
               << '\n'
               << "SHD seconds = " << writeSeconds << '\n';
      if (statistics.cacheFingerprintVerified) {
        printLog << "cache fingerprint verification = enabled\n"
                 << "cache fingerprint before = "
                 << statistics.cacheFingerprintBefore << '\n'
                 << "cache fingerprint after = "
                 << statistics.cacheFingerprintAfter << '\n';
        writePerSourceCacheFingerprints(
            printLog, statistics.sourceCacheFingerprintsBefore,
            statistics.sourceCacheFingerprintsAfter, "cache fingerprint");
      } else {
        printLog << "cache fingerprint verification = disabled\n";
      }
      if (options.profileInfluence) {
        writeInfluenceStatistics(printLog,
                                 statistics.phaseTotals.influenceStatistics);
      }
      if (options.profileFrequencyTasks) {
        writeFrequencyTaskTimings(printLog, parsed.simulationCase.frequencies(),
                                  statistics.frequencyTimings);
      }
    }

    printLog << "Total solver and product seconds = "
             << std::chrono::duration<double>(Clock::now() - solveBegin).count()
             << '\n'
             << "Bellhop RayReuse completed successfully\n";
    printLog.close();
    if (!printLog) {
      throw rayreuse::BellhopError("failed to finalize print output: " +
                                   printPath.string());
    }
    return 0;
  } catch (const std::exception& error) {
    if (productsPrepared) {
      removeProductArtifactsNoThrow(fileRoot);
    }
    printLog << "\nFATAL ERROR: " << error.what() << '\n';
    printLog.close();
    std::cerr << "bellhop_rayreuse: " << error.what() << '\n';
    return 1;
  }
}
