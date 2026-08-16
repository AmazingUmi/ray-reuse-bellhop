#include "rayreuse/io/ray_writer.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "rayreuse/cache/ray_path_cache.hpp"
#include "rayreuse/error.hpp"
#include "rayreuse/io/ray_prefix_writer.hpp"
#include "rayreuse/model/environment.hpp"
#include "rayreuse/model/simulation_case.hpp"
#include "rayreuse/ray/geometry_tracer.hpp"
#include "rayreuse/ray/ray_path.hpp"
#include "rayreuse/solver/ray_trace_product.hpp"
#include "support/test_harness.hpp"

namespace {

using rayreuse::BoundaryModel;
using rayreuse::Environment;
using rayreuse::FrequencyGrid;
using rayreuse::GeometryTracer;
using rayreuse::IntegratorSettings;
using rayreuse::LaunchFan;
using rayreuse::RayFrequencyState;
using rayreuse::RayPath;
using rayreuse::RayPathCache;
using rayreuse::RayWriter;
using rayreuse::readSourceBeamPattern;
using rayreuse::ReceiverGrid;
using rayreuse::SimulationCase;
using rayreuse::SimulationRunMode;
using rayreuse::SoundSpeedPoint;
using rayreuse::SoundSpeedProfile;
using rayreuse::SourceBeamPattern;
using rayreuse::traceRayProduct;
using rayreuse::ValidationError;
using rayreuse::test::Context;

SimulationCase makeSimulation(
    std::vector<double> frequencies, bool directional = false,
    SimulationRunMode runMode = SimulationRunMode::RayTrace,
    double minimumAngle = -0.5, double maximumAngle = 0.5,
    std::optional<std::size_t> explicitCount = 50U) {
  constexpr double kBottom = 100.0;
  Environment environment(
      SoundSpeedProfile(
          {{.depth = 0.0, .soundSpeed = 1500.0, .density = 1000.0},
           {.depth = kBottom, .soundSpeed = 1500.0, .density = 1000.0}}),
      BoundaryModel::vacuum(0.0), BoundaryModel::rigid(kBottom));
  SourceBeamPattern sourceBeamPattern =
      directional ? SourceBeamPattern::directional(
                        {{-180.0, -20.0}, {30.0, -10.0}, {180.0, -30.0}})
                  : SourceBeamPattern::omnidirectional();
  return SimulationCase(
      std::move(environment), {.depth = 50.0, .amplitude = 1.0},
      ReceiverGrid({50.0}, {1000.0}), FrequencyGrid(std::move(frequencies)),
      LaunchFan{.minimumAngle = minimumAngle,
                .maximumAngle = maximumAngle,
                .explicitLaunchAngleCount = explicitCount},
      IntegratorSettings{.stepLength = 10.0,
                         .rangeLimit = 1000.0,
                         .depthLimit = 1000.0,
                         .maximumRayPoints = 10000U},
      std::move(sourceBeamPattern), runMode);
}

void testDirectionalPattern(Context& context) {
  const SourceBeamPattern pattern = SourceBeamPattern::directional(
      {{-180.0, -20.0}, {30.0, -10.0}, {180.0, -30.0}});
  context.check(pattern.isDirectional(), "directional SBP remains marked");
  context.checkNear(
      pattern.amplitudeForLaunchAngle(30.0 * 3.141592653589793 / 180.0),
      0.31622776601683794, 1.0e-15, "SBP converts dB power to amplitude");
  context.checkNear(pattern.amplitudeForLaunchAngle(0.0), 0.28533808515728964,
                    1.0e-15, "SBP linearly interpolates in angle");

  const std::filesystem::path sbp =
      std::filesystem::temp_directory_path() / "rayreuse_rrb2_pattern.sbp";
  {
    std::ofstream output(sbp);
    output << "3 ! count\n-180 -20\n30 -10 ! middle\n180 -30\n";
  }
  const SourceBeamPattern parsed = readSourceBeamPattern(sbp);
  context.check(parsed.isDirectional() && parsed.size() == 3U,
                "independent SBP reader returns shared SourceBeamPattern");
  std::error_code ignored;
  std::filesystem::remove(sbp, ignored);
}

void testActiveTerminalPrefix(Context& context) {
  RayPath path;
  path.points.resize(4U);
  path.events.push_back({.rayPointIndex = 1U, .reflectedRayPointIndex = 2U});
  RayFrequencyState state;
  state.points.resize(4U);
  state.points[0U].active = true;
  state.points[1U].active = false;
  state.points[2U].active = false;
  state.points[3U].active = false;
  context.check(
      rayreuse::ray_output_detail::terminalPrefixPointCount(path, state) == 3U,
      "R prefix retains the first inactive post-reflection terminal point");
}

void testExplicitOneRayPlan(Context& context) {
  const double angle = 30.0 * 3.141592653589793 / 180.0;
  const SimulationCase single = makeSimulation(
      {1000.0}, false, SimulationRunMode::RayTrace, angle, angle, 1U);
  context.check(single.launchFanPlan().launchAngles.size() == 1U &&
                    single.launchFanPlan().launchAngles.front() == angle &&
                    single.launchFanPlan().launchAngleStep == 0.0,
                "R-only Nalpha=1 produces one angle and zero step");
  const SimulationCase defaultCount = makeSimulation(
      {1000.0}, false, SimulationRunMode::RayTrace, -0.1, 0.1, std::nullopt);
  context.check(defaultCount.launchFanPlan().launchAngleCount == 50U,
                "R-only omitted Nalpha selects the Origin default of 50");
  context.expectThrows<ValidationError>(
      [angle] {
        static_cast<void>(makeSimulation(
            {1000.0}, false, SimulationRunMode::Coherent, angle, angle, 1U));
      },
      "non-R products still reject a zero-width launch interval");
}

void testExplicitFrequencyAndMultiFrequencyRejection(Context& context) {
  const SimulationCase simulation = makeSimulation({1000.0});
  const std::filesystem::path output =
      std::filesystem::temp_directory_path() / "rayreuse_rrb2_writer.ray";
  std::error_code ignored;
  std::filesystem::remove(output, ignored);
  {
    RayWriter writer(output, "RR-B2 writer", simulation, 1000.0);
    context.expectThrows<ValidationError>(
        [&writer] { writer.finalize(); },
        "R writer requires a complete appended launch fan");
  }
  context.check(!std::filesystem::exists(output),
                "failed R writer leaves no published file");

  const SimulationCase multiFrequency = makeSimulation({500.0, 1000.0});
  context.expectThrows<ValidationError>(
      [&] {
        const RayWriter rejected(output, "multi-frequency R", multiFrequency,
                                 1000.0);
        static_cast<void>(rejected);
      },
      "multi-frequency R output is explicitly rejected");
  const SimulationCase wrongMode =
      makeSimulation({1000.0}, false, SimulationRunMode::Coherent);
  context.expectThrows<ValidationError>(
      [&] {
        const RayWriter rejected(output, "wrong mode", wrongMode, 1000.0);
        static_cast<void>(rejected);
      },
      "R writer rejects a non-ray-trace run mode");
}

void testRayFile(Context& context) {
  const double angle = 30.0 * 3.141592653589793 / 180.0;
  const SimulationCase simulation = makeSimulation(
      {1000.0}, true, SimulationRunMode::RayTrace, angle, angle, 1U);
  const RayPathCache cache = traceRayProduct(simulation);
  const std::filesystem::path output =
      std::filesystem::temp_directory_path() / "rayreuse_rrb2_writer.ray";
  std::error_code ignored;
  std::filesystem::remove(output, ignored);
  {
    RayWriter writer(output, "RR-B2 directional", simulation, 1000.0);
    writer.append(cache);
    writer.finalize();
  }
  std::ifstream input(output);
  std::ostringstream contents;
  contents << input.rdbuf();
  const std::string text = contents.str();
  context.check(text.find("RR-B2 directional") != std::string::npos,
                "R writer emits the title");
  context.check(text.find("1000") != std::string::npos,
                "R writer emits the explicitly selected frequency");
  context.check(!text.empty() && text.find("'rz'") != std::string::npos,
                "R writer emits the canonical 2-D header");
  std::filesystem::remove(output, ignored);
}

}  // namespace

int main() {
  Context context;
  testDirectionalPattern(context);
  testActiveTerminalPrefix(context);
  testExplicitOneRayPlan(context);
  testExplicitFrequencyAndMultiFrequencyRejection(context);
  testRayFile(context);
  if (context.failureCount() != 0) {
    std::cerr << context.failureCount()
              << " RR-B2 ray writer assertion(s) failed\n";
    return 1;
  }
  std::cout << "All RayReuse RR-B2 ray writer tests passed\n";
  return 0;
}
