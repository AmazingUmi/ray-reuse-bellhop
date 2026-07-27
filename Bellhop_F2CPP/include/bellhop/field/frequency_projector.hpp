#pragma once

#include "bellhop/field/frequency_workspace.hpp"
#include "bellhop/model/environment.hpp"
#include "bellhop/ray/ray_path.hpp"

namespace bellhop {

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

}  // namespace bellhop
