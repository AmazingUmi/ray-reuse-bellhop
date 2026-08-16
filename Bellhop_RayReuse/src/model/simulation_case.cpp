#include "rayreuse/model/simulation_case.hpp"

#include <cmath>
#include <string>
#include <utility>

#include "rayreuse/error.hpp"
#include "rayreuse/model/c_linear_ssp.hpp"

namespace rayreuse {
namespace {

void requireFinite(double value, const std::string& name) {
  if (!std::isfinite(value)) {
    throw ValidationError(name + " must be finite");
  }
}

void validateStrictlyIncreasing(const std::vector<double>& values,
                                const std::string& name) {
  if (values.empty()) {
    throw ValidationError(name + " must not be empty");
  }
  for (std::size_t index = 0; index < values.size(); ++index) {
    requireFinite(values[index], name);
    if (index > 0U && values[index - 1U] >= values[index]) {
      throw ValidationError(name + " must be strictly increasing");
    }
  }
}

}  // namespace

ReceiverGrid::ReceiverGrid(std::vector<double> depths,
                           std::vector<double> ranges)
    : depths_(std::move(depths)), ranges_(std::move(ranges)) {
  validateStrictlyIncreasing(depths_, "receiver depths");
  validateStrictlyIncreasing(ranges_, "receiver ranges");
  if (ranges_.front() < 0.0) {
    throw ValidationError("receiver ranges must be non-negative");
  }
}

const std::vector<double>& ReceiverGrid::depths() const noexcept {
  return depths_;
}

const std::vector<double>& ReceiverGrid::ranges() const noexcept {
  return ranges_;
}

std::size_t ReceiverGrid::depthCount() const noexcept { return depths_.size(); }

std::size_t ReceiverGrid::rangeCount() const noexcept { return ranges_.size(); }

FrequencyGrid::FrequencyGrid(std::vector<double> values)
    : values_(std::move(values)) {
  if (values_.empty()) {
    throw ValidationError("frequency grid must not be empty");
  }
  for (std::size_t index = 0U; index < values_.size(); ++index) {
    const double value = values_[index];
    requireFinite(value, "frequency");
    if (value <= 0.0) {
      throw ValidationError("frequencies must be positive");
    }
    if (index > 0U && values_[index - 1U] >= value) {
      throw ValidationError("frequency grid must be strictly increasing");
    }
  }
}

const std::vector<double>& FrequencyGrid::values() const noexcept {
  return values_;
}

std::size_t FrequencyGrid::size() const noexcept { return values_.size(); }

double FrequencyGrid::designFrequency() const noexcept {
  return values_.back();
}

SimulationCase::SimulationCase(Environment environment, Source source,
                               ReceiverGrid receivers,
                               FrequencyGrid frequencies, LaunchFan launchFan,
                               IntegratorSettings integrator)
    : environment_(std::move(environment)),
      source_(source),
      receivers_(std::move(receivers)),
      frequencies_(std::move(frequencies)),
      integrator_(integrator) {
  requireFinite(source_.depth, "source.depth");
  requireFinite(source_.amplitude, "source.amplitude");
  const Vec2 sourcePosition{.range = 0.0, .depth = source_.depth};
  if (environment_.seaSurface().geometry().interiorSignedDistance(
          sourcePosition, 0U) <= 0.0 ||
      environment_.seabed().geometry().interiorSignedDistance(sourcePosition,
                                                              0U) <= 0.0) {
    throw ValidationError("source depth must be strictly inside the water");
  }
  if (source_.amplitude < 0.0) {
    throw ValidationError("source amplitude must be non-negative");
  }

  for (double range : receivers_.ranges()) {
    for (double depth : receivers_.depths()) {
      const Vec2 receiverPosition{.range = range, .depth = depth};
      if (environment_.seaSurface().geometry().interiorSignedDistance(
              receiverPosition, 0U) < 0.0 ||
          environment_.seabed().geometry().interiorSignedDistance(
              receiverPosition, 0U) < 0.0) {
        throw ValidationError(
            "receiver grid must lie inside or on the water boundaries");
      }
    }
  }

  requireFinite(integrator_.stepLength, "integrator.stepLength");
  requireFinite(integrator_.rangeLimit, "integrator.rangeLimit");
  requireFinite(integrator_.depthLimit, "integrator.depthLimit");
  if (integrator_.stepLength <= 0.0) {
    throw ValidationError("integrator.stepLength must be positive");
  }
  if (integrator_.rangeLimit <= 0.0) {
    throw ValidationError("integrator.rangeLimit must be positive");
  }
  if (integrator_.depthLimit <= 0.0) {
    throw ValidationError("integrator.depthLimit must be positive");
  }
  if (integrator_.maximumRayPoints < 2U) {
    throw ValidationError("integrator.maximumRayPoints must be at least two");
  }

  const CLinearSsp soundSpeedProfile(environment_.soundSpeedProfile());
  const SoundSpeedSample sourceSample = soundSpeedProfile.evaluate(
      Vec2{.range = 0.0, .depth = source_.depth}, 0U);
  launchFanPlan_ = LaunchFanPlanner::plan(LaunchFanPlanningInput{
      .frequencies = frequencies_.values(),
      .sourceSoundSpeed = sourceSample.soundSpeed,
      .waterDepth = environment_.waterDepth(),
      .maximumRange = receivers_.ranges().back(),
      .minimumLaunchAngle = launchFan.minimumAngle,
      .maximumLaunchAngle = launchFan.maximumAngle,
      .explicitLaunchAngleCount = launchFan.explicitLaunchAngleCount,
      .inputDegreeBounds = launchFan.inputDegreeBounds,
  });
}

const Environment& SimulationCase::environment() const noexcept {
  return environment_;
}

const Source& SimulationCase::source() const noexcept { return source_; }

const ReceiverGrid& SimulationCase::receivers() const noexcept {
  return receivers_;
}

const FrequencyGrid& SimulationCase::frequencies() const noexcept {
  return frequencies_;
}

const LaunchFanPlan& SimulationCase::launchFanPlan() const noexcept {
  return launchFanPlan_;
}

const IntegratorSettings& SimulationCase::integrator() const noexcept {
  return integrator_;
}

}  // namespace rayreuse
