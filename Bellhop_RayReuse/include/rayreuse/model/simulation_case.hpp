#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "rayreuse/model/environment.hpp"
#include "rayreuse/model/launch_fan_planner.hpp"

namespace rayreuse {

class ReceiverGrid {
 public:
  ReceiverGrid(std::vector<double> depths, std::vector<double> ranges);

  [[nodiscard]] const std::vector<double>& depths() const noexcept;
  [[nodiscard]] const std::vector<double>& ranges() const noexcept;
  [[nodiscard]] std::size_t depthCount() const noexcept;
  [[nodiscard]] std::size_t rangeCount() const noexcept;

 private:
  std::vector<double> depths_;
  std::vector<double> ranges_;
};

class FrequencyGrid {
 public:
  explicit FrequencyGrid(std::vector<double> values);

  [[nodiscard]] const std::vector<double>& values() const noexcept;
  [[nodiscard]] std::size_t size() const noexcept;
  [[nodiscard]] double designFrequency() const noexcept;

 private:
  std::vector<double> values_;
};

struct Source {
  double depth{};
  double amplitude{1.0};
};

struct LaunchFan {
  double minimumAngle{};
  double maximumAngle{};
  std::optional<std::size_t> explicitLaunchAngleCount;
  std::optional<LaunchAngleDegreeBounds> inputDegreeBounds;
};

struct IntegratorSettings {
  double stepLength{};
  double rangeLimit{};
  double depthLimit{};
  std::size_t maximumRayPoints{};
};

class SimulationCase {
 public:
  SimulationCase(Environment environment, Source source,
                 ReceiverGrid receivers, FrequencyGrid frequencies,
                 LaunchFan launchFan, IntegratorSettings integrator);

  [[nodiscard]] const Environment& environment() const noexcept;
  [[nodiscard]] const Source& source() const noexcept;
  [[nodiscard]] const ReceiverGrid& receivers() const noexcept;
  [[nodiscard]] const FrequencyGrid& frequencies() const noexcept;
  [[nodiscard]] const LaunchFanPlan& launchFanPlan() const noexcept;
  [[nodiscard]] const IntegratorSettings& integrator() const noexcept;

 private:
  Environment environment_;
  Source source_;
  ReceiverGrid receivers_;
  FrequencyGrid frequencies_;
  LaunchFanPlan launchFanPlan_;
  IntegratorSettings integrator_;
};

}  // namespace rayreuse
