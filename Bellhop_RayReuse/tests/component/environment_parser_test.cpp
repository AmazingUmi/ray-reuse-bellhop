#include "rayreuse/io/environment_parser.hpp"

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "rayreuse/error.hpp"
#include "support/test_harness.hpp"

namespace {

using rayreuse::AttenuationUnit;
using rayreuse::BellhopError;
using rayreuse::BoundaryKind;
using rayreuse::EnvironmentParser;
using rayreuse::ParsedEnvironment;
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
  context.expectThrows<ValidationError>(
      [&] {
        std::string contents = direct;
        replaceFirst(contents, "'CC'", "'IC'");
        static_cast<void>(parseText(contents, "incoherent.env"));
      },
      "unsupported run type is explicitly rejected");
  context.expectThrows<ValidationError>(
      [&] {
        std::string contents = direct;
        replaceFirst(contents, "'MS' 1.0  2.5", "'FS' 1.0  2.5");
        static_cast<void>(parseText(contents, "filling_beam.env"));
      },
      "unsupported beam-width mode is explicitly rejected");
  context.expectThrows<ValidationError>(
      [&] {
        std::string contents = direct;
        replaceFirst(contents, "1000.0  1600.0  0.0  1.8",
                     "1000.0  1600.0  100.0  1.8");
        static_cast<void>(parseText(contents, "elastic_bottom.env"));
      },
      "elastic half-space is explicitly rejected");
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

int main() {
  Context context;
  testDirectCase(context);
  testFrequencyOverride(context);
  testEnvironmentFrequencyList(context);
  testBoundaryCases(context);
  testAttenuationCases(context);
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
