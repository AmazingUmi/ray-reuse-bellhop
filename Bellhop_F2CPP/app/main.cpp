#include <chrono>
#include <complex>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>

#include "bellhop/error.hpp"
#include "bellhop/io/arrival_writer.hpp"
#include "bellhop/io/environment_parser.hpp"
#include "bellhop/io/eigenray_writer.hpp"
#include "bellhop/io/output_layout.hpp"
#include "bellhop/io/ray_writer.hpp"
#include "bellhop/io/shd_writer.hpp"
#include "bellhop/model/sound_speed_evaluator.hpp"
#include "bellhop/solver/arrival_solver.hpp"
#include "bellhop/solver/eigenray_solver.hpp"
#include "bellhop/solver/ray_trace_solver.hpp"
#include "bellhop/solver/single_frequency_solver.hpp"

namespace {

using Clock = std::chrono::steady_clock;

void removeStaleProduct(const std::filesystem::path& path) {
  std::error_code error;
  static_cast<void>(std::filesystem::remove(path, error));
  if (error) {
    throw bellhop::BellhopError(
        "unable to remove stale output " + path.string() + ": " +
        error.message());
  }
}

void printUsage(std::ostream& stream) {
  stream << "Usage: bellhop_f2cpp <file-root>\n"
         << "\n"
         << "Reads <file-root>.env and writes <file-root>.prt plus "
            "<file-root>.shd for coherent-TL runs or <file-root>.ray "
            "for ray-trace/eigenray runs or <file-root>.arr for arrivals.\n";
}

std::string_view attenuationUnitLabel(bellhop::AttenuationUnit unit) {
  switch (unit) {
    case bellhop::AttenuationUnit::NepersPerMeter:
      return "nepers/m";
    case bellhop::AttenuationUnit::DecibelsPerMeter:
      return "dB/m";
    case bellhop::AttenuationUnit::DecibelsPerMeterPowerLaw:
      return "dB/m with power law";
    case bellhop::AttenuationUnit::DecibelsPerMeterKilohertz:
      return "dB/mkHz";
    case bellhop::AttenuationUnit::DecibelsPerWavelength:
      return "dB/wavelength";
    case bellhop::AttenuationUnit::QualityFactor:
      return "Q";
    case bellhop::AttenuationUnit::LossParameter:
      return "Loss parameter";
  }
  return "unknown";
}

std::string_view fieldComponentLabel(bellhop::FieldComponent component) {
  switch (component) {
    case bellhop::FieldComponent::Pressure:
      return "P";
    case bellhop::FieldComponent::Vertical:
      return "V";
    case bellhop::FieldComponent::Horizontal:
      return "H";
  }
  return "unknown";
}

std::string_view beamWidthLabel(bellhop::BeamWidthMode mode) {
  switch (mode) {
    case bellhop::BeamWidthMode::SpaceFilling:
      return "Space filling beams";
    case bellhop::BeamWidthMode::MinimumWidth:
      return "Minimum width beams";
    case bellhop::BeamWidthMode::Wkb:
      return "WKB beams";
  }
  return "unknown beam width";
}

std::string_view curvatureModeLabel(
    bellhop::BoundaryCurvatureMode mode) {
  switch (mode) {
    case bellhop::BoundaryCurvatureMode::Double:
      return "Curvature doubling invoked";
    case bellhop::BoundaryCurvatureMode::Standard:
      return "Standard curvature condition";
    case bellhop::BoundaryCurvatureMode::Zero:
      return "Curvature zeroing invoked";
  }
  return "unknown curvature condition";
}

std::string_view beamFamilyLabel(
    bellhop::BeamFamily family,
    bellhop::CervenyCoordinateSystem coordinates) {
  switch (family) {
    case bellhop::BeamFamily::CervenyGaussian:
      return coordinates == bellhop::CervenyCoordinateSystem::RayCentered
                 ? "Ray centered beams"
                 : "Cartesian beams";
    case bellhop::BeamFamily::GeometricHat:
      return coordinates == bellhop::CervenyCoordinateSystem::RayCentered
                 ? "Geometric hat beams in ray-centered coordinates"
                 : "Geometric hat beams in Cartesian coordinates";
    case bellhop::BeamFamily::GeometricGaussian:
      return "Geometric gaussian beams in Cartesian coordinates";
    case bellhop::BeamFamily::SimpleGaussian:
      return "Simple gaussian beams";
    default:
      throw bellhop::ValidationError("beam family is invalid");
  }
}

void writeConfigurationSummary(
    std::ostream& stream,
    const bellhop::ParsedEnvironment& parsed) {
  const bellhop::SimulationCase& simulation =
      parsed.simulationCase;
  const bellhop::Environment& environment =
      simulation.environment();
  stream << "BELLHOP F2CPP\n\n"
         << parsed.title << '\n'
         << "frequency = "
         << simulation.frequencies().values().front()
         << " Hz\n";
  if (simulation.runMode() == bellhop::SimulationRunMode::RayTrace) {
    stream << "Ray trace run\n"
           << "Geometric hat beams in Cartesian coordinates\n";
  } else if (bellhop::isArrivalMode(simulation.runMode())) {
    stream << (simulation.runMode() ==
                       bellhop::SimulationRunMode::AsciiArrivals
                   ? "Arrivals calculation, ASCII  file output\n"
                   : "Arrivals calculation, binary file output\n")
           << beamFamilyLabel(
                  simulation.beamFamily(),
                  simulation.cervenyCoordinateSystem())
           << '\n';
  } else if (bellhop::isEigenrayMode(simulation.runMode())) {
    stream << "Eigenray trace run\n"
           << beamFamilyLabel(
                  simulation.beamFamily(),
                  simulation.cervenyCoordinateSystem())
           << '\n';
  } else {
    switch (simulation.runMode()) {
      case bellhop::SimulationRunMode::CoherentTransmissionLoss:
        stream << "Coherent TL calculation\n";
        break;
      case bellhop::SimulationRunMode::IncoherentTransmissionLoss:
        stream << "Incoherent TL calculation\n";
        break;
      case bellhop::SimulationRunMode::SemiCoherentTransmissionLoss:
        stream << "Semi-coherent TL calculation\n";
        break;
      case bellhop::SimulationRunMode::AsciiArrivals:
      case bellhop::SimulationRunMode::BinaryArrivals:
      case bellhop::SimulationRunMode::Eigenray:
      case bellhop::SimulationRunMode::RayTrace:
        break;
      default:
        throw bellhop::ValidationError("simulation run mode is invalid");
    }
    stream << beamFamilyLabel(
                  simulation.beamFamily(),
                  simulation.cervenyCoordinateSystem())
           << '\n';
  }
  if (simulation.sourceGeometry() == bellhop::SourceGeometry::Line) {
    stream << "Line source (Cartesian coordinates)\n";
  } else {
    stream << "Point source (cylindrical coordinates)\n";
  }
  if (bellhop::isTransmissionLossMode(simulation.runMode())) {
    const bellhop::Source& firstSource = simulation.sources().front();
    const bellhop::SoundSpeedSample sourceSample =
        bellhop::GeometrySspEvaluator(
            environment.soundSpeedProfile())
            .evaluate(
                bellhop::Vec2{.range = 0.0,
                              .depth = firstSource.depth},
                0U);
    const bellhop::BeamEpsilon firstEpsilon = bellhop::pickBeamEpsilon(
        parsed.beam.widthMode,
        simulation.frequencies().values().front(),
        sourceSample.soundSpeed,
        sourceSample.soundSpeedGradient.depth,
        simulation.launchFanPlan().launchAngles.front(),
        simulation.launchFanPlan().launchAngleStep,
        parsed.beam.loopRange,
        parsed.beam.epsilonMultiplier);
    const std::complex<double> epsilonOptimal =
        firstEpsilon.value / parsed.beam.epsilonMultiplier;
    if (simulation.beamFamily() ==
        bellhop::BeamFamily::CervenyGaussian) {
      stream << "Component = "
             << fieldComponentLabel(simulation.fieldComponent()) << '\n'
             << beamWidthLabel(parsed.beam.widthMode) << '\n'
             << curvatureModeLabel(parsed.beam.curvatureMode) << '\n';
    } else {
      switch (simulation.beamFamily()) {
        case bellhop::BeamFamily::GeometricHat:
          stream << "Geometric hat beams\n";
          break;
        case bellhop::BeamFamily::GeometricGaussian:
          stream << "Geometric Gaussian beams\n";
          break;
        case bellhop::BeamFamily::SimpleGaussian:
          stream << "Simple Gaussian beams\n";
          break;
        case bellhop::BeamFamily::CervenyGaussian:
          break;
        default:
          throw bellhop::ValidationError("beam family is invalid");
      }
    }
    stream << "HalfWidth = " << firstEpsilon.halfWidthMeters << '\n'
           << "epsilonOpt = (" << epsilonOptimal.real() << ','
           << epsilonOptimal.imag() << ")\n"
           << "EpsMult = " << parsed.beam.epsilonMultiplier << '\n';
  }
  if (simulation.receivers().isIrregular()) {
    stream << "Irregular grid: paired receiver ranges and depths\n";
  } else {
    stream << "Rectilinear receiver grid\n";
  }
  stream
         << "VACUUM sea surface\n"
         << "Attenuation units: "
         << attenuationUnitLabel(
                environment.soundSpeedProfile()
                    .points()
                    .front()
                    .attenuation.unit)
         << '\n';
  if (environment.soundSpeedProfile().interpolationKind() ==
      bellhop::SspInterpolationKind::Quadrilateral) {
    stream << "Using range-dependent sound speed\n"
           << "Number of SSP ranges = "
           << environment.soundSpeedProfile()
                  .quadrilateralGrid()
                  ->rangeCount
           << '\n';
  }
  if (environment.seaSurface().geometry().interpolationKind() ==
          bellhop::BoundaryInterpolationKind::Curvilinear ||
      environment.seabed().geometry().interpolationKind() ==
          bellhop::BoundaryInterpolationKind::Curvilinear) {
    stream << "Curvilinear Interpolation\n";
  }
  if ((!environment.seaSurface().geometry().isFlat() &&
       environment.seaSurface().geometry().interpolationKind() ==
           bellhop::BoundaryInterpolationKind::PiecewiseLinear) ||
      (!environment.seabed().geometry().isFlat() &&
       environment.seabed().geometry().interpolationKind() ==
           bellhop::BoundaryInterpolationKind::PiecewiseLinear)) {
    stream << "Piecewise linear interpolation\n";
  }
  if (environment.seabed().hasRangeDependentMaterials()) {
    stream << "Long format (bathymetry and geoacoustics)\n";
  }
  if (environment.seabed().kind() ==
      bellhop::BoundaryKind::Rigid) {
    stream << "Perfectly RIGID seabed\n";
  } else if (environment.seabed().kind() ==
             bellhop::BoundaryKind::GrainSizeHalfSpace) {
    const auto& grain = *environment.seabed().grainSizeMaterial();
    stream << "Grain size to define half-space\n"
           << "Grain size = " << grain.meanGrainSize << '\n'
           << "Grain sound-speed ratio = " << grain.soundSpeedRatio << '\n'
           << "Grain density ratio = " << grain.densityRatio << '\n'
           << "Grain attenuation coefficient = "
           << grain.attenuationCoefficient << '\n';
  } else if (environment.seabed().kind() ==
             bellhop::BoundaryKind::TabulatedReflection) {
    const auto& table = *environment.seabed().reflectionTable();
    stream << "FILE used for reflection loss\n"
           << "Using tabulated bottom reflection coef.\n"
           << "Number of points in bottom reflection coefficient = "
           << table.size() << '\n'
           << "Reflection coefficient angle domain = "
           << table.front().angleDegrees << " to "
           << table.back().angleDegrees << " degrees\n";
  } else {
    stream << "ACOUSTO-ELASTIC half-space seabed\n";
  }
  const bellhop::VolumeAttenuation& volumeAttenuation =
      environment.volumeAttenuation();
  switch (volumeAttenuation.model) {
    case bellhop::VolumeAttenuationModel::None:
      break;
    case bellhop::VolumeAttenuationModel::Thorp:
      stream << "THORP volume attenuation added\n";
      break;
    case bellhop::VolumeAttenuationModel::FrancoisGarrison:
      stream << "Francois-Garrison volume attenuation added\n";
      break;
    case bellhop::VolumeAttenuationModel::Biological: {
      stream << "Biological attenaution\n";
      const auto& layers = *std::get<
          bellhop::SharedBiologicalAttenuationLayers>(
          volumeAttenuation.parameters);
      stream << "Number of Bio Layers = " << layers.size() << '\n';
      break;
    }
  }
  stream << "launch angles = "
         << simulation.launchFanPlan().launchAngleCount
         << '\n'
         << "source depths = "
         << simulation.sourceCount() << '\n'
         << "receiver depths = "
         << simulation.receivers().depthCount() << '\n'
         << "receiver ranges = "
         << simulation.receivers().rangeCount() << '\n'
         << "step length = "
         << simulation.integrator().stepLength << " m\n"
         << "range limit = "
         << simulation.integrator().rangeLimit << " m\n"
         << "depth limit = "
         << simulation.integrator().depthLimit << " m\n";
  if (bellhop::isTransmissionLossMode(simulation.runMode())) {
    const bellhop::Shd2DLayout layout = bellhop::planShd2DLayout(
        simulation.sourceCount(), simulation.receivers().depthCount(),
        simulation.receivers().rangeCount(),
        simulation.receivers().isIrregular());
    stream << "pressure values per source = "
           << simulation.receivers().receiversPerRange() *
                  simulation.receivers().rangeCount()
           << '\n'
           << "SHD record words = " << layout.recordWords << '\n'
           << "SHD record bytes = " << layout.recordBytes << '\n'
           << "SHD pressure records = " << layout.pressureRecordCount << '\n'
           << "SHD total records = " << layout.totalRecordCount << '\n'
           << "SHD expected bytes = " << layout.fileBytes << '\n';
  } else if (simulation.runMode() == bellhop::SimulationRunMode::RayTrace) {
    stream << "planned ray blocks = "
           << simulation.sourceCount() *
                  simulation.launchFanPlan().launchAngleCount
           << '\n';
  } else {
    stream << "configured launch rays = "
           << simulation.sourceCount() *
                  simulation.launchFanPlan().launchAngleCount
           << '\n';
  }
  if (simulation.sourceBeamPattern().isDirectional()) {
    stream << "______________________________\n"
           << "Using source beam pattern file\n"
           << "Number of source beam pattern points "
           << simulation.sourceBeamPattern().size() << '\n'
           << "Source beam pattern angle domain = "
           << simulation.sourceBeamPattern().minimumAngleDegrees()
           << " to "
           << simulation.sourceBeamPattern().maximumAngleDegrees()
           << " degrees\n";
  }
  stream << '\n';
}

}  // namespace

int main(int argumentCount, char* arguments[]) {
  if (argumentCount == 2 && std::string_view(arguments[1]) == "--help") {
    printUsage(std::cout);
    return 0;
  }

  if (argumentCount != 2) {
    printUsage(std::cerr);
    return 2;
  }

  const std::string fileRoot(arguments[1]);
  const std::filesystem::path environmentPath(
      fileRoot + ".env");
  const std::filesystem::path printPath(
      fileRoot + ".prt");
  const std::filesystem::path shadePath(
      fileRoot + ".shd");
  const std::filesystem::path rayPath(fileRoot + ".ray");
  const std::filesystem::path arrivalPath(fileRoot + ".arr");

  std::ofstream printLog(
      printPath, std::ios::out | std::ios::trunc);
  if (!printLog.is_open()) {
    std::cerr << "bellhop_f2cpp: unable to open print output: "
              << printPath << '\n';
    return 1;
  }
  printLog << std::setprecision(17);

  try {
    removeStaleProduct(std::filesystem::path(shadePath.string() + ".tmp"));
    removeStaleProduct(std::filesystem::path(rayPath.string() + ".tmp"));
    removeStaleProduct(
        std::filesystem::path(arrivalPath.string() + ".tmp"));
    const bellhop::ParsedEnvironment parsed =
        bellhop::EnvironmentParser::parseFile(
            environmentPath);
    writeConfigurationSummary(printLog, parsed);

    if (parsed.simulationCase.runMode() ==
        bellhop::SimulationRunMode::RayTrace) {
      bellhop::RayWriter writer(
          rayPath, parsed.title, parsed.simulationCase);
      const bellhop::RayTraceStatistics statistics =
          bellhop::RayTraceSolver::trace(
              parsed.simulationCase,
              [&writer](std::size_t sourceIndex,
                        const bellhop::RayPathCache& cache) {
                writer.appendSource(sourceIndex, cache);
              });
      writer.finalize();
      removeStaleProduct(shadePath);
      removeStaleProduct(arrivalPath);
      printLog << "ray count = " << statistics.rayCount << '\n'
               << "ray point count = "
               << statistics.totalRayPointCount << '\n'
               << "ray cache bytes = "
               << statistics.peakRayCacheBytes << '\n'
               << "Trace seconds = " << statistics.traceSeconds << '\n'
               << "RAY seconds = " << statistics.writeSeconds << '\n'
               << "Bellhop F2CPP ray trace completed successfully\n";
      printLog.close();
      if (!printLog) {
        throw bellhop::BellhopError(
            "failed to finalize print output: " + printPath.string());
      }
      return 0;
    }

    if (bellhop::isArrivalMode(parsed.simulationCase.runMode())) {
      bellhop::ArrivalWriter writer(arrivalPath, parsed.simulationCase);
      std::size_t appendCount = 0U;
      std::size_t mergeCount = 0U;
      std::size_t cuspGuardCount = 0U;
      std::size_t replacementCount = 0U;
      std::size_t discardCount = 0U;
      const bellhop::ArrivalSolverStatistics statistics =
          bellhop::ArrivalSolver::solve(
              parsed.simulationCase,
              [&writer, &appendCount, &mergeCount, &cuspGuardCount,
               &replacementCount, &discardCount](
                  std::size_t sourceIndex, const bellhop::RayPathCache&,
                  const bellhop::ArrivalWorkspace& workspace) {
                writer.appendSource(sourceIndex, workspace);
                appendCount += workspace.appendCount();
                mergeCount += workspace.mergeCount();
                cuspGuardCount += workspace.cuspGuardCount();
                replacementCount += workspace.weakestReplacementCount();
                discardCount += workspace.capacityDiscardCount();
              });
      writer.finalize();
      removeStaleProduct(shadePath);
      removeStaleProduct(rayPath);
      printLog
          << "ray count = " << statistics.rayCount << '\n'
          << "ray point count = " << statistics.totalRayPointCount << '\n'
          << "ray cache bytes = " << statistics.peakRayCacheBytes << '\n'
          << "arrival workspace bytes = "
          << statistics.peakArrivalWorkspaceBytes << '\n'
          << "arrival capacity per cell = "
          << writer.layout().perCellCapacity << '\n'
          << "arrival logical capacity = "
          << writer.layout().actualCellsPerSource *
                 writer.layout().perCellCapacity
          << '\n'
          << "arrival candidates = " << statistics.candidateCount << '\n'
          << "arrival appends = " << appendCount << '\n'
          << "arrival merges = " << mergeCount << '\n'
          << "arrival axial-cusp guards = " << cuspGuardCount << '\n'
          << "arrival replacements = " << replacementCount << '\n'
          << "arrival capacity discards = " << discardCount << '\n'
          << "arrival saturated cells = " << statistics.saturatedCellCount
          << '\n'
          << "Trace seconds = " << statistics.traceSeconds << '\n'
          << "Project seconds = " << statistics.projectSeconds << '\n'
          << "Influence seconds = " << statistics.influenceSeconds << '\n'
          << "ARR seconds = " << statistics.consumeSeconds << '\n'
          << "Bellhop F2CPP arrivals completed successfully\n";
      printLog.close();
      if (!printLog) {
        throw bellhop::BellhopError(
            "failed to finalize print output: " + printPath.string());
      }
      return 0;
    }

    if (bellhop::isEigenrayMode(parsed.simulationCase.runMode())) {
      bellhop::EigenrayWriter writer(
          rayPath, parsed.title, parsed.simulationCase);
      const bellhop::EigenraySolverStatistics statistics =
          bellhop::EigenraySolver::solve(
              parsed.simulationCase,
              [&writer](std::size_t sourceIndex, std::size_t launchIndex,
                        const bellhop::RayPathCache& cache,
                        const bellhop::RayPath& path,
                        const bellhop::EigenrayHit& hit) {
                writer.appendHit(sourceIndex, launchIndex, cache, path, hit);
              });
      writer.finalize();
      removeStaleProduct(shadePath);
      removeStaleProduct(arrivalPath);
      printLog
          << "configured launch rays = " << statistics.rayCount << '\n'
          << "eigenray hit blocks = " << statistics.totalHitCount << '\n'
          << "eigenray prefix points = "
          << statistics.totalPrefixPointCount << '\n'
          << "ray point count = " << statistics.totalRayPointCount << '\n'
          << "ray cache bytes = " << statistics.peakRayCacheBytes << '\n'
          << "Trace seconds = " << statistics.traceSeconds << '\n'
          << "Project seconds = " << statistics.projectSeconds << '\n'
          << "Influence seconds = " << statistics.influenceSeconds << '\n'
          << "RAY seconds = " << statistics.consumeSeconds << '\n'
          << "Bellhop F2CPP eigenray completed successfully\n";
      printLog.close();
      if (!printLog) {
        throw bellhop::BellhopError(
            "failed to finalize print output: " + printPath.string());
      }
      return 0;
    }

    const bellhop::SingleFrequencyResult result =
        bellhop::SingleFrequencySolver::solve(
            parsed.simulationCase,
            parsed.beam.epsilonMultiplier,
            parsed.beam.loopRange,
            parsed.beam.influence,
            parsed.beam.widthMode,
            parsed.beam.curvatureMode);

    const Clock::time_point writeBegin = Clock::now();
    bellhop::ShdWriter::writeSingleFrequency(
        shadePath, parsed.title,
        parsed.simulationCase, result.workspace,
        result.additionalSourceWorkspaces);
    removeStaleProduct(rayPath);
    removeStaleProduct(arrivalPath);
    const Clock::time_point writeEnd = Clock::now();
    const double writeSeconds =
        std::chrono::duration<double>(
            writeEnd - writeBegin)
            .count();

    printLog
        << "ray count = " << result.rayCount << '\n'
        << "ray point count = "
        << result.totalRayPointCount << '\n'
        << "ray cache bytes = "
        << result.rayCacheBytes << '\n'
        << "Trace seconds = "
        << result.timings.traceSeconds << '\n'
        << "Project seconds = "
        << result.timings.projectSeconds << '\n'
        << "Influence seconds = "
        << result.timings.influenceSeconds << '\n'
        << "Scale seconds = "
        << result.timings.scaleSeconds << '\n'
        << "SHD seconds = " << writeSeconds << '\n'
        << "Bellhop F2CPP completed successfully\n";
    printLog.close();
    if (!printLog) {
      throw bellhop::BellhopError(
          "failed to finalize print output: " +
          printPath.string());
    }
    return 0;
  } catch (const std::exception& error) {
    printLog << "\nFATAL ERROR: " << error.what() << '\n';
    printLog.close();
    std::cerr << "bellhop_f2cpp: " << error.what() << '\n';
    return 1;
  }
}
