#pragma once

#include <cstddef>
#include <optional>
#include <vector>

namespace rayreuse {

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

  // D-02 policy A keeps this parser-facing value for compatibility but always
  // derives the actual count from the highest design frequency.
  std::optional<std::size_t> explicitLaunchAngleCount;

  // The parser preserves the original degree endpoints so legacy Bellhop's
  // degree-domain SubTab operation can be reproduced without converting the
  // radian-domain core inputs back to degrees.
  std::optional<LaunchAngleDegreeBounds> inputDegreeBounds;
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
  [[nodiscard]] static LaunchFanPlan plan(const LaunchFanPlanningInput& input);
};

}  // namespace rayreuse
