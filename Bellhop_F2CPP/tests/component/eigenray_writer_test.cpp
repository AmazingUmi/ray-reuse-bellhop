#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numbers>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "bellhop/field/eigenray_hit.hpp"
#include "bellhop/error.hpp"
#include "bellhop/io/eigenray_writer.hpp"
#include "bellhop/model/simulation_case.hpp"
#include "support/test_harness.hpp"

namespace {

using bellhop::AcousticMaterial;
using bellhop::BeamFamily;
using bellhop::BoundaryModel;
using bellhop::CervenyCoordinateSystem;
using bellhop::EigenrayHit;
using bellhop::EigenrayWriter;
using bellhop::Environment;
using bellhop::FrequencyGrid;
using bellhop::IntegratorSettings;
using bellhop::LaunchFan;
using bellhop::RayPath;
using bellhop::RayPathCache;
using bellhop::RayState;
using bellhop::ReceiverGrid;
using bellhop::ReflectionBoundary;
using bellhop::ReflectionEvent;
using bellhop::SimulationCase;
using bellhop::SimulationRunMode;
using bellhop::SoundSpeedPoint;
using bellhop::SoundSpeedProfile;
using bellhop::Source;
using bellhop::StepQuadrature;
using bellhop::ValidationError;
using bellhop::Vec2;
using bellhop::VolumeAttenuation;
using bellhop::VolumeAttenuationModel;
using bellhop::test::Context;

class TemporaryDirectory {
 public:
  TemporaryDirectory() {
    const auto suffix =
        std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() /
            ("bellhop_f2cpp_eigenray_writer_" + std::to_string(suffix));
    std::filesystem::create_directory(path_);
  }
  ~TemporaryDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }
  [[nodiscard]] const std::filesystem::path& path() const noexcept {
    return path_;
  }
 private:
  std::filesystem::path path_;
};

SimulationCase makeSimulation() {
  return SimulationCase(
      Environment(
          SoundSpeedProfile({SoundSpeedPoint{.depth = 0.0,
                                               .soundSpeed = 1500.0,
                                               .density = 1000.0},
                            SoundSpeedPoint{.depth = 100.0,
                                            .soundSpeed = 1500.0,
                                            .density = 1000.0}}),
          BoundaryModel::vacuum(0.0), BoundaryModel::rigid(100.0),
          VolumeAttenuation{.model = VolumeAttenuationModel::Thorp}),
      std::vector<Source>{{.depth = 25.0, .amplitude = 1.0},
                          {.depth = 75.0, .amplitude = 1.0}},
      ReceiverGrid({50.0}, {100.0}), FrequencyGrid({1000.0}),
      LaunchFan{.minimumAngle = -0.2,
                .maximumAngle = 0.2,
                .explicitLaunchAngleCount = 2U},
      IntegratorSettings{.stepLength = 5.0,
                         .rangeLimit = 100.0,
                         .depthLimit = 100.0,
                         .maximumRayPoints = 100U},
      bellhop::SourceBeamPattern::omnidirectional(),
      SimulationRunMode::Eigenray, bellhop::FieldComponent::Pressure,
      bellhop::SourceGeometry::Point, CervenyCoordinateSystem::Cartesian,
      BeamFamily::GeometricHat);
}

RayPath makePath(double launchAngle, bool reflection) {
  RayPath path;
  path.launchAngle = launchAngle;
  for (std::size_t index = 0U; index < 4U; ++index) {
    const double range = reflection && index >= 2U
                             ? 100.0 * static_cast<double>(index - 1U)
                             : 100.0 * static_cast<double>(index);
    path.points.push_back(
        RayState{.position = {.range = range,
                              .depth = 50.0},
                 .slowness = {.range = 1.0 / 1500.0, .depth = 0.0},
                 .dynamicP = {}, .dynamicQ = {100.0, 0.0},
                 .soundSpeed = 1500.0, .realTravelTime = 0.0});
  }
  path.steps.resize(reflection ? 2U : 3U);
  for (StepQuadrature& step : path.steps) {
    step.stepLength = 100.0;
    step.startWeight = 100.0;
    step.midpointWeight = 0.0;
  }
  if (reflection) {
    path.events.push_back(ReflectionEvent{
        .rayPointIndex = 1U,
        .reflectedRayPointIndex = 2U,
        .boundary = ReflectionBoundary::SeaSurface,
        .boundarySegmentIndex = 0U,
        .boundaryCurvature = 0.0,
        .position = path.points[1U].position,
        .boundaryTangent = {.range = 1.0, .depth = 0.0},
        .outwardNormal = {.range = 0.0, .depth = -1.0},
        .incidentSlowness = path.points[1U].slowness,
        .reflectedSlowness = path.points[2U].slowness,
        .tangentSlowness = path.points[1U].slowness.range,
        .normalSlowness = 0.0,
        .longMaterialOverride = std::nullopt});
  }
  return path;
}

void testPrefixesAndZeroSource(Context& context) {
  const SimulationCase simulation = makeSimulation();
  const auto& fan = simulation.launchFanPlan();
  RayPathCache first;
  first.reserve(fan.launchAngleCount);
  for (std::size_t launch = 0U; launch < fan.launchAngleCount; ++launch) {
    first.append(makePath(fan.launchAngles[launch], true));
  }
  first.freeze();
  RayPathCache second;
  second.reserve(fan.launchAngleCount);
  for (std::size_t launch = 0U; launch < fan.launchAngleCount; ++launch) {
    second.append(makePath(fan.launchAngles[launch], false));
  }
  second.freeze();

  TemporaryDirectory directory;
  const auto output = directory.path() / "case.ray";
  EigenrayWriter writer(output, "E writer", simulation);
  writer.appendHit(0U, 0U, first, first.at(0U), EigenrayHit{0U, 0U, 2U});
  writer.appendHit(0U, 0U, first, first.at(0U), EigenrayHit{0U, 0U, 3U});
  writer.appendHit(0U, 1U, first, first.at(1U), EigenrayHit{0U, 0U, 4U});
  writer.finalize();

  std::ifstream input(output);
  std::ostringstream contents;
  contents << input.rdbuf();
  const std::string text = contents.str();
  std::size_t blockLines = 0U;
  for (std::size_t offset = 0U; offset < text.size();) {
    offset = text.find('\n', offset);
    if (offset == std::string::npos) break;
    ++offset;
    ++blockLines;
  }
  context.check(text.find("1 1 2\n") != std::string::npos,
                "E header retains configured launch count");
  context.check(blockLines > 7U && text.find("'rz'\n") != std::string::npos,
                "E writes variable blocks and permits a zero-hit source");

  const auto zeroOutput = directory.path() / "zero.ray";
  EigenrayWriter zeroWriter(zeroOutput, "zero E writer", simulation);
  zeroWriter.finalize();
  context.check(std::filesystem::exists(zeroOutput),
                "E publishes a header-only zero-hit product");
  context.expectThrows<ValidationError>(
      [&] {
        EigenrayWriter invalid(directory.path() / "invalid.ray", "bad",
                               simulation);
        invalid.appendHit(0U, 1U, first, first.at(1U),
                          EigenrayHit{0U, 0U, 1U});
      },
      "E rejects a prefix shorter than two points without publication");
}

}  // namespace

int main() {
  Context context;
  testPrefixesAndZeroSource(context);
  if (context.failureCount() != 0) {
    std::cerr << context.failureCount() << " eigenray writer assertion(s) failed\n";
    return 1;
  }
  std::cout << "All eigenray writer tests passed\n";
  return 0;
}
