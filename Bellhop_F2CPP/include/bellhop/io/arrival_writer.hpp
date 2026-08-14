#pragma once

#include <cstddef>
#include <filesystem>
#include <fstream>

#include "bellhop/field/arrival_workspace.hpp"
#include "bellhop/io/output_layout.hpp"
#include "bellhop/model/simulation_case.hpp"

namespace bellhop {

enum class ArrivalEncoding {
  Ascii,
  Binary,
};

// Streams one source-local ArrivalWorkspace at a time.  The writer owns only
// the temporary output and never mutates a workspace.
class ArrivalWriter {
 public:
  ArrivalWriter(std::filesystem::path outputPath,
                const SimulationCase& simulation);
  ArrivalWriter(std::filesystem::path outputPath,
                const SimulationCase& simulation, ArrivalEncoding encoding);
  ArrivalWriter(const ArrivalWriter&) = delete;
  ArrivalWriter& operator=(const ArrivalWriter&) = delete;
  ~ArrivalWriter();

  void appendSource(std::size_t sourceIndex,
                    const ArrivalWorkspace& workspace);
  void finalize();

  [[nodiscard]] const Arrival2DLayout& layout() const noexcept {
    return layout_;
  }
  [[nodiscard]] ArrivalEncoding encoding() const noexcept {
    return encoding_;
  }

 private:
  void writeAsciiHeader();
  void writeBinaryHeader();
  void writeAsciiSource(const ArrivalWorkspace& workspace);
  void writeBinarySource(const ArrivalWorkspace& workspace);

  std::filesystem::path outputPath_;
  std::filesystem::path temporaryPath_;
  const SimulationCase& simulation_;
  ArrivalEncoding encoding_;
  Arrival2DLayout layout_;
  std::ofstream output_;
  std::size_t nextSourceIndex_{};
  bool finalized_{};
};

}  // namespace bellhop
