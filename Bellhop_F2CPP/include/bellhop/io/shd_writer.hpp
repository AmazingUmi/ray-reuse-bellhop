#pragma once

#include <filesystem>
#include <string_view>

#include "bellhop/field/frequency_workspace.hpp"
#include "bellhop/model/simulation_case.hpp"

namespace bellhop {

class ShdWriter {
 public:
  static void writeSingleFrequency(
      const std::filesystem::path& path,
      std::string_view title,
      const SimulationCase& simulation,
      const FrequencyWorkspace& workspace);
};

}  // namespace bellhop
