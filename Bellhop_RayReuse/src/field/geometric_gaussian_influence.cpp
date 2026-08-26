#include "rayreuse/field/geometric_gaussian_influence.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "rayreuse/error.hpp"

namespace rayreuse {
namespace {
constexpr double kBeamWindow = 4.0;
constexpr double kNearFieldFactor = static_cast<double>(0.2F);

void requireFinite(double value, std::string_view name) {
  if (!std::isfinite(value)) {
    throw ValidationError(std::string(name) + " must be finite");
  }
}

void requireFiniteComplex(std::complex<double> value,
                          std::string_view name) {
  if (!std::isfinite(value.real()) || !std::isfinite(value.imag())) {
    throw ValidationError(std::string(name) + " must be finite");
  }
}

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

std::complex<double> negativeImaginaryExponential(
    std::complex<double> phase) {
  const double magnitude = std::exp(phase.imag());
  return {magnitude * std::cos(phase.real()),
          -magnitude * std::sin(phase.real())};
}

GeometricGaussianWidthBranch classifyWidthBranch(
    double geometricSigma, double nearFieldSigma,
    double wavelengthSigma) noexcept {
  const double broadeningSigma =
      std::min(nearFieldSigma, wavelengthSigma);
  if (geometricSigma >= broadeningSigma) {
    return GeometricGaussianWidthBranch::Geometric;
  }
  return nearFieldSigma <= wavelengthSigma
             ? GeometricGaussianWidthBranch::NearField
             : GeometricGaussianWidthBranch::WavelengthCap;
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

void validateField(
    const ReceiverGrid& receivers, const FrequencyWorkspace* pressureWorkspace,
    const IntensityWorkspace* intensityWorkspace, const RayPath& path,
    const RayFrequencyState& state, double launchSpacing,
    const std::optional<GeometricGaussianDiagnosticRequest>&
        diagnosticRequest) {
  validate(receivers, path, state, launchSpacing);
  if ((pressureWorkspace == nullptr) == (intensityWorkspace == nullptr)) {
    throw ValidationError(
        "geometric Gaussian field influence requires exactly one workspace");
  }
  const double workspaceFrequency =
      pressureWorkspace != nullptr ? pressureWorkspace->frequency()
                                   : intensityWorkspace->frequency();
  const std::size_t workspaceDepthCount =
      pressureWorkspace != nullptr ? pressureWorkspace->depthCount()
                                   : intensityWorkspace->depthCount();
  const std::size_t workspaceRangeCount =
      pressureWorkspace != nullptr ? pressureWorkspace->rangeCount()
                                   : intensityWorkspace->rangeCount();
  if (workspaceFrequency != state.frequency ||
      workspaceDepthCount != receivers.depthCount() ||
      workspaceRangeCount != receivers.rangeCount()) {
    throw ValidationError(
        "geometric Gaussian workspace metadata must match the run");
  }
  bool inactiveSeen = false;
  for (const RayFrequencyPoint& point : state.points) {
    if (inactiveSeen && point.active) {
      throw ValidationError(
          "geometric Gaussian active prefix must be contiguous");
    }
    inactiveSeen = inactiveSeen || !point.active;
  }
  for (const RayState& point : path.points) {
    if (!isFinite(point.position) || !isFinite(point.slowness) ||
        !std::isfinite(point.dynamicQ[0U]) ||
        !std::isfinite(point.soundSpeed) || point.soundSpeed <= 0.0 ||
        !std::isfinite(point.realTravelTime)) {
      throw ValidationError(
          "geometric Gaussian ray path contains an invalid state");
    }
  }
  if (pressureWorkspace != nullptr) {
    for (const std::complex<double> pressure : pressureWorkspace->pressure()) {
      requireFiniteComplex(pressure,
                           "geometric Gaussian existing workspace pressure");
    }
  } else {
    for (const double intensity : intensityWorkspace->intensity()) {
      if (!std::isfinite(intensity) || intensity < 0.0) {
        throw ValidationError(
            "geometric Gaussian existing workspace intensity must be finite "
            "and non-negative");
      }
    }
  }
  if (diagnosticRequest.has_value() &&
      (diagnosticRequest->receiverRangeIndex >= receivers.rangeCount() ||
       diagnosticRequest->receiverDepthIndex >= receivers.depthCount())) {
    throw ValidationError(
        "geometric Gaussian diagnostic receiver index is out of range");
  }
}
}  // namespace

GeometricGaussianInfluence::GeometricGaussianInfluence(ReceiverGrid receivers)
    : receivers_(std::move(receivers)) {}

std::optional<GeometricGaussianDiagnostic>
GeometricGaussianInfluence::accumulate(
    FrequencyWorkspace& workspace, const RayPath& path,
    const RayFrequencyState& frequencyState,
    double launchAngleSpacingRadians,
    std::optional<GeometricGaussianDiagnosticRequest>
        diagnosticRequest) const {
  return accumulateField(&workspace, nullptr, path, frequencyState,
                         launchAngleSpacingRadians, diagnosticRequest);
}

std::optional<GeometricGaussianDiagnostic>
GeometricGaussianInfluence::accumulateIntensity(
    IntensityWorkspace& workspace, const RayPath& path,
    const RayFrequencyState& frequencyState,
    double launchAngleSpacingRadians,
    std::optional<GeometricGaussianDiagnosticRequest>
        diagnosticRequest) const {
  return accumulateField(nullptr, &workspace, path, frequencyState,
                         launchAngleSpacingRadians, diagnosticRequest);
}

std::optional<GeometricGaussianDiagnostic>
GeometricGaussianInfluence::accumulateField(
    FrequencyWorkspace* pressureWorkspace,
    IntensityWorkspace* intensityWorkspace, const RayPath& path,
    const RayFrequencyState& state, double launchSpacing,
    std::optional<GeometricGaussianDiagnosticRequest>
        diagnosticRequest) const {
  validateField(receivers_, pressureWorkspace, intensityWorkspace, path, state,
                launchSpacing, diagnosticRequest);
  std::optional<GeometricGaussianDiagnostic> diagnostic;
  if (diagnosticRequest.has_value()) {
    diagnostic.emplace();
    diagnostic->receiverRangeIndex =
        diagnosticRequest->receiverRangeIndex;
    diagnostic->receiverDepthIndex =
        diagnosticRequest->receiverDepthIndex;
  }

  const std::size_t pointCount = activeCount(state);
  const double angularFrequency =
      2.0 * std::numbers::pi * state.frequency;
  const double q0 = path.points.front().soundSpeed / launchSpacing;
  const double sourceRatio =
      std::sqrt(std::abs(std::cos(path.launchAngle))) /
      std::sqrt(2.0 * std::numbers::pi);
  requireFinite(q0, "geometric Gaussian q0");
  requireFinite(sourceRatio, "geometric Gaussian source ratio");

  const std::vector<double>& ranges = receivers_.ranges();
  const auto firstReceiver =
      std::find_if(ranges.begin(), ranges.end(), [&](double range) {
        return range > path.points.front().position.range;
      });
  std::size_t receiverIndex{};
  if (firstReceiver == ranges.end()) {
    if (path.points.front().slowness.range >= 0.0) {
      return diagnostic;
    }
    receiverIndex = ranges.size() - 1U;
  } else {
    receiverIndex =
        static_cast<std::size_t>(firstReceiver - ranges.begin());
    if (path.points.front().slowness.range < 0.0 && receiverIndex > 0U) {
      --receiverIndex;
    }
  }

  double previousRange = path.points.front().position.range;
  double phase = 0.0;
  double previousQ = path.points.front().dynamicQ[0U];
  for (std::size_t rightIndex = 1U; rightIndex < pointCount; ++rightIndex) {
    const std::size_t leftIndex = rightIndex - 1U;
    const Vec2 segment =
        path.points[rightIndex].position - path.points[leftIndex].position;
    const double segmentLength = norm(segment);
    if (segmentLength <
        1000.0 * spacing(path.points[rightIndex].position.range)) {
      continue;
    }
    const Vec2 tangent = segment / segmentLength;
    const Vec2 normal{.range = -tangent.depth, .depth = tangent.range};
    const double rightRange = path.points[rightIndex].position.range;
    const double leftQ = path.points[leftIndex].dynamicQ[0U];
    if (crosses(previousQ, leftQ)) {
      phase += std::numbers::pi / 2.0;
    }
    previousQ = leftQ;

    const double wavelength =
        path.points[leftIndex].soundSpeed / state.frequency;
    const double wavelengthSigma = std::numbers::pi * wavelength;
    double segmentSigma =
        std::max(std::abs(path.points[leftIndex].dynamicQ[0U]),
                 std::abs(path.points[rightIndex].dynamicQ[0U])) /
        q0 / std::abs(tangent.range);
    segmentSigma =
        std::max(segmentSigma,
                 std::min(kNearFieldFactor * state.frequency *
                              state.points[rightIndex]
                                  .complexTravelTime.real(),
                          wavelengthSigma));
    const double segmentRadius = kBeamWindow * segmentSigma;
    double minimumDepth = -std::numeric_limits<double>::infinity();
    double maximumDepth = std::numeric_limits<double>::infinity();
    if (std::abs(tangent.range) > 0.5) {
      minimumDepth =
          std::min(path.points[leftIndex].position.depth,
                   path.points[rightIndex].position.depth) -
          segmentRadius;
      maximumDepth =
          std::max(path.points[leftIndex].position.depth,
                   path.points[rightIndex].position.depth) +
          segmentRadius;
    }

    for (;;) {
      const double receiverRange = ranges[receiverIndex];
      if (receiverRange >= std::min(previousRange, rightRange) &&
          receiverRange < std::max(previousRange, rightRange)) {
        for (std::size_t depthIndex = 0U;
             depthIndex < receivers_.depthCount(); ++depthIndex) {
          const Vec2 receiver{.range = receiverRange,
                              .depth = receivers_.depths()[depthIndex]};
          if (receiver.depth < minimumDepth ||
              receiver.depth > maximumDepth) {
            continue;
          }
          const Vec2 offset =
              receiver - path.points[leftIndex].position;
          const double interpolationWeight =
              fortranDotProduct2D(offset, tangent) / segmentLength;
          const double normalOffset =
              std::abs(fortranDotProduct2D(offset, normal));
          const double qInterpolated =
              leftQ + interpolationWeight *
                          (path.points[rightIndex].dynamicQ[0U] - leftQ);
          const double geometricSigma = std::abs(qInterpolated / q0);
          const double nearFieldSigma =
              kNearFieldFactor * state.frequency *
              state.points[rightIndex].complexTravelTime.real();
          const double sigma1 =
              std::max(geometricSigma,
                       std::min(nearFieldSigma, wavelengthSigma));
          if (normalOffset >= kBeamWindow * sigma1) {
            continue;
          }
          const std::complex<double> delay =
              state.points[leftIndex].complexTravelTime +
              interpolationWeight *
                  (state.points[rightIndex].complexTravelTime -
                   state.points[leftIndex].complexTravelTime);
          const double amplitudeConstant =
              sourceRatio *
              std::sqrt(path.points[rightIndex].soundSpeed / (q0 * sigma1)) *
              state.points[rightIndex].amplitude;
          const double normalizedOffset = normalOffset / sigma1;
          const double gaussianWeight =
              std::sqrt(geometricSigma / sigma1) *
              std::exp(-0.5 * normalizedOffset * normalizedOffset);
          double phaseAtReceiver =
              state.points[leftIndex].reflectionPhase + phase;
          if (crosses(previousQ, qInterpolated)) {
            phaseAtReceiver += std::numbers::pi / 2.0;
          }

          requireFinite(qInterpolated,
                        "geometric Gaussian interpolated q");
          requireFinite(geometricSigma,
                        "geometric Gaussian geometric sigma");
          requireFinite(nearFieldSigma,
                        "geometric Gaussian near-field sigma");
          requireFinite(wavelengthSigma,
                        "geometric Gaussian wavelength sigma");
          requireFinite(sigma1, "geometric Gaussian sigma1");
          requireFinite(gaussianWeight, "geometric Gaussian weight");
          requireFinite(amplitudeConstant,
                        "geometric Gaussian amplitude constant");
          requireFinite(phaseAtReceiver,
                        "geometric Gaussian caustic phase");
          requireFiniteComplex(delay, "geometric Gaussian delay");

          std::complex<double> pressureIncrement{};
          double intensityIncrement = 0.0;
          if (pressureWorkspace != nullptr) {
            const double amplitude = amplitudeConstant * gaussianWeight;
            const std::complex<double> phaseArgument =
                angularFrequency * delay - phaseAtReceiver;
            pressureIncrement =
                amplitude * negativeImaginaryExponential(phaseArgument);
            requireFiniteComplex(pressureIncrement,
                                 "geometric Gaussian pressure increment");
            const std::complex<double> updated =
                pressureWorkspace->at(depthIndex, receiverIndex) +
                pressureIncrement;
            requireFiniteComplex(updated,
                                 "geometric Gaussian accumulated pressure");
            pressureWorkspace->at(depthIndex, receiverIndex) = updated;
          } else {
            const double attenuatedConstant =
                amplitudeConstant *
                std::exp((angularFrequency * delay).imag());
            const double power = attenuatedConstant * attenuatedConstant;
            intensityIncrement = std::sqrt(2.0 * std::numbers::pi) *
                                 power * gaussianWeight;
            if (!std::isfinite(intensityIncrement) ||
                intensityIncrement < 0.0) {
              throw ValidationError(
                  "geometric Gaussian intensity increment must be finite "
                  "and non-negative");
            }
            intensityWorkspace->add(depthIndex, receiverIndex,
                                    intensityIncrement);
          }

          if (diagnosticRequest.has_value() &&
              diagnosticRequest->receiverRangeIndex == receiverIndex &&
              diagnosticRequest->receiverDepthIndex == depthIndex) {
            ++diagnostic->evaluationCount;
            if (!diagnostic->evaluated) {
              diagnostic->evaluated = true;
              diagnostic->leftPointIndex = leftIndex;
              diagnostic->rightPointIndex = rightIndex;
              diagnostic->interpolationWeight = interpolationWeight;
              diagnostic->normalOffset = normalOffset;
              diagnostic->qInterpolated = qInterpolated;
              diagnostic->geometricSigma = geometricSigma;
              diagnostic->nearFieldSigma = nearFieldSigma;
              diagnostic->wavelengthSigma = wavelengthSigma;
              diagnostic->sigma1 = sigma1;
              diagnostic->widthBranch = classifyWidthBranch(
                  geometricSigma, nearFieldSigma, wavelengthSigma);
              diagnostic->gaussianWeight = gaussianWeight;
              diagnostic->amplitudeConstant = amplitudeConstant;
              diagnostic->causticPhase = phaseAtReceiver;
              diagnostic->delay = delay;
              diagnostic->pressureIncrement = pressureIncrement;
              diagnostic->intensityIncrement = intensityIncrement;
            }
          }
        }
      }

      if (rightRange > ranges[receiverIndex]) {
        if (receiverIndex + 1U >= ranges.size()) {
          break;
        }
        const std::size_t next = receiverIndex + 1U;
        if (ranges[next] >= rightRange) {
          break;
        }
        receiverIndex = next;
      } else {
        if (receiverIndex == 0U) {
          break;
        }
        const std::size_t next = receiverIndex - 1U;
        if (ranges[next] <= rightRange) {
          break;
        }
        receiverIndex = next;
      }
    }
    previousRange = rightRange;
  }
  return diagnostic;
}

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
