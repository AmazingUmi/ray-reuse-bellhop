#pragma once

#include <filesystem>
#include <iosfwd>
#include <string>

#include "bellhop/field/cartesian_cerveny_influence.hpp"
#include "bellhop/model/simulation_case.hpp"

namespace bellhop {

struct CartesianCervenyInput {
  double epsilonMultiplier{};
  double loopRange{};
  CartesianCervenySettings influence;
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
