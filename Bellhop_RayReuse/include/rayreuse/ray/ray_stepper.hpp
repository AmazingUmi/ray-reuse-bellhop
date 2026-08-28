#pragma once

#include <cstddef>
#include <functional>

#include "rayreuse/model/c_linear_ssp.hpp"
#include "rayreuse/model/sound_speed_evaluator.hpp"
#include "rayreuse/numerics/vec2.hpp"
#include "rayreuse/ray/ray_path.hpp"

namespace rayreuse {

enum class StepLimitPhase {
  InitialTangent,
  PredictedMidpointTangent,
};

struct StepLimitRequest {
  StepLimitPhase phase{StepLimitPhase::InitialTangent};
  Vec2 initialPosition;
  Vec2 unitTangent;
  std::size_t initialSegmentIndex{};
  std::size_t initialRangeSegmentIndex{};
  double nominalStepLength{};
  double proposedStepLength{};
};

using StepLimiter = std::function<double(const StepLimitRequest&)>;

struct RayStepResult {
  RayState endState;
  StepQuadrature quadrature;
  std::size_t segmentIndex{};
  std::size_t rangeSegmentIndex{};
};

// Advances one modified-Heun/box step.
//
// If provided, limiter is called twice, matching the two ReduceStep2D calls in
// Step.f90. The first result fixes the predictor midpoint. The second call may
// further shorten the final step; the predictor is intentionally not recomputed
// and the returned quadrature weights describe the resulting blended update.
//
// This layer contains no reflection, absorption, or complex-time behavior.
[[nodiscard]] RayStepResult stepRay(const GeometrySspEvaluator& soundSpeedProfile,
                                    const RayState& initialState,
                                    std::size_t initialSegmentIndex,
                                    std::size_t initialRangeSegmentIndex,
                                    double nominalStepLength,
                                    const StepLimiter& limiter = {});

// Range-independent compatibility overload. Its range hint is always zero.
[[nodiscard]] RayStepResult stepRay(const GeometrySspEvaluator& soundSpeedProfile,
                                    const RayState& initialState,
                                    std::size_t initialSegmentIndex,
                                    double nominalStepLength,
                                    const StepLimiter& limiter = {});

[[nodiscard]] RayStepResult stepRay(const CLinearSsp& soundSpeedProfile,
                                    const RayState& initialState,
                                    std::size_t initialSegmentIndex,
                                    double nominalStepLength,
                                    const StepLimiter& limiter = {});

}  // namespace rayreuse
