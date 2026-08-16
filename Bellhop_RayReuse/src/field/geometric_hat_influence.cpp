#include "rayreuse/field/geometric_hat_influence.hpp"

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

double spacing(double value) {
  return std::nextafter(value, std::numeric_limits<double>::infinity()) - value;
}

std::size_t activeCount(const RayFrequencyState& state) {
  for (std::size_t i = 0U; i < state.points.size(); ++i)
    if (!state.points[i].active) return i + 1U;
  return state.points.size();
}

void validate(const ReceiverGrid& receivers, const RayPath& path,
              const RayFrequencyState& state, double launchSpacing) {
  if (!std::isfinite(launchSpacing) || launchSpacing <= 0.0)
    throw ValidationError(
        "geometric hat launch-angle spacing must be positive and finite");
  if (path.points.size() < 2U || path.points.size() != state.points.size())
    throw ValidationError(
        "geometric hat geometry and frequency point counts must match");
  if (state.points.front().active == false)
    throw ValidationError("geometric hat source point must be active");
  if (receivers.depthCount() == 0U || receivers.rangeCount() == 0U)
    throw ValidationError("geometric hat requires non-empty receivers");
  for (const auto& point : state.points) {
    if (!std::isfinite(point.amplitude) || point.amplitude < 0.0 ||
        !std::isfinite(point.reflectionPhase) ||
        !std::isfinite(point.complexTravelTime.real()) ||
        !std::isfinite(point.complexTravelTime.imag()))
      throw ValidationError("geometric hat frequency point is invalid");
  }
}

bool crosses(double previous, double current) {
  return (current <= 0.0 && previous > 0.0) ||
         (current >= 0.0 && previous < 0.0);
}

}  // namespace

GeometricHatInfluence::GeometricHatInfluence(ReceiverGrid receivers)
    : receivers_(std::move(receivers)) {}

void GeometricHatInfluence::accumulateArrivals(ArrivalWorkspace& workspace,
                                               const RayPath& path,
                                               const RayFrequencyState& state,
                                               double launchSpacing) const {
  validate(receivers_, path, state, launchSpacing);
  const std::size_t pointCount = activeCount(state);
  const double omega = 2.0 * std::numbers::pi * state.frequency;
  const double q0 = path.points.front().soundSpeed / launchSpacing;
  const double ratio = std::sqrt(std::abs(std::cos(path.launchAngle)));
  if (!std::isfinite(q0) || q0 == 0.0 || !std::isfinite(ratio))
    throw ValidationError("geometric hat source constants are invalid");

  std::vector<std::int32_t> top(pointCount, 0), bottom(pointCount, 0);
  for (const auto& event : path.events) {
    const std::size_t reflected = event.reflectedRayPointIndex;
    if (reflected != event.rayPointIndex + 1U ||
        reflected >= path.points.size())
      throw ValidationError(
          "geometric hat reflection event has invalid indices");
    if (reflected >= pointCount) continue;
    auto& count = event.boundary == ReflectionBoundary::SeaSurface
                      ? top[reflected]
                      : bottom[reflected];
    if (count == std::numeric_limits<std::int32_t>::max())
      throw ValidationError("geometric hat bounce count exceeds int32");
    ++count;
  }
  for (std::size_t i = 1U; i < pointCount; ++i) {
    top[i] += top[i - 1U];
    bottom[i] += bottom[i - 1U];
  }

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
  double phase = 0.0;
  double previousQ = path.points.front().dynamicQ[0];
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
    for (;;) {
      const double receiverRange = ranges[receiverIndex];
      if (receiverRange >= std::min(previousRange, rightRange) &&
          receiverRange < std::max(previousRange, rightRange)) {
        for (std::size_t di = 0U; di < receivers_.depthCount(); ++di) {
          const Vec2 receiver{receiverRange, receivers_.depths()[di]};
          const Vec2 offset = receiver - path.points[left].position;
          const double weight = fortranDotProduct2D(offset, tangent) / length;
          const double normalOffset =
              std::abs(fortranDotProduct2D(offset, normal));
          const double q =
              leftQ + weight * (path.points[right].dynamicQ[0] - leftQ);
          const double radius = std::abs(q / q0);
          if (radius == 0.0 || normalOffset >= radius) continue;
          const double hatWeight = (radius - normalOffset) / radius;
          const std::complex<double> delay =
              state.points[left].complexTravelTime +
              weight * (state.points[right].complexTravelTime -
                        state.points[left].complexTravelTime);
          const double amplitude =
              ratio * std::sqrt(path.points[right].soundSpeed / std::abs(q)) *
              state.points[right].amplitude * hatWeight;
          double candidatePhase = state.points[left].reflectionPhase + phase;
          if (crosses(previousQ, q)) candidatePhase += std::numbers::pi / 2.0;
          workspace.addCandidate(
              state.frequency,
              ArrivalCandidate{amplitude, candidatePhase, delay,
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
  static_cast<void>(omega);
}

void GeometricHatInfluence::collectEigenrayHits(const EigenrayHitSink& sink,
                                                const RayPath& path,
                                                const RayFrequencyState& state,
                                                double launchSpacing) const {
  if (!sink) throw ValidationError("geometric hat eigenray hit sink is empty");
  validate(receivers_, path, state, launchSpacing);
  const std::size_t pointCount = activeCount(state);
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
  double previousQ = path.points.front().dynamicQ[0];
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
    if (crosses(previousQ, leftQ)) previousQ = leftQ;
    const double rightRange = path.points[right].position.range;
    for (;;) {
      const double receiverRange = ranges[receiverIndex];
      if (receiverRange >= std::min(previousRange, rightRange) &&
          receiverRange < std::max(previousRange, rightRange)) {
        for (std::size_t di = 0U; di < receivers_.depthCount(); ++di) {
          const Vec2 receiver{receiverRange, receivers_.depths()[di]};
          const Vec2 offset = receiver - path.points[left].position;
          const double weight = fortranDotProduct2D(offset, tangent) / length;
          const double q =
              leftQ + weight * (path.points[right].dynamicQ[0] - leftQ);
          if (std::abs(fortranDotProduct2D(offset, normal)) <
              std::abs(q / (path.points.front().soundSpeed / launchSpacing)))
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
