#include "rayreuse/io/arrival_writer.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numbers>
#include <exception>
#include <span>
#include <string>
#include <system_error>
#include <vector>

#include "rayreuse/error.hpp"

namespace rayreuse {
namespace {
void checkEncoding(const SimulationCase& simulation, ArrivalEncoding encoding) {
  const SimulationRunMode requiredMode =
      encoding == ArrivalEncoding::Ascii ? SimulationRunMode::AsciiArrivals
                                         : SimulationRunMode::BinaryArrivals;
  if (simulation.runMode() != requiredMode) {
    throw ValidationError(
        "ARR encoding does not match the simulation run mode");
  }
}

void checkFrequency(const SimulationCase& simulation, double frequency) {
  if (frequency <= 0.0 ||
      std::find(simulation.frequencies().values().begin(),
                simulation.frequencies().values().end(),
                frequency) == simulation.frequencies().values().end()) {
    throw ValidationError("ARR workspace metadata does not match simulation");
  }
}
float sourceScale(SourceGeometry geometry, double range) {
  if (!std::isfinite(range) || range < 0.0)
    throw ValidationError("ARR receiver range must be finite and non-negative");
  if (geometry == SourceGeometry::Line) {
    return static_cast<float>(4.0 * std::sqrt(std::numbers::pi));
  }
  return range == 0.0 ? 1.0e5F : static_cast<float>(1.0 / std::sqrt(range));
}
template <typename WorkspaceView>
void writeAsciiSourceBlock(std::ofstream& output,
                           const SimulationCase& simulation,
                           const WorkspaceView& workspace) {
  std::size_t maximum = 0U;
  for (std::size_t cell = 0U; cell < workspace.receiverCellCount(); ++cell)
    maximum = std::max(maximum, workspace.cellAt(cell).size());
  output << std::setprecision(std::numeric_limits<float>::max_digits10)
         << maximum << '\n';
  const float radiansToDegrees = static_cast<float>(180.0 / std::numbers::pi);
  for (std::size_t d = 0U; d < workspace.depthCount(); ++d)
    for (std::size_t r = 0U; r < workspace.rangeCount(); ++r) {
      const auto arrivals = workspace.arrivalsAt(d, r);
      const float scale = sourceScale(simulation.sourceGeometry(),
                                      simulation.receivers().ranges()[r]);
      output << arrivals.size() << '\n';
      for (const Arrival& a : arrivals)
        output << scale * a.amplitude << ' '
               << radiansToDegrees * a.phaseRadians << ' '
               << a.delaySeconds.real() << ' ' << a.delaySeconds.imag() << ' '
               << a.sourceDeclinationDegrees << ' '
               << a.receiverDeclinationDegrees << ' ' << a.topBounceCount << ' '
               << a.bottomBounceCount << '\n';
    }
}
void writeAsciiHeader(std::ofstream& output, std::string_view title,
                      const SimulationCase& simulation, double frequency) {
  static_cast<void>(title);
  // Origin ArrMod/ReadEnvironmentBell header: '2D', frequency, then one line
  // per axis holding the count followed by the values (NSz Sz(1:NSz) here).
  output << "'2D'\n"
         << std::setprecision(std::numeric_limits<double>::max_digits10)
         << frequency << '\n'
         << simulation.sourceCount();
  for (const Source& source : simulation.sources())
    output << ' ' << source.depth;
  output << '\n' << simulation.receivers().depthCount();
  for (double d : simulation.receivers().depths()) output << ' ' << d;
  output << '\n' << simulation.receivers().rangeCount();
  for (double r : simulation.receivers().ranges()) output << ' ' << r;
  output << '\n';
}
void store32(std::vector<std::byte>& bytes, std::size_t offset,
             std::uint32_t value) {
  for (std::size_t i = 0U; i < 4U; ++i)
    bytes.at(offset + i) = static_cast<std::byte>((value >> (8U * i)) & 0xffU);
}
void record(std::ofstream& output, const std::vector<std::byte>& payload) {
  if (payload.size() >
      static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
    throw ValidationError("ARR binary record is too large");
  const auto size = static_cast<std::uint32_t>(payload.size());
  std::vector<std::byte> marker(4U);
  store32(marker, 0U, size);
  output.write(reinterpret_cast<const char*>(marker.data()), 4);
  output.write(reinterpret_cast<const char*>(payload.data()),
               static_cast<std::streamsize>(payload.size()));
  output.write(reinterpret_cast<const char*>(marker.data()), 4);
}
template <typename WorkspaceView>
void writeBinarySourceBlock(std::ofstream& output,
                            const SimulationCase& simulation,
                            const WorkspaceView& workspace) {
  std::size_t maximum = 0U;
  for (std::size_t c = 0U; c < workspace.receiverCellCount(); ++c)
    maximum = std::max(maximum, workspace.cellAt(c).size());
  std::vector<std::byte> count(4U);
  store32(count, 0U, static_cast<std::uint32_t>(maximum));
  record(output, count);
  const float radiansToDegrees = static_cast<float>(180.0 / std::numbers::pi);
  for (std::size_t d = 0U; d < workspace.depthCount(); ++d)
    for (std::size_t r = 0U; r < workspace.rangeCount(); ++r) {
      const auto arrivals = workspace.arrivalsAt(d, r);
      std::vector<std::byte> cell(4U);
      store32(cell, 0U, static_cast<std::uint32_t>(arrivals.size()));
      record(output, cell);
      const float scale = sourceScale(simulation.sourceGeometry(),
                                      simulation.receivers().ranges()[r]);
      for (const Arrival& a : arrivals) {
        std::vector<std::byte> payload(32U);
        auto put = [&](std::size_t o, float v) {
          store32(payload, o, std::bit_cast<std::uint32_t>(v));
        };
        put(0U, scale * a.amplitude);
        put(4U, radiansToDegrees * a.phaseRadians);
        put(8U, a.delaySeconds.real());
        put(12U, a.delaySeconds.imag());
        put(16U, a.sourceDeclinationDegrees);
        put(20U, a.receiverDeclinationDegrees);
        put(24U, static_cast<float>(a.topBounceCount));
        put(28U, static_cast<float>(a.bottomBounceCount));
        record(output, payload);
      }
    }
}
void writeBinaryHeader(std::ofstream& output,
                       const SimulationCase& simulation, double frequencyHz) {
  record(output, {static_cast<std::byte>(0x27), static_cast<std::byte>('2'),
                  static_cast<std::byte>('D'), static_cast<std::byte>(0x27)});
  std::vector<std::byte> frequency(4U);
  store32(frequency, 0U,
          std::bit_cast<std::uint32_t>(
              static_cast<float>(frequencyHz)));
  record(output, frequency);
  // F2CPP writeBinaryHeader source record: NSz followed by Sz(1:NSz) as
  // float32 values (one Fortran unformatted record).
  std::vector<std::byte> sources(4U + 4U * simulation.sourceCount());
  store32(sources, 0U, static_cast<std::uint32_t>(simulation.sourceCount()));
  for (std::size_t i = 0U; i < simulation.sourceCount(); ++i)
    store32(sources, 4U + 4U * i,
            std::bit_cast<std::uint32_t>(
                static_cast<float>(simulation.sources()[i].depth)));
  record(output, sources);
  std::vector<std::byte> depths(4U + 4U * simulation.receivers().depthCount());
  store32(depths, 0U,
          static_cast<std::uint32_t>(simulation.receivers().depthCount()));
  for (std::size_t i = 0U; i < simulation.receivers().depthCount(); ++i)
    store32(depths, 4U + 4U * i,
            std::bit_cast<std::uint32_t>(
                static_cast<float>(simulation.receivers().depths()[i])));
  record(output, depths);
  std::vector<std::byte> ranges(4U + 8U * simulation.receivers().rangeCount());
  store32(ranges, 0U,
          static_cast<std::uint32_t>(simulation.receivers().rangeCount()));
  for (std::size_t i = 0U; i < simulation.receivers().rangeCount(); ++i) {
    const auto value =
        std::bit_cast<std::uint64_t>(simulation.receivers().ranges()[i]);
    for (std::size_t b = 0U; b < 8U; ++b)
      ranges[4U + 8U * i + b] =
          static_cast<std::byte>((value >> (8U * b)) & 0xffU);
  }
  record(output, ranges);
}
}  // namespace

ArrivalWriter::ArrivalWriter(std::filesystem::path outputPath,
                             std::string title,
                             const SimulationCase& simulation,
                             double frequency, ArrivalEncoding encoding,
                             ArrivalWriterTestHooks testHooks)
    : outputPath_(std::move(outputPath)),
      temporaryPath_(outputPath_.string() + ".tmp"),
      simulation_(simulation),
      frequency_(frequency),
      encoding_(encoding) {
  checkEncoding(simulation_, encoding_);
  checkFrequency(simulation_, frequency_);
  const auto mode = encoding_ == ArrivalEncoding::Ascii
                        ? std::ios::out | std::ios::trunc
                        : std::ios::out | std::ios::binary | std::ios::trunc;
  output_.open(temporaryPath_, mode);
  if (!output_) {
    throw BellhopError("unable to open temporary ARR output: " +
                       temporaryPath_.string());
  }
  try {
    if (testHooks.afterTemporaryOpen) testHooks.afterTemporaryOpen();
    if (encoding_ == ArrivalEncoding::Ascii) {
      writeAsciiHeader(output_, title, simulation_, frequency_);
    } else {
      writeBinaryHeader(output_, simulation_, frequency_);
    }
    if (!output_) {
      throw BellhopError("failed while writing temporary ARR header");
    }
  } catch (...) {
    static_cast<void>(discardTemporary());
    throw;
  }
}

ArrivalWriter::~ArrivalWriter() {
  if (!published_) static_cast<void>(discardTemporary());
}

template <typename WorkspaceView>
void appendArrivalSource(std::ofstream& output,
                         const SimulationCase& simulation, double frequency,
                         ArrivalEncoding encoding, std::size_t sourceIndex,
                         std::size_t& nextSourceIndex,
                         const WorkspaceView& workspace) {
  if (sourceIndex != nextSourceIndex ||
      sourceIndex >= simulation.sourceCount()) {
    throw ValidationError("ARR writer source order is invalid");
  }
  if (workspace.frequency() != frequency ||
      workspace.depthCount() != simulation.receivers().receiversPerRange() ||
      workspace.rangeCount() != simulation.receivers().rangeCount()) {
    throw ValidationError("ARR workspace metadata does not match simulation");
  }
  if (encoding == ArrivalEncoding::Ascii) {
    writeAsciiSourceBlock(output, simulation, workspace);
  } else {
    writeBinarySourceBlock(output, simulation, workspace);
  }
  if (!output) throw BellhopError("failed while writing temporary ARR output");
  ++nextSourceIndex;
}

void ArrivalWriter::appendSource(std::size_t sourceIndex,
                                 const ArrivalWorkspace& workspace) {
  if (completed_) throw ValidationError("ARR writer is already complete");
  appendArrivalSource(output_, simulation_, frequency_, encoding_, sourceIndex,
                      nextSourceIndex_, workspace);
}

void ArrivalWriter::appendSource(
    std::size_t sourceIndex,
    BroadbandArrivalWorkspace::FrequencyView frequencyView) {
  if (completed_) throw ValidationError("ARR writer is already complete");
  appendArrivalSource(output_, simulation_, frequency_, encoding_, sourceIndex,
                      nextSourceIndex_, frequencyView);
}

void ArrivalWriter::complete() {
  if (completed_ || nextSourceIndex_ != simulation_.sourceCount()) {
    throw ValidationError("ARR writer source sequence is incomplete");
  }
  output_.flush();
  if (!output_) throw BellhopError("failed while writing temporary ARR output");
  output_.close();
  completed_ = true;
}

void ArrivalWriter::publish() {
  if (!completed_ || published_) {
    throw ValidationError("ARR writer publication state is invalid");
  }
  std::error_code error;
  std::filesystem::rename(temporaryPath_, outputPath_, error);
  if (error) {
    throw BellhopError("unable to publish ARR output: " + error.message());
  }
  published_ = true;
}

std::error_code ArrivalWriter::discardTemporary() noexcept {
  output_.close();
  std::error_code error;
  std::filesystem::remove(temporaryPath_, error);
  return error;
}

void ArrivalWriter::finalize() {
  complete();
  publish();
}

void ArrivalWriter::write(const std::filesystem::path& path,
                          std::string_view title,
                          const SimulationCase& simulation,
                          const ArrivalWorkspace& workspace,
                          ArrivalEncoding encoding) {
  write(path, title, simulation,
        std::span<const ArrivalWorkspace>(&workspace, 1U), encoding);
}
void ArrivalWriter::write(const std::filesystem::path& path,
                          std::string_view title,
                          const SimulationCase& simulation,
                          std::span<const ArrivalWorkspace> sourceWorkspaces,
                          ArrivalEncoding encoding) {
  if (sourceWorkspaces.empty()) {
    throw ValidationError(
        "ARR source workspace count must match the simulation sources");
  }
  if (sourceWorkspaces.size() != simulation.sourceCount()) {
    throw ValidationError(
        "ARR source workspace count must match the simulation sources");
  }
  ArrivalWriter writer(path, std::string(title), simulation,
                       sourceWorkspaces.front().frequency(), encoding);
  for (std::size_t sourceIndex = 0U;
       sourceIndex < sourceWorkspaces.size(); ++sourceIndex) {
    writer.appendSource(sourceIndex, sourceWorkspaces[sourceIndex]);
  }
  writer.finalize();
}

BroadbandArrivalWriterSet::BroadbandArrivalWriterSet(
    std::span<const std::filesystem::path> outputPaths, std::string title,
    const SimulationCase& simulation, ArrivalEncoding encoding,
    ArrivalWriterTestHooks testHooks)
    : testHooks_(std::move(testHooks)) {
  if (outputPaths.size() != simulation.frequencies().size()) {
    throw ValidationError(
        "broadband ARR output count must match the simulation frequencies");
  }
  writers_.reserve(outputPaths.size());
  for (std::size_t frequencyIndex = 0U; frequencyIndex < outputPaths.size();
       ++frequencyIndex) {
    writers_.push_back(std::make_unique<ArrivalWriter>(
        outputPaths[frequencyIndex], title, simulation,
        simulation.frequencies().values()[frequencyIndex], encoding));
  }
}

void BroadbandArrivalWriterSet::appendSource(
    std::size_t sourceIndex, const BroadbandArrivalWorkspace& workspace) {
  if (finalized_) throw ValidationError("broadband ARR writer is finalized");
  if (workspace.frequencyCount() != writers_.size()) {
    throw ValidationError(
        "broadband ARR workspace frequency count does not match writers");
  }
  for (std::size_t frequencyIndex = 0U; frequencyIndex < writers_.size();
       ++frequencyIndex) {
    writers_[frequencyIndex]->appendSource(
        sourceIndex, workspace.frequencyView(frequencyIndex));
  }
}

void BroadbandArrivalWriterSet::finalize() {
  if (finalized_) throw ValidationError("broadband ARR writer is finalized");
  std::vector<std::filesystem::path> backups(writers_.size());
  std::vector<bool> backupStaged(writers_.size(), false);
  std::size_t publishedCount = 0U;
  try {
    // Complete every frequency while all outputs are still private. This is
    // intentionally inside the rollback boundary: an incomplete source
    // sequence or an I/O failure must remove every temporary immediately,
    // even if the writer set remains alive after finalize() throws.
    for (const auto& writer : writers_) writer->complete();

    // Preserve any pre-existing complete set so rollback never destroys it.
    for (std::size_t index = 0U; index < writers_.size(); ++index) {
      const auto& finalPath = writers_[index]->outputPath_;
      std::error_code existsError;
      const bool exists = std::filesystem::exists(finalPath, existsError);
      if (existsError) {
        throw BellhopError("unable to inspect ARR output: " +
                           existsError.message());
      }
      if (!exists) continue;
      std::error_code typeError;
      const bool regular = std::filesystem::is_regular_file(finalPath,
                                                            typeError);
      if (typeError || !regular) {
        throw BellhopError("ARR output target is not a regular file: " +
                           finalPath.string());
      }
      backups[index] = finalPath.string() + ".rayreuse-backup";
      if (std::filesystem::exists(backups[index])) {
        throw BellhopError("ARR publication backup already exists: " +
                           backups[index].string());
      }
      std::filesystem::rename(finalPath, backups[index]);
      backupStaged[index] = true;
    }
    for (std::size_t index = 0U; index < writers_.size(); ++index) {
      if (testHooks_.beforeFrequencyPublish) {
        testHooks_.beforeFrequencyPublish(index);
      }
      writers_[index]->publish();
      ++publishedCount;
    }
    for (std::size_t index = 0U; index < backups.size(); ++index) {
      if (backupStaged[index]) {
        std::error_code ignored;
        std::filesystem::remove(backups[index], ignored);
      }
    }
    finalized_ = true;
  } catch (...) {
    const std::exception_ptr publicationFailure = std::current_exception();
    std::string rollbackErrors;
    const auto recordRollbackError = [&](std::string message,
                                         const std::error_code& error) {
      if (!error) return;
      if (!rollbackErrors.empty()) rollbackErrors += "; ";
      rollbackErrors += std::move(message) + ": " + error.message();
    };
    for (std::size_t index = 0U; index < publishedCount; ++index) {
      std::error_code error;
      std::filesystem::remove(writers_[index]->outputPath_, error);
      recordRollbackError("unable to remove published ARR output", error);
    }
    for (std::size_t index = 0U; index < backups.size(); ++index) {
      if (backupStaged[index]) {
        std::error_code error;
        std::filesystem::rename(backups[index], writers_[index]->outputPath_,
                                error);
        recordRollbackError("unable to restore ARR output backup", error);
      }
    }
    for (const auto& writer : writers_) {
      recordRollbackError("unable to remove temporary ARR output",
                          writer->discardTemporary());
    }
    if (!rollbackErrors.empty()) {
      throw BellhopError("ARR publication rollback incomplete: " +
                         rollbackErrors);
    }
    std::rethrow_exception(publicationFailure);
  }
}
}  // namespace rayreuse
