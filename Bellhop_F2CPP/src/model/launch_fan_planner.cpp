#include "bellhop/model/launch_fan_planner.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "bellhop/error.hpp"

namespace bellhop {
namespace {

constexpr double kPhaseCriterionScale = 0.3;
constexpr double kReferenceSoundSpeed = 1500.0;
constexpr double kDepthRangeFactor = 10.0;
constexpr double kSufficiencyFactor = 6.0;
constexpr std::size_t kMinimumPhaseCriterionCount = 300U;
constexpr double kRadiansPerDegree = std::numbers::pi / 180.0;

void requireFinite(double value, std::string_view name) {
  if (!std::isfinite(value)) {
    throw ValidationError(std::string(name) + " must be finite");
  }
}

void requireFinitePositive(double value, std::string_view name) {
  requireFinite(value, name);
  if (value <= 0.0) {
    throw ValidationError(std::string(name) + " must be positive");
  }
}

std::size_t checkedFloorToCount(double value, std::string_view name) {
  if (!std::isfinite(value) || value < 0.0) {
    throw ValidationError(std::string(name) +
                          " is not a finite non-negative count");
  }

  const double floored = std::floor(value);
  const long double exclusiveUpperBound =
      std::ldexp(1.0L, std::numeric_limits<std::size_t>::digits);
  if (static_cast<long double>(floored) >= exclusiveUpperBound) {
    throw ValidationError(std::string(name) + " exceeds size_t capacity");
  }
  return static_cast<std::size_t>(floored);
}

std::size_t checkedAddTwo(std::size_t value, std::string_view name) {
  constexpr std::size_t maximum = std::numeric_limits<std::size_t>::max();
  if (value > maximum - 2U) {
    throw ValidationError(std::string(name) + " exceeds size_t capacity");
  }
  return value + 2U;
}

}  // namespace

LaunchFanPlan LaunchFanPlanner::plan(const LaunchFanPlanningInput& input) {
  if (input.frequencies.empty()) {
    throw ValidationError("frequencies must not be empty");
  }

  double designFrequency = 0.0;
  for (double frequency : input.frequencies) {
    requireFinitePositive(frequency, "frequency");
    designFrequency = std::max(designFrequency, frequency);
  }

  requireFinitePositive(input.sourceSoundSpeed, "sourceSoundSpeed");
  requireFinitePositive(input.waterDepth, "waterDepth");
  requireFinitePositive(input.maximumRange, "maximumRange");
  requireFinite(input.minimumLaunchAngle, "minimumLaunchAngle");
  requireFinite(input.maximumLaunchAngle, "maximumLaunchAngle");
  if (input.minimumLaunchAngle >= input.maximumLaunchAngle) {
    throw ValidationError(
        "minimumLaunchAngle must be less than maximumLaunchAngle");
  }

  const double angleSpan =
      input.maximumLaunchAngle - input.minimumLaunchAngle;
  if (!std::isfinite(angleSpan) || angleSpan <= 0.0) {
    throw ValidationError("launch angle span must be finite and positive");
  }

  const double phaseCriterion =
      kPhaseCriterionScale * input.maximumRange * designFrequency /
      kReferenceSoundSpeed;
  const std::size_t phaseCriterionCount = std::max(
      checkedFloorToCount(phaseCriterion, "phase criterion count"),
      kMinimumPhaseCriterionCount);

  const double depthAngularStep =
      std::atan(input.waterDepth /
                (kDepthRangeFactor * input.maximumRange));
  if (!std::isfinite(depthAngularStep) || depthAngularStep <= 0.0) {
    throw ValidationError(
        "depth criterion angular step must be finite and positive");
  }
  const std::size_t depthCriterionCount = checkedFloorToCount(
      std::numbers::pi / depthAngularStep, "depth criterion count");

  const double sufficiencyAngularStep =
      std::sqrt(input.sourceSoundSpeed /
                (kSufficiencyFactor * designFrequency *
                 input.maximumRange));
  if (!std::isfinite(sufficiencyAngularStep) ||
      sufficiencyAngularStep <= 0.0) {
    throw ValidationError(
        "sufficiency angular step must be finite and positive");
  }
  const std::size_t minimumRecommendedAngleCount = checkedAddTwo(
      checkedFloorToCount(angleSpan / sufficiencyAngularStep,
                          "minimum recommended angle count"),
      "minimum recommended angle count");

  const std::size_t launchAngleCount =
      std::max({phaseCriterionCount, depthCriterionCount,
                minimumRecommendedAngleCount});

  std::vector<double> launchAngles;
  if (launchAngleCount > launchAngles.max_size()) {
    throw ValidationError(
        "launch angle count exceeds vector allocation capacity");
  }
  launchAngles.resize(launchAngleCount);

  double launchAngleStep =
      angleSpan / static_cast<double>(launchAngleCount - 1U);
  if (input.inputDegreeBounds.has_value()) {
    const LaunchAngleDegreeBounds bounds =
        input.inputDegreeBounds.value();
    requireFinite(bounds.minimum, "inputDegreeBounds.minimum");
    requireFinite(bounds.maximum, "inputDegreeBounds.maximum");
    if (bounds.minimum >= bounds.maximum) {
      throw ValidationError(
          "input degree minimum must be less than maximum");
    }
    if (bounds.minimum * kRadiansPerDegree !=
            input.minimumLaunchAngle ||
        bounds.maximum * kRadiansPerDegree !=
            input.maximumLaunchAngle) {
      throw ValidationError(
          "input degree bounds must exactly match the radian bounds");
    }

    // Legacy Bellhop first subtabulates the user-supplied endpoints in
    // degrees and only then converts every generated angle to radians.
    // Preserve those original degree inputs: converting runtime radians back
    // to degrees can itself shift the negative endpoint by one ULP.
    const double launchAngleStepDegrees =
        (bounds.maximum - bounds.minimum) /
        static_cast<double>(launchAngleCount - 1U);
    for (std::size_t index = 0U; index < launchAngleCount; ++index) {
      const double launchAngleDegrees =
          bounds.minimum +
          static_cast<double>(index) * launchAngleStepDegrees;
      launchAngles[index] =
          launchAngleDegrees * kRadiansPerDegree;
    }
    launchAngleStep =
        (launchAngles.back() - launchAngles.front()) /
        static_cast<double>(launchAngleCount - 1U);
  } else {
    for (std::size_t index = 0U; index < launchAngleCount; ++index) {
      launchAngles[index] =
          std::fma(static_cast<double>(index), launchAngleStep,
                   input.minimumLaunchAngle);
    }
    launchAngles.front() = input.minimumLaunchAngle;
    launchAngles.back() = input.maximumLaunchAngle;
  }
  if (!std::isfinite(launchAngleStep) || launchAngleStep <= 0.0) {
    throw ValidationError(
        "launch angle step is not representable as a positive finite value");
  }

  return LaunchFanPlan{
      .designFrequency = designFrequency,
      .phaseCriterionCount = phaseCriterionCount,
      .depthCriterionCount = depthCriterionCount,
      .minimumRecommendedAngleCount = minimumRecommendedAngleCount,
      .launchAngleCount = launchAngleCount,
      .launchAngleStep = launchAngleStep,
      .launchAngles = std::move(launchAngles),
  };
}

}  // namespace bellhop
