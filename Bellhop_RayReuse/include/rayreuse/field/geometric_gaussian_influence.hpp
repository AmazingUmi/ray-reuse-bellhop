#pragma once

#include <cstddef>
#include <functional>

#include "rayreuse/field/arrival_workspace.hpp"
#include "rayreuse/field/eigenray_hit.hpp"
#include "rayreuse/field/frequency_workspace.hpp"
#include "rayreuse/model/simulation_case.hpp"
#include "rayreuse/ray/ray_path.hpp"

namespace rayreuse {

class GeometricGaussianInfluence {
 public:
  using EigenrayHitSink = std::function<void(const EigenrayHit&)>;

  explicit GeometricGaussianInfluence(ReceiverGrid receivers);

  void accumulateArrivals(ArrivalWorkspace& workspace, const RayPath& path,
                          const RayFrequencyState& frequencyState,
                          double launchAngleSpacingRadians) const;

  void collectEigenrayHits(const EigenrayHitSink& sink, const RayPath& path,
                           const RayFrequencyState& frequencyState,
                           double launchAngleSpacingRadians) const;

 private:
  ReceiverGrid receivers_;
};

}  // namespace rayreuse
