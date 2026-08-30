#pragma once

#include <cstddef>
#include <filesystem>
#include <span>
#include <string_view>

#include "rayreuse/field/arrival_workspace.hpp"
#include "rayreuse/model/simulation_case.hpp"

namespace rayreuse {

enum class ArrivalEncoding { Ascii, Binary };

class ArrivalWriter {
 public:
  // Single-source entry. Requires simulation.sourceCount() == 1; multi-source
  // simulations must use the per-source overload below.
  static void write(const std::filesystem::path& path, std::string_view title,
                    const SimulationCase& simulation,
                    const ArrivalWorkspace& workspace,
                    ArrivalEncoding encoding = ArrivalEncoding::Ascii);

  // Per-source entry (F2CPP `ArrivalWriter` append-source shape, batch form).
  // The file header carries the source count and every source depth; the body
  // holds one block per source in SimulationCase::sources() order (depth
  // ascending), each block covering receiversPerRange x rangeCount cells.
  static void write(const std::filesystem::path& path, std::string_view title,
                    const SimulationCase& simulation,
                    std::span<const ArrivalWorkspace> sourceWorkspaces,
                    ArrivalEncoding encoding = ArrivalEncoding::Ascii);
};

}  // namespace rayreuse
