#include "rayreuse/field/ray_centered_cerveny_influence.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <limits>
#include <numbers>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "rayreuse/error.hpp"

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

void requireFiniteComplex(std::complex<double> value,
                          std::string_view name) {
  if (!finiteComplex(value)) {
    throw ValidationError(std::string(name) + " must be finite");
  }
}

[[nodiscard]] double floatingSpacing(double value) {
  return std::nextafter(value, std::numeric_limits<double>::infinity()) -
         value;
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
    const RayPath& path, std::complex<double> epsilon,
    std::size_t pointCount, BeamWidthMode widthMode) {
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
  const double workspaceFrequency =
      pressureWorkspace != nullptr ? pressureWorkspace->frequency()
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
      receiverRangeDelta_(
          receivers_.rangeCount() >= 2U
              ? receivers_.ranges()[1U] - receivers_.ranges()[0U]
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
    std::optional<RayCenteredCervenyDiagnosticRequest>
        diagnosticRequest) const {
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
    std::optional<RayCenteredCervenyDiagnosticRequest>
        diagnosticRequest) const {
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
    std::optional<RayCenteredCervenyDiagnosticRequest>
        diagnosticRequest) const {
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
  const double beamWindowSquared =
      static_cast<double>(settings_.beamWindow) *
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
          imageIndex == 0U
              ? CervenyImageKind::True
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
          imageDepth =
              2.0 * environment_.seaSurface().depth() - imageDepth;
        } else if (imageKind == CervenyImageKind::Bottom) {
          imageDepth = 2.0 * environment_.seabed().depth() - imageDepth;
        }
        const double normalOffset =
            (receiverDepth - imageDepth) / endpointNormal.depth;
        const double projectedRange =
            rightPoint.position.range + normalOffset * endpointNormal.range;
        const std::size_t upperReceiverIndex1Based =
            clampedReceiverIndex1Based(projectedRange, receivers_,
                                       receiverRangeDelta_);
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

        for (std::size_t receiverIndex1Based =
                 previousReceiverIndex1Based + 1U;
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
              weight *
                  (ray.gamma[rightIndex] - ray.gamma[rightIndex - 1U]);
          const double normal =
              previousNormalOffset +
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
          const double soundSpeed =
              path.points[rightIndex - 1U].soundSpeed;
          const std::complex<double> tau =
              frequencyState.points[rightIndex - 1U].complexTravelTime +
              weight *
                  (frequencyState.points[rightIndex].complexTravelTime -
                   frequencyState.points[rightIndex - 1U]
                       .complexTravelTime);
          std::complex<double> contribution =
              ratio * frequencyState.points[rightIndex].amplitude *
              std::sqrt(soundSpeed * std::abs(epsilon) / q) *
              negativeImaginaryExponential(
                  angularFrequency *
                          (tau + 0.5 * gamma * normalSquared) -
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
          int kmah = ray.kmah[rightIndex - 1U];
          kmah = updateCervenyKmah(ray.q[rightIndex - 1U], q, kmah,
                                  widthMode_);
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
            requireFiniteComplex(updated,
                                 "ray-centered accumulated pressure");
            pressureWorkspace->at(depthIndex, rangeIndex) = updated;
          } else {
            const double magnitude = std::abs(contribution);
            const double power = magnitude * magnitude;
            intensityIncrement = taper * power;
            intensityWorkspace->add(depthIndex, rangeIndex,
                                    intensityIncrement);
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

}  // namespace rayreuse
