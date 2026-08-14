#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

#include "bellhop/error.hpp"
#include "bellhop/cache/ray_path_cache.hpp"
#include "bellhop/field/frequency_projector.hpp"
#include "bellhop/io/environment_parser.hpp"
#include "bellhop/ray/geometry_tracer.hpp"
#include "support/test_harness.hpp"

namespace {

using bellhop::AttenuationUnit;
using bellhop::BellhopError;
using bellhop::BiologicalAttenuationLayers;
using bellhop::BoundaryInterpolationKind;
using bellhop::BoundaryKind;
using bellhop::BeamFamily;
using bellhop::BeamWidthMode;
using bellhop::CervenyCoordinateSystem;
using bellhop::EnvironmentParser;
using bellhop::FieldComponent;
using bellhop::FrequencyProjector;
using bellhop::GeometryTracer;
using bellhop::FrancoisGarrisonParameters;
using bellhop::ParsedEnvironment;
using bellhop::RayPathCache;
using bellhop::SspInterpolationKind;
using bellhop::SharedBiologicalAttenuationLayers;
using bellhop::ReflectionBoundary;
using bellhop::SimulationRunMode;
using bellhop::SourceGeometry;
using bellhop::ValidationError;
using bellhop::VolumeAttenuationModel;
using bellhop::test::Context;

const std::filesystem::path kCasesRoot =
    std::filesystem::path(F2CPP_WORKSPACE_ROOT) /
    "test/standard_cases/cases";

void replaceAll(std::string& contents, const std::string& pattern,
                const std::string& replacement) {
  std::size_t position = 0U;
  bool replaced = false;
  while ((position = contents.find(pattern, position)) !=
         std::string::npos) {
    contents.replace(position, pattern.size(), replacement);
    position += replacement.size();
    replaced = true;
  }
  if (!replaced) {
    throw std::runtime_error(
        "fixture token not found: " + pattern);
  }
}

void replaceFirst(std::string& contents, const std::string& pattern,
                  const std::string& replacement) {
  const std::size_t position = contents.find(pattern);
  if (position == std::string::npos) {
    throw std::runtime_error(
        "fixture token not found: " + pattern);
  }
  contents.replace(position, pattern.size(), replacement);
}

std::string renderCase(const std::string& caseName,
                       double frequency,
                       std::size_t launchAngleCount) {
  const std::filesystem::path path =
      kCasesRoot / caseName / "origin.env.in";
  std::ifstream input(path);
  if (!input.is_open()) {
    throw std::runtime_error(
        "unable to open test fixture: " + path.string());
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  std::string contents = buffer.str();
  std::ostringstream frequencyText;
  frequencyText.precision(12);
  frequencyText << frequency;
  replaceAll(contents, "@FREQUENCY_HZ@", frequencyText.str());
  replaceAll(
      contents, "@NALPHA@", std::to_string(launchAngleCount));
  return contents;
}

ParsedEnvironment parseText(const std::string& contents,
                            const std::string& name) {
  std::istringstream input(contents);
  return EnvironmentParser::parse(input, name);
}

std::string withoutCervenyTail(std::string contents) {
  replaceFirst(
      contents,
      "'MS' 1.0  2.5           ! Standard curvature, epsilon multiplier, Rloop (km)\n"
      "3  5  'P'               ! Images, beam window, pressure component\n",
      "");
  return contents;
}

class TemporaryBoundaryCase {
 public:
  TemporaryBoundaryCase(const std::string& name,
                        const std::string& topFormat)
      : directory_(std::filesystem::temp_directory_path() /
                   ("bellhop_f2cpp_" + name)),
        environmentPath_(directory_ / "case.env") {
    std::error_code cleanupError;
    std::filesystem::remove_all(directory_, cleanupError);
    std::filesystem::create_directories(directory_);
    const std::filesystem::path fixtureRoot =
        std::filesystem::path(F2CPP_WORKSPACE_ROOT) /
        "Bellhop_F2CPP/tests/fixtures";
    std::filesystem::copy_file(
        fixtureRoot / "i3_piecewise.env", environmentPath_);
    std::filesystem::copy_file(
        fixtureRoot / "i3_piecewise.bty", directory_ / "case.bty");
    std::ofstream top(directory_ / "case.ati");
    if (!top.is_open()) {
      throw std::runtime_error("unable to create temporary ATI fixture");
    }
    top << topFormat << "\n3\n0.0 0.0\n1.0 20.0\n2.0 0.0\n";
  }

  TemporaryBoundaryCase(const TemporaryBoundaryCase&) = delete;
  TemporaryBoundaryCase& operator=(const TemporaryBoundaryCase&) = delete;

  ~TemporaryBoundaryCase() {
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

class TemporaryLongBoundaryCase {
 public:
  TemporaryLongBoundaryCase(const std::string& name,
                            const std::string& bathymetryContents)
      : directory_(std::filesystem::temp_directory_path() /
                   ("bellhop_f2cpp_" + name)),
        environmentPath_(directory_ / "case.env") {
    std::error_code cleanupError;
    std::filesystem::remove_all(directory_, cleanupError);
    std::filesystem::create_directories(directory_);
    const std::filesystem::path fixtureRoot =
        std::filesystem::path(F2CPP_WORKSPACE_ROOT) /
        "Bellhop_F2CPP/tests/fixtures";
    std::filesystem::copy_file(
        fixtureRoot / "i3_long_materials.env", environmentPath_);
    std::filesystem::copy_file(
        fixtureRoot / "i3_long_materials.ati", directory_ / "case.ati");
    std::ofstream bottom(directory_ / "case.bty");
    if (!bottom.is_open()) {
      throw std::runtime_error("unable to create temporary BTY fixture");
    }
    bottom << bathymetryContents;
  }

  TemporaryLongBoundaryCase(const TemporaryLongBoundaryCase&) = delete;
  TemporaryLongBoundaryCase& operator=(const TemporaryLongBoundaryCase&) =
      delete;

  ~TemporaryLongBoundaryCase() {
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

void testDirectCase(Context& context) {
  const ParsedEnvironment parsed = parseText(
      renderCase("constant_speed_direct", 50.0, 300U),
      "constant_speed_direct.env");
  const auto& simulation = parsed.simulationCase;
  const auto& environment = simulation.environment();

  context.check(
      parsed.title ==
          "Constant speed direct field, Cartesian Cerveny beams",
      "direct title excludes quotes and comments");
  context.checkNear(
      simulation.frequencies().values().front(),
      50.0, 0.0, "direct frequency");
  context.check(
      environment.soundSpeedProfile().points().size() == 2U,
      "direct SSP point count is terminated by bottom depth");
  context.checkNear(
      environment.soundSpeedProfile().points().front().density,
      1000.0, 0.0, "default water density converts to SI");
  context.check(
      environment.seaSurface().kind() == BoundaryKind::Vacuum,
      "direct vacuum surface");
  context.check(
      environment.seabed().kind() ==
          BoundaryKind::AcousticHalfSpace,
      "direct acoustic seabed");
  context.checkNear(
      environment.seabed().material()->density,
      1800.0, 0.0, "direct seabed density converts to SI");
  context.checkNear(
      environment.seabed()
          .material()
          ->compressionalSoundSpeed,
      1600.0, 0.0, "direct seabed sound speed");
  context.checkNear(
      simulation.source().depth, 500.0, 0.0,
      "direct source depth");
  context.check(
      simulation.receivers().depthCount() == 21U &&
          simulation.receivers().rangeCount() == 51U,
      "direct receiver dimensions");
  const double expectedSecondRange =
      (0.1 + (5.0 - 0.1) / 50.0) * 1000.0;
  context.checkNear(
      simulation.receivers().ranges()[1U],
      expectedSecondRange, 0.0,
      "receiver subtabulation occurs in km before SI conversion");
  context.check(
      simulation.launchFanPlan().launchAngleCount == 300U,
      "direct D-02 launch count");
  context.checkNear(
      simulation.launchFanPlan().launchAngles.front(),
      -5.0 * std::numbers::pi / 180.0, 0.0,
      "minimum launch angle converts to radians");
  context.checkNear(
      simulation.integrator().stepLength,
      10.0, 0.0, "direct explicit step length");
  context.checkNear(
      simulation.integrator().rangeLimit,
      5100.0, 0.0, "range box converts from km");
  context.check(
      simulation.integrator().maximumRayPoints == 2'000'000U,
      "parser freezes the legacy ray-point guard");
  context.checkNear(
      parsed.beam.epsilonMultiplier,
      1.0, 0.0, "direct epsilon multiplier");
  context.checkNear(
      parsed.beam.loopRange,
      2500.0, 0.0, "direct beam loop range converts from km");
  context.check(
      parsed.beam.influence.imageCount == 3U &&
          parsed.beam.influence.beamWindow == 5,
      "direct image/window settings");
  context.check(
      simulation.fieldComponent() == FieldComponent::Pressure,
      "direct case preserves the pressure component");
  context.check(
      simulation.sourceGeometry() == SourceGeometry::Point,
      "blank source geometry defaults to a point source");
  context.check(
      simulation.cervenyCoordinateSystem() ==
          CervenyCoordinateSystem::Cartesian,
      "Cartesian Cerveny coordinates remain the parser default");
  context.check(
      simulation.beamFamily() == BeamFamily::CervenyGaussian &&
          parsed.beam.family == BeamFamily::CervenyGaussian,
      "explicit CC preserves the legacy Cerveny family");
}

void testSourceGeometry(Context& context) {
  const std::string point =
      renderCase("constant_speed_direct", 50.0, 300U);
  std::string line = point;
  replaceFirst(line, "'CC'", "'CC X'");
  const ParsedEnvironment parsed = parseText(line, "line_source.env");
  context.check(
      parsed.simulationCase.sourceGeometry() == SourceGeometry::Line,
      "run-type fourth character X selects a line source");

  std::string explicitPoint = point;
  replaceFirst(explicitPoint, "'CC'", "'CC R'");
  context.check(
      parseText(explicitPoint, "point_source.env")
              .simulationCase.sourceGeometry() == SourceGeometry::Point,
      "run-type fourth character R selects a point source");

  context.expectThrows<ValidationError>(
      [&point] {
        std::string invalid = point;
        replaceFirst(invalid, "'CC'", "'CC Y'");
        static_cast<void>(parseText(invalid, "invalid_source_geometry.env"));
      },
      "parser rejects an unknown source-geometry character");
}

void testTransmissionLossModes(Context& context) {
  const std::string coherent =
      renderCase("constant_speed_direct", 50.0, 300U);
  struct ModeCase {
    const char* token;
    SimulationRunMode expected;
    CervenyCoordinateSystem coordinateSystem;
  };
  const std::array cases{
      ModeCase{"CC", SimulationRunMode::CoherentTransmissionLoss,
               CervenyCoordinateSystem::Cartesian},
      ModeCase{"IC", SimulationRunMode::IncoherentTransmissionLoss,
               CervenyCoordinateSystem::Cartesian},
      ModeCase{"SC", SimulationRunMode::SemiCoherentTransmissionLoss,
               CervenyCoordinateSystem::Cartesian},
      ModeCase{"CR", SimulationRunMode::CoherentTransmissionLoss,
               CervenyCoordinateSystem::RayCentered},
      ModeCase{"IR", SimulationRunMode::IncoherentTransmissionLoss,
               CervenyCoordinateSystem::RayCentered},
      ModeCase{"SR", SimulationRunMode::SemiCoherentTransmissionLoss,
               CervenyCoordinateSystem::RayCentered}};
  for (const ModeCase& modeCase : cases) {
    std::string contents = coherent;
    replaceFirst(contents, "'CC'", "'" + std::string(modeCase.token) + "'");
    const ParsedEnvironment parsed =
        parseText(contents, std::string(modeCase.token) + ".env");
    context.check(
        parsed.simulationCase.runMode() == modeCase.expected &&
            parsed.simulationCase.cervenyCoordinateSystem() ==
                modeCase.coordinateSystem &&
            bellhop::isTransmissionLossMode(modeCase.expected),
        "C/I/S and C/R run-type characters select TL mode and coordinates");
    context.check(
        parsed.beam.widthMode == bellhop::BeamWidthMode::MinimumWidth &&
            parsed.beam.curvatureMode ==
                bellhop::BoundaryCurvatureMode::Standard &&
            parsed.beam.epsilonMultiplier == 1.0 &&
            parsed.beam.loopRange == 2500.0 &&
            parsed.beam.influence.imageCount == 3U &&
            parsed.beam.influence.beamWindow == 5 &&
            parsed.simulationCase.fieldComponent() ==
                FieldComponent::Pressure,
        "all TL modes consume the identical Cerveny and image tail");
  }

  std::string explicitOmniLine = coherent;
  replaceFirst(explicitOmniLine, "'CC'", "'SCOX'");
  const ParsedEnvironment semiLine =
      parseText(explicitOmniLine, "semi_omni_line.env");
  context.check(
      semiLine.simulationCase.runMode() ==
              SimulationRunMode::SemiCoherentTransmissionLoss &&
          semiLine.simulationCase.sourceGeometry() == SourceGeometry::Line &&
          !semiLine.simulationCase.sourceBeamPattern().isDirectional(),
      "semi-coherent mode composes with explicit omni and line-source chars");
}

void testBeamFamilies(Context& context) {
  const std::string cerveny =
      renderCase("constant_speed_direct", 50.0, 300U);
  struct FamilyCase {
    const char* token;
    SimulationRunMode runMode;
    BeamFamily family;
    CervenyCoordinateSystem coordinateSystem;
  };
  const std::array cases{
      FamilyCase{"CG", SimulationRunMode::CoherentTransmissionLoss,
                 BeamFamily::GeometricHat,
                 CervenyCoordinateSystem::Cartesian},
      FamilyCase{"IG", SimulationRunMode::IncoherentTransmissionLoss,
                 BeamFamily::GeometricHat,
                 CervenyCoordinateSystem::Cartesian},
      FamilyCase{"SB", SimulationRunMode::SemiCoherentTransmissionLoss,
                 BeamFamily::GeometricGaussian,
                 CervenyCoordinateSystem::Cartesian},
      FamilyCase{"Cg", SimulationRunMode::CoherentTransmissionLoss,
                 BeamFamily::GeometricHat,
                 CervenyCoordinateSystem::RayCentered},
      FamilyCase{"CB", SimulationRunMode::CoherentTransmissionLoss,
                 BeamFamily::GeometricGaussian,
                 CervenyCoordinateSystem::Cartesian},
      FamilyCase{"CS", SimulationRunMode::CoherentTransmissionLoss,
                 BeamFamily::SimpleGaussian,
                 CervenyCoordinateSystem::Cartesian},
      FamilyCase{"C^", SimulationRunMode::CoherentTransmissionLoss,
                 BeamFamily::GeometricHat,
                 CervenyCoordinateSystem::Cartesian},
      FamilyCase{"C", SimulationRunMode::CoherentTransmissionLoss,
                 BeamFamily::GeometricHat,
                 CervenyCoordinateSystem::Cartesian}};
  for (const FamilyCase& familyCase : cases) {
    std::string contents = withoutCervenyTail(cerveny);
    replaceFirst(contents, "'CC'", "'" + std::string(familyCase.token) + "'");
    const ParsedEnvironment parsed =
        parseText(contents, std::string(familyCase.token) + ".env");
    context.check(
        parsed.simulationCase.runMode() == familyCase.runMode &&
            parsed.simulationCase.beamFamily() == familyCase.family &&
            parsed.simulationCase.cervenyCoordinateSystem() ==
                familyCase.coordinateSystem &&
            parsed.beam.family == familyCase.family,
        "run-type second character selects a strong beam family");
    context.check(
        parsed.beam.widthMode == BeamWidthMode::SpaceFilling &&
            parsed.beam.curvatureMode ==
                bellhop::BoundaryCurvatureMode::Standard &&
            parsed.beam.epsilonMultiplier == 1.0 &&
            parsed.beam.loopRange == 1.0 &&
            parsed.beam.influence.imageCount == 1U &&
            parsed.beam.influence.beamWindow == 1 &&
            parsed.simulationCase.fieldComponent() ==
                FieldComponent::Pressure,
        "non-Cerveny beam families retain fixed pressure-only defaults");
  }

  std::string explicitOmniLine = withoutCervenyTail(cerveny);
  replaceFirst(explicitOmniLine, "'CC'", "'CGOX'");
  const ParsedEnvironment line =
      parseText(explicitOmniLine, "geometric_hat_line.env");
  context.check(
      line.simulationCase.beamFamily() == BeamFamily::GeometricHat &&
          line.simulationCase.sourceGeometry() == SourceGeometry::Line &&
          !line.simulationCase.sourceBeamPattern().isDirectional(),
      "non-Cerveny families preserve common omni and line-source chars");

  context.expectThrows<ValidationError>(
      [&] {
        std::string contents = withoutCervenyTail(cerveny);
        replaceFirst(contents, "'CC'", "'Cb'");
        static_cast<void>(parseText(contents, "unsupported_b.env"));
      },
      "ray-centered geometric Gaussian is rejected at the parser boundary");
  context.expectThrows<ValidationError>(
      [&] {
        std::string contents = withoutCervenyTail(cerveny);
        replaceFirst(contents, "'CC'", "'IS'");
        static_cast<void>(parseText(contents, "simple_incoherent.env"));
      },
      "simple Gaussian rejects incoherent mode");
  context.expectThrows<ValidationError>(
      [&] {
        std::string contents = withoutCervenyTail(cerveny);
        replaceFirst(contents, "'CC'", "'CS X'");
        static_cast<void>(parseText(contents, "simple_line.env"));
      },
      "simple Gaussian rejects line sources");
  context.expectThrows<ValidationError>(
      [&] {
        std::string contents = cerveny;
        replaceFirst(contents, "'CC'", "'CG'");
        static_cast<void>(parseText(contents, "geometric_with_tail.env"));
      },
      "non-Cerveny families do not consume Cerveny-only tail records");

  std::string nonuniform = withoutCervenyTail(cerveny);
  replaceFirst(nonuniform, "'CC'", "'CG'");
  replaceFirst(nonuniform, "51                      ! NR",
               "3                       ! NR");
  replaceFirst(nonuniform, "0.1  5.0 /",
               "0.1 0.2 0.5 /");
  context.check(
      parseText(nonuniform, "geometric_nonuniform.env")
              .simulationCase.receivers().rangeCount() == 3U,
      "Cartesian geometric hat accepts increasing nonuniform ranges");
}

void testFieldComponents(Context& context) {
  const std::string pressure =
      renderCase("constant_speed_direct", 50.0, 300U);
  for (const auto& [token, expected] :
       std::array<std::pair<std::string, FieldComponent>, 2>{
           std::pair{"V", FieldComponent::Vertical},
           std::pair{"H", FieldComponent::Horizontal}}) {
    std::string contents = pressure;
    replaceFirst(contents, "3  5  'P'", "3  5  '" + token + "'");
    const ParsedEnvironment parsed =
        parseText(contents, "component_" + token + ".env");
    context.check(
        parsed.simulationCase.fieldComponent() == expected,
        "parser preserves Cartesian legacy field component " + token);
  }
  for (const std::string token : {"X", "p", ""}) {
    context.expectThrows<ValidationError>(
        [&pressure, &token] {
          std::string contents = pressure;
          replaceFirst(contents, "3  5  'P'",
                       "3  5  '" + token + "'");
          static_cast<void>(
              parseText(contents, "invalid_component.env"));
        },
        "parser rejects an unsupported field component");
  }
}

void testMultipleSourceDepths(Context& context) {
  std::string explicitSources =
      renderCase("constant_speed_direct", 50.0, 300U);
  replaceFirst(
      explicitSources,
      "1                       ! NSD\n"
      "500.0 /                 ! Source depth (m)",
      "3                       ! NSD\n"
      "80.0 20.0 50.0 /       ! Source depths (m)");
  const auto explicitParsed =
      parseText(explicitSources, "multi_source_explicit.env");
  const auto& explicitSimulation = explicitParsed.simulationCase;
  context.check(
      explicitSimulation.sourceCount() == 3U &&
          explicitSimulation.sources()[0U].depth == 20.0 &&
          explicitSimulation.sources()[1U].depth == 50.0 &&
          explicitSimulation.sources()[2U].depth == 80.0 &&
          explicitSimulation.source().depth == 20.0,
      "multi-source parser sorts source depths and preserves all slices");

  std::string subtabulated =
      renderCase("constant_speed_direct", 50.0, 300U);
  replaceFirst(
      subtabulated,
      "1                       ! NSD\n"
      "500.0 /                 ! Source depth (m)",
      "3                       ! NSD\n"
      "20.0 80.0 /            ! Source-depth endpoints");
  const auto subtabulatedParsed =
      parseText(subtabulated, "multi_source_subtab.env");
  context.check(
      subtabulatedParsed.simulationCase.sources()[0U].depth == 20.0 &&
          subtabulatedParsed.simulationCase.sources()[1U].depth == 50.0 &&
          subtabulatedParsed.simulationCase.sources()[2U].depth == 80.0,
      "multi-source endpoints use legacy single-precision subtabulation");

  std::string automatic = explicitSources;
  replaceFirst(automatic, "\n300", "\n0");
  const auto automaticParsed =
      parseText(automatic, "multi_source_automatic.env");
  context.check(
      automaticParsed.simulationCase.launchFanPlan().launchAngleCount ==
          explicitSimulation.launchFanPlan().launchAngleCount,
      "multi-source automatic planning creates one shared D-02 fan");
}

void testIrregularReceiverGrid(Context& context) {
  std::string contents =
      renderCase("constant_speed_direct", 50.0, 300U);
  replaceFirst(
      contents,
      "21                      ! NRD\n"
      "400.0  600.0 /          ! Receiver depths (m)\n"
      "51                      ! NR\n"
      "0.1  5.0 /              ! Receiver ranges (km)\n"
      "'CC'                    ! Coherent TL, Cartesian Cerveny beams",
      "3                       ! NRD\n"
      "400.0 500.0 600.0 /     ! Paired receiver depths (m)\n"
      "3                       ! NR\n"
      "0.1 0.2 0.3 /           ! Paired receiver ranges (km)\n"
      "'CC RI2'                ! Coherent Cartesian irregular grid");
  const ParsedEnvironment parsed =
      parseText(contents, "irregular_receivers.env");
  const auto& receivers = parsed.simulationCase.receivers();
  context.check(
      receivers.isIrregular() && receivers.depthCount() == 3U &&
          receivers.rangeCount() == 3U &&
          receivers.receiversPerRange() == 1U &&
          receivers.depthAt(0U, 0U) == 400.0 &&
          receivers.depthAt(0U, 2U) == 600.0,
      "run-type fifth character I selects paired irregular receivers");

  std::string geometricGaussian = contents;
  replaceFirst(geometricGaussian, "'CC RI2'", "'CB RI2'");
  geometricGaussian = withoutCervenyTail(std::move(geometricGaussian));
  const ParsedEnvironment geometricParsed = parseText(
      geometricGaussian, "geometric_gaussian_irregular.env");
  context.check(
      geometricParsed.simulationCase.beamFamily() ==
              BeamFamily::GeometricGaussian &&
          geometricParsed.simulationCase.receivers().isIrregular(),
      "non-Cerveny families preserve the common irregular-grid character");

  std::string mismatch = contents;
  replaceFirst(mismatch, "3                       ! NRD",
               "2                       ! NRD");
  replaceFirst(mismatch, "400.0 500.0 600.0 /",
               "400.0 600.0 /");
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(parseText(mismatch, "irregular_mismatch.env"));
      },
      "irregular receiver parsing rejects unequal coordinate counts");

  context.expectThrows<ValidationError>(
      [&] {
        std::string rayCentered = contents;
        replaceFirst(rayCentered, "'CC RI2'", "'Cg RI2'");
        rayCentered = withoutCervenyTail(std::move(rayCentered));
        static_cast<void>(
            parseText(rayCentered, "ray_centered_irregular.env"));
      },
      "ray-centered geometric-hat parsing explicitly rejects irregular receivers");
  context.expectThrows<ValidationError>(
      [&] {
        std::string simple = contents;
        replaceFirst(simple, "'CC RI2'", "'CS RI2'");
        simple = withoutCervenyTail(std::move(simple));
        static_cast<void>(
            parseText(simple, "simple_gaussian_irregular.env"));
      },
      "simple-Gaussian parsing rejects irregular receivers");
}

void testBoundaryCases(Context& context) {
  const ParsedEnvironment rigid = parseText(
      renderCase(
          "constant_speed_vacuum_rigid", 250.0, 1570U),
      "constant_speed_vacuum_rigid.env");
  context.check(
      rigid.simulationCase.environment().seabed().kind() ==
          BoundaryKind::Rigid,
      "rigid standard case parses rigid seabed");
  context.checkNear(
      rigid.simulationCase.integrator().stepLength,
      10.0, 0.0,
      "zero step selects one tenth of shallow-water depth");
  context.check(
      rigid.simulationCase.launchFanPlan().launchAngleCount ==
          1570U,
      "shallow-water depth criterion determines launch count");

  const ParsedEnvironment acoustic = parseText(
      renderCase(
          "constant_speed_acoustic_bottom", 250.0, 1570U),
      "constant_speed_acoustic_bottom.env");
  const auto& material =
      *acoustic.simulationCase.environment().seabed().material();
  context.checkNear(
      material.compressionalSoundSpeed, 1590.0, 0.0,
      "lossy acoustic-bottom sound speed");
  context.checkNear(
      material.density, 1200.0, 0.0,
      "lossy acoustic-bottom density converts to SI");
  context.checkNear(
      material.compressionalAttenuation.value,
      0.5, 0.0, "lossy acoustic-bottom raw attenuation");
  context.check(
      material.compressionalAttenuation.unit ==
          AttenuationUnit::DecibelsPerWavelength,
      "acoustic-bottom attenuation options");
}

void testAttenuationCases(Context& context) {
  const ParsedEnvironment lossless = parseText(
      renderCase(
          "constant_speed_no_attenuation_5khz",
          5000.0, 10000U),
      "constant_speed_no_attenuation_5khz.env");
  const ParsedEnvironment thorp = parseText(
      renderCase(
          "constant_speed_thorp", 5000.0, 10000U),
      "constant_speed_thorp.env");
  context.check(
      lossless.simulationCase.environment().volumeAttenuation().model ==
          VolumeAttenuationModel::None,
      "lossless case keeps volume attenuation disabled");
  context.check(
      thorp.simulationCase.environment().volumeAttenuation().model ==
          VolumeAttenuationModel::Thorp,
      "Thorp case stores one environment-level volume model");
  context.check(
      thorp.simulationCase.launchFanPlan().launchAngleCount ==
          10000U,
      "5 kHz phase criterion determines launch count");

  constexpr std::array unitCases{
      std::pair{'N', AttenuationUnit::NepersPerMeter},
      std::pair{'F', AttenuationUnit::DecibelsPerMeterKilohertz},
      std::pair{'M', AttenuationUnit::DecibelsPerMeter},
      std::pair{'W', AttenuationUnit::DecibelsPerWavelength},
      std::pair{'Q', AttenuationUnit::QualityFactor},
      std::pair{'L', AttenuationUnit::LossParameter},
  };
  for (const auto& [option, expectedUnit] : unitCases) {
    std::string contents = renderCase(
        "constant_speed_no_attenuation_5khz", 5000.0, 10000U);
    replaceFirst(contents, "'CVW'", std::string("'CV") + option + "'");
    const ParsedEnvironment parsed = parseText(
        contents, std::string("attenuation_unit_") + option + ".env");
    const auto& environment = parsed.simulationCase.environment();
    context.check(
        environment.soundSpeedProfile()
                .points()
                .front()
                .attenuation.unit == expectedUnit,
        std::string("SSP attenuation unit parses option ") + option);
    context.check(
        environment.seabed()
                .material()
                ->compressionalAttenuation.unit == expectedUnit,
        std::string("half-space attenuation unit parses option ") + option);
  }
}

void testVolumeAttenuationCases(Context& context) {
  const std::string direct =
      renderCase("constant_speed_direct", 1000.0, 300U);

  {
    std::string contents = direct;
    replaceFirst(contents, "'CVW'", "'CVWF'\n20 35 8 750");
    const ParsedEnvironment parsed =
        parseText(contents, "francois_garrison.env");
    const auto& volume =
        parsed.simulationCase.environment().volumeAttenuation();
    context.check(
        volume.model == VolumeAttenuationModel::FrancoisGarrison,
        "F suffix selects Francois-Garrison volume attenuation");
    context.check(
        std::holds_alternative<FrancoisGarrisonParameters>(
            volume.parameters),
        "Francois-Garrison parameters are owned by the Environment");
    const auto& parameters =
        std::get<FrancoisGarrisonParameters>(volume.parameters);
    context.checkNear(parameters.temperatureCelsius, 20.0, 0.0,
                      "Francois-Garrison temperature is retained");
    context.checkNear(parameters.salinityPsu, 35.0, 0.0,
                      "Francois-Garrison salinity is retained");
    context.checkNear(parameters.pH, 8.0, 0.0,
                      "Francois-Garrison pH is retained");
    context.checkNear(parameters.meanDepthMeters, 750.0, 0.0,
                      "Francois-Garrison mean depth is retained");
  }

  {
    std::string contents = direct;
    replaceFirst(contents, "'CVW'",
                 "'CVWB'\n2\n100 300 1000 10 0.25\n"
                 "200 400 500 5 0.5");
    const ParsedEnvironment parsed =
        parseText(contents, "biological.env");
    const auto& volume =
        parsed.simulationCase.environment().volumeAttenuation();
    context.check(
        volume.model == VolumeAttenuationModel::Biological,
        "B suffix selects biological volume attenuation");
    context.check(
        std::holds_alternative<SharedBiologicalAttenuationLayers>(
            volume.parameters),
        "biological layers are owned once by the Environment");
    const auto& layers = *std::get<SharedBiologicalAttenuationLayers>(
        volume.parameters);
    context.check(layers.size() == 2U,
                  "biological parser retains every layer");
    context.checkNear(layers[0U].minimumDepth, 100.0, 0.0,
                      "biological input order is retained");
    context.checkNear(layers[1U].minimumDepth, 200.0, 0.0,
                      "overlapping biological layer is retained");
  }

  {
    std::string contents = direct;
    replaceFirst(contents, "'CVW'", "'CVWB'\n0");
    const ParsedEnvironment parsed =
        parseText(contents, "biological_empty.env");
    const auto& layers = *std::get<SharedBiologicalAttenuationLayers>(
        parsed.simulationCase.environment()
            .volumeAttenuation()
            .parameters);
    context.check(layers.empty(),
                  "zero biological layers are accepted as a no-op");
  }

  {
    std::ostringstream replacement;
    replacement << "'CVWB'\n200\n";
    for (std::size_t index = 0U; index < 200U; ++index) {
      replacement << index << ' ' << index
                  << " 1000 10 0.01\n";
    }
    std::string contents = direct;
    replaceFirst(contents, "'CVW'", replacement.str());
    const ParsedEnvironment parsed =
        parseText(contents, "biological_200.env");
    const auto& layers = *std::get<SharedBiologicalAttenuationLayers>(
        parsed.simulationCase.environment()
            .volumeAttenuation()
            .parameters);
    context.check(layers.size() == 200U,
                  "the 200-layer biological safety limit is accepted");
  }

  context.expectThrows<ValidationError>(
      [&] {
        std::string contents = direct;
        replaceFirst(contents, "'CVW'", "'CVWF'\n-273 35 8 0");
        static_cast<void>(parseText(contents, "invalid_fg.env"));
      },
      "invalid Francois-Garrison physical parameters are rejected");
  context.expectThrows<ValidationError>(
      [&] {
        std::string contents = direct;
        replaceFirst(contents, "'CVW'", "'CVWB'\n201");
        static_cast<void>(parseText(contents, "too_many_bio.env"));
      },
      "more than 200 biological layers are rejected safely");
  context.expectThrows<ValidationError>(
      [&] {
        std::string contents = direct;
        replaceFirst(contents, "'CVW'",
                     "'CVWB'\n1\n300 100 1000 10 0.25");
        static_cast<void>(parseText(contents, "invalid_bio.env"));
      },
      "reversed biological depth bounds are rejected");
}

void testMunkCase(Context& context) {
  const ParsedEnvironment parsed = parseText(
      renderCase("munk_cerveny_cc", 50.0, 1000U),
      "munk_cerveny_cc.env");
  const auto& simulation = parsed.simulationCase;
  const auto& points =
      simulation.environment().soundSpeedProfile().points();
  context.check(
      points.size() == 27U,
      "Munk parser reads SSP until the declared bottom, not mesh count");
  context.checkNear(
      points[6U].depth, 1000.0, 0.0,
      "Munk source SSP-node depth");
  context.checkNear(
      points[6U].soundSpeed, 1501.38, 0.0,
      "Munk source SSP-node sound speed");
  context.check(
      simulation.receivers().depthCount() == 201U &&
          simulation.receivers().rangeCount() == 501U,
      "Munk receiver dimensions");
  context.checkNear(
      simulation.receivers().ranges().back(),
      100000.0, 0.0, "Munk range endpoint converts to meters");
  context.checkNear(
      simulation.integrator().stepLength,
      500.0, 0.0,
      "Munk automatic step is one tenth water depth");
  context.check(
      simulation.launchFanPlan().launchAngleCount == 1000U,
      "Munk D-02 launch count");
  context.checkNear(
      parsed.beam.loopRange,
      25000.0, 0.0, "Munk loop range converts to meters");
}

void testFortranNumericSpelling(Context& context) {
  std::string contents =
      renderCase("constant_speed_direct", 50.0, 300U);
  replaceFirst(contents, "50          ! FREQ", "5.0D1,     ! FREQ");
  const ParsedEnvironment parsed =
      parseText(contents, "d_exponent.env");
  context.checkNear(
      parsed.simulationCase.frequencies().values().front(),
      50.0, 0.0,
      "Fortran D exponent and comma separator are accepted");
}

void testPiecewiseLinearShortBoundaries(Context& context) {
  const std::filesystem::path fixtureRoot =
      std::filesystem::path(F2CPP_WORKSPACE_ROOT) /
      "Bellhop_F2CPP/tests/fixtures";
  const ParsedEnvironment parsed = EnvironmentParser::parseFile(
      fixtureRoot / "i3_piecewise.env");
  const auto& environment = parsed.simulationCase.environment();
  const auto& top = environment.seaSurface().geometry();
  const auto& bottom = environment.seabed().geometry();

  context.check(!top.isFlat() && !bottom.isFlat(),
                "ATI and BTY selectors load range-dependent geometry");
  context.check(top.segmentCount() == 4U && bottom.segmentCount() == 4U,
                "three user nodes create two physical segments and two "
                "horizontal extensions");
  const auto topSlope = top.evaluate(500.0, 0U);
  context.check(topSlope.segmentIndex == 1U,
                "ATI range conversion selects the first sloping segment");
  context.checkNear(topSlope.tangent.depth,
                    20.0 / std::hypot(1000.0, 20.0), 1.0e-15,
                    "ATI ranges convert from km before tangent formation");
  context.checkNear(topSlope.outwardNormal.range,
                    topSlope.tangent.depth, 0.0,
                    "ATI outward normal follows the upper orientation");

  const auto bottomSlope = bottom.evaluate(1500.0, 0U);
  context.check(bottomSlope.segmentIndex == 2U,
                "BTY range conversion selects the second sloping segment");
  context.checkNear(bottomSlope.outwardNormal.range,
                    -bottomSlope.tangent.depth, 0.0,
                    "BTY outward normal follows the lower orientation");
  context.checkNear(parsed.simulationCase.integrator().stepLength,
                    12.0, 0.0,
                    "automatic step retains declared reference water depth");

  context.expectThrows<ValidationError>(
      [&] {
        std::string contents =
            renderCase("constant_speed_vacuum_rigid", 250.0, 1570U);
        replaceFirst(contents, "'CVW'", "'CVW ~'");
        static_cast<void>(parseText(contents, "memory.env"));
      },
      "stream parsing explicitly rejects unresolved ATI sidecars");
  context.expectThrows<ValidationError>(
      [&] {
        std::string contents =
            renderCase("constant_speed_vacuum_rigid", 250.0, 1570U);
        replaceFirst(contents, "'CVW'", "'CVW~'");
        static_cast<void>(parseText(contents, "misplaced_selector.env"));
      },
      "topography selector in the attenuation position is rejected");
  context.expectThrows<BellhopError>(
      [&] {
        static_cast<void>(EnvironmentParser::parseFile(
            fixtureRoot / "i3_missing_bty.env"));
      },
      "missing BTY sidecar reports a Bellhop I/O error");
}

void testPiecewiseLinearLongMaterials(Context& context) {
  const std::filesystem::path fixtureRoot =
      std::filesystem::path(F2CPP_WORKSPACE_ROOT) /
      "Bellhop_F2CPP/tests/fixtures";
  const ParsedEnvironment parsed = EnvironmentParser::parseFile(
      fixtureRoot / "i3_long_materials.env");
  const auto& seabed = parsed.simulationCase.environment().seabed();
  context.check(seabed.hasRangeDependentMaterials(),
                "LL bathymetry retains a long-format material profile");
  context.checkNear(seabed.material()->compressionalSoundSpeed,
                    1550.0, 0.0,
                    "ordinary ENV half-space remains the explicit fallback");
  context.checkNear(seabed.materialAtSegment(0U).compressionalSoundSpeed,
                    1600.0, 0.0,
                    "left extension copies the first LL node material");
  context.checkNear(seabed.materialAtSegment(1U).compressionalSoundSpeed,
                    1600.0, 0.0,
                    "first physical segment uses its left node material");
  context.checkNear(seabed.materialAtSegment(2U).compressionalSoundSpeed,
                    1800.0, 0.0,
                    "second physical segment switches to its left node");
  context.checkNear(seabed.materialAtSegment(3U).compressionalSoundSpeed,
                    2000.0, 0.0,
                    "right extension copies the final LL node material");
  context.checkNear(seabed.materialAtSegment(2U).density, 2000.0, 0.0,
                    "LL density converts from g/cm3 to SI");
  context.check(
      seabed.materialAtSegment(2U).compressionalAttenuation.unit ==
          AttenuationUnit::DecibelsPerWavelength,
      "LL raw attenuation retains the ENV attenuation unit");
  context.checkNear(
      seabed.materialAtSegment(2U).compressionalAttenuation.value,
      0.20, 0.0, "LL raw attenuation remains unconverted in Environment");
  context.checkNear(seabed.materialAttenuationDepthAtSegment(2U),
                    1.0e20, 0.0,
                    "LL frequency conversion uses the Origin 1D20 depth");

  const auto& simulation = parsed.simulationCase;
  const auto path = GeometryTracer(simulation).trace(
      simulation.source(), std::numbers::pi / 4.0);
  std::vector<const bellhop::ReflectionEvent*> seabedEvents;
  for (const auto& event : path.events) {
    if (event.boundary == ReflectionBoundary::Seabed) {
      seabedEvents.push_back(&event);
    }
  }
  context.check(seabedEvents.size() == 2U,
                "45-degree LL fixture reaches two seabed segments");
  if (seabedEvents.size() == 2U) {
    context.check(seabedEvents[0U]->boundarySegmentIndex == 1U &&
                      seabedEvents[1U]->boundarySegmentIndex == 2U,
                  "LL reflection events freeze the active segment index");
    context.check(
        seabedEvents[0U]->longMaterialOverride.has_value() &&
            seabedEvents[1U]->longMaterialOverride.has_value(),
        "LL reflection events freeze their raw material override");
    if (seabedEvents[0U]->longMaterialOverride.has_value() &&
        seabedEvents[1U]->longMaterialOverride.has_value()) {
      context.checkNear(
          seabedEvents[0U]
              ->longMaterialOverride->material.compressionalSoundSpeed,
          1600.0, 0.0,
          "first LL reflection freezes the first node material");
      context.checkNear(
          seabedEvents[1U]
              ->longMaterialOverride->material.compressionalSoundSpeed,
          1800.0, 0.0,
          "second LL reflection freezes the switched node material");
      context.checkNear(
          seabedEvents[1U]
              ->longMaterialOverride->attenuationEvaluationDepth,
          1.0e20, 0.0,
          "frozen LL event retains the 1D20 attenuation depth");
    }
  }

  RayPathCache cache;
  cache.append(path);
  cache.freeze();
  const auto firstProjection =
      FrequencyProjector(simulation.environment())
          .project(cache.at(0U), 1000.0, simulation.source().amplitude);
  const auto secondProjection =
      FrequencyProjector(simulation.environment())
          .project(cache.at(0U), 2000.0, simulation.source().amplitude);
  context.check(
      firstProjection.points.size() == cache.at(0U).points.size() &&
          secondProjection.points.size() == cache.at(0U).points.size(),
      "frozen parsed LL path projects independently at two frequencies");
  std::vector<double> frozenSpeeds;
  for (const auto& event : cache.at(0U).events) {
    if (event.longMaterialOverride.has_value()) {
      frozenSpeeds.push_back(
          event.longMaterialOverride->material.compressionalSoundSpeed);
    }
  }
  context.check(
      cache.at(0U).events.size() == path.events.size() &&
          frozenSpeeds == std::vector<double>{1600.0, 1800.0},
      "two-frequency projection preserves frozen LL event materials");

  for (const auto& [name, bathymetry] :
       std::array<std::pair<std::string, std::string>, 3>{
           std::pair{
               "ll_six_columns",
               "LL\n2\n-1 1000 1600 0 1.5 0.1\n"
               "2 1000 1800 0 2.0 0.2\n"},
           std::pair{
               "ll_eight_columns",
               "LL\n2\n-1 1000 1600 0 1.5 0.1 0 99\n"
               "2 1000 1800 0 2.0 0.2 0 99\n"},
           std::pair{
               "ll_elastic_material",
               "LL\n2\n-1 1000 1600 100 1.5 0.1 0\n"
               "2 1000 1800 100 2.0 0.2 0\n"}}) {
    const TemporaryLongBoundaryCase rejected(name, bathymetry);
    context.expectThrows<ValidationError>(
        [&] {
          static_cast<void>(
              EnvironmentParser::parseFile(rejected.environmentPath()));
        },
        "LL rejects malformed column counts and elastic materials");
  }
}

void testCurvilinearShortBoundaries(Context& context) {
  const TemporaryBoundaryCase canonical("canonical_c_boundary", "C");
  const ParsedEnvironment parsed =
      EnvironmentParser::parseFile(canonical.environmentPath());
  const auto& environment = parsed.simulationCase.environment();
  context.check(
      environment.seaSurface().geometry().interpolationKind() ==
          BoundaryInterpolationKind::Curvilinear,
      "canonical C short ATI selects curvilinear geometry");
  context.check(
      environment.seabed().geometry().interpolationKind() ==
          BoundaryInterpolationKind::PiecewiseLinear,
      "LS short BTY remains piecewise linear");

  for (const std::string format : {"CS", "CL"}) {
    const TemporaryBoundaryCase rejected(
        "rejected_" + format + "_boundary", format);
    context.expectThrows<ValidationError>(
        [&] {
          static_cast<void>(
              EnvironmentParser::parseFile(rejected.environmentPath()));
        },
        "noncanonical curvilinear short boundary token is rejected");
  }
}

void testGrainSizeBottom(Context& context) {
  const std::string direct =
      renderCase("constant_speed_direct", 1000.0, 300U);
  {
    std::string contents = direct;
    replaceFirst(contents, "'A' 0.0", "'G' 0.0");
    replaceFirst(contents, "1000.0  1600.0  0.0  1.8  0.0",
                 "1000.0  2.6");
    const ParsedEnvironment parsed = parseText(contents, "grain_size.env");
    const auto& seabed = parsed.simulationCase.environment().seabed();
    context.check(seabed.kind() == BoundaryKind::GrainSizeHalfSpace,
                  "G bottom selects a grain-size half-space");
    context.check(seabed.grainSizeMaterial().has_value(),
                  "G bottom retains its derived grain properties");
    if (seabed.grainSizeMaterial().has_value()) {
      const auto& grain = *seabed.grainSizeMaterial();
      context.checkNear(grain.meanGrainSize, 2.6, 0.0,
                        "G bottom retains mean grain size");
      context.checkNear(grain.soundSpeedRatio,
                        1.10143906552903359, 0.0,
                        "G bottom uses Origin sound-speed-ratio branch");
      context.checkNear(grain.densityRatio,
                        1.42501035840809331, 0.0,
                        "G bottom uses Origin density-ratio branch");
      context.checkNear(grain.attenuationCoefficient,
                        0.521499992907047294, 0.0,
                        "G bottom uses Origin attenuation branch");
    }
  }
  context.expectThrows<ValidationError>(
      [&] {
        std::string contents = direct;
        replaceFirst(contents, "'A' 0.0", "'G' 0.0");
        replaceFirst(contents, "1000.0  1600.0  0.0  1.8  0.0",
                     "999.0  2.6");
        static_cast<void>(parseText(contents, "grain_size_depth.env"));
      },
      "G bottom requires the declared half-space depth");
  context.expectThrows<ValidationError>(
      [&] {
        std::string contents = direct;
        replaceFirst(contents, "'A' 0.0", "'G' 0.0");
        replaceFirst(contents, "1000.0  1600.0  0.0  1.8  0.0",
                     "1000.0 NaN");
        static_cast<void>(parseText(contents, "grain_size_nan.env"));
      },
      "G bottom rejects non-finite mean grain size");
}

void testTabulatedReflectionBottom(Context& context) {
  const std::string direct =
      renderCase("constant_speed_direct", 1000.0, 300U);
  std::string contents = direct;
  replaceFirst(contents, "'A' 0.0", "'F' 0.0");
  replaceFirst(contents, "1000.0  1600.0  0.0  1.8  0.0", "");

  const std::filesystem::path directory =
      std::filesystem::temp_directory_path() /
      "bellhop_f2cpp_tabulated_reflection";
  std::error_code cleanupError;
  std::filesystem::remove_all(directory, cleanupError);
  std::filesystem::create_directories(directory);
  const std::filesystem::path environmentPath = directory / "case.env";
  {
    std::ofstream environmentFile(environmentPath);
    environmentFile << contents;
    std::ofstream tableFile(directory / "case.brc");
    tableFile << "3\n0 1.0 0\n30 0.5 90\n90 0.0 180\n";
  }
  const ParsedEnvironment parsed = EnvironmentParser::parseFile(environmentPath);
  const auto& seabed = parsed.simulationCase.environment().seabed();
  context.check(seabed.kind() == BoundaryKind::TabulatedReflection,
                "F bottom selects tabulated reflection");
  context.check(seabed.reflectionTable() &&
                    seabed.reflectionTable()->size() == 3U,
                "F bottom owns an immutable BRC table");
  if (seabed.reflectionTable()) {
    context.checkNear((*seabed.reflectionTable())[1U].phaseRadians,
                      std::numbers::pi / 2.0, 1.0e-15,
                      "BRC phase converts from degrees to radians");
  }
  context.expectThrows<ValidationError>(
      [&] { static_cast<void>(parseText(contents, "tabulated.env")); },
      "stream parsing rejects unresolved BRC sidecars");
  {
    std::ofstream tableFile(directory / "case.brc", std::ios::trunc);
    tableFile << "2\n30 0.5 0\n30 0.4 10\n";
  }
  context.expectThrows<ValidationError>(
      [&] { static_cast<void>(EnvironmentParser::parseFile(environmentPath)); },
      "BRC angles must be strictly increasing");
  {
    std::ofstream environmentFile(environmentPath, std::ios::trunc);
    std::string bathymetryContents = contents;
    replaceFirst(bathymetryContents, "'F' 0.0", "'F~' 0.0");
    environmentFile << bathymetryContents;
    std::ofstream tableFile(directory / "case.brc", std::ios::trunc);
    tableFile << "2\n0 1.0 0\n90 0.5 180\n";
    std::ofstream bathymetryFile(directory / "case.bty");
    bathymetryFile << "LS\n2\n0 1000\n1 1000\n";
  }
  const ParsedEnvironment withShortBathymetry =
      EnvironmentParser::parseFile(environmentPath);
  context.check(
      withShortBathymetry.simulationCase.environment().seabed().kind() ==
          BoundaryKind::TabulatedReflection,
      "F bottom composes with short-format bathymetry");
  {
    std::ofstream bathymetryFile(directory / "case.bty", std::ios::trunc);
    bathymetryFile
        << "LL\n2\n"
        << "0 1000 1600 0 1.8 0 0\n"
        << "1 1000 1700 0 2.0 0 0\n";
  }
  context.expectThrows<ValidationError>(
      [&] { static_cast<void>(EnvironmentParser::parseFile(environmentPath)); },
      "F bottom explicitly rejects long-format bathymetry materials");
  std::filesystem::remove_all(directory, cleanupError);
}

void testSourceBeamPattern(Context& context) {
  std::string contents =
      renderCase("constant_speed_direct", 1000.0, 300U);
  replaceFirst(contents, "'CC'", "'CC*'");
  const std::filesystem::path directory =
      std::filesystem::temp_directory_path() /
      "bellhop_f2cpp_source_beam_pattern";
  std::error_code cleanupError;
  std::filesystem::remove_all(directory, cleanupError);
  std::filesystem::create_directories(directory);
  const std::filesystem::path environmentPath = directory / "case.env";
  {
    std::ofstream environmentFile(environmentPath);
    environmentFile << contents;
    std::ofstream patternFile(directory / "case.sbp");
    patternFile << "3\n-30 -6\n0 0\n20 -12\n";
  }
  const ParsedEnvironment parsed =
      EnvironmentParser::parseFile(environmentPath);
  const auto& pattern = parsed.simulationCase.sourceBeamPattern();
  context.check(pattern.isDirectional() && pattern.size() == 3U,
                "CC* loads the sibling source beam pattern");
  context.checkNear(pattern.minimumAngleDegrees(), -30.0, 0.0,
                    "SBP parser retains the first angle");
  context.checkNear(pattern.maximumAngleDegrees(), 20.0, 0.0,
                    "SBP parser retains the last angle");
  context.expectThrows<ValidationError>(
      [&] { static_cast<void>(parseText(contents, "directional.env")); },
      "stream parsing rejects an unresolved SBP sidecar");
  {
    std::ofstream patternFile(directory / "case.sbp", std::ios::trunc);
    patternFile << "1\n0 0\n";
  }
  context.expectThrows<ValidationError>(
      [&] { static_cast<void>(EnvironmentParser::parseFile(environmentPath)); },
      "SBP parser rejects a single point");
  {
    std::ofstream patternFile(directory / "case.sbp", std::ios::trunc);
    patternFile << "2\n0 0\n0 -3\n";
  }
  context.expectThrows<ValidationError>(
      [&] { static_cast<void>(EnvironmentParser::parseFile(environmentPath)); },
      "SBP parser rejects duplicate angles");
  {
    std::ofstream patternFile(directory / "case.sbp", std::ios::trunc);
    patternFile << "2\n0 0\n1 NaN\n";
  }
  context.expectThrows<ValidationError>(
      [&] { static_cast<void>(EnvironmentParser::parseFile(environmentPath)); },
      "SBP parser rejects non-finite power values");
  std::filesystem::remove_all(directory, cleanupError);
}

void testQuadrilateralSsp(Context& context) {
  const std::string direct =
      renderCase("constant_speed_direct", 1000.0, 300U);
  std::string contents = direct;
  replaceFirst(contents, "'CVW'", "'QVW'");
  const std::filesystem::path directory =
      std::filesystem::temp_directory_path() / "bellhop_f2cpp_q_ssp";
  std::error_code cleanupError;
  std::filesystem::remove_all(directory, cleanupError);
  std::filesystem::create_directories(directory);
  const std::filesystem::path environmentPath = directory / "case.env";
  {
    std::ofstream environmentFile(environmentPath);
    environmentFile << contents;
    std::ofstream sspFile(directory / "case.ssp");
    sspFile << "3\n0.0 1.0 2.0\n1500 1510 1520\n"
            << "1600 1610 1620\n";
  }
  const ParsedEnvironment parsed = EnvironmentParser::parseFile(environmentPath);
  const auto& profile = parsed.simulationCase.environment().soundSpeedProfile();
  context.check(profile.interpolationKind() == SspInterpolationKind::Quadrilateral &&
                    profile.quadrilateralGrid(),
                "Q ENV retains an immutable quadrilateral SSP grid");
  if (profile.quadrilateralGrid()) {
    const auto& grid = *profile.quadrilateralGrid();
    context.check(grid.depthCount == 2U && grid.rangeCount == 3U,
                  "Q SSP retains depth and range dimensions");
    context.checkNear(grid.rangesMeters[1U], 1000.0, 0.0,
                      "Q SSP converts profile ranges from km to m");
    context.checkNear(grid.speedsDepthMajor[4U], 1610.0, 0.0,
                      "Q SSP stores rows in depth-major order");
    context.checkNear(profile.quadrilateralRealSoundSpeedAt(
                          {.range = 500.0, .depth = 500.0}),
                      1555.0, 1.0e-12,
                      "Q SSP helper bilinearly evaluates a cell");
  }
  context.expectThrows<ValidationError>(
      [&] { static_cast<void>(parseText(contents, "q_stream.env")); },
      "stream Q parsing rejects an unresolved SSP sidecar");
  std::filesystem::remove(directory / "case.ssp", cleanupError);
  context.expectThrows<BellhopError>(
      [&] { static_cast<void>(EnvironmentParser::parseFile(environmentPath)); },
      "Q parsing reports a missing SSP sidecar");
  {
    std::ofstream sspFile(directory / "case.ssp");
    sspFile << "2\n0.0 0.0\n1500 1510\n1600 1610\n";
  }
  context.expectThrows<ValidationError>(
      [&] { static_cast<void>(EnvironmentParser::parseFile(environmentPath)); },
      "Q parsing rejects non-increasing SSP ranges");
  const auto expectRejectedSsp = [&](const std::string& data,
                                     const std::string& message) {
    std::ofstream sspFile(directory / "case.ssp", std::ios::trunc);
    sspFile << data;
    sspFile.close();
    context.expectThrows<ValidationError>(
        [&] { static_cast<void>(EnvironmentParser::parseFile(environmentPath)); },
        message);
  };
  expectRejectedSsp("2\n0 1\n1500\n1600 1610\n",
                    "Q parsing rejects a short speed row");
  expectRejectedSsp("2\n0 1\n1500 1510 1520\n1600 1610\n",
                    "Q parsing rejects a long speed row");
  expectRejectedSsp("2\n0 1\n1500 0\n1600 1610\n",
                    "Q parsing rejects non-positive sound speed");
  expectRejectedSsp("2\n0 1\n1500 NaN\n1600 1610\n",
                    "Q parsing rejects non-finite sound speed");
  expectRejectedSsp("2\n0 1\n1500 1510\n1600 1610\n1\n",
                    "Q parsing rejects trailing SSP data");
  expectRejectedSsp("1000001\n",
                    "Q parsing rejects a grid above the sample limit before "
                    "allocation");
  std::filesystem::remove_all(directory, cleanupError);
}

void testUnsupportedAndMalformedInput(Context& context) {
  const std::string direct =
      renderCase("constant_speed_direct", 50.0, 300U);

  context.expectThrows<ValidationError>(
      [&] {
        std::string contents = direct;
        replaceFirst(contents, "\n1                       ! NMEDIA",
                     "\n2                       ! NMEDIA");
        static_cast<void>(
            parseText(contents, "two_media.env"));
      },
      "multiple media are explicitly rejected");
  {
    std::string contents = direct;
    replaceFirst(contents, "'CVW'", "'PVW'");
    const ParsedEnvironment parsed = parseText(contents, "pchip_ssp.env");
    context.check(
        parsed.simulationCase.environment()
                .soundSpeedProfile()
                .interpolationKind() == SspInterpolationKind::Pchip,
        "PCHIP SSP interpolation is parsed and retained");
  }
  {
    std::string contents = direct;
    replaceFirst(contents, "'CVW'", "'NVW'");
    const ParsedEnvironment parsed = parseText(contents, "n2_ssp.env");
    context.check(
        parsed.simulationCase.environment()
                .soundSpeedProfile()
                .interpolationKind() == SspInterpolationKind::N2Linear,
        "N2-linear SSP interpolation is parsed and retained");
  }

  {
    std::string contents = direct;
    replaceFirst(contents, "'CVW'", "'SVW'");
    const ParsedEnvironment parsed = parseText(contents, "spline_ssp.env");
    context.check(
        parsed.simulationCase.environment()
                .soundSpeedProfile()
                .interpolationKind() == SspInterpolationKind::CubicSpline,
        "cubic-spline SSP interpolation is parsed and retained");
  }

  for (const char interpolation : {'Q'}) {
    context.expectThrows<ValidationError>(
        [&, interpolation] {
          std::string contents = direct;
          replaceFirst(contents, "'CVW'",
                       std::string("'") + interpolation + "VW'");
          static_cast<void>(parseText(contents, "known_ssp.env"));
        },
        "recognized unsupported SSP interpolation is explicitly rejected");
  }
  context.expectThrows<ValidationError>(
      [&] {
        std::string contents = direct;
        replaceFirst(contents, "'CVW'", "'XVW'");
        static_cast<void>(parseText(contents, "unknown_ssp.env"));
      },
      "unknown SSP interpolation is explicitly rejected");
  for (const std::string option : {"CVX", "CVm"}) {
    context.expectThrows<ValidationError>(
        [&, option] {
          std::string contents = direct;
          replaceFirst(contents, "'CVW'", "'" + option + "'");
          static_cast<void>(parseText(contents, "unknown_attenuation.env"));
        },
        "unsupported attenuation unit is explicitly rejected");
  }
  context.expectThrows<ValidationError>(
      [&] {
        std::string contents = direct;
        replaceFirst(contents, "'CVW'", "'CVWX'");
        static_cast<void>(parseText(contents, "unknown_volume.env"));
      },
      "unknown volume attenuation suffix is explicitly rejected");
  context.expectThrows<ValidationError>(
      [&] {
        std::string contents = direct;
        replaceFirst(contents, "'CC'", "'UC'");
        static_cast<void>(
            parseText(contents, "unknown_mode.env"));
      },
      "unsupported run type is explicitly rejected");
  context.expectThrows<ValidationError>(
      [&] {
        std::string contents = direct;
        replaceFirst(contents, "'CC'", "'ic'");
        static_cast<void>(parseText(contents, "lowercase_mode.env"));
      },
      "lowercase TL mode is explicitly rejected");
  for (const char width : {'F', 'M', 'W'}) {
    for (const char curvature : {'D', 'S', 'Z'}) {
      std::string contents = direct;
      const std::string option{width, curvature};
      replaceFirst(contents, "'MS' 1.0  2.5",
                   "'" + option + "' 1.0  2.5");
      const ParsedEnvironment parsed =
          parseText(contents, "beam_" + option + ".env");
      const auto expectedWidth =
          width == 'F' ? bellhop::BeamWidthMode::SpaceFilling
          : width == 'M' ? bellhop::BeamWidthMode::MinimumWidth
                         : bellhop::BeamWidthMode::Wkb;
      const auto expectedCurvature =
          curvature == 'D' ? bellhop::BoundaryCurvatureMode::Double
          : curvature == 'S' ? bellhop::BoundaryCurvatureMode::Standard
                             : bellhop::BoundaryCurvatureMode::Zero;
      context.check(parsed.beam.widthMode == expectedWidth &&
                        parsed.beam.curvatureMode == expectedCurvature,
                    "all nine Cerveny width/curvature combinations parse");
    }
  }
  for (const std::string option : {"AS", "MX", "M", "MSS"}) {
    context.expectThrows<ValidationError>(
        [&, option] {
          std::string contents = direct;
          replaceFirst(contents, "'MS' 1.0  2.5",
                       "'" + option + "' 1.0  2.5");
          static_cast<void>(parseText(contents, "invalid_beam.env"));
        },
        "invalid Cerveny width/curvature token is rejected");
  }
  {
    std::string contents = direct;
    replaceFirst(
        contents, "1000.0  1600.0  0.0  1.8  0.0",
        "1000.0  2000.0  1000.0  2.0  0.5  1.0");
    const ParsedEnvironment elastic =
        parseText(contents, "elastic_bottom.env");
    const auto& material =
        *elastic.simulationCase.environment().seabed().material();
    context.checkNear(material.compressionalSoundSpeed, 2000.0, 0.0,
                      "ordinary elastic bottom retains P-wave speed");
    context.checkNear(material.shearSoundSpeed, 1000.0, 0.0,
                      "ordinary elastic bottom retains S-wave speed");
    context.checkNear(material.density, 2000.0, 0.0,
                      "ordinary elastic bottom density converts to SI");
    context.checkNear(material.compressionalAttenuation.value, 0.5, 0.0,
                      "ordinary elastic bottom retains P-wave loss");
    context.checkNear(material.shearAttenuation.value, 1.0, 0.0,
                      "ordinary elastic bottom retains S-wave loss");
    context.check(
        material.compressionalAttenuation.unit ==
                AttenuationUnit::DecibelsPerWavelength &&
            material.shearAttenuation.unit ==
                AttenuationUnit::DecibelsPerWavelength,
        "ordinary elastic bottom applies the ENV attenuation unit to P and S");
  }
  for (const auto& [name, replacement] :
       std::array<std::pair<std::string, std::string>, 3>{
           std::pair{"negative_shear_speed",
                     "1000.0  1600.0  -100.0  1.8  0.5  0.0"},
           std::pair{"negative_shear_loss",
                     "1000.0  1600.0  100.0  1.8  0.5  -0.1"},
           std::pair{"loss_without_shear_speed",
                     "1000.0  1600.0  0.0  1.8  0.5  0.1"}}) {
    context.expectThrows<ValidationError>(
        [&, name, replacement] {
          std::string contents = direct;
          replaceFirst(
              contents, "1000.0  1600.0  0.0  1.8  0.0",
              replacement);
          static_cast<void>(parseText(contents, name + ".env"));
        },
        "ordinary elastic bottom rejects invalid shear properties");
  }
  context.expectThrows<ValidationError>(
      [&] {
        std::string contents = direct;
        replaceFirst(contents, "\n51                      ! NR",
                     "\n3                       ! NR");
        replaceFirst(contents, "0.1  5.0 /",
                     "0.1  0.2  0.5 /");
        static_cast<void>(
            parseText(contents, "nonuniform_ranges.env"));
      },
      "nonuniform Cartesian receiver ranges are rejected");
  context.expectThrows<ValidationError>(
      [&] {
        std::string contents = direct;
        const std::size_t lastLine =
            contents.rfind("3  5  'P'");
        contents.erase(lastLine);
        static_cast<void>(
            parseText(contents, "truncated.env"));
      },
      "truncated input reports unexpected end of file");
  context.expectThrows<ValidationError>(
      [&] {
        std::string contents = direct;
        contents += "\nEXTRA\n";
        static_cast<void>(
            parseText(contents, "trailing.env"));
      },
      "trailing unsupported input is rejected");
  context.expectThrows<ValidationError>(
      [] {
        std::istringstream input("'unterminated\n");
        static_cast<void>(
            EnvironmentParser::parse(input, "quote.env"));
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

int main() {
  Context context;
  testDirectCase(context);
  testFieldComponents(context);
  testSourceGeometry(context);
  testTransmissionLossModes(context);
  testBeamFamilies(context);
  testMultipleSourceDepths(context);
  testIrregularReceiverGrid(context);
  testBoundaryCases(context);
  testAttenuationCases(context);
  testVolumeAttenuationCases(context);
  testMunkCase(context);
  testFortranNumericSpelling(context);
  testPiecewiseLinearShortBoundaries(context);
  testPiecewiseLinearLongMaterials(context);
  testCurvilinearShortBoundaries(context);
  testGrainSizeBottom(context);
  testTabulatedReflectionBottom(context);
  testSourceBeamPattern(context);
  testQuadrilateralSsp(context);
  testUnsupportedAndMalformedInput(context);

  if (context.failureCount() != 0) {
    std::cerr << context.failureCount()
              << " environment-parser assertion(s) failed\n";
    return 1;
  }
  std::cout << "All Bellhop F2CPP environment-parser tests passed\n";
  return 0;
}
