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
#include "rayreuse/field/broadband_arrival_workspace.hpp"
#include "rayreuse/field/fused_intensity_workspace.hpp"
#include "rayreuse/field/fused_pressure_workspace.hpp"

namespace rayreuse {
namespace {
constexpr double kBeamWindow = 4.0;
constexpr double kNearFieldFactor = static_cast<double>(0.2F);

void requireFinite(double value, std::string_view name) {
  if (!std::isfinite(value)) {
    throw ValidationError(std::string(name) + " must be finite");
  }
}

void requireFiniteComplex(std::complex<double> value, std::string_view name) {
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

std::complex<double> negativeImaginaryExponential(std::complex<double> phase) {
  const double magnitude = std::exp(phase.imag());
  return {magnitude * std::cos(phase.real()),
          -magnitude * std::sin(phase.real())};
}

GeometricGaussianWidthBranch classifyWidthBranch(
    double geometricSigma, double nearFieldSigma,
    double wavelengthSigma) noexcept {
  const double broadeningSigma = std::min(nearFieldSigma, wavelengthSigma);
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

void validateField(const ReceiverGrid& receivers,
                   const FrequencyWorkspace* pressureWorkspace,
                   const IntensityWorkspace* intensityWorkspace,
                   const RayPath& path, const RayFrequencyState& state,
                   double launchSpacing,
                   const std::optional<GeometricGaussianDiagnosticRequest>&
                       diagnosticRequest) {
  validate(receivers, path, state, launchSpacing);
  if ((pressureWorkspace == nullptr) == (intensityWorkspace == nullptr)) {
    throw ValidationError(
        "geometric Gaussian field influence requires exactly one workspace");
  }
  const double workspaceFrequency = pressureWorkspace != nullptr
                                        ? pressureWorkspace->frequency()
                                        : intensityWorkspace->frequency();
  const std::size_t workspaceDepthCount =
      pressureWorkspace != nullptr ? pressureWorkspace->depthCount()
                                   : intensityWorkspace->depthCount();
  const std::size_t workspaceRangeCount =
      pressureWorkspace != nullptr ? pressureWorkspace->rangeCount()
                                   : intensityWorkspace->rangeCount();
  if (workspaceFrequency != state.frequency ||
      workspaceDepthCount != receivers.receiversPerRange() ||
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
       diagnosticRequest->receiverDepthIndex >=
           receivers.receiversPerRange())) {
    throw ValidationError(
        "geometric Gaussian diagnostic receiver index is out of range");
  }
}

// Fused-kernel entry checks (IGR-3A A05, design §5; A04 Hat precedent): the
// per-ray conditions of the legacy validateField (validate + contiguous
// active prefix + ray-state finiteness) with fused-prefixed diagnostics so
// the failing layer is identifiable; the fused workspace replaces the legacy
// per-frequency workspace metadata checks. The legacy per-call
// whole-workspace payload rescan is deliberately not restored (CC A02 / Hat
// A04 precedent: the store-time finite checks preserve the accumulation
// contract). Templated over the fused workspace kind (both payloads expose
// the same dimension checks).
template <typename FusedWorkspace>
void validateFusedGaussianInput(const FusedWorkspace& workspace,
                                std::span<const double> frequencies,
                                const RayPath& path,
                                std::span<const RayFrequencyState>
                                    frequencyStates,
                                const ReceiverGrid& receivers,
                                double launchSpacing) {
  const std::size_t frequencyCount = frequencyStates.size();
  if (frequencyCount == 0U) {
    throw ValidationError(
        "fused geometric Gaussian influence requires at least one frequency");
  }
  if (workspace.frequencyCount() != frequencyCount ||
      frequencies.size() != frequencyCount) {
    throw ValidationError(
        "fused geometric Gaussian influence requires workspace, frequency, "
        "and frequency-state dimensions of equal size");
  }
  if (workspace.depthCount() != receivers.receiversPerRange() ||
      workspace.rangeCount() != receivers.rangeCount()) {
    throw ValidationError(
        "fused geometric Gaussian workspace and receiver-grid sizes must "
        "match");
  }
  if (!std::isfinite(launchSpacing) || launchSpacing <= 0.0) {
    throw ValidationError(
        "geometric Gaussian launch-angle spacing must be positive and finite");
  }
  if (path.points.size() < 2U) {
    throw ValidationError(
        "fused geometric Gaussian geometry and frequency point counts must "
        "match");
  }
  if (receivers.depthCount() == 0U || receivers.rangeCount() == 0U) {
    throw ValidationError("geometric Gaussian requires non-empty receivers");
  }
  for (std::size_t frequencyIndex = 0U; frequencyIndex < frequencyCount;
       ++frequencyIndex) {
    const RayFrequencyState& state = frequencyStates[frequencyIndex];
    if (frequencies[frequencyIndex] != state.frequency) {
      throw ValidationError(
          "fused geometric Gaussian workspace and ray frequencies must match");
    }
    if (path.points.size() != state.points.size()) {
      throw ValidationError(
          "fused geometric Gaussian geometry and frequency point counts must "
          "match");
    }
    if (state.points.front().active == false) {
      throw ValidationError("geometric Gaussian source point must be active");
    }
    bool inactiveSeen = false;
    for (const RayFrequencyPoint& point : state.points) {
      if (inactiveSeen && point.active) {
        throw ValidationError(
            "geometric Gaussian active prefix must be contiguous");
      }
      inactiveSeen = inactiveSeen || !point.active;
      if (!std::isfinite(point.amplitude) || point.amplitude < 0.0 ||
          !std::isfinite(point.reflectionPhase) ||
          !std::isfinite(point.complexTravelTime.real()) ||
          !std::isfinite(point.complexTravelTime.imag())) {
        throw ValidationError("geometric Gaussian frequency point is invalid");
      }
    }
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
}
}  // namespace

GeometricGaussianInfluence::GeometricGaussianInfluence(
    ReceiverGrid receivers, SourceGeometry sourceGeometry)
    : receivers_(std::move(receivers)), sourceGeometry_(sourceGeometry) {}

std::optional<GeometricGaussianDiagnostic>
GeometricGaussianInfluence::accumulate(
    FrequencyWorkspace& workspace, const RayPath& path,
    const RayFrequencyState& frequencyState, double launchAngleSpacingRadians,
    std::optional<GeometricGaussianDiagnosticRequest> diagnosticRequest) const {
  return accumulateField(&workspace, nullptr, path, frequencyState,
                         launchAngleSpacingRadians, diagnosticRequest);
}

std::optional<GeometricGaussianDiagnostic>
GeometricGaussianInfluence::accumulateIntensity(
    IntensityWorkspace& workspace, const RayPath& path,
    const RayFrequencyState& frequencyState, double launchAngleSpacingRadians,
    std::optional<GeometricGaussianDiagnosticRequest> diagnosticRequest) const {
  return accumulateField(nullptr, &workspace, path, frequencyState,
                         launchAngleSpacingRadians, diagnosticRequest);
}

std::optional<GeometricGaussianDiagnostic>
GeometricGaussianInfluence::accumulateField(
    FrequencyWorkspace* pressureWorkspace,
    IntensityWorkspace* intensityWorkspace, const RayPath& path,
    const RayFrequencyState& state, double launchSpacing,
    std::optional<GeometricGaussianDiagnosticRequest> diagnosticRequest) const {
  validateField(receivers_, pressureWorkspace, intensityWorkspace, path, state,
                launchSpacing, diagnosticRequest);
  std::optional<GeometricGaussianDiagnostic> diagnostic;
  if (diagnosticRequest.has_value()) {
    diagnostic.emplace();
    diagnostic->receiverRangeIndex = diagnosticRequest->receiverRangeIndex;
    diagnostic->receiverDepthIndex = diagnosticRequest->receiverDepthIndex;
  }

  const std::size_t pointCount = activeCount(state);
  const double angularFrequency = 2.0 * std::numbers::pi * state.frequency;
  const double q0 = path.points.front().soundSpeed / launchSpacing;
  const double sourceRatio =
      sourceGeometry_ == SourceGeometry::Point
          ? std::sqrt(std::abs(std::cos(path.launchAngle))) /
                std::sqrt(2.0 * std::numbers::pi)
          : 1.0 / std::sqrt(2.0 * std::numbers::pi);
  requireFinite(q0, "geometric Gaussian q0");
  requireFinite(sourceRatio, "geometric Gaussian source ratio");

  const std::vector<double>& ranges = receivers_.ranges();
  const auto firstReceiver = std::find_if(
      ranges.begin(), ranges.end(),
      [&](double range) { return range > path.points.front().position.range; });
  std::size_t receiverIndex{};
  if (firstReceiver == ranges.end()) {
    if (path.points.front().slowness.range >= 0.0) {
      return diagnostic;
    }
    receiverIndex = ranges.size() - 1U;
  } else {
    receiverIndex = static_cast<std::size_t>(firstReceiver - ranges.begin());
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
                              state.points[rightIndex].complexTravelTime.real(),
                          wavelengthSigma));
    const double segmentRadius = kBeamWindow * segmentSigma;
    double minimumDepth = -std::numeric_limits<double>::infinity();
    double maximumDepth = std::numeric_limits<double>::infinity();
    if (std::abs(tangent.range) > 0.5) {
      minimumDepth = std::min(path.points[leftIndex].position.depth,
                              path.points[rightIndex].position.depth) -
                     segmentRadius;
      maximumDepth = std::max(path.points[leftIndex].position.depth,
                              path.points[rightIndex].position.depth) +
                     segmentRadius;
    }

    for (;;) {
      const double receiverRange = ranges[receiverIndex];
      if (receiverRange >= std::min(previousRange, rightRange) &&
          receiverRange < std::max(previousRange, rightRange)) {
        // Origin InfluenceGeoGaussianCart pairs each irregular receiver
        // range with Pos%Rz(ir); rectilinear grids keep the shared depth
        // rows.
        for (std::size_t depthIndex = 0U;
             depthIndex < receivers_.receiversPerRange(); ++depthIndex) {
          const Vec2 receiver{
              .range = receiverRange,
              .depth = receivers_.depthAt(depthIndex, receiverIndex)};
          if (receiver.depth < minimumDepth || receiver.depth > maximumDepth) {
            continue;
          }
          const Vec2 offset = receiver - path.points[leftIndex].position;
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
          const double sigma1 = std::max(
              geometricSigma, std::min(nearFieldSigma, wavelengthSigma));
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

          requireFinite(qInterpolated, "geometric Gaussian interpolated q");
          requireFinite(geometricSigma, "geometric Gaussian geometric sigma");
          requireFinite(nearFieldSigma, "geometric Gaussian near-field sigma");
          requireFinite(wavelengthSigma, "geometric Gaussian wavelength sigma");
          requireFinite(sigma1, "geometric Gaussian sigma1");
          requireFinite(gaussianWeight, "geometric Gaussian weight");
          requireFinite(amplitudeConstant,
                        "geometric Gaussian amplitude constant");
          requireFinite(phaseAtReceiver, "geometric Gaussian caustic phase");
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
                amplitudeConstant * std::exp((angularFrequency * delay).imag());
            const double power = attenuatedConstant * attenuatedConstant;
            intensityIncrement =
                std::sqrt(2.0 * std::numbers::pi) * power * gaussianWeight;
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
  const double sourceRatio =
      sourceGeometry_ == SourceGeometry::Point
          ? std::sqrt(std::abs(std::cos(path.launchAngle))) /
                std::sqrt(2.0 * std::numbers::pi)
          : 1.0 / std::sqrt(2.0 * std::numbers::pi);
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
        for (std::size_t di = 0U; di < receivers_.receiversPerRange(); ++di) {
          const double receiverDepth = receivers_.depthAt(di, receiverIndex);
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
        for (std::size_t di = 0U; di < receivers_.receiversPerRange(); ++di) {
          const double receiverDepth = receivers_.depthAt(di, receiverIndex);
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

void GeometricGaussianInfluence::setFusedLaunchAngleStep(
    double launchAngleStep) {
  fusedLaunchAngleStep_ = launchAngleStep;
}

bool GeometricGaussianInfluence::accumulateFusedPrevalidated(
    FusedPressureWorkspace& workspace,
    std::span<const double> frequencies, const RayPath& path,
    std::span<const RayFrequencyState> frequencyStates,
    std::size_t rangeBegin, std::size_t rangeEnd,
    CartesianCervenyStatistics* statistics) const {
  // The legacy Gaussian kernel produces no influence counters; the pointer is
  // accepted for adapter-shape uniformity (design §3.4 documents the
  // zero-counter envelope for non-CC families).
  static_cast<void>(statistics);
  return accumulateFusedImpl<false, false>(
      workspace, frequencies, path, frequencyStates, rangeBegin, rangeEnd);
}

bool GeometricGaussianInfluence::accumulateFusedIntensityPrevalidated(
    FusedIntensityWorkspace& workspace,
    std::span<const double> frequencies, const RayPath& path,
    std::span<const RayFrequencyState> frequencyStates,
    std::size_t rangeBegin, std::size_t rangeEnd,
    CartesianCervenyStatistics* statistics) const {
  static_cast<void>(statistics);
  return accumulateFusedImpl<true, false>(
      workspace, frequencies, path, frequencyStates, rangeBegin, rangeEnd);
}

bool GeometricGaussianInfluence::accumulateFusedArrivalsPrevalidated(
    BroadbandArrivalWorkspace& workspace,
    std::span<const double> frequencies, const RayPath& path,
    std::span<const RayFrequencyState> frequencyStates,
    std::size_t rangeBegin, std::size_t rangeEnd,
    ArrivalAccumulationStatistics& statistics) const {
  return accumulateFusedImpl<false, true>(
      workspace, frequencies, path, frequencyStates, rangeBegin, rangeEnd,
      &statistics);
}

// IGR-3A A05 fused kernel (design §5/§8): entry validation, then the single
// legacy Cartesian traversal. The coherent and intensity twins share one
// traversal with a per-lane payload branch at the store, mirroring the
// legacy single-traversal accumulateField split.
template <bool IntensityPayload, bool ArrivalPayload, typename Workspace>
bool GeometricGaussianInfluence::accumulateFusedImpl(
    Workspace& workspace, std::span<const double> frequencies,
    const RayPath& path, std::span<const RayFrequencyState> frequencyStates,
    std::size_t rangeBegin, std::size_t rangeEnd,
    ArrivalAccumulationStatistics* arrivalStatistics) const {
  validateFusedGaussianInput(workspace, frequencies, path, frequencyStates,
                             receivers_, fusedLaunchAngleStep_);
  if (rangeBegin >= rangeEnd || rangeEnd > workspace.rangeCount()) {
    throw ValidationError("fused geometric Gaussian range partition is invalid");
  }

  const std::size_t frequencyCount = frequencyStates.size();
  std::vector<std::size_t> activePrefixPointCount(frequencyCount);
  std::vector<double> angularFrequency(frequencyCount);
  std::size_t unionPrefix = 0U;
  for (std::size_t frequencyIndex = 0U; frequencyIndex < frequencyCount;
       ++frequencyIndex) {
    // Exact per-frequency active-prefix scan of the legacy kernel (the
    // first inactive point is retained).
    activePrefixPointCount[frequencyIndex] =
        activeCount(frequencyStates[frequencyIndex]);
    unionPrefix =
        std::max(unionPrefix, activePrefixPointCount[frequencyIndex]);
    angularFrequency[frequencyIndex] =
        2.0 * std::numbers::pi * frequencyStates[frequencyIndex].frequency;
  }
  const double q0 = path.points.front().soundSpeed / fusedLaunchAngleStep_;
  const double sourceRatio =
      sourceGeometry_ == SourceGeometry::Point
          ? std::sqrt(std::abs(std::cos(path.launchAngle))) /
                std::sqrt(2.0 * std::numbers::pi)
          : 1.0 / std::sqrt(2.0 * std::numbers::pi);
  requireFinite(q0, "geometric Gaussian q0");
  requireFinite(sourceRatio, "geometric Gaussian source ratio");

  std::vector<std::int32_t> top;
  std::vector<std::int32_t> bottom;
  if constexpr (ArrivalPayload) {
    top.assign(unionPrefix, 0);
    bottom.assign(unionPrefix, 0);
    for (const auto& event : path.events) {
      const std::size_t reflected = event.reflectedRayPointIndex;
      if (reflected != event.rayPointIndex + 1U ||
          reflected >= path.points.size()) {
        throw ValidationError(
            "geometric Gaussian reflection event has invalid indices");
      }
      if (reflected >= unionPrefix) continue;
      ++(event.boundary == ReflectionBoundary::SeaSurface ? top[reflected]
                                                           : bottom[reflected]);
    }
    for (std::size_t index = 1U; index < unionPrefix; ++index) {
      top[index] += top[index - 1U];
      bottom[index] += bottom[index - 1U];
    }
  }

  const std::vector<double>& ranges = receivers_.ranges();
  const auto firstReceiver = std::find_if(
      ranges.begin(), ranges.end(),
      [&](double range) { return range > path.points.front().position.range; });
  std::size_t receiverIndex{};
  if (firstReceiver == ranges.end()) {
    if (path.points.front().slowness.range >= 0.0) {
      return true;
    }
    receiverIndex = ranges.size() - 1U;
  } else {
    receiverIndex = static_cast<std::size_t>(firstReceiver - ranges.begin());
    if (path.points.front().slowness.range < 0.0 && receiverIndex > 0U) {
      --receiverIndex;
    }
  }

  // Per-lane segment-level width state, recomputed every segment exactly
  // where the legacy per-frequency loop computes it (design §8: the beam
  // width sigma1 is frequency-local — wavelength and near-field branches —
  // so receiver eligibility windows and depth prefilters are evaluated per
  // frequency exactly as legacy, NOT lifted to frequency-independent
  // geometry). Allocated once per ray; the entries are pure functions of
  // (segment, lane).
  std::vector<double> wavelengthSigma(frequencyCount);
  std::vector<double> nearFieldSigma(frequencyCount);
  std::vector<double> broadeningSigma(frequencyCount);
  std::vector<double> segmentSigma(frequencyCount);
  std::vector<double> minimumDepth(frequencyCount);
  std::vector<double> maximumDepth(frequencyCount);

  double previousRange = path.points.front().position.range;
  double phase = 0.0;
  double previousQ = path.points.front().dynamicQ[0U];
  for (std::size_t rightIndex = 1U; rightIndex < unionPrefix; ++rightIndex) {
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

    // Shared frequency-independent q term of the legacy segment sigma.
    const double segmentQTerm =
        std::max(std::abs(path.points[leftIndex].dynamicQ[0U]),
                 std::abs(path.points[rightIndex].dynamicQ[0U])) /
        q0 / std::abs(tangent.range);
    const bool depthWindowed = std::abs(tangent.range) > 0.5;
    const double segmentMinimumDepth =
        std::min(path.points[leftIndex].position.depth,
                 path.points[rightIndex].position.depth);
    const double segmentMaximumDepth =
        std::max(path.points[leftIndex].position.depth,
                 path.points[rightIndex].position.depth);
    for (std::size_t frequencyIndex = 0U; frequencyIndex < frequencyCount;
         ++frequencyIndex) {
      const RayFrequencyState& state = frequencyStates[frequencyIndex];
      const double wavelength =
          path.points[leftIndex].soundSpeed / state.frequency;
      wavelengthSigma[frequencyIndex] = std::numbers::pi * wavelength;
      // Same association as the legacy segment and receiver expressions:
      // (kNearFieldFactor * frequency) * Re(tau_right).
      nearFieldSigma[frequencyIndex] =
          kNearFieldFactor * state.frequency *
          state.points[rightIndex].complexTravelTime.real();
      broadeningSigma[frequencyIndex] =
          std::min(nearFieldSigma[frequencyIndex],
                   wavelengthSigma[frequencyIndex]);
      segmentSigma[frequencyIndex] =
          std::max(segmentQTerm, broadeningSigma[frequencyIndex]);
      const double segmentRadius =
          kBeamWindow * segmentSigma[frequencyIndex];
      minimumDepth[frequencyIndex] =
          depthWindowed ? segmentMinimumDepth - segmentRadius
                        : -std::numeric_limits<double>::infinity();
      maximumDepth[frequencyIndex] =
          depthWindowed ? segmentMaximumDepth + segmentRadius
                        : std::numeric_limits<double>::infinity();
    }

    for (;;) {
      const double receiverRange = ranges[receiverIndex];
      if (receiverRange >= std::min(previousRange, rightRange) &&
          receiverRange < std::max(previousRange, rightRange) &&
          receiverIndex >= rangeBegin && receiverIndex < rangeEnd) {
        // Origin InfluenceGeoGaussianCart pairs each irregular receiver
        // range with Pos%Rz(ir); rectilinear grids keep the shared depth
        // rows.
        for (std::size_t depthIndex = 0U;
             depthIndex < receivers_.receiversPerRange(); ++depthIndex) {
          const double receiverDepth =
              receivers_.depthAt(depthIndex, receiverIndex);
          const Vec2 receiver{
              .range = receiverRange, .depth = receiverDepth};
          const Vec2 offset = receiver - path.points[leftIndex].position;
          const double interpolationWeight =
              fortranDotProduct2D(offset, tangent) / segmentLength;
          const double normalOffset =
              std::abs(fortranDotProduct2D(offset, normal));
          const double qInterpolated =
              leftQ + interpolationWeight *
                          (path.points[rightIndex].dynamicQ[0U] - leftQ);
          const double geometricSigma = std::abs(qInterpolated / q0);
          const bool receiverCausticCross = crosses(previousQ, qInterpolated);
          for (std::size_t frequencyIndex = 0U;
               frequencyIndex < frequencyCount; ++frequencyIndex) {
            // That frequency's own legacy loop bound.
            if (rightIndex >= activePrefixPointCount[frequencyIndex]) {
              continue;
            }
            // Legacy per-frequency depth prefilter (segment sigma window).
            if (receiverDepth < minimumDepth[frequencyIndex] ||
                receiverDepth > maximumDepth[frequencyIndex]) {
              continue;
            }
            // Legacy per-frequency eligibility window: sigma1 is
            // frequency-local, evaluated exactly at the legacy point.
            const double sigma1 =
                std::max(geometricSigma, broadeningSigma[frequencyIndex]);
            if (normalOffset >= kBeamWindow * sigma1) {
              continue;
            }
            const RayFrequencyState& state =
                frequencyStates[frequencyIndex];
            const std::complex<double> delay =
                state.points[leftIndex].complexTravelTime +
                interpolationWeight *
                    (state.points[rightIndex].complexTravelTime -
                     state.points[leftIndex].complexTravelTime);
            const double amplitudeConstant =
                sourceRatio *
                std::sqrt(path.points[rightIndex].soundSpeed /
                          (q0 * sigma1)) *
                state.points[rightIndex].amplitude;
            const double normalizedOffset = normalOffset / sigma1;
            const double gaussianWeight =
                std::sqrt(geometricSigma / sigma1) *
                std::exp(-0.5 * normalizedOffset * normalizedOffset);
            double phaseAtReceiver =
                state.points[leftIndex].reflectionPhase + phase;
            if (receiverCausticCross) {
              phaseAtReceiver += std::numbers::pi / 2.0;
            }

            requireFinite(qInterpolated,
                          "geometric Gaussian interpolated q");
            requireFinite(geometricSigma, "geometric Gaussian geometric sigma");
            requireFinite(nearFieldSigma[frequencyIndex],
                          "geometric Gaussian near-field sigma");
            requireFinite(wavelengthSigma[frequencyIndex],
                          "geometric Gaussian wavelength sigma");
            requireFinite(sigma1, "geometric Gaussian sigma1");
            requireFinite(gaussianWeight, "geometric Gaussian weight");
            requireFinite(amplitudeConstant,
                          "geometric Gaussian amplitude constant");
            requireFinite(phaseAtReceiver, "geometric Gaussian caustic phase");
            requireFiniteComplex(delay, "geometric Gaussian delay");

            if constexpr (ArrivalPayload) {
              workspace.addCandidate(
                  frequencyIndex,
                  ArrivalCandidate{
                      .amplitude = amplitudeConstant * gaussianWeight,
                      .phaseRadians = phaseAtReceiver,
                      .delaySeconds = delay,
                      .sourceDeclinationDegrees =
                          path.launchAngle * (180.0 / std::numbers::pi),
                      .receiverDeclinationDegrees =
                          std::atan2(tangent.depth, tangent.range) *
                          (180.0 / std::numbers::pi),
                      .topBounceCount = top[rightIndex],
                      .bottomBounceCount = bottom[rightIndex]},
                  depthIndex, receiverIndex, *arrivalStatistics);
            } else if constexpr (IntensityPayload) {
              // Legacy intensity branch: the attenuated real constant is
              // squared, the Gaussian weight applied once, with the extra
              // sqrt(2 pi) factor of the family (design §8; the store-time
              // checks reproduce IntensityWorkspace::add's contract with
              // the legacy message verbatim).
              const double attenuatedConstant =
                  amplitudeConstant *
                  std::exp(
                      (angularFrequency[frequencyIndex] * delay).imag());
              const double power = attenuatedConstant * attenuatedConstant;
              const double intensityIncrement =
                  std::sqrt(2.0 * std::numbers::pi) * power * gaussianWeight;
              if (!std::isfinite(intensityIncrement) ||
                  intensityIncrement < 0.0) {
                throw ValidationError(
                    "geometric Gaussian intensity increment must be finite "
                    "and non-negative");
              }
              double& intensityValue =
                  workspace.cell(receiverIndex, depthIndex)[frequencyIndex];
              const double updatedIntensity =
                  intensityValue + intensityIncrement;
              if (!std::isfinite(updatedIntensity)) {
                throw ValidationError(
                    "accumulated intensity must remain finite");
              }
              intensityValue = updatedIntensity;
            } else {
              const double amplitude = amplitudeConstant * gaussianWeight;
              const std::complex<double> phaseArgument =
                  angularFrequency[frequencyIndex] * delay - phaseAtReceiver;
              const std::complex<double> pressureIncrement =
                  amplitude * negativeImaginaryExponential(phaseArgument);
              requireFiniteComplex(pressureIncrement,
                                   "geometric Gaussian pressure increment");
              std::complex<double>& pressureValue =
                  workspace.cell(receiverIndex, depthIndex)[frequencyIndex];
              const std::complex<double> updatedPressure =
                  pressureValue + pressureIncrement;
              requireFiniteComplex(updatedPressure,
                                   "geometric Gaussian accumulated pressure");
              pressureValue = updatedPressure;
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
  return true;
}

}  // namespace rayreuse
