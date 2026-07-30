#pragma once

#include "rayreuse/field/frequency_workspace.hpp"
#include "rayreuse/model/environment.hpp"
#include "rayreuse/ray/ray_path.hpp"

namespace rayreuse {

// Reconstructs one frequency's complex travel time and reflection state from
// an immutable geometry path.  No projected value is written back to RayPath.
class FrequencyProjector {
 public:
  explicit FrequencyProjector(Environment environment);

  [[nodiscard]] RayFrequencyState project(
      const RayPath& path, double frequency,
      double sourceAmplitude) const;

 private:
  Environment environment_;
};

}  // namespace rayreuse
