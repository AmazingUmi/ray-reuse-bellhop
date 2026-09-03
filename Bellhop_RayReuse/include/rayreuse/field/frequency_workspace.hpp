#pragma once

#include <complex>
#include <cstddef>
#include <span>
#include <vector>

#include "rayreuse/model/simulation_case.hpp"

namespace rayreuse {

struct RayFrequencyPoint {
  std::complex<double> complexTravelTime{};
  double amplitude{};
  double reflectionPhase{};
  bool active{true};
};

struct RayFrequencyState {
  double frequency{};
  std::vector<RayFrequencyPoint> points;
};

class FrequencyWorkspace {
 public:
  FrequencyWorkspace(double frequency, const ReceiverGrid& receivers);

  [[nodiscard]] double frequency() const noexcept;
  [[nodiscard]] std::size_t depthCount() const noexcept;
  [[nodiscard]] std::size_t rangeCount() const noexcept;
  [[nodiscard]] std::span<std::complex<double>> pressure() noexcept;
  [[nodiscard]] std::span<const std::complex<double>> pressure() const noexcept;
  [[nodiscard]] std::complex<double>& at(std::size_t depthIndex,
                                         std::size_t rangeIndex);
  [[nodiscard]] const std::complex<double>& at(std::size_t depthIndex,
                                               std::size_t rangeIndex) const;
  void clear() noexcept;

 private:
  [[nodiscard]] std::size_t flatIndex(std::size_t depthIndex,
                                      std::size_t rangeIndex) const;

  double frequency_;
  std::size_t depthCount_;
  std::size_t rangeCount_;
  std::vector<std::complex<double>> pressure_;
};

// I/S runs accumulate non-negative per-ray power before converting it to a
// pressure amplitude. Keep that state distinct from coherent complex pressure
// so the two accumulation laws cannot be mixed accidentally.
class IntensityWorkspace {
 public:
  IntensityWorkspace(double frequency, const ReceiverGrid& receivers);

  [[nodiscard]] double frequency() const noexcept;
  [[nodiscard]] std::size_t depthCount() const noexcept;
  [[nodiscard]] std::size_t rangeCount() const noexcept;
  [[nodiscard]] std::span<const double> intensity() const noexcept;
  [[nodiscard]] double at(std::size_t depthIndex, std::size_t rangeIndex) const;
  void add(std::size_t depthIndex, std::size_t rangeIndex, double contribution);
  void clear() noexcept;

 private:
  // IGR-3A design §6.2: fused intensity materialization is a bitwise lane
  // copy into the payload; the add()-path validation and 0.0 + x
  // reassociation must not apply, so the fused twin writes directly.
  friend class FusedIntensityWorkspace;

  [[nodiscard]] std::size_t flatIndex(std::size_t depthIndex,
                                      std::size_t rangeIndex) const;

  double frequency_;
  std::size_t depthCount_;
  std::size_t rangeCount_;
  std::vector<double> intensity_;
};

}  // namespace rayreuse
