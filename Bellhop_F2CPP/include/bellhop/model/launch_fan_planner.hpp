#pragma once

#include <cstddef>
#include <optional>
#include <vector>

namespace bellhop {

struct LaunchAngleDegreeBounds {
  double minimum{};
  double maximum{};
};

struct LaunchFanPlanningInput {
  std::vector<double> frequencies;
  double sourceSoundSpeed{};
  double waterDepth{};
  double maximumRange{};
  double minimumLaunchAngle{};
  double maximumLaunchAngle{};

  // D-02 policy A ignores this value for coherent-field runs. Ray-trace mode
  // follows the legacy contract: an explicit count is used as-is and a
  // missing count selects the 50-ray default.
  std::optional<std::size_t> explicitLaunchAngleCount{};

  // The parser preserves the original degree endpoints so legacy Bellhop's
  // degree-domain SubTab operation can be reproduced without converting the
  // radian-domain core inputs back to degrees.
  std::optional<LaunchAngleDegreeBounds> inputDegreeBounds{};
  bool rayTraceMode{};
};

struct LaunchFanPlan {
  double designFrequency{};
  std::size_t phaseCriterionCount{};
  std::size_t depthCriterionCount{};
  std::size_t minimumRecommendedAngleCount{};
  std::size_t launchAngleCount{};
  double launchAngleStep{};
  std::vector<double> launchAngles;
};

class LaunchFanPlanner {
 public:
  [[nodiscard]] static LaunchFanPlan plan(
      const LaunchFanPlanningInput& input);
};

}  // namespace bellhop
