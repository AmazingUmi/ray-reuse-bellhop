#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

#include "bellhop/model/environment.hpp"
#include "bellhop/model/simulation_case.hpp"
#include "bellhop/ray/geometry_tracer.hpp"
#include "support/munk_case_fixture.hpp"

namespace {

const char* terminationName(bellhop::RayTerminationReason reason) {
  switch (reason) {
    case bellhop::RayTerminationReason::ExitedDomain:
      return "ExitedDomain";
    case bellhop::RayTerminationReason::NumericalFailure:
      return "NumericalFailure";
    case bellhop::RayTerminationReason::PointLimit:
      return "PointLimit";
  }
  return "Unknown";
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc != 3 && argc != 4 && argc != 7) {
    std::cerr << "usage: geometry_oracle_probe OUTPUT_CSV "
                 "LAUNCH_ANGLE_RAD "
                 "[munk | BOTTOM_DEPTH SOURCE_DEPTH DEPTH_LIMIT "
                 "MAX_POINTS]\n";
    return 2;
  }

  const bool useMunkConfiguration =
      argc == 4 && std::string(argv[3]) == "munk";
  if (argc == 4 && !useMunkConfiguration) {
    std::cerr << "unknown probe configuration: " << argv[3] << '\n';
    return 2;
  }
  double launchAngle = 0.0;
  double bottomDepth = useMunkConfiguration ? 5000.0 : 1000.0;
  double sourceDepth = useMunkConfiguration ? 1000.0 : 500.0;
  double depthLimit = useMunkConfiguration ? 5500.0 : 1100.0;
  double stepLength = useMunkConfiguration ? 500.0 : 10.0;
  double rangeLimit = useMunkConfiguration ? 101000.0 : 5100.0;
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
      if (requestedMaximum >
          std::numeric_limits<std::size_t>::max()) {
        throw std::out_of_range("MAX_POINTS exceeds size_t");
      }
      maximumRayPoints =
          static_cast<std::size_t>(requestedMaximum);
    }
  } catch (const std::exception& error) {
    std::cerr << "invalid probe argument: " << error.what() << '\n';
    return 2;
  }

  const bellhop::Environment environment =
      useMunkConfiguration
          ? bellhop::test::makeMunkEnvironment()
          : bellhop::Environment(
                bellhop::SoundSpeedProfile(
                    {{.depth = 0.0,
                      .soundSpeed = 1500.0,
                      .density = 1000.0},
                     {.depth = bottomDepth,
                      .soundSpeed = 1500.0,
                      .density = 1000.0}}),
                bellhop::BoundaryModel::vacuum(0.0),
                bellhop::BoundaryModel::rigid(bottomDepth));
  const bellhop::GeometryTracer tracer(
      environment,
      bellhop::IntegratorSettings{.stepLength = stepLength,
                                  .rangeLimit = rangeLimit,
                                  .depthLimit = depthLimit,
                                  .maximumRayPoints = maximumRayPoints});
  const bellhop::RayPath path =
      tracer.trace(bellhop::Source{.depth = sourceDepth}, launchAngle);

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
    const bellhop::RayState& point = path.points[index];
    bellhop::StepQuadrature step;
    const char* pointKind = "source";
    std::size_t stepValid = 0U;
    std::size_t incomingStepIndex = 0U;
    if (index > 0U && eventIndex < path.events.size() &&
        path.events[eventIndex].rayPointIndex + 1U == index) {
      if (path.events[eventIndex].boundary ==
          bellhop::ReflectionBoundary::SeaSurface) {
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
           << point.realTravelTime << ',' << topBounces << ','
           << bottomBounces << ',' << step.stepLength << ','
           << step.startWeight << ',' << step.midpointWeight << ','
           << step.midpoint.range << ',' << step.midpoint.depth << '\n';
  }
  if (!output) {
    std::cerr << "failed while writing output CSV\n";
    return 2;
  }

  std::cout << path.points.size() << ',' << path.steps.size() << ','
            << terminationName(path.terminationReason) << '\n';
  return 0;
}
