#include "rayreuse/io/environment_parser.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

#include "rayreuse/cache/ray_path_cache.hpp"
#include "rayreuse/error.hpp"
#include "rayreuse/field/frequency_projector.hpp"
#include "rayreuse/ray/geometry_tracer.hpp"
#include "support/test_harness.hpp"

namespace {

using rayreuse::AttenuationUnit;
using rayreuse::BeamFamily;
using rayreuse::BeamWidthMode;
using rayreuse::BellhopError;
using rayreuse::BoundaryCurvatureMode;
using rayreuse::BoundaryKind;
using rayreuse::CervenyCoordinateSystem;
using rayreuse::EnvironmentParser;
using rayreuse::FieldComponent;
using rayreuse::FrequencyProjector;
using rayreuse::GeometryTracer;
using rayreuse::ParsedEnvironment;
using rayreuse::RayPath;
using rayreuse::RayPathCache;
using rayreuse::SimulationRunMode;
using rayreuse::ValidationError;
using rayreuse::VolumeAttenuationModel;
using rayreuse::test::Context;

const std::filesystem::path kCasesRoot =
    std::filesystem::path(RAYREUSE_WORKSPACE_ROOT) /
    "test/standard_cases/cases";

void replaceAll(std::string& contents, const std::string& pattern,
                const std::string& replacement) {
  std::size_t position = 0U;
  bool replaced = false;
  while ((position = contents.find(pattern, position)) != std::string::npos) {
    contents.replace(position, pattern.size(), replacement);
    position += replacement.size();
    replaced = true;
  }
  if (!replaced) {
    throw std::runtime_error("fixture token not found: " + pattern);
  }
}

void replaceFirst(std::string& contents, const std::string& pattern,
                  const std::string& replacement) {
  const std::size_t position = contents.find(pattern);
  if (position == std::string::npos) {
    throw std::runtime_error("fixture token not found: " + pattern);
  }
  contents.replace(position, pattern.size(), replacement);
}

std::string renderCase(const std::string& caseName, double frequency,
                       std::size_t launchAngleCount) {
  const std::filesystem::path path = kCasesRoot / caseName / "origin.env.in";
  std::ifstream input(path);
  if (!input.is_open()) {
    throw std::runtime_error("unable to open test fixture: " + path.string());
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  std::string contents = buffer.str();
  std::ostringstream frequencyText;
  frequencyText.precision(12);
  frequencyText << frequency;
  replaceAll(contents, "@FREQUENCY_HZ@", frequencyText.str());
  replaceAll(contents, "@NALPHA@", std::to_string(launchAngleCount));
  return contents;
}

class TemporaryStandardCase {
 public:
  TemporaryStandardCase(const std::string& caseName, double frequency,
                        std::size_t launchAngleCount)
      : directory_(std::filesystem::temp_directory_path() /
                   ("rayreuse_rr_b1_" + caseName)),
        environmentPath_(directory_ / "case.env") {
    std::error_code cleanupError;
    std::filesystem::remove_all(directory_, cleanupError);
    std::filesystem::create_directories(directory_);
    std::ofstream environment(environmentPath_);
    if (!environment.is_open()) {
      throw std::runtime_error("unable to stage RR-B1 environment fixture");
    }
    environment << renderCase(caseName, frequency, launchAngleCount);
    environment.close();

    const std::filesystem::path source = kCasesRoot / caseName;
    for (const std::string extension :
         {".ati", ".bty", ".trc", ".brc", ".sbp"}) {
      const std::filesystem::path companion = source / ("origin" + extension);
      if (std::filesystem::exists(companion)) {
        std::filesystem::copy_file(companion,
                                   directory_ / ("case" + extension));
      }
    }
  }

  TemporaryStandardCase(const TemporaryStandardCase&) = delete;
  TemporaryStandardCase& operator=(const TemporaryStandardCase&) = delete;

  ~TemporaryStandardCase() {
    std::error_code cleanupError;
    std::filesystem::remove_all(directory_, cleanupError);
  }

  [[nodiscard]] const std::filesystem::path& environmentPath() const noexcept {
    return environmentPath_;
  }

 private:
  std::filesystem::path directory_;
  std::filesystem::path environmentPath_;
};

ParsedEnvironment parseText(const std::string& contents,
                            const std::string& name) {
  std::istringstream input(contents);
  return EnvironmentParser::parse(input, name);
}

void testDirectCase(Context& context) {
  const ParsedEnvironment parsed =
      parseText(renderCase("constant_speed_direct", 50.0, 300U),
                "constant_speed_direct.env");
  const auto& simulation = parsed.simulationCase;
  const auto& environment = simulation.environment();

  context.check(
      parsed.title == "Constant speed direct field, Cartesian Cerveny beams",
      "direct title excludes quotes and comments");
  context.checkNear(simulation.frequencies().values().front(), 50.0, 0.0,
                    "direct frequency");
  context.check(environment.soundSpeedProfile().points().size() == 2U,
                "direct SSP point count is terminated by bottom depth");
  context.checkNear(environment.soundSpeedProfile().points().front().density,
                    1000.0, 0.0, "default water density converts to SI");
  context.check(environment.seaSurface().kind() == BoundaryKind::Vacuum,
                "direct vacuum surface");
  context.check(environment.seabed().kind() == BoundaryKind::AcousticHalfSpace,
                "direct acoustic seabed");
  context.checkNear(environment.seabed().material()->density, 1800.0, 0.0,
                    "direct seabed density converts to SI");
  context.checkNear(environment.seabed().material()->compressionalSoundSpeed,
                    1600.0, 0.0, "direct seabed sound speed");
  context.checkNear(simulation.source().depth, 500.0, 0.0,
                    "direct source depth");
  context.check(simulation.receivers().depthCount() == 21U &&
                    simulation.receivers().rangeCount() == 51U,
                "direct receiver dimensions");
  const double expectedSecondRange = (0.1 + (5.0 - 0.1) / 50.0) * 1000.0;
  context.checkNear(simulation.receivers().ranges()[1U], expectedSecondRange,
                    0.0,
                    "receiver subtabulation occurs in km before SI conversion");
  context.check(simulation.launchFanPlan().launchAngleCount == 300U,
                "direct D-02 launch count");
  context.checkNear(simulation.launchFanPlan().launchAngles.front(),
                    -5.0 * std::numbers::pi / 180.0, 0.0,
                    "minimum launch angle converts to radians");
  context.checkNear(simulation.integrator().stepLength, 10.0, 0.0,
                    "direct explicit step length");
  context.checkNear(simulation.integrator().rangeLimit, 5100.0, 0.0,
                    "range box converts from km");
  context.check(simulation.integrator().maximumRayPoints == 2'000'000U,
                "parser freezes the legacy ray-point guard");
  context.checkNear(parsed.beam.epsilonMultiplier, 1.0, 0.0,
                    "direct epsilon multiplier");
  context.checkNear(parsed.beam.loopRange, 2500.0, 0.0,
                    "direct beam loop range converts from km");
  context.check(parsed.beam.influence.imageCount == 3U &&
                    parsed.beam.influence.beamWindow == 5,
                "direct image/window settings");
}

void testFrequencyOverride(Context& context) {
  std::istringstream input(renderCase("constant_speed_direct", 50.0, 300U));
  const ParsedEnvironment parsed = EnvironmentParser::parse(
      input, "broadband_direct.env", std::vector<double>{50.0, 250.0});

  context.check(
      parsed.simulationCase.frequencies().values() ==
              std::vector<double>({50.0, 250.0}) &&
          parsed.simulationCase.frequencies().designFrequency() == 250.0 &&
          parsed.simulationCase.launchFanPlan().designFrequency == 250.0,
      "frequency override replaces the env scalar and plans at fmax");
}

void testEnvironmentFrequencyList(Context& context) {
  std::string contents = renderCase("munk_cerveny_cc", 50.0, 5000U);
  replaceFirst(contents, "50          ! FREQ (Hz)",
               "50 100 150 200 250 / ! FREQS (Hz), RayReuse extension");
  const ParsedEnvironment parsed =
      parseText(contents, "munk_multifrequency.env");

  context.check(
      parsed.simulationCase.frequencies().values() ==
              std::vector<double>({50.0, 100.0, 150.0, 200.0, 250.0}) &&
          parsed.simulationCase.frequencies().designFrequency() == 250.0 &&
          parsed.simulationCase.launchFanPlan().designFrequency == 250.0,
      "an ENV frequency list directly configures broadband fmax planning");

  context.expectThrows<ValidationError>(
      [&] {
        std::string descending = contents;
        replaceFirst(descending, "50 100 150 200 250", "50 100 90 200 250");
        static_cast<void>(parseText(descending, "descending_frequencies.env"));
      },
      "a descending ENV frequency list is rejected");
  context.expectThrows<ValidationError>(
      [&] {
        std::string duplicate = contents;
        replaceFirst(duplicate, "50 100 150 200 250", "50 100 100 200 250");
        static_cast<void>(parseText(duplicate, "duplicate_frequencies.env"));
      },
      "a duplicate ENV frequency is rejected");
}

void testCartesianCervenyComponents(Context& context) {
  const std::string pressureFixture =
      renderCase("cartesian_component_pressure", 1000.0, 300U);
  for (const auto& [runType, expectedMode] :
       std::vector<std::pair<std::string, SimulationRunMode>>{
           {"CC", SimulationRunMode::Coherent},
           {"IC", SimulationRunMode::Incoherent},
           {"SC", SimulationRunMode::SemiCoherent}}) {
    for (const auto& [widthToken, expectedWidth] :
         std::vector<std::pair<std::string, BeamWidthMode>>{
             {"F", BeamWidthMode::SpaceFilling},
             {"M", BeamWidthMode::MinimumWidth},
             {"W", BeamWidthMode::Wkb}}) {
      for (const auto& [curvatureToken, expectedCurvature] :
           std::vector<std::pair<std::string, BoundaryCurvatureMode>>{
               {"D", BoundaryCurvatureMode::Double},
               {"S", BoundaryCurvatureMode::Standard},
               {"Z", BoundaryCurvatureMode::Zero}}) {
        for (const auto& [componentToken, expectedComponent] :
             std::vector<std::pair<std::string, FieldComponent>>{
                 {"P", FieldComponent::Pressure},
                 {"V", FieldComponent::Vertical},
                 {"H", FieldComponent::Horizontal}}) {
          std::string contents = pressureFixture;
          replaceFirst(contents, "'CC'", "'" + runType + "'");
          replaceFirst(contents, "'MS'",
                       "'" + widthToken + curvatureToken + "'");
          replaceFirst(contents, "1  5  'P'", "1  5  '" + componentToken + "'");
          const ParsedEnvironment parsed =
              parseText(contents, "cartesian_" + runType + curvatureToken +
                                      componentToken + ".env");
          context.check(
              parsed.simulationCase.runMode() == expectedMode &&
                  parsed.simulationCase.beamFamily() ==
                      BeamFamily::CervenyGaussian &&
                  parsed.simulationCase.fieldComponent() == expectedComponent &&
                  parsed.simulationCase.curvatureMode() == expectedCurvature &&
                  parsed.simulationCase.beamWidthMode() == expectedWidth &&
                  parsed.simulationCase.cervenyCoordinateSystem() ==
                      CervenyCoordinateSystem::Cartesian,
              "Cartesian Cerveny parser preserves C/I/S x F/M/W x D/S/Z x "
              "P/V/H");
        }
      }
    }
  }
  for (const auto& [runType, expectedMode] :
       std::vector<std::pair<std::string, SimulationRunMode>>{
           {"CR", SimulationRunMode::Coherent},
           {"IR", SimulationRunMode::Incoherent},
           {"SR", SimulationRunMode::SemiCoherent}}) {
    for (const auto& [widthToken, expectedWidth] :
         std::vector<std::pair<std::string, BeamWidthMode>>{
             {"F", BeamWidthMode::SpaceFilling},
             {"M", BeamWidthMode::MinimumWidth},
             {"W", BeamWidthMode::Wkb}}) {
      for (const auto& [curvatureToken, expectedCurvature] :
           std::vector<std::pair<std::string, BoundaryCurvatureMode>>{
               {"D", BoundaryCurvatureMode::Double},
               {"S", BoundaryCurvatureMode::Standard},
               {"Z", BoundaryCurvatureMode::Zero}}) {
        for (const auto& [componentToken, expectedComponent] :
             std::vector<std::pair<std::string, FieldComponent>>{
                 {"P", FieldComponent::Pressure},
                 {"V", FieldComponent::Vertical},
                 {"H", FieldComponent::Horizontal}}) {
          std::string contents = pressureFixture;
          replaceFirst(contents, "'CC'", "'" + runType + "'");
          replaceFirst(contents, "'MS'",
                       "'" + widthToken + curvatureToken + "'");
          replaceFirst(contents, "1  5  'P'", "1  5  '" + componentToken + "'");
          const ParsedEnvironment parsed =
              parseText(contents, "ray_centered_" + runType + widthToken +
                                      curvatureToken + componentToken + ".env");
          context.check(
              parsed.simulationCase.runMode() == expectedMode &&
                  parsed.simulationCase.beamFamily() ==
                      BeamFamily::CervenyGaussian &&
                  parsed.simulationCase.fieldComponent() == expectedComponent &&
                  parsed.simulationCase.curvatureMode() == expectedCurvature &&
                  parsed.simulationCase.beamWidthMode() == expectedWidth &&
                  parsed.simulationCase.cervenyCoordinateSystem() ==
                      CervenyCoordinateSystem::RayCentered,
              "ray-centered Cerveny parser preserves C/I/S x F/M/W x "
              "D/S/Z x P/V/H");
        }
      }
    }
  }
  context.expectThrows<ValidationError>(
      [&] {
        std::string contents = pressureFixture;
        replaceFirst(contents, "1  5  'P'", "1  5  'X'");
        static_cast<void>(parseText(contents, "cartesian_component_x.env"));
      },
      "Cartesian Cerveny rejects an unknown field component");
  for (const std::string beamType : {"XS", "MX", "M", "MSS"}) {
    context.expectThrows<ValidationError>(
        [&, beamType] {
          std::string contents = pressureFixture;
          replaceFirst(contents, "'MS'", "'" + beamType + "'");
          static_cast<void>(parseText(contents, "beam_" + beamType + ".env"));
        },
        "unknown width and invalid curvature tokens remain rejected");
  }
  for (const std::string& runType : {"CG", "CB", "CS"}) {
    context.expectThrows<ValidationError>(
        [&] {
          std::string contents = pressureFixture;
          replaceFirst(contents, "'CC'", "'" + runType + "'");
          replaceFirst(contents, "1  5  'P'", "1  5  'V'");
          static_cast<void>(
              parseText(contents, "non_cerveny_" + runType + "V.env"));
        },
        "non-Cerveny TL families reject a component option tail");
  }
}

void testBoundaryCases(Context& context) {
  const ParsedEnvironment rigid =
      parseText(renderCase("constant_speed_vacuum_rigid", 250.0, 1570U),
                "constant_speed_vacuum_rigid.env");
  context.check(
      rigid.simulationCase.environment().seabed().kind() == BoundaryKind::Rigid,
      "rigid standard case parses rigid seabed");
  context.checkNear(rigid.simulationCase.integrator().stepLength, 10.0, 0.0,
                    "zero step selects one tenth of shallow-water depth");
  context.check(rigid.simulationCase.launchFanPlan().launchAngleCount == 1570U,
                "shallow-water depth criterion determines launch count");

  const ParsedEnvironment acoustic =
      parseText(renderCase("constant_speed_acoustic_bottom", 250.0, 1570U),
                "constant_speed_acoustic_bottom.env");
  const auto& material =
      *acoustic.simulationCase.environment().seabed().material();
  context.checkNear(material.compressionalSoundSpeed, 1590.0, 0.0,
                    "lossy acoustic-bottom sound speed");
  context.checkNear(material.density, 1200.0, 0.0,
                    "lossy acoustic-bottom density converts to SI");
  context.checkNear(material.compressionalAttenuation.value, 0.5, 0.0,
                    "lossy acoustic-bottom raw attenuation");
  context.check(material.compressionalAttenuation.unit ==
                        AttenuationUnit::DecibelsPerWavelength &&
                    material.compressionalAttenuation.volumeModel ==
                        VolumeAttenuationModel::None,
                "acoustic-bottom attenuation options");
}

void testRrB1BoundarySidecarsAndFrozenEvents(Context& context) {
  const TemporaryStandardCase piecewise("i3_piecewise_boundaries", 100.0, 400U);
  const ParsedEnvironment parsedPiecewise =
      EnvironmentParser::parseFile(piecewise.environmentPath());
  const auto& piecewiseEnvironment =
      parsedPiecewise.simulationCase.environment();
  context.check(!piecewiseEnvironment.seaSurface().geometry().isFlat() &&
                    !piecewiseEnvironment.seabed().geometry().isFlat(),
                "standard LS .ati/.bty files create range-dependent geometry");
  context.checkNear(
      piecewiseEnvironment.seaSurface().geometry().depthAt(1000.0, 0U), 20.0,
      1.0e-12, "LS topography interpolates in SI range units");

  const TemporaryStandardCase longCase("i3_long_format_materials", 1000.0,
                                       100U);
  const ParsedEnvironment parsedLong =
      EnvironmentParser::parseFile(longCase.environmentPath());
  const auto& longSimulation = parsedLong.simulationCase;
  const auto& longSeabed = longSimulation.environment().seabed();
  context.check(longSeabed.hasRangeDependentMaterials(),
                "LL bathymetry retains immutable raw segment materials");
  context.checkNear(longSeabed.materialAtSegment(2U).compressionalSoundSpeed,
                    1800.0, 0.0,
                    "LL physical segment selects its left-node material");

  RayPath longPath =
      GeometryTracer(longSimulation)
          .trace(longSimulation.source(), 45.0 * std::numbers::pi / 180.0);
  const auto frozenEvent = std::find_if(
      longPath.events.begin(), longPath.events.end(),
      [](const auto& event) { return event.longMaterialOverride.has_value(); });
  context.check(frozenEvent != longPath.events.end(),
                "LL reflection freezes raw material in ReflectionEvent");
  if (frozenEvent != longPath.events.end()) {
    context.check(
        frozenEvent->reflectedRayPointIndex ==
                frozenEvent->rayPointIndex + 1U &&
            frozenEvent->boundarySegmentIndex > 0U &&
            frozenEvent->longMaterialOverride->attenuationEvaluationDepth ==
                rayreuse::kLegacyLongBoundaryAttenuationDepth,
        "frozen event retains explicit point pair, segment identity, and "
        "legacy LL attenuation depth");
  }
  RayPathCache cache;
  cache.append(std::move(longPath));
  cache.freeze();
  const std::uint64_t fingerprint = cache.contentFingerprint();
  const FrequencyProjector projector(longSimulation.environment());
  const auto low = projector.project(cache.at(0U), 1000.0, 1.0);
  const auto high = projector.project(cache.at(0U), 2000.0, 1.0);
  context.check(cache.contentFingerprint() == fingerprint,
                "two-frequency LL projection does not mutate frozen cache");
  context.check(low.frequency == 1000.0 && high.frequency == 2000.0,
                "LL reflection is projected independently per frequency");

  const TemporaryStandardCase elastic("elastic_ll_top_bottom", 1000.0, 100U);
  const ParsedEnvironment parsedElastic =
      EnvironmentParser::parseFile(elastic.environmentPath());
  context.check(parsedElastic.simulationCase.environment()
                            .seaSurface()
                            .materialAtSegment(1U)
                            .shearSoundSpeed > 0.0 &&
                    parsedElastic.simulationCase.environment()
                            .seabed()
                            .materialAtSegment(1U)
                            .shearSoundSpeed > 0.0,
                "elastic LL preserves top and bottom P/S raw materials");

  const TemporaryStandardCase grain("grain_size_flat", 1000.0, 200U);
  context.check(EnvironmentParser::parseFile(grain.environmentPath())
                        .simulationCase.environment()
                        .seabed()
                        .kind() == BoundaryKind::GrainSizeHalfSpace,
                "standard G case selects grain-size bottom");

  const TemporaryStandardCase bottomTable("tabulated_reflection_bottom", 1000.0,
                                          200U);
  const ParsedEnvironment parsedBottomTable =
      EnvironmentParser::parseFile(bottomTable.environmentPath());
  const auto& bottomBoundary =
      parsedBottomTable.simulationCase.environment().seabed();
  context.check(bottomBoundary.kind() == BoundaryKind::TabulatedReflection &&
                    bottomBoundary.reflectionTable() &&
                    bottomBoundary.reflectionTable()->size() == 4U,
                "standard F bottom loads sibling .brc table");

  const TemporaryStandardCase topTable("top_tabulated_bottom_vacuum", 1000.0,
                                       200U);
  const ParsedEnvironment parsedTopTable =
      EnvironmentParser::parseFile(topTable.environmentPath());
  context.check(
      parsedTopTable.simulationCase.environment().seaSurface().kind() ==
              BoundaryKind::TabulatedReflection &&
          parsedTopTable.simulationCase.environment().seabed().kind() ==
              BoundaryKind::Vacuum,
      "standard top F/bottom V case loads sibling .trc table");

  std::string topRigid =
      renderCase("top_tabulated_bottom_vacuum", 1000.0, 200U);
  replaceFirst(topRigid, "'CFW'", "'CRW'");
  context.check(parseText(topRigid, "top_rigid.env")
                        .simulationCase.environment()
                        .seaSurface()
                        .kind() == BoundaryKind::Rigid,
                "flat top R is accepted");
  std::string topGrain =
      renderCase("top_tabulated_bottom_vacuum", 1000.0, 200U);
  replaceFirst(topGrain, "'CFW'", "'CGW'\n0.0 3.0 /");
  context.check(parseText(topGrain, "top_grain.env")
                        .simulationCase.environment()
                        .seaSurface()
                        .kind() == BoundaryKind::GrainSizeHalfSpace,
                "flat top G is accepted");
}

void testAttenuationCases(Context& context) {
  const ParsedEnvironment lossless = parseText(
      renderCase("constant_speed_no_attenuation_5khz", 5000.0, 10000U),
      "constant_speed_no_attenuation_5khz.env");
  const ParsedEnvironment thorp =
      parseText(renderCase("constant_speed_thorp", 5000.0, 10000U),
                "constant_speed_thorp.env");
  context.check(
      lossless.simulationCase.environment()
              .soundSpeedProfile()
              .points()
              .front()
              .attenuation.volumeModel == VolumeAttenuationModel::None,
      "lossless case keeps volume attenuation disabled");
  context.check(
      thorp.simulationCase.environment()
              .soundSpeedProfile()
              .points()
              .front()
              .attenuation.volumeModel == VolumeAttenuationModel::Thorp,
      "Thorp case enables volume attenuation at every SSP node");
  context.check(thorp.simulationCase.environment()
                        .seabed()
                        .material()
                        ->compressionalAttenuation.volumeModel ==
                    VolumeAttenuationModel::Thorp,
                "Thorp option also reaches the acoustic half-space CRCI input");
  context.check(thorp.simulationCase.launchFanPlan().launchAngleCount == 10000U,
                "5 kHz phase criterion determines launch count");
}

void testRrB4ProductRunTypes(Context& context) {
  const TemporaryStandardCase ray("ray_trace_directional_tabulated", 1000.0,
                                  1U);
  const ParsedEnvironment parsedRay =
      EnvironmentParser::parseFile(ray.environmentPath());
  context.check(
      parsedRay.simulationCase.runMode() == SimulationRunMode::RayTrace &&
          parsedRay.simulationCase.beamFamily() ==
              BeamFamily::CervenyGaussian &&
          parsedRay.simulationCase.sourceBeamPattern().isDirectional() &&
          parsedRay.simulationCase.launchFanPlan().launchAngleCount == 1U &&
          parsedRay.simulationCase.launchFanPlan().launchAngles.size() == 1U,
      "R parser selects RayTrace, reads directional SBP, and accepts one "
      "explicit angle");

  const TemporaryStandardCase coherentPattern("source_beam_pattern_directional",
                                              1000.0, 80U);
  const ParsedEnvironment parsedCoherentPattern =
      EnvironmentParser::parseFile(coherentPattern.environmentPath());
  context.check(
      parsedCoherentPattern.simulationCase.runMode() ==
              SimulationRunMode::Coherent &&
          parsedCoherentPattern.simulationCase.beamFamily() ==
              BeamFamily::CervenyGaussian &&
          parsedCoherentPattern.simulationCase.sourceBeamPattern()
              .isDirectional(),
      "CC* parser loads directional SBP without changing the coherent mode");

  const ParsedEnvironment ascii =
      parseText(renderCase("arrival_geometric_hat_ascii", 1000.0, 80U),
                "arrival_geometric_hat_ascii.env");
  context.check(
      ascii.simulationCase.runMode() == SimulationRunMode::AsciiArrivals &&
          ascii.simulationCase.beamFamily() == BeamFamily::GeometricHat,
      "AG parser selects ASCII arrivals and Cartesian geometric hat");

  const ParsedEnvironment binary =
      parseText(renderCase("arrival_geometric_hat_binary", 1000.0, 80U),
                "arrival_geometric_hat_binary.env");
  context.check(
      binary.simulationCase.runMode() == SimulationRunMode::BinaryArrivals &&
          binary.simulationCase.beamFamily() == BeamFamily::GeometricHat,
      "aG parser selects binary arrivals and Cartesian geometric hat");

  const ParsedEnvironment gaussianEigenray =
      parseText(renderCase("eigenray_geometric_gaussian", 1000.0, 80U),
                "eigenray_geometric_gaussian.env");
  context.check(gaussianEigenray.simulationCase.runMode() ==
                        SimulationRunMode::Eigenray &&
                    gaussianEigenray.simulationCase.beamFamily() ==
                        BeamFamily::GeometricGaussian,
                "EB parser selects eigenray and Cartesian geometric Gaussian");

  for (const auto& [caseId, originalRunType, rayCenteredRunType, expectedMode] :
       std::vector<std::tuple<std::string, std::string, std::string,
                              SimulationRunMode>>{
           {"arrival_geometric_hat_ray_centered", "'Ag RR'", "'Ag RR'",
            SimulationRunMode::AsciiArrivals},
           {"arrival_geometric_hat_ray_centered", "'Ag RR'", "'ag RR'",
            SimulationRunMode::BinaryArrivals},
           {"eigenray_geometric_hat_ray_centered", "'Eg RR'", "'Eg RR'",
            SimulationRunMode::Eigenray}}) {
    std::string contents = renderCase(caseId, 1000.0, 80U);
    replaceFirst(contents, originalRunType, rayCenteredRunType);
    const ParsedEnvironment parsed =
        parseText(contents, "ray_centered_product.env");
    context.check(
        parsed.simulationCase.runMode() == expectedMode &&
            parsed.simulationCase.beamFamily() == BeamFamily::GeometricHat &&
            parsed.simulationCase.cervenyCoordinateSystem() ==
                CervenyCoordinateSystem::RayCentered,
        "Ag/ag/Eg select ray-centered geometric-hat product paths");
  }
  context.expectThrows<ValidationError>(
      [&] {
        std::string contents =
            renderCase("arrival_geometric_hat_ascii", 1000.0, 80U);
        replaceFirst(contents, "'AG RR'", "'AC RR'");
        static_cast<void>(parseText(contents, "cerveny_arrival.env"));
      },
      "Cerveny arrivals are explicitly rejected");
  context.expectThrows<ValidationError>(
      [&] {
        std::string contents =
            renderCase("arrival_geometric_hat_ascii", 1000.0, 80U);
        replaceFirst(contents, "'AG RR'", "'AG RI'");
        static_cast<void>(parseText(contents, "irregular_arrival.env"));
      },
      "irregular receiver arrivals are explicitly rejected");
}

void testMunkCase(Context& context) {
  const ParsedEnvironment parsed = parseText(
      renderCase("munk_cerveny_cc", 50.0, 1000U), "munk_cerveny_cc.env");
  const auto& simulation = parsed.simulationCase;
  const auto& points = simulation.environment().soundSpeedProfile().points();
  context.check(
      points.size() == 27U,
      "Munk parser reads SSP until the declared bottom, not mesh count");
  context.checkNear(points[6U].depth, 1000.0, 0.0,
                    "Munk source SSP-node depth");
  context.checkNear(points[6U].soundSpeed, 1501.38, 0.0,
                    "Munk source SSP-node sound speed");
  context.check(simulation.receivers().depthCount() == 201U &&
                    simulation.receivers().rangeCount() == 501U,
                "Munk receiver dimensions");
  context.checkNear(simulation.receivers().ranges().back(), 100000.0, 0.0,
                    "Munk range endpoint converts to meters");
  context.checkNear(simulation.integrator().stepLength, 500.0, 0.0,
                    "Munk automatic step is one tenth water depth");
  context.check(simulation.launchFanPlan().launchAngleCount == 1000U,
                "Munk D-02 launch count");
  context.checkNear(parsed.beam.loopRange, 25000.0, 0.0,
                    "Munk loop range converts to meters");
}

void testFortranNumericSpelling(Context& context) {
  std::string contents = renderCase("constant_speed_direct", 50.0, 300U);
  replaceFirst(contents, "50          ! FREQ", "5.0D1,     ! FREQ");
  const ParsedEnvironment parsed = parseText(contents, "d_exponent.env");
  context.checkNear(parsed.simulationCase.frequencies().values().front(), 50.0,
                    0.0, "Fortran D exponent and comma separator are accepted");
}

void testUnsupportedAndMalformedInput(Context& context) {
  const std::string direct = renderCase("constant_speed_direct", 50.0, 300U);

  context.expectThrows<ValidationError>(
      [&] {
        std::string contents = direct;
        replaceFirst(contents, "\n1                       ! NMEDIA",
                     "\n2                       ! NMEDIA");
        static_cast<void>(parseText(contents, "two_media.env"));
      },
      "multiple media are explicitly rejected");
  context.expectThrows<ValidationError>(
      [&] {
        std::string contents = direct;
        replaceFirst(contents, "'CVW'", "'NVW'");
        static_cast<void>(parseText(contents, "n2_ssp.env"));
      },
      "unsupported SSP interpolation is explicitly rejected");
  std::string incoherentContents = direct;
  replaceFirst(incoherentContents, "'CC'", "'IC'");
  const ParsedEnvironment incoherent =
      parseText(incoherentContents, "incoherent.env");
  std::string semiCoherentContents = direct;
  replaceFirst(semiCoherentContents, "'CC'", "'SC'");
  const ParsedEnvironment semiCoherent =
      parseText(semiCoherentContents, "semi_coherent.env");
  context.check(
      incoherent.simulationCase.runMode() == SimulationRunMode::Incoherent &&
          semiCoherent.simulationCase.runMode() ==
              SimulationRunMode::SemiCoherent &&
          incoherent.simulationCase.beamFamily() ==
              BeamFamily::CervenyGaussian &&
          semiCoherent.simulationCase.beamFamily() ==
              BeamFamily::CervenyGaussian,
      "IC/SC select Cartesian Cerveny intensity accumulation modes");
  const std::string hat =
      renderCase("geometric_hat_cartesian_safe_control", 1000.0, 300U);
  for (const auto& [runType, expectedMode] :
       std::vector<std::pair<std::string, SimulationRunMode>>{
           {"CG", SimulationRunMode::Coherent},
           {"IG", SimulationRunMode::Incoherent},
           {"SG", SimulationRunMode::SemiCoherent},
           {"C^", SimulationRunMode::Coherent},
           {"C", SimulationRunMode::Coherent}}) {
    std::string contents = hat;
    replaceFirst(contents, "'CG'", "'" + runType + "'");
    const ParsedEnvironment parsed =
        parseText(contents, "cartesian_hat_" + runType + ".env");
    context.check(
        parsed.simulationCase.runMode() == expectedMode &&
            parsed.simulationCase.beamFamily() == BeamFamily::GeometricHat,
        "G/caret/blank aliases select the Cartesian GeoHat C/I/S path");
  }
  for (const auto& [runType, expectedMode] :
       std::vector<std::pair<std::string, SimulationRunMode>>{
           {"Cg", SimulationRunMode::Coherent},
           {"Ig", SimulationRunMode::Incoherent},
           {"Sg", SimulationRunMode::SemiCoherent}}) {
    std::string contents = hat;
    replaceFirst(contents, "'CG'", "'" + runType + "'");
    const ParsedEnvironment parsed =
        parseText(contents, "ray_centered_hat_" + runType + ".env");
    context.check(
        parsed.simulationCase.runMode() == expectedMode &&
            parsed.simulationCase.beamFamily() == BeamFamily::GeometricHat &&
            parsed.simulationCase.cervenyCoordinateSystem() ==
                CervenyCoordinateSystem::RayCentered,
        "Cg/Ig/Sg select the ray-centered GeoHat C/I/S path");
  }
  std::string nonuniformHat = hat;
  replaceFirst(nonuniformHat, "\n21\n0.02  0.25 /", "\n3\n0.02  0.08  0.25 /");
  const ParsedEnvironment parsedNonuniformHat =
      parseText(nonuniformHat, "nonuniform_cartesian_hat.env");
  context.check(
      parsedNonuniformHat.simulationCase.receivers().rangeCount() == 3U,
      "Cartesian GeoHat accepts nonuniform rectilinear receiver ranges");
  context.expectThrows<ValidationError>(
      [&] {
        std::string contents = nonuniformHat;
        replaceFirst(contents, "'CG'", "'Cg'");
        static_cast<void>(
            parseText(contents, "nonuniform_ray_centered_hat.env"));
      },
      "ray-centered GeoHat rejects nonuniform receiver ranges");
  for (const auto& [runType, expectedMode] :
       std::vector<std::pair<std::string, SimulationRunMode>>{
           {"CB", SimulationRunMode::Coherent},
           {"IB", SimulationRunMode::Incoherent},
           {"SB", SimulationRunMode::SemiCoherent}}) {
    std::string contents = hat;
    replaceFirst(contents, "'CG'", "'" + runType + "'");
    const ParsedEnvironment parsed =
        parseText(contents, "cartesian_gaussian_" + runType + ".env");
    context.check(
        parsed.simulationCase.runMode() == expectedMode &&
            parsed.simulationCase.beamFamily() == BeamFamily::GeometricGaussian,
        "CB/IB/SB select the Cartesian GeoGaussian C/I/S path");
  }
  std::string simple = hat;
  replaceFirst(simple, "'CG'", "'CS'");
  const ParsedEnvironment parsedSimple =
      parseText(simple, "simple_gaussian.env");
  context.check(
      parsedSimple.simulationCase.runMode() == SimulationRunMode::Coherent &&
          parsedSimple.simulationCase.beamFamily() ==
              BeamFamily::SimpleGaussian,
      "CS selects the coherent Cartesian Simple Gaussian path");
  for (const std::string& unsupported : {"IS", "SS"}) {
    context.expectThrows<ValidationError>(
        [&] {
          std::string contents = hat;
          replaceFirst(contents, "'CG'", "'" + unsupported + "'");
          static_cast<void>(
              parseText(contents, "simple_gaussian_" + unsupported + ".env"));
        },
        "Simple Gaussian intensity run modes remain explicitly rejected");
  }
  context.expectThrows<ValidationError>(
      [&] {
        std::string contents = simple;
        contents += "'MS' 1.0 2.5\n";
        static_cast<void>(parseText(contents, "simple_gaussian_with_tail.env"));
      },
      "Cartesian Simple Gaussian rejects a Cerveny-only option tail");
  context.expectThrows<ValidationError>(
      [&] {
        std::string contents = hat;
        contents += "'MS' 1.0 2.5\n";
        static_cast<void>(parseText(contents, "hat_with_cerveny_tail.env"));
      },
      "Cartesian GeoHat rejects a Cerveny-only option tail");
  context.expectThrows<ValidationError>(
      [&] {
        std::string contents = direct;
        replaceFirst(contents, "'MS' 1.0  2.5", "'XS' 1.0  2.5");
        static_cast<void>(parseText(contents, "filling_beam.env"));
      },
      "unknown beam-width mode is explicitly rejected");
  std::string elasticContents = direct;
  replaceFirst(elasticContents, "1000.0  1600.0  0.0  1.8",
               "1000.0  1600.0  100.0  1.8");
  const ParsedEnvironment elastic =
      parseText(elasticContents, "elastic_bottom.env");
  context.checkNear(
      elastic.simulationCase.environment().seabed().material()->shearSoundSpeed,
      100.0, 0.0, "elastic half-space shear speed is retained raw");
  context.expectThrows<ValidationError>(
      [&] {
        std::string contents = direct;
        replaceFirst(contents, "\n51                      ! NR",
                     "\n3                       ! NR");
        replaceFirst(contents, "0.1  5.0 /", "0.1  0.2  0.5 /");
        static_cast<void>(parseText(contents, "nonuniform_ranges.env"));
      },
      "nonuniform Cartesian receiver ranges are rejected");
  context.expectThrows<ValidationError>(
      [&] {
        std::string contents = direct;
        const std::size_t lastLine = contents.rfind("3  5  'P'");
        contents.erase(lastLine);
        static_cast<void>(parseText(contents, "truncated.env"));
      },
      "truncated input reports unexpected end of file");
  context.expectThrows<ValidationError>(
      [&] {
        std::string contents = direct;
        contents += "\nEXTRA\n";
        static_cast<void>(parseText(contents, "trailing.env"));
      },
      "trailing unsupported input is rejected");
  context.expectThrows<ValidationError>(
      [] {
        std::istringstream input("'unterminated\n");
        static_cast<void>(EnvironmentParser::parse(input, "quote.env"));
      },
      "unterminated quote is rejected");
  context.expectThrows<BellhopError>(
      [] {
        static_cast<void>(EnvironmentParser::parseFile(
            "/definitely/not/a/bellhop/environment.env"));
      },
      "missing environment file reports a Bellhop I/O error");
}

}  // namespace

void testSspInterpolationKinds(Context& context) {
  const std::string munkContents = renderCase("munk_pchip", 50.0, 300U);
  const ParsedEnvironment pchipParsed =
      parseText(munkContents, "munk_pchip.env");
  context.check(
      pchipParsed.simulationCase.environment().soundSpeedProfile().interpolationKind() ==
          rayreuse::SspInterpolationKind::Pchip,
      "top option 'P' parses as PCHIP SSP interpolation");

  std::string cLinearContents = munkContents;
  replaceFirst(cLinearContents, "'PVW'", "'CVW'");
  const ParsedEnvironment cParsed =
      parseText(cLinearContents, "munk_clinear.env");
  context.check(
      cParsed.simulationCase.environment().soundSpeedProfile().interpolationKind() ==
          rayreuse::SspInterpolationKind::CLinear,
      "top option 'C' parses as C-linear SSP interpolation");

  for (const std::string& unsupported : {"'NVW'", "'SVW'", "'QVW'"}) {
    context.expectThrows<ValidationError>(
        [&] {
          std::string contents = munkContents;
          replaceFirst(contents, "'PVW'", unsupported);
          static_cast<void>(parseText(contents, "unsupported_ssp.env"));
        },
        "unsupported SSP interpolation option " + unsupported + " is rejected");
  }

  context.expectThrows<ValidationError>(
      [&] {
        std::string contents = munkContents;
        replaceFirst(contents, "'PVW'", "'XVW'");
        static_cast<void>(parseText(contents, "unknown_ssp.env"));
      },
      "unknown SSP interpolation option is rejected");

  const rayreuse::SoundSpeedProfile programmatic(
      {rayreuse::SoundSpeedPoint{.depth = 0.0, .soundSpeed = 1500.0, .density = 1000.0},
       rayreuse::SoundSpeedPoint{.depth = 100.0, .soundSpeed = 1500.0, .density = 1000.0}});
  context.check(
      programmatic.interpolationKind() == rayreuse::SspInterpolationKind::CLinear,
      "programmatic construction defaults to C-linear");
}

int main() {
  Context context;
  testDirectCase(context);
  testSspInterpolationKinds(context);
  testFrequencyOverride(context);
  testEnvironmentFrequencyList(context);
  testCartesianCervenyComponents(context);
  testBoundaryCases(context);
  testRrB1BoundarySidecarsAndFrozenEvents(context);
  testAttenuationCases(context);
  testRrB4ProductRunTypes(context);
  testMunkCase(context);
  testFortranNumericSpelling(context);
  testUnsupportedAndMalformedInput(context);

  if (context.failureCount() != 0) {
    std::cerr << context.failureCount()
              << " environment-parser assertion(s) failed\n";
    return 1;
  }
  std::cout << "All Bellhop RayReuse environment-parser tests passed\n";
  return 0;
}
