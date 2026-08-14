#pragma once

#include <filesystem>
#include <iosfwd>
#include <string>

#include "bellhop/field/cartesian_cerveny_influence.hpp"
#include "bellhop/field/beam_epsilon.hpp"
#include "bellhop/model/simulation_case.hpp"
#include "bellhop/ray/flat_boundary_reflection.hpp"

namespace bellhop {

struct CartesianCervenyInput {
  BeamWidthMode widthMode{BeamWidthMode::MinimumWidth};
  BoundaryCurvatureMode curvatureMode{BoundaryCurvatureMode::Standard};
  double epsilonMultiplier{};
  double loopRange{};
  CartesianCervenySettings influence;
  BeamFamily family{BeamFamily::CervenyGaussian};
};

struct ParsedEnvironment {
  std::string title;
  SimulationCase simulationCase;
  CartesianCervenyInput beam;
};

class EnvironmentParser {
 public:
  [[nodiscard]] static ParsedEnvironment parse(
      std::istream& input,
      std::string sourceName = "<stream>");

  [[nodiscard]] static ParsedEnvironment parseFile(
      const std::filesystem::path& path);
};

}  // namespace bellhop
