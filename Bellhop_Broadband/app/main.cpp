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
#include <utility>
#include <variant>
#include <vector>

#include "broadband/error.hpp"
#include "broadband/io/arrival_writer.hpp"
#include "broadband/io/command_line.hpp"
#include "broadband/io/eigenray_writer.hpp"
#include "broadband/io/environment_parser.hpp"
#include "broadband/io/ray_writer.hpp"
#include "broadband/io/shd_writer.hpp"
#include "broadband/solver/arrival_solver.hpp"
#include "broadband/solver/eigenray_solver.hpp"
#include "broadband/solver/nonreuse_solver.hpp"
#include "broadband/solver/ray_trace_product.hpp"
#include "broadband/solver/reuse_freq_para_solver.hpp"
#include "broadband/solver/reuse_range_para_solver.hpp"
#include "broadband/solver/reuse_serial_solver.hpp"
#include "broadband/solver/single_frequency_solver.hpp"

namespace {

using Clock = std::chrono::steady_clock;

void printUsage(std::ostream& stream) {
  stream << "Usage: bellhop_broadband --version\n"
         << "       bellhop_broadband <file-root> "
            "[--frequencies-hz <f0,f1,...>] "
            "[--execution-mode <nonreuse|reuse>] "
            "[--reuse-mode <serial|frequency|range>] "
            "[--trace-workers <count>] "
            "[--reuse-workers <count>] "
            "[--verify-cache] [--profile-influence] "
            "[--profile-frequency-tasks] "
            "[--output-queue-capacity <count>] "
            "[--memory-budget-mib <MiB>]\n"
         << "\n"
         << "Reads <file-root>.env and writes <file-root>.prt plus the "
            "mode-specific product.\n"
         << "Without --frequencies-hz, the scalar or strictly increasing "
            "frequency list in the .env file is used.\n"
         << "With --frequencies-hz, the strictly increasing list "
            "overrides the .env frequency (an Input override).\n"
         << "Execution defaults to nonreuse; under --execution-mode reuse "
            "the reuse mode defaults to serial.\n"
         << "--trace-workers defaults to 1 (serial trace at N=1, static "
            "parallel trace above 1) and applies to nonreuse and to every "
            "reuse route.\n"
         << "--reuse-workers defaults to 1; the frequency route splits "
            "frequency tasks and the range route splits receiver-range "
            "blocks across the workers, while the serial route does not "
            "accept the option.\n"
         << "--verify-cache hashes the complete frozen ray cache before "
            "and after projection and is intended for validation.\n"
         << "--profile-influence records detailed Influence work counts "
            "and sub-phase timings for Cartesian Cerveny TL only; it is "
            "disabled by default.\n"
         << "--profile-frequency-tasks, --output-queue-capacity, and "
            "--memory-budget-mib apply only to reuse + frequency "
            "multi-frequency TL runs; the output queue defaults to 2 and "
            "a zero/unset memory budget means no explicit budget.\n";
}

[[nodiscard]] std::string runModeName(broadband::SimulationRunMode mode) {
  switch (mode) {
    case broadband::SimulationRunMode::Coherent:
      return "coherent TL (SHD)";
    case broadband::SimulationRunMode::Incoherent:
      return "incoherent TL (SHD)";
    case broadband::SimulationRunMode::SemiCoherent:
      return "semi-coherent TL (SHD)";
    case broadband::SimulationRunMode::RayTrace:
      return "ray trace (RAY)";
    case broadband::SimulationRunMode::AsciiArrivals:
      return "ASCII arrivals (ARR)";
    case broadband::SimulationRunMode::BinaryArrivals:
      return "binary arrivals (ARR)";
    case broadband::SimulationRunMode::Eigenray:
      return "eigenray (RAY)";
  }
  throw broadband::ValidationError("unknown simulation run mode");
}

[[nodiscard]] char fieldComponentToken(broadband::FieldComponent component) {
  switch (component) {
    case broadband::FieldComponent::Pressure:
      return 'P';
    case broadband::FieldComponent::Vertical:
      return 'V';
    case broadband::FieldComponent::Horizontal:
      return 'H';
  }
  throw broadband::ValidationError("field component is invalid");
}

[[nodiscard]] std::string_view curvatureModeLabel(
    broadband::BoundaryCurvatureMode mode) {
  switch (mode) {
    case broadband::BoundaryCurvatureMode::Double:
      return "Curvature doubling invoked";
    case broadband::BoundaryCurvatureMode::Standard:
      return "Standard curvature condition";
    case broadband::BoundaryCurvatureMode::Zero:
      return "Curvature zeroing invoked";
  }
  throw broadband::ValidationError("boundary curvature mode is invalid");
}

[[nodiscard]] std::string_view beamWidthModeLabel(
    broadband::BeamWidthMode mode) {
  switch (mode) {
    case broadband::BeamWidthMode::SpaceFilling:
      return "Space filling beams";
    case broadband::BeamWidthMode::MinimumWidth:
      return "Minimum width beams";
    case broadband::BeamWidthMode::Wkb:
      return "WKB beams";
  }
  throw broadband::ValidationError("beam width mode is invalid");
}

[[nodiscard]] std::string_view attenuationUnitLabel(
    broadband::AttenuationUnit unit) {
  switch (unit) {
    case broadband::AttenuationUnit::NepersPerMeter:
      return "nepers/m";
    case broadband::AttenuationUnit::DecibelsPerMeter:
      return "dB/m";
    case broadband::AttenuationUnit::DecibelsPerMeterPowerLaw:
      return "dB/m with power law";
    case broadband::AttenuationUnit::DecibelsPerMeterKilohertz:
      return "dB/mkHz";
    case broadband::AttenuationUnit::DecibelsPerWavelength:
      return "dB/wavelength";
    case broadband::AttenuationUnit::QualityFactor:
      return "Q";
    case broadband::AttenuationUnit::LossParameter:
      return "Loss parameter";
  }
  throw broadband::ValidationError("attenuation unit is invalid");
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
    throw broadband::BellhopError("unable to remove stale product " +
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
    throw broadband::BellhopError("unable to scan stale products for " +
                                  fileRoot + ": " + iteratorError.message());
  }
}

void removeProductArtifactsNoThrow(const std::string& fileRoot) noexcept {
  try {
    removeProductArtifacts(fileRoot);
  } catch (...) {
  }
}

void validateProductOptions(const broadband::ParsedEnvironment& parsed,
                            const broadband::CommandLineOptions& options) {
  const broadband::SimulationRunMode mode = parsed.simulationCase.runMode();
  const bool unsupportedFrequencyReuseTuning =
      options.outputQueueCapacitySpecified || options.memoryBudgetSpecified;
  if (mode == broadband::SimulationRunMode::RayTrace) {
    if (parsed.simulationCase.frequencies().size() != 1U) {
      throw broadband::ValidationError(
          "multi-frequency R products are not supported by the executable");
    }
    if (options.executionMode == broadband::ExecutionMode::Reuse) {
      throw broadband::ValidationError(
          "--execution-mode reuse is not defined for R products");
    }
    if (options.profileInfluence || options.profileFrequencyTasks ||
        unsupportedFrequencyReuseTuning) {
      throw broadband::ValidationError(
          "profiling and frequency reuse tuning options are only supported for "
          "TL");
    }
    return;
  }
  if (broadband::isTransmissionLossMode(mode) &&
      parsed.simulationCase.frequencies().size() == 1U &&
      options.executionMode == broadband::ExecutionMode::Reuse) {
    throw broadband::ValidationError(
        "--execution-mode reuse requires a multi-frequency TL run");
  }
  if (broadband::isTransmissionLossMode(mode) &&
      options.executionMode == broadband::ExecutionMode::Reuse &&
      options.reuseMode == broadband::ReuseMode::Range) {
    // Range Reuse TL covers every run mode of the Cerveny family in
    // both coordinate systems (coherent, incoherent, semi-coherent; IGR-3A
    // A02b/A03 design §9), of the geometric hat family in both coordinate
    // systems since A04, of the Cartesian geometric Gaussian family since
    // A05, and of the simple Gaussian family in its only legal mode,
    // coherent, since A06 (non-coherent simple Gaussian runs are rejected
    // earlier, at SimulationCase construction).
    if (parsed.simulationCase.beamFamily() !=
            broadband::BeamFamily::CervenyGaussian &&
        parsed.simulationCase.beamFamily() !=
            broadband::BeamFamily::GeometricHat &&
        parsed.simulationCase.beamFamily() !=
            broadband::BeamFamily::GeometricGaussian &&
        parsed.simulationCase.beamFamily() !=
            broadband::BeamFamily::SimpleGaussian) {
      throw broadband::ValidationError(
          "--reuse-mode range requires Cerveny Gaussian, geometric hat, "
          "geometric Gaussian, or simple Gaussian TL");
    }
    if (parsed.simulationCase.sourceCount() != 1U) {
      throw broadband::ValidationError(
          "--reuse-mode range requires a single source");
    }
    if (parsed.simulationCase.receivers().isIrregular()) {
      throw broadband::ValidationError(
          "--reuse-mode range requires a rectilinear receiver grid");
    }
  }
  if (broadband::isTransmissionLossMode(mode) && options.profileInfluence &&
      (parsed.simulationCase.beamFamily() !=
           broadband::BeamFamily::CervenyGaussian ||
       parsed.simulationCase.cervenyCoordinateSystem() !=
           broadband::CervenyCoordinateSystem::Cartesian)) {
    throw broadband::ValidationError(
        "--profile-influence is currently defined only for Cartesian "
        "Cerveny TL");
  }
  if (mode == broadband::SimulationRunMode::AsciiArrivals ||
      mode == broadband::SimulationRunMode::BinaryArrivals) {
    if (options.profileInfluence || options.profileFrequencyTasks ||
        unsupportedFrequencyReuseTuning) {
      throw broadband::ValidationError(
          "Influence profiling and frequency reuse tuning options are not "
          "supported "
          "for arrival/eigenray products");
    }
    if (options.executionMode == broadband::ExecutionMode::Reuse &&
        options.reuseMode == broadband::ReuseMode::Range) {
      if (parsed.simulationCase.frequencies().size() < 2U) {
        throw broadband::ValidationError(
            "--reuse-mode range requires a multi-frequency arrival run");
      }
      if (parsed.simulationCase.beamFamily() !=
              broadband::BeamFamily::GeometricHat &&
          parsed.simulationCase.beamFamily() !=
              broadband::BeamFamily::GeometricGaussian) {
        throw broadband::ValidationError(
            "--reuse-mode range arrivals require geometric hat or "
            "geometric Gaussian beams");
      }
      if (parsed.simulationCase.receivers().isIrregular()) {
        throw broadband::ValidationError(
            "--reuse-mode range arrivals require a rectilinear receiver "
            "grid");
      }
    }
  }
  if (mode == broadband::SimulationRunMode::Eigenray) {
    if (options.profileInfluence || options.profileFrequencyTasks ||
        unsupportedFrequencyReuseTuning) {
      throw broadband::ValidationError(
          "Influence profiling and frequency reuse tuning options are not "
          "supported "
          "for arrival/eigenray products");
    }
    if (options.executionMode == broadband::ExecutionMode::Reuse &&
        options.reuseMode == broadband::ReuseMode::Range) {
      throw broadband::ValidationError(
          "--reuse-mode range is not defined for eigenray products");
    }
  }
}

void printVersion(std::ostream& stream) {
  stream << "Bellhop Broadband " << BELLHOP_BROADBAND_VERSION << '\n';
}

void writeBoundarySummary(std::ostream& stream,
                          const broadband::BoundaryModel& boundary,
                          std::string_view name) {
  switch (boundary.kind()) {
    case broadband::BoundaryKind::Vacuum:
      stream << "VACUUM " << name << '\n';
      break;
    case broadband::BoundaryKind::Rigid:
      stream << "Perfectly RIGID " << name << '\n';
      break;
    case broadband::BoundaryKind::AcousticHalfSpace:
      stream << "ACOUSTO-ELASTIC half-space " << name << '\n';
      break;
    case broadband::BoundaryKind::GrainSizeHalfSpace:
      stream << "Grain size to define half-space " << name << '\n'
             << "Grain size = " << boundary.grainSizeMaterial()->meanGrainSize
             << '\n';
      break;
    case broadband::BoundaryKind::TabulatedReflection:
      stream << "FILE used for reflection loss\n"
             << "Using tabulated " << name << " reflection coef.\n"
             << "Number of points in " << name << " reflection coefficient = "
             << boundary.reflectionTable()->size() << '\n';
      break;
  }
  if (!boundary.geometry().isFlat()) {
    if (boundary.geometry().interpolationKind() ==
        broadband::BoundaryInterpolationKind::Curvilinear) {
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
                               const broadband::ParsedEnvironment& parsed) {
  const broadband::SimulationCase& simulation = parsed.simulationCase;
  const broadband::Environment& environment = simulation.environment();
  stream << "BELLHOP BROADBAND\n"
         << "program version = " << BELLHOP_BROADBAND_VERSION << "\n\n"
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
    case broadband::BeamFamily::CervenyGaussian:
      stream << "Cerveny Gaussian\n";
      break;
    case broadband::BeamFamily::GeometricHat:
      stream << "geometric hat\n";
      break;
    case broadband::BeamFamily::GeometricGaussian:
      stream << "geometric Gaussian\n";
      break;
    case broadband::BeamFamily::SimpleGaussian:
      stream << "simple Gaussian\n";
      break;
  }
  stream << "source beam pattern = "
         << (simulation.sourceBeamPattern().isDirectional() ? "directional"
                                                            : "omnidirectional")
         << '\n';
  if (simulation.beamFamily() == broadband::BeamFamily::CervenyGaussian ||
      !broadband::isTransmissionLossMode(simulation.runMode())) {
    stream << (simulation.cervenyCoordinateSystem() ==
                       broadband::CervenyCoordinateSystem::RayCentered
                   ? "Ray centered beams\n"
                   : "Cartesian beams\n");
  }
  if (simulation.beamFamily() == broadband::BeamFamily::CervenyGaussian &&
      broadband::isTransmissionLossMode(simulation.runMode())) {
    stream << "Component = " << fieldComponentToken(simulation.fieldComponent())
           << '\n'
           << beamWidthModeLabel(simulation.beamWidthMode()) << '\n'
           << curvatureModeLabel(simulation.curvatureMode()) << '\n';
  }
  switch (simulation.runMode()) {
    case broadband::SimulationRunMode::Coherent:
      stream << "Coherent TL calculation\n";
      break;
    case broadband::SimulationRunMode::Incoherent:
      stream << "Incoherent TL calculation\n";
      break;
    case broadband::SimulationRunMode::SemiCoherent:
      stream << "Semi-coherent TL calculation\n";
      break;
    case broadband::SimulationRunMode::RayTrace:
      stream << "Ray trace run\n";
      break;
    case broadband::SimulationRunMode::AsciiArrivals:
      stream << "Arrivals calculation\n";
      stream << "Arrivals calculation, ASCII  file output\n";
      break;
    case broadband::SimulationRunMode::BinaryArrivals:
      stream << "Arrivals calculation\n"
             << "Arrivals calculation, binary file output\n";
      break;
    case broadband::SimulationRunMode::Eigenray:
      stream << "Eigenray trace run\n";
      break;
  }
  switch (simulation.beamFamily()) {
    case broadband::BeamFamily::CervenyGaussian:
      if (simulation.cervenyCoordinateSystem() ==
          broadband::CervenyCoordinateSystem::RayCentered) {
        stream << "Cerveny beams in ray-centered coordinates\n";
      } else {
        stream << "Cerveny beams in Cartesian coordinates\n";
      }
      break;
    case broadband::BeamFamily::GeometricHat:
      stream << (simulation.cervenyCoordinateSystem() ==
                         broadband::CervenyCoordinateSystem::RayCentered
                     ? "Geometric hat beams in ray-centered coordinates\n"
                     : "Geometric hat beams in Cartesian coordinates\n")
             << "Geometric hat beams\n";
      break;
    case broadband::BeamFamily::GeometricGaussian:
      stream << "Geometric gaussian beams in Cartesian coordinates\n"
             << "Geometric Gaussian beams\n";
      break;
    case broadband::BeamFamily::SimpleGaussian:
      stream << "Simple gaussian beams\n"
             << "Simple Gaussian beams\n";
      break;
  }
  if (simulation.sourceBeamPattern().isDirectional()) {
    stream << "Using source beam pattern file\n"
           << "Number of source beam pattern points = "
           << simulation.sourceBeamPattern().size() << '\n';
  }
  if (simulation.sourceGeometry() == broadband::SourceGeometry::Line) {
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
      broadband::SspInterpolationKind::Quadrilateral) {
    stream << "Using range-dependent sound speed\n"
           << "Number of SSP ranges = "
           << environment.soundSpeedProfile().quadrilateralGrid()->rangeCount
           << '\n';
  }
  writeBoundarySummary(stream, environment.seabed(), "bottom");
  const broadband::VolumeAttenuation& volumeAttenuation =
      environment.volumeAttenuation();
  switch (volumeAttenuation.model) {
    case broadband::VolumeAttenuationModel::None:
      break;
    case broadband::VolumeAttenuationModel::Thorp:
      stream << "THORP volume attenuation added\n";
      break;
    case broadband::VolumeAttenuationModel::FrancoisGarrison:
      stream << "Francois-Garrison volume attenuation added\n";
      break;
    case broadband::VolumeAttenuationModel::Biological: {
      stream << "Biological attenaution\n";
      const auto& layers =
          *std::get<broadband::SharedBiologicalAttenuationLayers>(
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
    for (const broadband::Source& source : simulation.sources()) {
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
    std::ostream& stream, const broadband::SingleFrequencyResult& result) {
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
    const broadband::CartesianCervenyStatistics& statistics) {
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
    std::ostream& stream, const broadband::FrequencyGrid& frequencies,
    const std::vector<broadband::SingleFrequencyTimings>& timings) {
  if (timings.size() != frequencies.size()) {
    throw broadband::ValidationError(
        "frequency-task timing count must match frequency count");
  }
  stream << "frequency task count = " << timings.size() << '\n';
  for (std::size_t index = 0U; index < timings.size(); ++index) {
    const broadband::SingleFrequencyTimings& timing = timings[index];
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

[[nodiscard]] std::size_t memoryBudgetBytes(std::size_t memoryBudgetMiB) {
  constexpr std::size_t bytesPerMiB = 1024U * 1024U;
  if (memoryBudgetMiB > std::numeric_limits<std::size_t>::max() / bytesPerMiB) {
    throw broadband::ValidationError(
        "--memory-budget-mib exceeds the platform size limit");
  }
  return memoryBudgetMiB * bytesPerMiB;
}

void writeProductExecutionMode(std::ostream& stream,
                               broadband::ExecutionMode mode,
                               broadband::ReuseMode reuseMode,
                               std::size_t frequencyCount,
                               std::size_t sourceCount = 1U) {
  switch (mode) {
    case broadband::ExecutionMode::NonReuse:
      // Single-frequency non-reuse keeps the historical line wording.
      stream << (frequencyCount == 1U
                     ? "execution mode = single-frequency non-reuse\n"
                     : "execution mode = broadband nonreuse\n");
      break;
    case broadband::ExecutionMode::Reuse:
      // Single-frequency reuse keeps the historical first-line wording, so
      // single-frequency ARR/E runs are not misdescribed as broadband; the
      // reuse-mode line is emitted in both cases.
      stream << (frequencyCount == 1U
                     ? "execution mode = single-frequency reuse\n"
                     : "execution mode = broadband reuse\n");
      switch (reuseMode) {
        case broadband::ReuseMode::Serial:
          stream << "reuse mode = serial\n";
          break;
        case broadband::ReuseMode::Frequency:
          stream << "reuse mode = frequency\n";
          break;
        case broadband::ReuseMode::Range:
          stream << "reuse mode = range\n";
          break;
      }
      break;
  }
  // Frozen semantics (Worklist FP-2F §1.5): trace passes count per-source
  // fan traces (non-reuse = Nfreq x NSz, reuse routes = NSz).
  stream << "Trace passes = "
         << (mode == broadband::ExecutionMode::NonReuse
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

// Trace-worker PRT lines (Worklist PERF-TRACE-PAR-1 A01), emitted only when
// --trace-workers is present. Non-reuse products re-trace per frequency, so
// their flat second groups are labeled with the frequency task and source
// indices; every other product reports one group per source like reuse.
void writeTraceWorkerCounts(std::ostream& stream,
                            std::size_t requestedTraceWorkerCount,
                            std::size_t effectiveTraceWorkerCount) {
  stream << "requested trace worker count = " << requestedTraceWorkerCount
         << '\n'
         << "effective trace worker count = " << effectiveTraceWorkerCount
         << '\n';
}

void writeTraceWorkerSeconds(
    std::ostream& stream,
    const std::vector<std::vector<double>>& traceWorkerSeconds,
    std::size_t sourceCount, bool frequencyTaskGroups) {
  for (std::size_t group = 0U; group < traceWorkerSeconds.size(); ++group) {
    const std::vector<double>& workerSeconds = traceWorkerSeconds[group];
    const std::size_t frequencyTask =
        frequencyTaskGroups && sourceCount > 0U ? group / sourceCount : 0U;
    const std::size_t sourceIndex =
        frequencyTaskGroups && sourceCount > 0U ? group % sourceCount : group;
    for (std::size_t workerIndex = 0U; workerIndex < workerSeconds.size();
         ++workerIndex) {
      if (frequencyTaskGroups) {
        stream << "frequency task " << frequencyTask << " source "
               << sourceIndex << " trace worker " << workerIndex
               << " seconds = " << workerSeconds[workerIndex] << '\n';
      } else {
        stream << "source " << sourceIndex << " trace worker " << workerIndex
               << " seconds = " << workerSeconds[workerIndex] << '\n';
      }
    }
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

  broadband::CommandLineOptions options;
  try {
    options = broadband::parseCommandLine(argumentViews);
  } catch (const std::exception& error) {
    printUsage(std::cerr);
    std::cerr << "bellhop_broadband: " << error.what() << '\n';
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
    std::cerr << "bellhop_broadband: unable to open print output: " << printPath
              << '\n';
    return 1;
  }
  printLog << std::setprecision(17);

  bool productsPrepared = false;
  try {
    const broadband::ParsedEnvironment parsed =
        broadband::EnvironmentParser::parseFile(
            environmentPath, std::move(options.frequencyOverrideHz));
    validateProductOptions(parsed, options);
    // Product files are mode-owned.  Remove every known product for this
    // root before solving so switching modes cannot expose stale output.
    removeProductArtifacts(fileRoot);
    productsPrepared = true;
    writeConfigurationSummary(printLog, parsed);
    broadband::CartesianCervenySettings influenceSettings =
        parsed.beam.influence;
    influenceSettings.collectStatistics = options.profileInfluence;
    // Shared-seam trace settings (Worklist PERF-TRACE-PAR-1 A01): every
    // product consumes the same value; the default of 1 keeps the serial
    // fast path and byte-identical output.
    const broadband::RayFanTraceSettings traceSettings{
        .workerCount = options.traceWorkerCount};

    const Clock::time_point solveBegin = Clock::now();
    const broadband::SimulationRunMode runMode =
        parsed.simulationCase.runMode();
    if (runMode == broadband::SimulationRunMode::RayTrace) {
      const double frequency =
          parsed.simulationCase.frequencies().values().front();
      // One frozen per-source cache per SimulationCase::sources() entry; the
      // R product carries every source's fan in depth-ascending order.
      const std::vector<broadband::RayFanTraceResult> sourceTraces =
          broadband::traceRayProducts(parsed.simulationCase, traceSettings);
      std::vector<std::uint64_t> fingerprintsBefore;
      fingerprintsBefore.reserve(sourceTraces.size());
      std::size_t rayCount = 0U;
      std::size_t rayCacheBytes = 0U;
      for (const broadband::RayFanTraceResult& trace : sourceTraces) {
        fingerprintsBefore.push_back(trace.cache.contentFingerprint());
        rayCount += trace.cache.size();
        rayCacheBytes += trace.cache.memoryFootprintBytes();
      }
      const std::filesystem::path output =
          productPath(fileRoot, 0U, 1U, frequency, ".ray");
      broadband::RayWriter writer(output, parsed.title, parsed.simulationCase,
                                  frequency);
      // One fan block per source in SimulationCase::sources() order (depth
      // ascending) with the `1 1 NSz` ray-file header (Origin WriteRay).
      for (std::size_t sourceIndex = 0U; sourceIndex < sourceTraces.size();
           ++sourceIndex) {
        writer.appendSource(sourceIndex, sourceTraces[sourceIndex].cache);
      }
      writer.finalize();
      std::vector<std::uint64_t> fingerprintsAfter;
      fingerprintsAfter.reserve(sourceTraces.size());
      for (const broadband::RayFanTraceResult& trace : sourceTraces) {
        fingerprintsAfter.push_back(trace.cache.contentFingerprint());
      }
      if (options.verifyCache && fingerprintsAfter != fingerprintsBefore) {
        throw broadband::ValidationError(
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
      if (options.traceWorkerCountSpecified) {
        writeTraceWorkerCounts(printLog,
                               sourceTraces.front().requestedWorkerCount,
                               sourceTraces.front().effectiveWorkerCount);
        std::vector<std::vector<double>> traceWorkerSeconds;
        traceWorkerSeconds.reserve(sourceTraces.size());
        for (const broadband::RayFanTraceResult& trace : sourceTraces) {
          traceWorkerSeconds.push_back(trace.workerSeconds);
        }
        writeTraceWorkerSeconds(printLog, traceWorkerSeconds, 0U, false);
      }
    } else if (runMode == broadband::SimulationRunMode::AsciiArrivals ||
               runMode == broadband::SimulationRunMode::BinaryArrivals) {
      const broadband::ArrivalEncoding encoding =
          runMode == broadband::SimulationRunMode::AsciiArrivals
              ? broadband::ArrivalEncoding::Ascii
              : broadband::ArrivalEncoding::Binary;
      const std::string_view extension = ".arr";
      const auto consumer =
          [&](std::size_t frequencyIndex,
              const std::vector<broadband::RayPathCache>& caches,
              const std::vector<broadband::ArrivalWorkspace>& workspaces) {
            std::vector<std::uint64_t> before;
            before.reserve(caches.size());
            for (const broadband::RayPathCache& cache : caches) {
              before.push_back(cache.contentFingerprint());
            }
            const double frequency = workspaces.front().frequency();
            const std::filesystem::path output =
                productPath(fileRoot, frequencyIndex,
                            parsed.simulationCase.frequencies().size(),
                            frequency, extension);
            // One per-source block in depth-ascending order; the ARR header
            // carries the source count and every source depth (Origin ArrMod).
            broadband::ArrivalWriter::write(output, parsed.title,
                                            parsed.simulationCase, workspaces,
                                            encoding);
            std::vector<std::uint64_t> after;
            after.reserve(caches.size());
            for (const broadband::RayPathCache& cache : caches) {
              after.push_back(cache.contentFingerprint());
            }
            if (options.verifyCache && after != before) {
              throw broadband::ValidationError(
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
      broadband::ArrivalSolverStatistics statistics;
      std::size_t rangeRequestedWorkers = 0U;
      std::size_t rangeEffectiveWorkers = 0U;
      double rangeWriterSeconds = 0.0;
      if (options.executionMode == broadband::ExecutionMode::NonReuse) {
        statistics = broadband::ArrivalSolver::solveNonReuse(
            parsed.simulationCase, consumer, options.verifyCache,
            traceSettings);
      } else if (options.reuseMode == broadband::ReuseMode::Serial) {
        statistics =
            broadband::ArrivalSolver::solve(parsed.simulationCase, consumer,
                                            options.verifyCache, traceSettings);
      } else if (options.reuseMode == broadband::ReuseMode::Frequency) {
        statistics = broadband::ArrivalSolver::solveFrequency(
            parsed.simulationCase, consumer, options.reuseWorkerCount,
            options.verifyCache, traceSettings);
      } else {
        // ReuseMode::Range: multi-frequency arrivals streamed through the
        // Range Reuse solver, one source at a time.
        std::vector<std::filesystem::path> outputPaths;
        outputPaths.reserve(parsed.simulationCase.frequencies().size());
        for (std::size_t frequencyIndex = 0U;
             frequencyIndex < parsed.simulationCase.frequencies().size();
             ++frequencyIndex) {
          outputPaths.push_back(productPath(
              fileRoot, frequencyIndex,
              parsed.simulationCase.frequencies().size(),
              parsed.simulationCase.frequencies().values()[frequencyIndex],
              extension));
        }

        const Clock::time_point writerSetupBegin = Clock::now();
        broadband::BroadbandArrivalWriterSet writers(
            outputPaths, parsed.title, parsed.simulationCase, encoding);
        rangeWriterSeconds +=
            std::chrono::duration<double>(Clock::now() - writerSetupBegin)
                .count();
        const broadband::FusedArrivalSourceConsumer fusedConsumer =
            [&](std::size_t sourceIndex,
                const broadband::BroadbandArrivalWorkspace& workspace) {
              const Clock::time_point appendBegin = Clock::now();
              writers.appendSource(sourceIndex, workspace);
              rangeWriterSeconds +=
                  std::chrono::duration<double>(Clock::now() - appendBegin)
                      .count();
            };
        rangeRequestedWorkers = options.reuseWorkerCount;
        rangeEffectiveWorkers =
            std::min(rangeRequestedWorkers,
                     parsed.simulationCase.receivers().rangeCount());
        statistics = broadband::ReuseRangeParaSolver::solveArrivalStreaming(
            parsed.simulationCase, fusedConsumer, influenceSettings,
            options.verifyCache,
            broadband::ReuseRangeParaExecutionSettings{
                .requestedRangeWorkers = rangeRequestedWorkers,
                .traceSettings = traceSettings});
        const Clock::time_point finalizeBegin = Clock::now();
        writers.finalize();
        rangeWriterSeconds +=
            std::chrono::duration<double>(Clock::now() - finalizeBegin).count();

        for (std::size_t frequencyIndex = 0U;
             frequencyIndex < outputPaths.size(); ++frequencyIndex) {
          printLog
              << "frequency product index = " << frequencyIndex
              << " frequency Hz = "
              << parsed.simulationCase.frequencies().values()[frequencyIndex]
              << '\n'
              << "product = " << outputPaths[frequencyIndex] << '\n';
        }
      }
      writeProductExecutionMode(printLog, options.executionMode,
                                options.reuseMode, statistics.frequencyCount,
                                parsed.simulationCase.sourceCount());
      printLog << "frequency count = " << statistics.frequencyCount << '\n'
               << "ray count = " << statistics.rayCount << '\n'
               << "arrival candidate count = " << statistics.candidateCount
               << '\n'
               << "arrival consume seconds = " << statistics.consumeSeconds
               << '\n';
      if (options.executionMode == broadband::ExecutionMode::Reuse &&
          options.reuseMode == broadband::ReuseMode::Range) {
        printLog << "requested reuse worker count = " << rangeRequestedWorkers
                 << '\n'
                 << "effective reuse worker count = " << rangeEffectiveWorkers
                 << '\n'
                 << "Trace seconds = " << statistics.traceSeconds << '\n'
                 << "Project seconds = " << statistics.projectSeconds << '\n'
                 << "Influence seconds = " << statistics.influenceSeconds
                 << '\n'
                 << "peak ray cache bytes = " << statistics.peakRayCacheBytes
                 << '\n'
                 << "peak arrival workspace bytes = "
                 << statistics.peakArrivalWorkspaceBytes << '\n'
                 << "ARR writer seconds = " << rangeWriterSeconds << '\n';
      }
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
      if (options.traceWorkerCountSpecified) {
        writeTraceWorkerCounts(printLog, statistics.requestedTraceWorkerCount,
                               statistics.effectiveTraceWorkerCount);
        writeTraceWorkerSeconds(
            printLog, statistics.traceWorkerSecondsBySource,
            parsed.simulationCase.sourceCount(),
            options.executionMode == broadband::ExecutionMode::NonReuse);
      }
    } else if (runMode == broadband::SimulationRunMode::Eigenray) {
      const auto consumer =
          [&](std::size_t frequencyIndex,
              const std::vector<broadband::RayPathCache>& caches,
              const std::vector<broadband::EigenraySourceHits>& sourceHits) {
            std::vector<std::uint64_t> before;
            before.reserve(caches.size());
            for (const broadband::RayPathCache& cache : caches) {
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
            broadband::EigenrayWriter::write(output, parsed.title,
                                             parsed.simulationCase, frequency,
                                             caches, sourceHits);
            std::size_t frequencyHitCount = 0U;
            for (const broadband::EigenraySourceHits& hits : sourceHits) {
              frequencyHitCount += hits.size();
            }
            std::vector<std::uint64_t> after;
            after.reserve(caches.size());
            for (const broadband::RayPathCache& cache : caches) {
              after.push_back(cache.contentFingerprint());
            }
            if (options.verifyCache && after != before) {
              throw broadband::ValidationError(
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
      broadband::EigenraySolverStatistics statistics;
      if (options.executionMode == broadband::ExecutionMode::NonReuse) {
        statistics = broadband::EigenraySolver::solveNonReuse(
            parsed.simulationCase, consumer, options.verifyCache,
            traceSettings);
      } else if (options.reuseMode == broadband::ReuseMode::Serial) {
        statistics = broadband::EigenraySolver::solve(
            parsed.simulationCase, consumer, options.verifyCache,
            traceSettings);
      } else if (options.reuseMode == broadband::ReuseMode::Frequency) {
        statistics = broadband::EigenraySolver::solveFrequency(
            parsed.simulationCase, consumer, options.reuseWorkerCount,
            options.verifyCache, traceSettings);
      } else {
        // ReuseMode::Range is rejected by validateProductOptions for
        // eigenray products; this arm is unreachable defense.
        throw broadband::ValidationError(
            "--reuse-mode range is not defined for eigenray products");
      }
      writeProductExecutionMode(printLog, options.executionMode,
                                options.reuseMode, statistics.frequencyCount,
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
      if (options.traceWorkerCountSpecified) {
        writeTraceWorkerCounts(printLog, statistics.requestedTraceWorkerCount,
                               statistics.effectiveTraceWorkerCount);
        writeTraceWorkerSeconds(
            printLog, statistics.traceWorkerSecondsBySource,
            parsed.simulationCase.sourceCount(),
            options.executionMode == broadband::ExecutionMode::NonReuse);
      }
    } else if (parsed.simulationCase.frequencies().size() == 1U) {
      const broadband::SingleFrequencyResult result =
          broadband::SingleFrequencySolver::solve(
              parsed.simulationCase, parsed.beam.epsilonMultiplier,
              parsed.beam.loopRange, influenceSettings, traceSettings);

      const Clock::time_point writeBegin = Clock::now();
      // Per-source SHD records: one receiversPerRange-record block per source
      // under the NSz-carrying header (Origin: IRec = 10 + NSz per source).
      broadband::ShdWriter::writeSingleFrequency(
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
      if (options.traceWorkerCountSpecified) {
        writeTraceWorkerCounts(printLog, result.requestedTraceWorkerCount,
                               result.effectiveTraceWorkerCount);
        writeTraceWorkerSeconds(printLog, result.traceWorkerSecondsBySource, 0U,
                                false);
      }
    } else if (options.executionMode == broadband::ExecutionMode::NonReuse) {
      broadband::NonReuseResult result = broadband::NonReuseSolver::solve(
          parsed.simulationCase, parsed.beam.epsilonMultiplier,
          parsed.beam.loopRange, influenceSettings, traceSettings);

      // One source-major workspace vector per frequency (first source in
      // `workspace`, the rest in `additionalSourceWorkspaces`).
      std::vector<std::vector<broadband::FrequencyWorkspace>>
          sourceWorkspacesPerFrequency;
      sourceWorkspacesPerFrequency.reserve(result.frequencyResults.size());
      for (broadband::SingleFrequencyResult& frequencyResult :
           result.frequencyResults) {
        std::vector<broadband::FrequencyWorkspace> sourceWorkspaces;
        sourceWorkspaces.reserve(frequencyResult.sourceCount());
        sourceWorkspaces.push_back(std::move(frequencyResult.workspace));
        for (broadband::FrequencyWorkspace& additional :
             frequencyResult.additionalSourceWorkspaces) {
          sourceWorkspaces.push_back(std::move(additional));
        }
        sourceWorkspacesPerFrequency.push_back(std::move(sourceWorkspaces));
      }

      const Clock::time_point writeBegin = Clock::now();
      broadband::ShdWriter::writeFrequencies(shadePath, parsed.title,
                                             parsed.simulationCase,
                                             sourceWorkspacesPerFrequency);
      const double writeSeconds =
          std::chrono::duration<double>(Clock::now() - writeBegin).count();

      printLog << "execution mode = broadband nonreuse\n"
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
               << "Solver wall seconds = " << result.statistics.wallSeconds
               << '\n'
               << "SHD seconds = " << writeSeconds << '\n';
      if (options.profileInfluence) {
        writeInfluenceStatistics(
            printLog, result.statistics.phaseTotals.influenceStatistics);
      }
      if (options.traceWorkerCountSpecified) {
        writeTraceWorkerCounts(
            printLog, result.frequencyResults.front().requestedTraceWorkerCount,
            result.frequencyResults.front().effectiveTraceWorkerCount);
        std::vector<std::vector<double>> traceWorkerSeconds;
        traceWorkerSeconds.reserve(result.statistics.tracePassCount);
        for (const broadband::SingleFrequencyResult& frequencyResult :
             result.frequencyResults) {
          for (const std::vector<double>& sourceWorkerSeconds :
               frequencyResult.traceWorkerSecondsBySource) {
            traceWorkerSeconds.push_back(sourceWorkerSeconds);
          }
        }
        writeTraceWorkerSeconds(printLog, traceWorkerSeconds,
                                parsed.simulationCase.sourceCount(), true);
      }
    } else if (options.reuseMode == broadband::ReuseMode::Serial) {
      double writeSeconds = 0.0;
      const Clock::time_point writerSetupBegin = Clock::now();
      broadband::ShdFrequencyWriter writer(shadePath, parsed.title,
                                           parsed.simulationCase);
      writeSeconds +=
          std::chrono::duration<double>(Clock::now() - writerSetupBegin)
              .count();
      const broadband::RayReuseFrequencyConsumer consumer =
          [&](std::size_t frequencyIndex,
              std::vector<broadband::FrequencyWorkspace>&& sourceWorkspaces,
              const broadband::SingleFrequencyTimings&) {
            const Clock::time_point writeBegin = Clock::now();
            // One receiversPerRange-record block per source in the frequency
            // slot (source-major, Origin IRec addressing).
            writer.writeFrequency(frequencyIndex, sourceWorkspaces);
            writeSeconds +=
                std::chrono::duration<double>(Clock::now() - writeBegin)
                    .count();
          };
      const broadband::ReuseSerialStatistics statistics =
          broadband::ReuseSerialSolver::solveStreaming(
              parsed.simulationCase, parsed.beam.epsilonMultiplier,
              parsed.beam.loopRange, consumer, influenceSettings,
              options.verifyCache, traceSettings);
      const Clock::time_point finalizeBegin = Clock::now();
      writer.finalize();
      writeSeconds +=
          std::chrono::duration<double>(Clock::now() - finalizeBegin).count();

      printLog << "execution mode = broadband reuse\n"
               << "reuse mode = serial\n"
               << "Trace passes = " << statistics.tracePassCount << '\n'
               << "ray count = " << statistics.rayCount << '\n'
               << "ray point count = " << statistics.totalRayPointCount << '\n'
               << "ray cache bytes = " << statistics.rayCacheBytes << '\n';
      if (options.traceWorkerCountSpecified) {
        printLog << "requested trace worker count = "
                 << statistics.requestedTraceWorkerCount << '\n'
                 << "effective trace worker count = "
                 << statistics.effectiveTraceWorkerCount << '\n';
      }
      printLog << "Trace seconds = " << statistics.phaseTotals.traceSeconds
               << '\n'
               << "Project seconds = " << statistics.phaseTotals.projectSeconds
               << '\n'
               << "Influence seconds = "
               << statistics.phaseTotals.influenceSeconds << '\n'
               << "Scale seconds = " << statistics.phaseTotals.scaleSeconds
               << '\n'
               << "Solver wall seconds = " << statistics.wallSeconds << '\n'
               << "SHD seconds = " << writeSeconds << '\n';
      if (options.traceWorkerCountSpecified) {
        for (std::size_t sourceIndex = 0U;
             sourceIndex < statistics.traceWorkerSecondsBySource.size();
             ++sourceIndex) {
          const std::vector<double>& workerSeconds =
              statistics.traceWorkerSecondsBySource[sourceIndex];
          for (std::size_t workerIndex = 0U; workerIndex < workerSeconds.size();
               ++workerIndex) {
            printLog << "source " << sourceIndex << " trace worker "
                     << workerIndex
                     << " seconds = " << workerSeconds[workerIndex] << '\n';
          }
        }
      }
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
    } else if (options.reuseMode == broadband::ReuseMode::Range) {
      double writeSeconds = 0.0;
      const Clock::time_point writerSetupBegin = Clock::now();
      broadband::ShdFrequencyWriter writer(shadePath, parsed.title,
                                           parsed.simulationCase);
      writeSeconds +=
          std::chrono::duration<double>(Clock::now() - writerSetupBegin)
              .count();
      const broadband::RayReuseFrequencyConsumer consumer =
          [&](std::size_t frequencyIndex,
              std::vector<broadband::FrequencyWorkspace>&& sourceWorkspaces,
              const broadband::SingleFrequencyTimings&) {
            const Clock::time_point writeBegin = Clock::now();
            // One receiversPerRange-record block per source in the frequency
            // slot (source-major, Origin IRec addressing).
            writer.writeFrequency(frequencyIndex, sourceWorkspaces);
            writeSeconds +=
                std::chrono::duration<double>(Clock::now() - writeBegin)
                    .count();
          };
      const broadband::ReuseRangeParaStatistics statistics =
          broadband::ReuseRangeParaSolver::solveStreaming(
              parsed.simulationCase, parsed.beam.epsilonMultiplier,
              parsed.beam.loopRange, consumer, influenceSettings,
              options.verifyCache,
              broadband::ReuseRangeParaExecutionSettings{
                  .requestedRangeWorkers = options.reuseWorkerCount,
                  .traceSettings = traceSettings});
      const Clock::time_point finalizeBegin = Clock::now();
      writer.finalize();
      writeSeconds +=
          std::chrono::duration<double>(Clock::now() - finalizeBegin).count();

      printLog << "execution mode = broadband reuse\n"
               << "reuse mode = range\n"
               << "requested reuse worker count = "
               << statistics.requestedRangeWorkers << '\n'
               << "effective reuse worker count = "
               << statistics.effectiveRangeWorkers << '\n'
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
               << "Solver wall seconds = " << statistics.wallSeconds << '\n'
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
      if (options.traceWorkerCountSpecified) {
        writeTraceWorkerCounts(printLog, statistics.requestedTraceWorkerCount,
                               statistics.effectiveTraceWorkerCount);
        writeTraceWorkerSeconds(printLog, statistics.traceWorkerSecondsBySource,
                                0U, false);
      }
    } else {
      // ReuseMode::Frequency: Frequency Reuse TL.
      double writeSeconds = 0.0;
      const Clock::time_point writerSetupBegin = Clock::now();
      broadband::ShdFrequencyWriter writer(shadePath, parsed.title,
                                           parsed.simulationCase);
      writeSeconds +=
          std::chrono::duration<double>(Clock::now() - writerSetupBegin)
              .count();
      const broadband::RayReuseFrequencyConsumer consumer =
          [&](std::size_t frequencyIndex,
              std::vector<broadband::FrequencyWorkspace>&& sourceWorkspaces,
              const broadband::SingleFrequencyTimings&) {
            const Clock::time_point writeBegin = Clock::now();
            // One receiversPerRange-record block per source in the frequency
            // slot (source-major, Origin IRec addressing).
            writer.writeFrequency(frequencyIndex, sourceWorkspaces);
            writeSeconds +=
                std::chrono::duration<double>(Clock::now() - writeBegin)
                    .count();
          };
      const broadband::ReuseFreqParaSettings settings{
          .workerCount = options.reuseWorkerCount,
          .outputQueueCapacity = options.outputQueueCapacity,
          .memoryBudgetBytes = memoryBudgetBytes(options.memoryBudgetMiB),
          .traceSettings = traceSettings,
      };
      const broadband::ReuseFreqParaStatistics statistics =
          broadband::ReuseFreqParaSolver::solveStreaming(
              parsed.simulationCase, parsed.beam.epsilonMultiplier,
              parsed.beam.loopRange, consumer, settings, influenceSettings,
              options.verifyCache);
      const Clock::time_point finalizeBegin = Clock::now();
      writer.finalize();
      writeSeconds +=
          std::chrono::duration<double>(Clock::now() - finalizeBegin).count();

      printLog << "execution mode = broadband reuse\n"
               << "reuse mode = frequency\n"
               << "Trace passes = " << statistics.tracePassCount << '\n'
               << "ray count = " << statistics.rayCount << '\n'
               << "ray point count = " << statistics.totalRayPointCount << '\n'
               << "ray cache bytes = " << statistics.rayCacheBytes << '\n'
               << "requested reuse worker count = "
               << statistics.requestedWorkerCount << '\n'
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
               << "Solver wall seconds = " << statistics.wallSeconds << '\n'
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
      if (options.traceWorkerCountSpecified) {
        writeTraceWorkerCounts(printLog, statistics.requestedTraceWorkerCount,
                               statistics.effectiveTraceWorkerCount);
        writeTraceWorkerSeconds(printLog, statistics.traceWorkerSecondsBySource,
                                0U, false);
      }
    }

    printLog << "Total solver and product seconds = "
             << std::chrono::duration<double>(Clock::now() - solveBegin).count()
             << '\n'
             << "Bellhop Broadband completed successfully\n";
    printLog.close();
    if (!printLog) {
      throw broadband::BellhopError("failed to finalize print output: " +
                                    printPath.string());
    }
    return 0;
  } catch (const std::exception& error) {
    if (productsPrepared) {
      removeProductArtifactsNoThrow(fileRoot);
    }
    printLog << "\nFATAL ERROR: " << error.what() << '\n';
    printLog.close();
    std::cerr << "bellhop_broadband: " << error.what() << '\n';
    return 1;
  }
}
