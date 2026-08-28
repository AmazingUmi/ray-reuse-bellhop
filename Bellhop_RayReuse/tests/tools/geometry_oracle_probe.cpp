#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

#include "rayreuse/model/environment.hpp"
#include "rayreuse/model/simulation_case.hpp"
#include "rayreuse/ray/geometry_tracer.hpp"
#include "support/munk_case_fixture.hpp"

namespace {

const char* terminationName(rayreuse::RayTerminationReason reason) {
  switch (reason) {
    case rayreuse::RayTerminationReason::ExitedDomain:
      return "ExitedDomain";
    case rayreuse::RayTerminationReason::NumericalFailure:
      return "NumericalFailure";
    case rayreuse::RayTerminationReason::PointLimit:
      return "PointLimit";
  }
  return "Unknown";
}

void writeManifest(const std::filesystem::path& csvPath, double launchAngle,
                   const std::string& configuration,
                   const rayreuse::RayPath& path) {
  const std::filesystem::path manifestPath(csvPath.string() + ".manifest.json");
  std::ofstream output(manifestPath);
  if (!output) {
    throw std::runtime_error("cannot open probe manifest");
  }
  output << std::scientific << std::setprecision(17) << "{\n"
         << "  \"schema\": \"bellhop.cpp.ray_path_probe\",\n"
         << "  \"schema_version\": 1,\n"
         << "  \"contract_version\": 1,\n"
         << "  \"producer\": \"rayreuse\",\n"
         << "  \"status\": \"complete\",\n"
         << "  \"configuration\": \"" << configuration << "\",\n"
         << "  \"launch_angle_rad\": " << launchAngle << ",\n"
         << "  \"points_file\": \"" << csvPath.filename().string() << "\",\n"
         << "  \"point_count\": " << path.points.size() << ",\n"
         << "  \"integrated_step_count\": " << path.steps.size() << ",\n"
         << "  \"reflection_event_count\": " << path.events.size() << ",\n"
         << "  \"termination\": \"" << terminationName(path.terminationReason)
         << "\",\n"
         << "  \"index_base\": 1,\n"
         << "  \"numeric_precision\": \"binary64\",\n"
         << "  \"units\": \"SI\",\n"
         << "  \"columns\": [\n"
         << "    \"point_index\", \"point_kind\", \"step_valid\", "
            "\"incoming_step_index\",\n"
         << "    \"r_m\", \"z_m\", \"t_r_s_per_m\", \"t_z_s_per_m\",\n"
         << "    \"p1\", \"p2\", \"q1\", \"q2\", \"c_m_per_s\", "
            "\"tau_real_s\",\n"
         << "    \"num_top_bounces\", \"num_bottom_bounces\", \"h_m\", "
            "\"hw0_m\", \"hw1_m\",\n"
         << "    \"mid_r_m\", \"mid_z_m\"\n"
         << "  ]\n"
         << "}\n";
  if (!output) {
    throw std::runtime_error("failed while writing probe manifest");
  }
}

rayreuse::Environment makeI5QuadrilateralEnvironment() {
  const auto grid = std::make_shared<const rayreuse::QuadrilateralSspGrid>(
      rayreuse::QuadrilateralSspGrid{
          .rangesMeters = {0.0, 350.0, 800.0},
          .speedsDepthMajor = {1500.0, 1540.0, 1580.0,
                               1500.0, 1520.0, 1540.0},
          .depthCount = 2U,
          .rangeCount = 3U});
  return rayreuse::Environment(
      rayreuse::SoundSpeedProfile(
          {{.depth = 0.0, .soundSpeed = 1500.0, .density = 1000.0},
           {.depth = 100.0,
            .soundSpeed = 1500.0,
            .density = 1000.0}},
          rayreuse::SspInterpolationKind::Quadrilateral, grid),
      rayreuse::BoundaryModel::vacuum(0.0),
      rayreuse::BoundaryModel::rigid(100.0));
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc != 3 && argc != 4 && argc != 7) {
    std::cerr << "usage: geometry_oracle_probe OUTPUT_CSV "
                 "LAUNCH_ANGLE_RAD "
                 "[munk | munk-n2 | munk-pchip | munk-spline | "
                 "i5-quadrilateral | "
                 "BOTTOM_DEPTH SOURCE_DEPTH "
                 "DEPTH_LIMIT MAX_POINTS]\n";
    return 2;
  }

  const std::string namedConfiguration = argc == 4 ? argv[3] : "";
  const bool useMunkConfiguration =
      namedConfiguration == "munk" || namedConfiguration == "munk-n2" ||
      namedConfiguration == "munk-pchip" ||
      namedConfiguration == "munk-spline";
  const bool useI5Quadrilateral =
      namedConfiguration == "i5-quadrilateral";
  const bool useN2 = namedConfiguration == "munk-n2";
  const bool usePchip = namedConfiguration == "munk-pchip";
  const bool useSpline = namedConfiguration == "munk-spline";
  if (argc == 4 && !useMunkConfiguration && !useI5Quadrilateral) {
    std::cerr << "unknown probe configuration: " << argv[3] << '\n';
    return 2;
  }
  double launchAngle = 0.0;
  double bottomDepth = useMunkConfiguration ? 5000.0
                       : (useI5Quadrilateral ? 100.0 : 1000.0);
  double sourceDepth = useMunkConfiguration ? 1000.0
                       : (useI5Quadrilateral ? 50.0 : 500.0);
  double depthLimit = useMunkConfiguration ? 5500.0
                      : (useI5Quadrilateral ? 101.0 : 1100.0);
  double stepLength = useMunkConfiguration ? 500.0
                      : (useI5Quadrilateral ? 1.0 : 10.0);
  double rangeLimit = useMunkConfiguration ? 101000.0
                      : (useI5Quadrilateral ? 710.0 : 5100.0);
  std::size_t maximumRayPoints = 10000U;
  try {
    std::size_t parsedCharacters = 0U;
    launchAngle = std::stod(argv[2], &parsedCharacters);
    if (parsedCharacters != std::string(argv[2]).size()) {
      throw std::invalid_argument("trailing characters");
    }
    if (argc == 7) {
      bottomDepth = std::stod(argv[3]);
      sourceDepth = std::stod(argv[4]);
      depthLimit = std::stod(argv[5]);
      const unsigned long long requestedMaximum = std::stoull(argv[6]);
      if (requestedMaximum > std::numeric_limits<std::size_t>::max()) {
        throw std::out_of_range("MAX_POINTS exceeds size_t");
      }
      maximumRayPoints = static_cast<std::size_t>(requestedMaximum);
    }
  } catch (const std::exception& error) {
    std::cerr << "invalid probe argument: " << error.what() << '\n';
    return 2;
  }

  const rayreuse::Environment environment =
      useMunkConfiguration
          ? rayreuse::test::makeMunkEnvironment(
                usePchip
                    ? rayreuse::SspInterpolationKind::Pchip
                    : (useSpline
                           ? rayreuse::SspInterpolationKind::CubicSpline
                           : (useN2 ? rayreuse::SspInterpolationKind::N2Linear
                                    : rayreuse::SspInterpolationKind::CLinear)))
          : (useI5Quadrilateral
                 ? makeI5QuadrilateralEnvironment()
                 : rayreuse::Environment(
                       rayreuse::SoundSpeedProfile(
                           {{.depth = 0.0,
                             .soundSpeed = 1500.0,
                             .density = 1000.0},
                            {.depth = bottomDepth,
                             .soundSpeed = 1500.0,
                             .density = 1000.0}}),
                       rayreuse::BoundaryModel::vacuum(0.0),
                       rayreuse::BoundaryModel::rigid(bottomDepth)));
  const rayreuse::GeometryTracer tracer(
      environment,
      rayreuse::IntegratorSettings{.stepLength = stepLength,
                                   .rangeLimit = rangeLimit,
                                   .depthLimit = depthLimit,
                                   .maximumRayPoints = maximumRayPoints});
  const rayreuse::RayPath path =
      tracer.trace(rayreuse::Source{.depth = sourceDepth}, launchAngle);

  std::ofstream output(argv[1]);
  if (!output) {
    std::cerr << "cannot open output CSV\n";
    return 2;
  }
  output << "point_index,point_kind,step_valid,incoming_step_index,"
            "r_m,z_m,t_r_s_per_m,t_z_s_per_m,"
            "p1,p2,q1,q2,c_m_per_s,tau_real_s,"
            "num_top_bounces,num_bottom_bounces,"
            "h_m,hw0_m,hw1_m,mid_r_m,mid_z_m\n";
  output << std::scientific << std::setprecision(17);
  std::size_t eventIndex = 0U;
  std::size_t stepIndex = 0U;
  std::size_t topBounces = 0U;
  std::size_t bottomBounces = 0U;
  for (std::size_t index = 0U; index < path.points.size(); ++index) {
    const rayreuse::RayState& point = path.points[index];
    rayreuse::StepQuadrature step;
    const char* pointKind = "source";
    std::size_t stepValid = 0U;
    std::size_t incomingStepIndex = 0U;
    if (index > 0U && eventIndex < path.events.size() &&
        path.events[eventIndex].rayPointIndex + 1U == index) {
      if (path.events[eventIndex].boundary ==
          rayreuse::ReflectionBoundary::SeaSurface) {
        pointKind = "top_reflection";
        ++topBounces;
      } else {
        pointKind = "bottom_reflection";
        ++bottomBounces;
      }
      ++eventIndex;
    } else if (index > 0U) {
      pointKind = "integrated";
      step = path.steps.at(stepIndex);
      ++stepIndex;
      stepValid = 1U;
      incomingStepIndex = stepIndex;
    }
    output << index + 1U << ',' << pointKind << ',' << stepValid << ','
           << incomingStepIndex << ',' << point.position.range << ','
           << point.position.depth << ',' << point.slowness.range << ','
           << point.slowness.depth << ',' << point.dynamicP[0] << ','
           << point.dynamicP[1] << ',' << point.dynamicQ[0] << ','
           << point.dynamicQ[1] << ',' << point.soundSpeed << ','
           << point.realTravelTime << ',' << topBounces << ',' << bottomBounces
           << ',' << step.stepLength << ',' << step.startWeight << ','
           << step.midpointWeight << ',' << step.midpoint.range << ','
           << step.midpoint.depth << '\n';
  }
  if (!output) {
    std::cerr << "failed while writing output CSV\n";
    return 2;
  }
  output.close();
  try {
    writeManifest(argv[1], launchAngle,
                  namedConfiguration.empty()
                      ? (argc == 7 ? "flat-boundary-custom" : "direct")
                      : namedConfiguration,
                  path);
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 2;
  }

  std::cout << path.points.size() << ',' << path.steps.size() << ','
            << terminationName(path.terminationReason) << '\n';
  return 0;
}
