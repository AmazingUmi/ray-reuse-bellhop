#include "rayreuse/field/ray_centered_cerveny_influence.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <limits>
#include <numbers>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "rayreuse/error.hpp"
#include "rayreuse/field/fused_intensity_workspace.hpp"

namespace rayreuse {
namespace {

[[nodiscard]] bool finiteComplex(std::complex<double> value) noexcept {
  return std::isfinite(value.real()) && std::isfinite(value.imag());
}

void requireFinite(double value, std::string_view name) {
  if (!std::isfinite(value)) {
    throw ValidationError(std::string(name) + " must be finite");
  }
}

void requireFiniteComplex(std::complex<double> value, std::string_view name) {
  if (!finiteComplex(value)) {
    throw ValidationError(std::string(name) + " must be finite");
  }
}

[[nodiscard]] double floatingSpacing(double value) {
  return std::nextafter(value, std::numeric_limits<double>::infinity()) - value;
}

[[nodiscard]] std::complex<double> negativeImaginaryExponential(
    std::complex<double> phase) {
  const double magnitude = std::exp(phase.imag());
  return {magnitude * std::cos(phase.real()),
          -magnitude * std::sin(phase.real())};
}

void validateUniformReceiverRanges(const ReceiverGrid& receivers,
                                   double rangeDelta) {
  if (receivers.rangeCount() < 2U) {
    throw ValidationError(
        "ray-centered Cerveny requires at least two receiver ranges");
  }
  const std::vector<double>& ranges = receivers.ranges();
  for (std::size_t index = 2U; index < ranges.size(); ++index) {
    const double expected =
        ranges.front() + static_cast<double>(index) * rangeDelta;
    const double tolerance =
        32.0 * std::numeric_limits<double>::epsilon() *
        std::max({1.0, std::abs(expected), std::abs(ranges[index])});
    if (std::abs(ranges[index] - expected) > tolerance) {
      throw ValidationError(
          "ray-centered Cerveny receiver ranges must be equally spaced");
    }
  }
}

[[nodiscard]] std::size_t clampedReceiverIndex1Based(
    double range, const ReceiverGrid& receivers, double rangeDelta) {
  const double raw =
      std::trunc((range - receivers.ranges().front()) / rangeDelta) + 1.0;
  if (raw <= 1.0) {
    return 1U;
  }
  if (raw >= static_cast<double>(receivers.rangeCount())) {
    return receivers.rangeCount();
  }
  return static_cast<std::size_t>(raw);
}

struct PrecomputedRayValues {
  std::vector<std::complex<double>> q;
  std::vector<std::complex<double>> gamma;
  std::vector<Vec2> normal;
  std::vector<int> kmah;
};

[[nodiscard]] PrecomputedRayValues precomputeRayValues(
    const RayPath& path, std::complex<double> epsilon, std::size_t pointCount,
    BeamWidthMode widthMode) {
  PrecomputedRayValues values;
  values.q.reserve(pointCount);
  values.gamma.reserve(pointCount);
  values.normal.reserve(pointCount);
  values.kmah.reserve(pointCount);
  for (std::size_t index = 0U; index < pointCount; ++index) {
    const RayState& point = path.points[index];
    const std::complex<double> p =
        point.dynamicP[0U] + epsilon * point.dynamicP[1U];
    const std::complex<double> q =
        point.dynamicQ[0U] + epsilon * point.dynamicQ[1U];
    if (q == std::complex<double>{}) {
      throw ValidationError("ray-centered Cerveny q must not be zero");
    }
    const std::complex<double> gamma = p / q;
    const Vec2 tangent = point.soundSpeed * point.slowness;
    const Vec2 normal{.range = tangent.depth, .depth = -tangent.range};
    requireFiniteComplex(p, "ray-centered Cerveny p");
    requireFiniteComplex(q, "ray-centered Cerveny q");
    requireFiniteComplex(gamma, "ray-centered Cerveny gamma");
    if (!isFinite(normal)) {
      throw ValidationError("ray-centered Cerveny normal must be finite");
    }
    values.q.push_back(q);
    values.gamma.push_back(gamma);
    values.normal.push_back(normal);
    int kmah = index == 0U ? 1 : values.kmah.back();
    if (index != 0U) {
      kmah = updateCervenyKmah(values.q[index - 1U], q, kmah, widthMode);
    }
    values.kmah.push_back(kmah);
  }
  return values;
}

// Fused-only L1 layout (IGR-3A A03, design §5): at a fixed ray point, all
// frequency lanes of the epsilon-dependent quantities are contiguous. The
// normal is NOT part of this layout — it depends only on the tangent
// c·slowness and is therefore shared across lanes (the per-lane image flip
// lives in the traversal's per-lane sign state, design §8).
struct FusedRayCenteredRayValues {
  std::size_t frequencyCount{};
  std::vector<std::complex<double>> q;
  std::vector<std::complex<double>> gamma;
  std::vector<int> kmah;

  [[nodiscard]] std::size_t index(std::size_t pointIndex,
                                  std::size_t frequencyIndex) const noexcept {
    return pointIndex * frequencyCount + frequencyIndex;
  }
};

// Fused-kernel entry checks (design §5): identical conditions to
// validateAccumulateInput with fused-prefixed diagnostics so the failing
// layer is identifiable; templated over the fused workspace kind (both
// payloads expose the same depth/range-count checks).
template <typename FusedWorkspace>
void validateFusedRayCenteredInput(const FusedWorkspace& workspace,
                                   double frequency, const RayPath& path,
                                   const RayFrequencyState& frequencyState,
                                   std::complex<double> epsilon,
                                   const ReceiverGrid& receivers,
                                   BeamWidthMode widthMode) {
  if (path.points.size() < 2U ||
      path.points.size() != frequencyState.points.size()) {
    throw ValidationError(
        "fused ray-centered Cerveny geometry and frequency-state sizes must "
        "match and be non-empty");
  }
  if (frequency != frequencyState.frequency) {
    throw ValidationError(
        "fused ray-centered Cerveny workspace and ray frequencies must match");
  }
  if (workspace.depthCount() != receivers.depthCount() ||
      workspace.rangeCount() != receivers.rangeCount()) {
    throw ValidationError(
        "fused ray-centered Cerveny workspace and receiver-grid sizes must "
        "match");
  }
  if (!frequencyState.points.front().active) {
    throw ValidationError(
        "fused ray-centered Cerveny source frequency point must be active");
  }
  bool inactiveSeen = false;
  for (const RayFrequencyPoint& point : frequencyState.points) {
    if (inactiveSeen && point.active) {
      throw ValidationError(
          "fused ray-centered Cerveny active prefix must be contiguous");
    }
    inactiveSeen = inactiveSeen || !point.active;
    requireFiniteComplex(point.complexTravelTime,
                         "ray-centered Cerveny complex travel time");
    requireFinite(point.amplitude, "ray-centered Cerveny amplitude");
    requireFinite(point.reflectionPhase,
                  "ray-centered Cerveny reflection phase");
    if (point.active && point.amplitude < 0.0) {
      throw ValidationError(
          "ray-centered Cerveny active amplitude must be non-negative");
    }
  }
  for (const RayState& point : path.points) {
    if (!isFinite(point.position) || !isFinite(point.slowness) ||
        !std::isfinite(point.dynamicP[0U]) ||
        !std::isfinite(point.dynamicP[1U]) ||
        !std::isfinite(point.dynamicQ[0U]) ||
        !std::isfinite(point.dynamicQ[1U]) ||
        !std::isfinite(point.soundSpeed) || point.soundSpeed <= 0.0 ||
        !std::isfinite(point.realTravelTime)) {
      throw ValidationError(
          "ray-centered Cerveny ray path contains an invalid state");
    }
  }
  requireFiniteComplex(epsilon, "ray-centered Cerveny epsilon");
  if ((widthMode == BeamWidthMode::SpaceFilling ||
       widthMode == BeamWidthMode::MinimumWidth) &&
      (epsilon.real() != 0.0 || epsilon.imag() <= 0.0)) {
    throw ValidationError(
        "ray-centered F/M epsilon must be positive imaginary");
  }
  if (widthMode == BeamWidthMode::Wkb && epsilon.imag() != 0.0) {
    throw ValidationError("ray-centered WKB epsilon must be real");
  }
}

// Fused twin of precomputeRayValues for one frequency lane, bounded by that
// lane's own active prefix. The frequency-independent normal is computed
// separately over the union prefix (see accumulateFusedImpl).
void precomputeFusedRayCenteredValuesForFrequency(
    const RayPath& path, std::complex<double> epsilon,
    std::size_t pointCount, BeamWidthMode widthMode,
    std::size_t frequencyIndex, FusedRayCenteredRayValues& values) {
  for (std::size_t pointIndex = 0U; pointIndex < pointCount; ++pointIndex) {
    const RayState& point = path.points[pointIndex];
    const std::complex<double> p =
        point.dynamicP[0U] + epsilon * point.dynamicP[1U];
    const std::complex<double> q =
        point.dynamicQ[0U] + epsilon * point.dynamicQ[1U];
    if (q == std::complex<double>{}) {
      throw ValidationError("ray-centered Cerveny q must not be zero");
    }
    const std::complex<double> gamma = p / q;
    requireFiniteComplex(p, "ray-centered Cerveny p");
    requireFiniteComplex(q, "ray-centered Cerveny q");
    requireFiniteComplex(gamma, "ray-centered Cerveny gamma");

    const std::size_t flatIndex = values.index(pointIndex, frequencyIndex);
    values.q[flatIndex] = q;
    values.gamma[flatIndex] = gamma;

    int kmah = 1;
    if (pointIndex != 0U) {
      const std::size_t previousFlatIndex =
          values.index(pointIndex - 1U, frequencyIndex);
      kmah = updateCervenyKmah(values.q[previousFlatIndex], q,
                               values.kmah[previousFlatIndex], widthMode);
    }
    values.kmah[flatIndex] = kmah;
  }
}

void validateAccumulateInput(
    const FrequencyWorkspace* pressureWorkspace,
    const IntensityWorkspace* intensityWorkspace, const RayPath& path,
    const RayFrequencyState& frequencyState, std::complex<double> epsilon,
    const ReceiverGrid& receivers, BeamWidthMode widthMode,
    const std::optional<RayCenteredCervenyDiagnosticRequest>& request) {
  if ((pressureWorkspace == nullptr) == (intensityWorkspace == nullptr)) {
    throw ValidationError(
        "ray-centered Cerveny requires exactly one workspace");
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
  if (path.points.size() < 2U ||
      path.points.size() != frequencyState.points.size()) {
    throw ValidationError(
        "ray-centered Cerveny geometry and frequency states must match");
  }
  if (workspaceFrequency != frequencyState.frequency ||
      workspaceDepthCount != receivers.depthCount() ||
      workspaceRangeCount != receivers.rangeCount()) {
    throw ValidationError(
        "ray-centered Cerveny workspace metadata must match the run");
  }
  if (!frequencyState.points.front().active) {
    throw ValidationError(
        "ray-centered Cerveny source frequency point must be active");
  }
  bool inactiveSeen = false;
  for (const RayFrequencyPoint& point : frequencyState.points) {
    if (inactiveSeen && point.active) {
      throw ValidationError(
          "ray-centered Cerveny active prefix must be contiguous");
    }
    inactiveSeen = inactiveSeen || !point.active;
    requireFiniteComplex(point.complexTravelTime,
                         "ray-centered Cerveny complex travel time");
    requireFinite(point.amplitude, "ray-centered Cerveny amplitude");
    requireFinite(point.reflectionPhase,
                  "ray-centered Cerveny reflection phase");
    if (point.active && point.amplitude < 0.0) {
      throw ValidationError(
          "ray-centered Cerveny active amplitude must be non-negative");
    }
  }
  for (const RayState& point : path.points) {
    if (!isFinite(point.position) || !isFinite(point.slowness) ||
        !std::isfinite(point.dynamicP[0U]) ||
        !std::isfinite(point.dynamicP[1U]) ||
        !std::isfinite(point.dynamicQ[0U]) ||
        !std::isfinite(point.dynamicQ[1U]) ||
        !std::isfinite(point.soundSpeed) || point.soundSpeed <= 0.0 ||
        !std::isfinite(point.realTravelTime)) {
      throw ValidationError(
          "ray-centered Cerveny ray path contains an invalid state");
    }
  }
  requireFiniteComplex(epsilon, "ray-centered Cerveny epsilon");
  if ((widthMode == BeamWidthMode::SpaceFilling ||
       widthMode == BeamWidthMode::MinimumWidth) &&
      (epsilon.real() != 0.0 || epsilon.imag() <= 0.0)) {
    throw ValidationError(
        "ray-centered F/M epsilon must be positive imaginary");
  }
  if (widthMode == BeamWidthMode::Wkb && epsilon.imag() != 0.0) {
    throw ValidationError("ray-centered WKB epsilon must be real");
  }
  if (request.has_value() &&
      (request->receiverRangeIndex >= receivers.rangeCount() ||
       request->receiverDepthIndex >= receivers.depthCount())) {
    throw ValidationError(
        "ray-centered diagnostic receiver index is out of range");
  }
}

}  // namespace

RayCenteredCervenyInfluence::RayCenteredCervenyInfluence(
    Environment environment, ReceiverGrid receivers,
    CartesianCervenySettings settings, BeamWidthMode widthMode,
    SimulationRunMode runMode, FieldComponent fieldComponent,
    SourceGeometry sourceGeometry)
    : environment_(std::move(environment)),
      receivers_(std::move(receivers)),
      settings_(settings),
      widthMode_(widthMode),
      runMode_(runMode),
      fieldComponent_(fieldComponent),
      sourceGeometry_(sourceGeometry),
      receiverRangeDelta_(receivers_.rangeCount() >= 2U
                              ? receivers_.ranges()[1U] -
                                    receivers_.ranges()[0U]
                              : 0.0) {
  if (settings_.imageCount == 0U || settings_.imageCount > 3U) {
    throw ValidationError(
        "ray-centered Cerveny image count must lie in [1, 3]");
  }
  if (settings_.beamWindow <= 0) {
    throw ValidationError("ray-centered Cerveny beam window must be positive");
  }
  switch (widthMode_) {
    case BeamWidthMode::SpaceFilling:
    case BeamWidthMode::MinimumWidth:
    case BeamWidthMode::Wkb:
      break;
    default:
      throw ValidationError("ray-centered beam-width mode is invalid");
  }
  switch (fieldComponent_) {
    case FieldComponent::Pressure:
    case FieldComponent::Vertical:
    case FieldComponent::Horizontal:
      break;
    default:
      throw ValidationError("ray-centered field component is invalid");
  }
  if (!isTransmissionLossMode(runMode_)) {
    throw ValidationError(
        "ray-centered Cerveny requires a transmission-loss run mode");
  }
  validateUniformReceiverRanges(receivers_, receiverRangeDelta_);
}

std::optional<RayCenteredCervenyDiagnostic>
RayCenteredCervenyInfluence::accumulate(
    FrequencyWorkspace& workspace, const RayPath& path,
    const RayFrequencyState& frequencyState, std::complex<double> epsilon,
    std::optional<RayCenteredCervenyDiagnosticRequest> diagnosticRequest)
    const {
  if (runMode_ != SimulationRunMode::Coherent) {
    throw ValidationError(
        "ray-centered complex pressure requires coherent TL mode");
  }
  return accumulateImpl(&workspace, nullptr, path, frequencyState, epsilon,
                        diagnosticRequest);
}

std::optional<RayCenteredCervenyDiagnostic>
RayCenteredCervenyInfluence::accumulateIntensity(
    IntensityWorkspace& workspace, const RayPath& path,
    const RayFrequencyState& frequencyState, std::complex<double> epsilon,
    std::optional<RayCenteredCervenyDiagnosticRequest> diagnosticRequest)
    const {
  if (runMode_ != SimulationRunMode::Incoherent &&
      runMode_ != SimulationRunMode::SemiCoherent) {
    throw ValidationError(
        "ray-centered intensity requires incoherent or semi-coherent mode");
  }
  return accumulateImpl(nullptr, &workspace, path, frequencyState, epsilon,
                        diagnosticRequest);
}

std::optional<RayCenteredCervenyDiagnostic>
RayCenteredCervenyInfluence::accumulateImpl(
    FrequencyWorkspace* pressureWorkspace,
    IntensityWorkspace* intensityWorkspace, const RayPath& path,
    const RayFrequencyState& frequencyState, std::complex<double> epsilon,
    std::optional<RayCenteredCervenyDiagnosticRequest> diagnosticRequest)
    const {
  validateAccumulateInput(pressureWorkspace, intensityWorkspace, path,
                          frequencyState, epsilon, receivers_, widthMode_,
                          diagnosticRequest);
  std::optional<RayCenteredCervenyDiagnostic> diagnostic;
  if (diagnosticRequest.has_value()) {
    diagnostic.emplace();
    diagnostic->receiverRangeIndex = diagnosticRequest->receiverRangeIndex;
    diagnostic->receiverDepthIndex = diagnosticRequest->receiverDepthIndex;
  }

  std::size_t activePointCount = path.points.size();
  for (std::size_t index = 0U; index < frequencyState.points.size(); ++index) {
    if (!frequencyState.points[index].active) {
      activePointCount = index + 1U;
      break;
    }
  }
  const PrecomputedRayValues ray =
      precomputeRayValues(path, epsilon, activePointCount, widthMode_);
  const double angularFrequency =
      2.0 * std::numbers::pi * frequencyState.frequency;
  const double radiusMax =
      30.0 * path.points.front().soundSpeed / frequencyState.frequency;
  const double beamWindowSquared = static_cast<double>(settings_.beamWindow) *
                                   static_cast<double>(settings_.beamWindow);
  const double ratio = sourceGeometry_ == SourceGeometry::Line
                           ? 1.0
                           : std::sqrt(std::abs(std::cos(path.launchAngle)));
  // Origin initializes rnV once on entry. Its legacy whole-array flips then
  // persist not only across images, but also across receiver depths.
  std::vector<Vec2> imageNormals = ray.normal;

  for (std::size_t depthIndex = 0U; depthIndex < receivers_.depthCount();
       ++depthIndex) {
    const double receiverDepth = receivers_.depths()[depthIndex];
    for (std::size_t imageIndex = 0U; imageIndex < settings_.imageCount;
         ++imageIndex) {
      const CervenyImageKind imageKind =
          imageIndex == 0U ? CervenyImageKind::True
                           : (imageIndex == 1U ? CervenyImageKind::Surface
                                               : CervenyImageKind::Bottom);
      std::size_t previousReceiverIndex1Based =
          std::numeric_limits<std::size_t>::max();
      double previousProjectedRange = 0.0;
      double previousNormalOffset = 0.0;

      for (std::size_t rightIndex = 1U; rightIndex < activePointCount;
           ++rightIndex) {
        // Origin tests znV before executing the image-specific rnV=-rnV
        // assignment. A skipped near-horizontal step therefore must not
        // change the persistent flip state.
        if (std::abs(imageNormals[rightIndex].depth) <
            std::numeric_limits<double>::epsilon()) {
          continue;
        }
        // Origin's array assignment rnV=-rnV is inside the stepping loop for
        // surface and bottom images. It therefore flips every stored normal
        // on every step, including the legacy alternating orientation that
        // this produces within each image.
        if (imageIndex != 0U) {
          for (Vec2& imageNormal : imageNormals) {
            imageNormal.range = -imageNormal.range;
          }
        }
        const RayState& rightPoint = path.points[rightIndex];
        const Vec2 endpointNormal = imageNormals[rightIndex];
        double imageDepth = rightPoint.position.depth;
        if (imageKind == CervenyImageKind::Surface) {
          imageDepth = 2.0 * environment_.seaSurface().depth() - imageDepth;
        } else if (imageKind == CervenyImageKind::Bottom) {
          imageDepth = 2.0 * environment_.seabed().depth() - imageDepth;
        }
        const double normalOffset =
            (receiverDepth - imageDepth) / endpointNormal.depth;
        const double projectedRange =
            rightPoint.position.range + normalOffset * endpointNormal.range;
        const std::size_t upperReceiverIndex1Based = clampedReceiverIndex1Based(
            projectedRange, receivers_, receiverRangeDelta_);
        const bool duplicatePoint =
            std::abs(rightPoint.position.range -
                     path.points[rightIndex - 1U].position.range) <
            1000.0 * floatingSpacing(rightPoint.position.range);
        if (previousReceiverIndex1Based >= upperReceiverIndex1Based ||
            duplicatePoint) {
          previousProjectedRange = projectedRange;
          previousNormalOffset = normalOffset;
          previousReceiverIndex1Based = upperReceiverIndex1Based;
          continue;
        }

        for (std::size_t receiverIndex1Based = previousReceiverIndex1Based + 1U;
             receiverIndex1Based <= upperReceiverIndex1Based;
             ++receiverIndex1Based) {
          const std::size_t rangeIndex = receiverIndex1Based - 1U;
          const double weight =
              (receivers_.ranges()[rangeIndex] - previousProjectedRange) /
              (projectedRange - previousProjectedRange);
          const std::complex<double> q =
              ray.q[rightIndex - 1U] +
              weight * (ray.q[rightIndex] - ray.q[rightIndex - 1U]);
          const std::complex<double> gamma =
              ray.gamma[rightIndex - 1U] +
              weight * (ray.gamma[rightIndex] - ray.gamma[rightIndex - 1U]);
          const double normal = previousNormalOffset +
                                weight * (normalOffset - previousNormalOffset);
          const double normalSquared = normal * normal;
          requireFinite(weight, "ray-centered interpolation weight");
          requireFiniteComplex(q, "ray-centered interpolated q");
          requireFiniteComplex(gamma, "ray-centered interpolated gamma");
          requireFinite(normal, "ray-centered normal offset");
          if (gamma.imag() > 0.0 ||
              -0.5 * angularFrequency * gamma.imag() * normalSquared >=
                  beamWindowSquared) {
            continue;
          }
          const double soundSpeed = path.points[rightIndex - 1U].soundSpeed;
          const std::complex<double> tau =
              frequencyState.points[rightIndex - 1U].complexTravelTime +
              weight *
                  (frequencyState.points[rightIndex].complexTravelTime -
                   frequencyState.points[rightIndex - 1U].complexTravelTime);
          std::complex<double> contribution =
              ratio * frequencyState.points[rightIndex].amplitude *
              std::sqrt(soundSpeed * std::abs(epsilon) / q) *
              negativeImaginaryExponential(
                  angularFrequency * (tau + 0.5 * gamma * normalSquared) -
                  frequencyState.points[rightIndex].reflectionPhase);
          const Vec2 slowness = rightPoint.slowness;
          if (fieldComponent_ != FieldComponent::Pressure) {
            const std::complex<double> normalDerivative =
                std::complex<double>{0.0, -1.0} * angularFrequency * gamma *
                normal * contribution;
            const std::complex<double> alongDerivative =
                std::complex<double>{0.0, -1.0} * angularFrequency /
                soundSpeed * contribution;
            if (fieldComponent_ == FieldComponent::Vertical) {
              // Fortran DOT_PRODUCT conjugates its first argument when it is
              // complex. Reflect that asymmetric legacy rule for V; the H
              // branch below is handwritten and is not conjugated in Origin.
              contribution =
                  soundSpeed * (std::conj(normalDerivative) * slowness.range +
                                std::conj(alongDerivative) * slowness.depth);
            } else {
              contribution = soundSpeed * (-normalDerivative * slowness.depth +
                                           alongDerivative * slowness.range);
            }
          }
          int kmah = ray.kmah[rightIndex - 1U];
          kmah = updateCervenyKmah(ray.q[rightIndex - 1U], q, kmah, widthMode_);
          if (kmah < 0) {
            contribution = -contribution;
          }
          if (imageKind == CervenyImageKind::Surface) {
            contribution = -contribution;
          }
          requireFiniteComplex(contribution,
                               "ray-centered pressure contribution");
          const double taper =
              cervenyHermiteTaper(normal, radiusMax, 2.0 * radiusMax);
          double intensityIncrement = 0.0;
          if (pressureWorkspace != nullptr) {
            const std::complex<double> increment = taper * contribution;
            const std::complex<double> updated =
                pressureWorkspace->at(depthIndex, rangeIndex) + increment;
            requireFiniteComplex(updated, "ray-centered accumulated pressure");
            pressureWorkspace->at(depthIndex, rangeIndex) = updated;
          } else {
            const double magnitude = std::abs(contribution);
            const double power = magnitude * magnitude;
            intensityIncrement = taper * power;
            intensityWorkspace->add(depthIndex, rangeIndex, intensityIncrement);
          }

          if (diagnosticRequest.has_value() &&
              diagnosticRequest->receiverRangeIndex == rangeIndex &&
              diagnosticRequest->receiverDepthIndex == depthIndex) {
            ++diagnostic->evaluationCount;
            if (!diagnostic->evaluated) {
              diagnostic->evaluated = true;
              diagnostic->leftPointIndex = rightIndex - 1U;
              diagnostic->rightPointIndex = rightIndex;
              diagnostic->imageKind = imageKind;
              diagnostic->interpolationWeight = weight;
              diagnostic->normalOffset = normal;
              diagnostic->hermiteTaper = taper;
              diagnostic->kmahFinal = kmah;
              diagnostic->qInterpolated = q;
              diagnostic->gammaInterpolated = gamma;
              diagnostic->pressureContribution = contribution;
              diagnostic->intensityIncrement = intensityIncrement;
            }
          }
        }
        previousProjectedRange = projectedRange;
        previousNormalOffset = normalOffset;
        previousReceiverIndex1Based = upperReceiverIndex1Based;
      }
    }
  }
  return diagnostic;
}

bool RayCenteredCervenyInfluence::accumulateFusedPrevalidated(
    FusedPressureWorkspace& workspace,
    std::span<const double> frequencies, const RayPath& path,
    std::span<const RayFrequencyState> frequencyStates,
    std::span<const std::complex<double>> epsilons,
    std::size_t rangeBegin, std::size_t rangeEnd,
    CartesianCervenyStatistics* statistics) const {
  return accumulateFusedImpl<false>(workspace, frequencies, path,
                                    frequencyStates, epsilons, rangeBegin,
                                    rangeEnd, statistics);
}

bool RayCenteredCervenyInfluence::accumulateFusedIntensityPrevalidated(
    FusedIntensityWorkspace& workspace,
    std::span<const double> frequencies, const RayPath& path,
    std::span<const RayFrequencyState> frequencyStates,
    std::span<const std::complex<double>> epsilons,
    std::size_t rangeBegin, std::size_t rangeEnd,
    CartesianCervenyStatistics* statistics) const {
  return accumulateFusedImpl<true>(workspace, frequencies, path,
                                   frequencyStates, epsilons, rangeBegin,
                                   rangeEnd, statistics);
}

// Production fused RayReuse kernel (IGR-3A A03, design §5/§8): one shared
// traversal for the coherent and intensity twins with a per-lane payload
// branch at the store, mirroring the legacy single-traversal accumulateImpl.
// Loop skeleton = legacy depth -> image -> step -> ascending receiver run;
// the step loop runs to the UNION of the per-frequency active prefixes and
// every per-lane gate (prefix bound, window, masks) reproduces that
// frequency's legacy conditions exactly. The A03-specific contract is the
// PERSISTENT IMAGE-NORMAL FLIP (design §8): legacy initializes imageNormals
// once before the depth loop and its whole-array range negations persist
// across depths AND images, evolved in (depth, image, step) order bounded by
// that frequency's own active prefix — so each frequency lane carries its
// own sign state here (normalSign), reset per ray, flipped exactly where the
// legacy non-True-image accepted step flips. The near-horizontal gate reads
// the depth component of the normal, which the flip never touches, so the
// gate itself is frequency-independent. Flip parity enters projectedRange
// via endpointNormal.range and therefore the receiver coverage and
// interpolation weights of that lane. Frequency-local quantities
// (q/gamma/kmah via epsilon, omega, radiusMax, window, taper,
// tau/reflectionPhase/amplitude, V-H factors) are evaluated per lane with
// the legacy expressions verbatim. The statistics pointer is accepted for
// adapter-shape uniformity; the legacy per-frequency RC kernel produces no
// influence counters, so none are collected (design §3.4 documents the
// zero-counter envelope for non-CC families).
template <bool IntensityPayload, typename Workspace>
bool RayCenteredCervenyInfluence::accumulateFusedImpl(
    Workspace& workspace, std::span<const double> frequencies,
    const RayPath& path, std::span<const RayFrequencyState> frequencyStates,
    std::span<const std::complex<double>> epsilons,
    std::size_t rangeBegin, std::size_t rangeEnd,
    CartesianCervenyStatistics* statistics) const {
  static_cast<void>(statistics);
  // Entry-kind validation mirrors the public per-frequency entries.
  if constexpr (IntensityPayload) {
    if (runMode_ != SimulationRunMode::Incoherent &&
        runMode_ != SimulationRunMode::SemiCoherent) {
      throw ValidationError(
          "ray-centered intensity requires incoherent or semi-coherent mode");
    }
  } else {
    if (runMode_ != SimulationRunMode::Coherent) {
      throw ValidationError(
          "ray-centered complex pressure requires coherent TL mode");
    }
  }
  const std::size_t frequencyCount = frequencyStates.size();
  if (frequencyCount == 0U) {
    throw ValidationError(
        "fused ray-centered Cerveny influence requires at least one "
        "frequency");
  }
  if (workspace.frequencyCount() != frequencyCount ||
      frequencies.size() != frequencyCount ||
      epsilons.size() != frequencyCount) {
    throw ValidationError(
        "fused ray-centered Cerveny influence requires workspace, frequency, "
        "frequency-state, and epsilon dimensions of equal size");
  }
  if (workspace.depthCount() != receivers_.depthCount() ||
      workspace.rangeCount() != receivers_.rangeCount()) {
    throw ValidationError(
        "fused ray-centered Cerveny workspace and receiver-grid sizes must "
        "match");
  }
  if (rangeBegin >= rangeEnd || rangeEnd > workspace.rangeCount()) {
    throw ValidationError(
        "fused ray-centered Cerveny range partition is invalid");
  }
  for (std::size_t frequencyIndex = 0U; frequencyIndex < frequencyCount;
       ++frequencyIndex) {
    validateFusedRayCenteredInput(workspace, frequencies[frequencyIndex], path,
                                  frequencyStates[frequencyIndex],
                                  epsilons[frequencyIndex], receivers_,
                                  widthMode_);
  }

  // Per-lane active prefixes (the first inactive point is retained, exactly
  // as the legacy scan) and per-lane scalars; traversal bound = union.
  std::vector<std::size_t> activePrefixPointCount(frequencyCount);
  std::vector<double> angularFrequency(frequencyCount);
  std::vector<double> radiusMax(frequencyCount);
  std::size_t unionPrefix = 0U;
  for (std::size_t frequencyIndex = 0U; frequencyIndex < frequencyCount;
       ++frequencyIndex) {
    std::size_t prefixPointCount = path.points.size();
    for (std::size_t index = 0U;
         index < frequencyStates[frequencyIndex].points.size(); ++index) {
      if (!frequencyStates[frequencyIndex].points[index].active) {
        prefixPointCount = index + 1U;
        break;
      }
    }
    activePrefixPointCount[frequencyIndex] = prefixPointCount;
    unionPrefix = std::max(unionPrefix, prefixPointCount);
    angularFrequency[frequencyIndex] =
        2.0 * std::numbers::pi * frequencyStates[frequencyIndex].frequency;
    radiusMax[frequencyIndex] =
        30.0 * path.points.front().soundSpeed /
        frequencyStates[frequencyIndex].frequency;
  }
  const std::size_t fusedValueCount = unionPrefix * frequencyCount;
  FusedRayCenteredRayValues ray{
      .frequencyCount = frequencyCount,
      .q = std::vector<std::complex<double>>(fusedValueCount),
      .gamma = std::vector<std::complex<double>>(fusedValueCount),
      .kmah = std::vector<int>(fusedValueCount),
  };
  // The normal depends only on the tangent c·slowness, never on epsilon, so
  // it is computed once over the UNION prefix (never per lane): the shared
  // near-horizontal gate and the per-lane endpoint normals read it at every
  // step any lane reaches.
  std::vector<Vec2> normal(unionPrefix);
  for (std::size_t pointIndex = 0U; pointIndex < unionPrefix; ++pointIndex) {
    const RayState& point = path.points[pointIndex];
    const Vec2 tangent = point.soundSpeed * point.slowness;
    normal[pointIndex] = Vec2{.range = tangent.depth, .depth = -tangent.range};
    if (!isFinite(normal[pointIndex])) {
      throw ValidationError("ray-centered Cerveny normal must be finite");
    }
  }
  for (std::size_t frequencyIndex = 0U; frequencyIndex < frequencyCount;
       ++frequencyIndex) {
    // Preserve the ascending-frequency, ascending-point evaluation sequence
    // and each lane's own prefix. Rectangular inactive tails are storage
    // only and are never read by the per-lane prefix gates.
    precomputeFusedRayCenteredValuesForFrequency(
        path, epsilons[frequencyIndex],
        activePrefixPointCount[frequencyIndex], widthMode_, frequencyIndex,
        ray);
  }

  const double beamWindowSquared = static_cast<double>(settings_.beamWindow) *
                                   static_cast<double>(settings_.beamWindow);
  const double ratio = sourceGeometry_ == SourceGeometry::Line
                           ? 1.0
                           : std::sqrt(std::abs(std::cos(path.launchAngle)));
  const std::vector<double>& receiverRanges = receivers_.ranges();
  // Per-lane traversal state (design §7): the persistent flip sign is reset
  // once per ray (legacy imageNormals = ray.normal before the depth loop);
  // the receiver-run anchors are reset per (depth, image) exactly where the
  // legacy locals are re-declared inside the image loop.
  std::vector<double> normalSign(frequencyCount, 1.0);
  std::vector<std::size_t> previousReceiverIndex1Based(frequencyCount);
  std::vector<double> previousProjectedRange(frequencyCount);
  std::vector<double> previousNormalOffset(frequencyCount);

  for (std::size_t depthIndex = 0U; depthIndex < receivers_.depthCount();
       ++depthIndex) {
    const double receiverDepth = receivers_.depths()[depthIndex];
    for (std::size_t imageIndex = 0U; imageIndex < settings_.imageCount;
         ++imageIndex) {
      const CervenyImageKind imageKind =
          imageIndex == 0U ? CervenyImageKind::True
                           : (imageIndex == 1U ? CervenyImageKind::Surface
                                               : CervenyImageKind::Bottom);
      for (std::size_t frequencyIndex = 0U; frequencyIndex < frequencyCount;
           ++frequencyIndex) {
        previousReceiverIndex1Based[frequencyIndex] =
            std::numeric_limits<std::size_t>::max();
        previousProjectedRange[frequencyIndex] = 0.0;
        previousNormalOffset[frequencyIndex] = 0.0;
      }

      for (std::size_t rightIndex = 1U; rightIndex < unionPrefix;
           ++rightIndex) {
        // Origin tests znV before executing the image-specific rnV=-rnV
        // assignment: a skipped near-horizontal step must not change the
        // persistent flip state. The depth component is never flipped, so
        // this gate is frequency-independent.
        if (std::abs(normal[rightIndex].depth) <
            std::numeric_limits<double>::epsilon()) {
          continue;
        }
        // Origin's array assignment rnV=-rnV is inside the stepping loop for
        // surface and bottom images, flipping every stored normal on every
        // accepted step; per-lane legacy-exact evolution is bounded by that
        // lane's own active prefix (design §8).
        if (imageIndex != 0U) {
          for (std::size_t frequencyIndex = 0U;
               frequencyIndex < frequencyCount; ++frequencyIndex) {
            if (rightIndex < activePrefixPointCount[frequencyIndex]) {
              normalSign[frequencyIndex] = -normalSign[frequencyIndex];
            }
          }
        }
        const RayState& rightPoint = path.points[rightIndex];
        double imageDepth = rightPoint.position.depth;
        if (imageKind == CervenyImageKind::Surface) {
          imageDepth = 2.0 * environment_.seaSurface().depth() - imageDepth;
        } else if (imageKind == CervenyImageKind::Bottom) {
          imageDepth = 2.0 * environment_.seabed().depth() - imageDepth;
        }
        const bool duplicatePoint =
            std::abs(rightPoint.position.range -
                     path.points[rightIndex - 1U].position.range) <
            1000.0 * floatingSpacing(rightPoint.position.range);

        for (std::size_t frequencyIndex = 0U; frequencyIndex < frequencyCount;
             ++frequencyIndex) {
          // That frequency's own legacy loop bound.
          if (rightIndex >= activePrefixPointCount[frequencyIndex]) {
            continue;
          }
          const Vec2 endpointNormal{
              .range = normalSign[frequencyIndex] * normal[rightIndex].range,
              .depth = normal[rightIndex].depth};
          const double normalOffset =
              (receiverDepth - imageDepth) / endpointNormal.depth;
          const double projectedRange =
              rightPoint.position.range + normalOffset * endpointNormal.range;
          const std::size_t upperReceiverIndex1Based =
              clampedReceiverIndex1Based(projectedRange, receivers_,
                                         receiverRangeDelta_);
          if (previousReceiverIndex1Based[frequencyIndex] >=
                  upperReceiverIndex1Based ||
              duplicatePoint) {
            previousProjectedRange[frequencyIndex] = projectedRange;
            previousNormalOffset[frequencyIndex] = normalOffset;
            previousReceiverIndex1Based[frequencyIndex] =
                upperReceiverIndex1Based;
            continue;
          }
          // Receiver run of this lane, intersected with the worker's
          // partition [rangeBegin, rangeEnd); the run anchors keep their
          // legacy values so later weights are unchanged by the clamp.
          const std::size_t firstReceiverIndex1Based = std::max(
              previousReceiverIndex1Based[frequencyIndex] + 1U,
              rangeBegin + 1U);
          const std::size_t lastReceiverIndex1Based =
              std::min(upperReceiverIndex1Based, rangeEnd);
          const std::size_t leftFlatIndex =
              ray.index(rightIndex - 1U, frequencyIndex);
          const std::size_t rightFlatIndex =
              ray.index(rightIndex, frequencyIndex);
          for (std::size_t receiverIndex1Based = firstReceiverIndex1Based;
               receiverIndex1Based <= lastReceiverIndex1Based;
               ++receiverIndex1Based) {
            const std::size_t rangeIndex = receiverIndex1Based - 1U;
            const double weight =
                (receiverRanges[rangeIndex] -
                 previousProjectedRange[frequencyIndex]) /
                (projectedRange - previousProjectedRange[frequencyIndex]);
            const std::complex<double> q =
                ray.q[leftFlatIndex] +
                weight * (ray.q[rightFlatIndex] - ray.q[leftFlatIndex]);
            const std::complex<double> gamma =
                ray.gamma[leftFlatIndex] +
                weight *
                    (ray.gamma[rightFlatIndex] - ray.gamma[leftFlatIndex]);
            const double normalOffsetInterpolated =
                previousNormalOffset[frequencyIndex] +
                weight *
                    (normalOffset - previousNormalOffset[frequencyIndex]);
            const double normalSquared =
                normalOffsetInterpolated * normalOffsetInterpolated;
            requireFinite(weight, "ray-centered interpolation weight");
            requireFiniteComplex(q, "ray-centered interpolated q");
            requireFiniteComplex(gamma, "ray-centered interpolated gamma");
            requireFinite(normalOffsetInterpolated,
                          "ray-centered normal offset");
            if (gamma.imag() > 0.0 ||
                -0.5 * angularFrequency[frequencyIndex] * gamma.imag() *
                        normalSquared >=
                    beamWindowSquared) {
              continue;
            }
            const double soundSpeed =
                path.points[rightIndex - 1U].soundSpeed;
            const std::complex<double> tau =
                frequencyStates[frequencyIndex]
                    .points[rightIndex - 1U]
                    .complexTravelTime +
                weight *
                    (frequencyStates[frequencyIndex]
                         .points[rightIndex]
                         .complexTravelTime -
                     frequencyStates[frequencyIndex]
                         .points[rightIndex - 1U]
                         .complexTravelTime);
            std::complex<double> contribution =
                ratio *
                frequencyStates[frequencyIndex].points[rightIndex].amplitude *
                std::sqrt(soundSpeed * std::abs(epsilons[frequencyIndex]) /
                          q) *
                negativeImaginaryExponential(
                    angularFrequency[frequencyIndex] *
                        (tau + 0.5 * gamma * normalSquared) -
                    frequencyStates[frequencyIndex]
                        .points[rightIndex]
                        .reflectionPhase);
            const Vec2 slowness = rightPoint.slowness;
            if (fieldComponent_ != FieldComponent::Pressure) {
              const std::complex<double> normalDerivative =
                  std::complex<double>{0.0, -1.0} *
                  angularFrequency[frequencyIndex] * gamma *
                  normalOffsetInterpolated * contribution;
              const std::complex<double> alongDerivative =
                  std::complex<double>{0.0, -1.0} *
                  angularFrequency[frequencyIndex] / soundSpeed *
                  contribution;
              if (fieldComponent_ == FieldComponent::Vertical) {
                // Fortran DOT_PRODUCT conjugates its first argument when it
                // is complex. Reflect that asymmetric legacy rule for V; the
                // H branch below is handwritten and is not conjugated in
                // Origin.
                contribution =
                    soundSpeed *
                    (std::conj(normalDerivative) * slowness.range +
                     std::conj(alongDerivative) * slowness.depth);
              } else {
                contribution =
                    soundSpeed *
                    (-normalDerivative * slowness.depth +
                     alongDerivative * slowness.range);
              }
            }
            int kmah = ray.kmah[leftFlatIndex];
            kmah = updateCervenyKmah(ray.q[leftFlatIndex], q, kmah, widthMode_);
            if (kmah < 0) {
              contribution = -contribution;
            }
            if (imageKind == CervenyImageKind::Surface) {
              contribution = -contribution;
            }
            requireFiniteComplex(contribution,
                                 "ray-centered pressure contribution");
            const double taper = cervenyHermiteTaper(
                normalOffsetInterpolated, radiusMax[frequencyIndex],
                2.0 * radiusMax[frequencyIndex]);
            if constexpr (IntensityPayload) {
              // Legacy intensity branch: taper multiplies ABS(contribution)^2
              // (abs then multiply; std::norm is forbidden, design §8), with
              // the IntensityWorkspace::add validation messages verbatim,
              // applied as a read-add-assign on the fused lane.
              const double magnitude = std::abs(contribution);
              const double power = magnitude * magnitude;
              const double intensityIncrement = taper * power;
              if (!std::isfinite(intensityIncrement) ||
                  intensityIncrement < 0.0) {
                throw ValidationError(
                    "intensity contribution must be finite and non-negative");
              }
              double& intensityValue =
                  workspace.cell(rangeIndex, depthIndex)[frequencyIndex];
              const double updatedIntensity =
                  intensityValue + intensityIncrement;
              if (!std::isfinite(updatedIntensity)) {
                throw ValidationError(
                    "accumulated intensity must remain finite");
              }
              intensityValue = updatedIntensity;
            } else {
              const std::complex<double> increment = taper * contribution;
              std::complex<double>& pressureValue =
                  workspace.cell(rangeIndex, depthIndex)[frequencyIndex];
              const std::complex<double> updatedPressure =
                  pressureValue + increment;
              requireFiniteComplex(updatedPressure,
                                   "ray-centered accumulated pressure");
              pressureValue = updatedPressure;
            }
          }
          previousProjectedRange[frequencyIndex] = projectedRange;
          previousNormalOffset[frequencyIndex] = normalOffset;
          previousReceiverIndex1Based[frequencyIndex] =
              upperReceiverIndex1Based;
        }
      }
    }
  }
  return true;
}

}  // namespace rayreuse
