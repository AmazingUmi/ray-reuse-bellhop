#pragma once

#include <filesystem>
#include <string_view>

#include "rayreuse/field/arrival_workspace.hpp"
#include "rayreuse/model/simulation_case.hpp"

namespace rayreuse {

enum class ArrivalEncoding { Ascii, Binary };

class ArrivalWriter {
 public:
  static void write(const std::filesystem::path& path, std::string_view title,
                    const SimulationCase& simulation,
                    const ArrivalWorkspace& workspace,
                    ArrivalEncoding encoding = ArrivalEncoding::Ascii);
};

}  // namespace rayreuse
