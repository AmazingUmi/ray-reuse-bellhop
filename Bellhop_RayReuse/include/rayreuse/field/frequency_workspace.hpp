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

}  // namespace rayreuse
