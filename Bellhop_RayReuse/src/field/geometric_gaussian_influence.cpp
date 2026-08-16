#include "rayreuse/field/geometric_gaussian_influence.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

#include "rayreuse/error.hpp"

namespace rayreuse {
namespace {
constexpr double kBeamWindow = 4.0;
constexpr double kNearFieldFactor = static_cast<double>(0.2F);

double spacing(double value) {
  return std::nextafter(value, std::numeric_limits<double>::infinity()) - value;
}
std::size_t activeCount(const RayFrequencyState& state) {
  for (std::size_t i = 0U; i < state.points.size(); ++i)
    if (!state.points[i].active) return i + 1U;
  return state.points.size();
}
bool crosses(double previous, double current) {
  return (current <= 0.0 && previous > 0.0) ||
         (current >= 0.0 && previous < 0.0);
}
void validate(const ReceiverGrid& receivers, const RayPath& path,
              const RayFrequencyState& state, double launchSpacing) {
  if (!std::isfinite(launchSpacing) || launchSpacing <= 0.0)
    throw ValidationError(
        "geometric Gaussian launch-angle spacing must be positive and finite");
  if (path.points.size() < 2U || path.points.size() != state.points.size())
    throw ValidationError(
        "geometric Gaussian geometry and frequency point counts must match");
  if (!state.points.front().active || receivers.depthCount() == 0U ||
      receivers.rangeCount() == 0U)
    throw ValidationError("geometric Gaussian input is invalid");
  for (const auto& point : state.points) {
    if (!std::isfinite(point.amplitude) || point.amplitude < 0.0 ||
        !std::isfinite(point.reflectionPhase) ||
        !std::isfinite(point.complexTravelTime.real()) ||
        !std::isfinite(point.complexTravelTime.imag()))
      throw ValidationError("geometric Gaussian frequency point is invalid");
  }
}
}  // namespace

GeometricGaussianInfluence::GeometricGaussianInfluence(ReceiverGrid receivers)
    : receivers_(std::move(receivers)) {}

void GeometricGaussianInfluence::accumulateArrivals(
    ArrivalWorkspace& workspace, const RayPath& path,
    const RayFrequencyState& state, double launchSpacing) const {
  validate(receivers_, path, state, launchSpacing);
  const std::size_t pointCount = activeCount(state);
  const double q0 = path.points.front().soundSpeed / launchSpacing;
  const double sourceRatio = std::sqrt(std::abs(std::cos(path.launchAngle))) /
                             std::sqrt(2.0 * std::numbers::pi);
  if (!std::isfinite(q0) || q0 == 0.0 || !std::isfinite(sourceRatio))
    throw ValidationError("geometric Gaussian source constants are invalid");
  std::vector<std::int32_t> top(pointCount, 0), bottom(pointCount, 0);
  for (const auto& event : path.events) {
    const std::size_t reflected = event.reflectedRayPointIndex;
    if (reflected != event.rayPointIndex + 1U ||
        reflected >= path.points.size())
      throw ValidationError(
          "geometric Gaussian reflection event has invalid indices");
    if (reflected >= pointCount) continue;
    ++(event.boundary == ReflectionBoundary::SeaSurface ? top[reflected]
                                                        : bottom[reflected]);
  }
  for (std::size_t i = 1U; i < pointCount; ++i) {
    top[i] += top[i - 1U];
    bottom[i] += bottom[i - 1U];
  }

  double phase = 0.0;
  double previousQ = path.points.front().dynamicQ[0];
  const std::vector<double>& ranges = receivers_.ranges();
  const double rangeDelta = ranges.size() >= 2U ? ranges[1U] - ranges[0U] : 0.0;
  std::size_t receiverIndex = 0U;
  if (ranges.size() >= 2U && rangeDelta != 0.0) {
    const auto first =
        std::find_if(ranges.begin(), ranges.end(), [&](double range) {
          return range > path.points.front().position.range;
        });
    if (first == ranges.end()) {
      if (path.points.front().slowness.range >= 0.0) return;
      receiverIndex = ranges.size() - 1U;
    } else {
      receiverIndex = static_cast<std::size_t>(first - ranges.begin());
      if (path.points.front().slowness.range < 0.0 && receiverIndex > 0U)
        --receiverIndex;
    }
  }
  double previousRange = path.points.front().position.range;
  for (std::size_t right = 1U; right < pointCount; ++right) {
    const std::size_t left = right - 1U;
    const Vec2 segment =
        path.points[right].position - path.points[left].position;
    const double length = norm(segment);
    if (length < 1000.0 * spacing(path.points[right].position.range)) continue;
    const Vec2 tangent = segment / length;
    const Vec2 normal{.range = -tangent.depth, .depth = tangent.range};
    const double leftQ = path.points[left].dynamicQ[0];
    if (crosses(previousQ, leftQ)) phase += std::numbers::pi / 2.0;
    previousQ = leftQ;
    const double rightRange = path.points[right].position.range;
    const double wavelengthSigma =
        std::numbers::pi * path.points[left].soundSpeed / state.frequency;
    const double segmentSigma =
        std::max(std::max(std::abs(path.points[left].dynamicQ[0]),
                          std::abs(path.points[right].dynamicQ[0])) /
                     q0 / std::abs(tangent.range),
                 std::min(kNearFieldFactor * state.frequency *
                              state.points[right].complexTravelTime.real(),
                          wavelengthSigma));
    const double segmentRadius = kBeamWindow * segmentSigma;
    const double minDepth = std::abs(tangent.range) > 0.5
                                ? std::min(path.points[left].position.depth,
                                           path.points[right].position.depth) -
                                      segmentRadius
                                : -std::numeric_limits<double>::infinity();
    const double maxDepth = std::abs(tangent.range) > 0.5
                                ? std::max(path.points[left].position.depth,
                                           path.points[right].position.depth) +
                                      segmentRadius
                                : std::numeric_limits<double>::infinity();
    for (;;) {
      const double receiverRange = ranges[receiverIndex];
      if (receiverRange >= std::min(previousRange, rightRange) &&
          receiverRange < std::max(previousRange, rightRange)) {
        for (std::size_t di = 0U; di < receivers_.depthCount(); ++di) {
          const double receiverDepth = receivers_.depths()[di];
          if (receiverDepth < minDepth || receiverDepth > maxDepth) continue;
          const Vec2 receiver{receiverRange, receiverDepth};
          const Vec2 offset = receiver - path.points[left].position;
          const double weight = fortranDotProduct2D(offset, tangent) / length;
          const double normalOffset =
              std::abs(fortranDotProduct2D(offset, normal));
          const double q =
              leftQ + weight * (path.points[right].dynamicQ[0] - leftQ);
          const double geometricSigma = std::abs(q / q0);
          const double nearFieldSigma =
              kNearFieldFactor * state.frequency *
              state.points[right].complexTravelTime.real();
          const double sigma = std::max(
              geometricSigma, std::min(nearFieldSigma, wavelengthSigma));
          if (sigma <= 0.0 || normalOffset >= kBeamWindow * sigma) continue;
          const double gaussianWeight =
              std::sqrt(geometricSigma / sigma) *
              std::exp(-0.5 * (normalOffset / sigma) * (normalOffset / sigma));
          const std::complex<double> delay =
              state.points[left].complexTravelTime +
              weight * (state.points[right].complexTravelTime -
                        state.points[left].complexTravelTime);
          const double amplitude =
              sourceRatio *
              std::sqrt(path.points[right].soundSpeed / (q0 * sigma)) *
              state.points[right].amplitude;
          double candidatePhase = state.points[left].reflectionPhase + phase;
          if (crosses(previousQ, q)) candidatePhase += std::numbers::pi / 2.0;
          workspace.addCandidate(
              state.frequency,
              ArrivalCandidate{amplitude * gaussianWeight, candidatePhase,
                               delay,
                               path.launchAngle * 180.0 / std::numbers::pi,
                               std::atan2(tangent.depth, tangent.range) *
                                   180.0 / std::numbers::pi,
                               top[right], bottom[right]},
              di, receiverIndex);
        }
      }
      if (ranges.size() < 2U || rangeDelta == 0.0) break;
      if (ranges[receiverIndex] < rightRange) {
        if (receiverIndex + 1U >= ranges.size()) break;
        const std::size_t next = receiverIndex + 1U;
        if (ranges[next] >= rightRange) break;
        receiverIndex = next;
      } else {
        if (receiverIndex == 0U) break;
        const std::size_t next = receiverIndex - 1U;
        if (ranges[next] <= rightRange) break;
        receiverIndex = next;
      }
    }
    previousRange = rightRange;
  }
}

void GeometricGaussianInfluence::collectEigenrayHits(
    const EigenrayHitSink& sink, const RayPath& path,
    const RayFrequencyState& state, double launchSpacing) const {
  if (!sink)
    throw ValidationError("geometric Gaussian eigenray hit sink is empty");
  validate(receivers_, path, state, launchSpacing);
  const std::size_t pointCount = activeCount(state);
  const double q0 = path.points.front().soundSpeed / launchSpacing;
  const std::vector<double>& ranges = receivers_.ranges();
  const double rangeDelta = ranges.size() >= 2U ? ranges[1U] - ranges[0U] : 0.0;
  std::size_t receiverIndex = 0U;
  if (ranges.size() >= 2U && rangeDelta != 0.0) {
    const auto first =
        std::find_if(ranges.begin(), ranges.end(), [&](double range) {
          return range > path.points.front().position.range;
        });
    if (first == ranges.end()) {
      if (path.points.front().slowness.range >= 0.0) return;
      receiverIndex = ranges.size() - 1U;
    } else {
      receiverIndex = static_cast<std::size_t>(first - ranges.begin());
      if (path.points.front().slowness.range < 0.0 && receiverIndex > 0U)
        --receiverIndex;
    }
  }
  double previousRange = path.points.front().position.range;
  for (std::size_t right = 1U; right < pointCount; ++right) {
    const std::size_t left = right - 1U;
    if (!state.points[right].active) continue;
    const Vec2 segment =
        path.points[right].position - path.points[left].position;
    const double length = norm(segment);
    if (length < 1000.0 * spacing(path.points[right].position.range)) continue;
    const Vec2 tangent = segment / length;
    const Vec2 normal{.range = -tangent.depth, .depth = tangent.range};
    const double leftQ = path.points[left].dynamicQ[0];
    const double rightRange = path.points[right].position.range;
    const double wavelengthSigma =
        std::numbers::pi * path.points[left].soundSpeed / state.frequency;
    const double segmentSigma =
        std::max(std::max(std::abs(path.points[left].dynamicQ[0]),
                          std::abs(path.points[right].dynamicQ[0])) /
                     q0 / std::abs(tangent.range),
                 std::min(kNearFieldFactor * state.frequency *
                              state.points[right].complexTravelTime.real(),
                          wavelengthSigma));
    const double segmentRadius = kBeamWindow * segmentSigma;
    const double minimumDepth =
        std::abs(tangent.range) > 0.5
            ? std::min(path.points[left].position.depth,
                       path.points[right].position.depth) -
                  segmentRadius
            : -std::numeric_limits<double>::infinity();
    const double maximumDepth =
        std::abs(tangent.range) > 0.5
            ? std::max(path.points[left].position.depth,
                       path.points[right].position.depth) +
                  segmentRadius
            : std::numeric_limits<double>::infinity();
    for (;;) {
      const double receiverRange = ranges[receiverIndex];
      if (receiverRange >= std::min(previousRange, rightRange) &&
          receiverRange < std::max(previousRange, rightRange)) {
        for (std::size_t di = 0U; di < receivers_.depthCount(); ++di) {
          const double receiverDepth = receivers_.depths()[di];
          if (receiverDepth < minimumDepth || receiverDepth > maximumDepth)
            continue;
          const Vec2 receiver{receiverRange, receiverDepth};
          const Vec2 offset = receiver - path.points[left].position;
          const double weight = fortranDotProduct2D(offset, tangent) / length;
          const double q =
              leftQ + weight * (path.points[right].dynamicQ[0] - leftQ);
          const double normalOffset =
              std::abs(fortranDotProduct2D(offset, normal));
          const double sigma = std::max(
              std::abs(q / q0),
              std::min(kNearFieldFactor * state.frequency *
                           state.points[right].complexTravelTime.real(),
                       wavelengthSigma));
          if (sigma > 0.0 && normalOffset < kBeamWindow * sigma)
            sink(EigenrayHit{receiverIndex, di, right + 1U});
        }
      }
      if (ranges.size() < 2U || rangeDelta == 0.0) break;
      if (ranges[receiverIndex] < rightRange) {
        if (receiverIndex + 1U >= ranges.size()) break;
        const std::size_t next = receiverIndex + 1U;
        if (ranges[next] >= rightRange) break;
        receiverIndex = next;
      } else {
        if (receiverIndex == 0U) break;
        const std::size_t next = receiverIndex - 1U;
        if (ranges[next] <= rightRange) break;
        receiverIndex = next;
      }
    }
    previousRange = rightRange;
  }
}

}  // namespace rayreuse
