#pragma once

#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

#include "broadband/field/cartesian_cerveny_influence.hpp"
#include "broadband/model/simulation_case.hpp"

namespace broadband {

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
      std::istream& input, std::string sourceName = "<stream>",
      std::optional<std::vector<double>> frequencyOverrideHz = std::nullopt);

  [[nodiscard]] static ParsedEnvironment parseFile(
      const std::filesystem::path& path,
      std::optional<std::vector<double>> frequencyOverrideHz = std::nullopt);
};

}  // namespace broadband
