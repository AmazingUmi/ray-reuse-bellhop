#include "bellhop/field/cartesian_cerveny_influence.hpp"

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

#include "bellhop/error.hpp"

namespace bellhop {
namespace {

[[nodiscard]] bool finiteComplex(
    std::complex<double> value) noexcept {
  return std::isfinite(value.real()) &&
         std::isfinite(value.imag());
}

void requireFinite(double value, std::string_view name) {
  if (!std::isfinite(value)) {
    throw ValidationError(
        std::string(name) + " must be finite");
  }
}

void requireFiniteComplex(std::complex<double> value,
                          std::string_view name) {
  if (!finiteComplex(value)) {
    throw ValidationError(
        std::string(name) + " must be finite");
  }
}

[[nodiscard]] double floatingSpacing(double value) {
  const double next =
      std::nextafter(value, std::numeric_limits<double>::infinity());
  return next - value;
}

[[nodiscard]] std::complex<double> negativeImaginaryExponential(
    std::complex<double> phase) {
  const double magnitude = std::exp(phase.imag());
  return {
      magnitude * std::cos(phase.real()),
      -magnitude * std::sin(phase.real())};
}

[[nodiscard]] std::size_t fortranUpperRangeIndex(
    double range, const std::vector<double>& receivers,
    double rangeDelta) {
  const double rawIndex =
      std::trunc((range - receivers.front()) / rangeDelta) + 1.0;
  if (rawIndex <= 1.0) {
    return 1U;
  }
  const double count = static_cast<double>(receivers.size());
  if (rawIndex >= count) {
    return receivers.size();
  }
  return static_cast<std::size_t>(rawIndex);
}

void validateUniformReceiverRanges(const ReceiverGrid& receivers,
                                   double rangeDelta) {
  if (receivers.rangeCount() < 2U) {
    throw ValidationError(
        "Cartesian Cerveny influence requires at least two receiver ranges");
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
          "Cartesian Cerveny receiver ranges must be equally spaced");
    }
  }
}

void validateAccumulateInput(
    const FrequencyWorkspace* pressureWorkspace,
    const IntensityWorkspace* intensityWorkspace, const RayPath& path,
    const RayFrequencyState& frequencyState,
    std::complex<double> epsilon, const ReceiverGrid& receivers,
    BeamWidthMode widthMode,
    const std::optional<CartesianCervenyDiagnosticRequest>& request) {
  if ((pressureWorkspace == nullptr) == (intensityWorkspace == nullptr)) {
    throw ValidationError(
        "Cartesian Cerveny influence requires exactly one workspace");
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
  if (path.points.empty()) {
    throw ValidationError(
        "Cartesian Cerveny influence requires a non-empty ray path");
  }
  if (path.points.size() != frequencyState.points.size()) {
    throw ValidationError(
        "Cartesian Cerveny geometry and frequency-state sizes must match");
  }
  if (workspaceFrequency != frequencyState.frequency) {
    throw ValidationError(
        "Cartesian Cerveny workspace and ray frequencies must match");
  }
  if (!frequencyState.points.front().active) {
    throw ValidationError(
        "Cartesian Cerveny source frequency point must be active");
  }
  bool inactiveSeen = false;
  for (const RayFrequencyPoint& point : frequencyState.points) {
    if (inactiveSeen && point.active) {
      throw ValidationError(
          "Cartesian Cerveny active state must remain inactive once stopped");
    }
    inactiveSeen = inactiveSeen || !point.active;
    requireFiniteComplex(
        point.complexTravelTime,
        "Cartesian Cerveny complex travel time");
    requireFinite(point.amplitude, "Cartesian Cerveny amplitude");
    requireFinite(
        point.reflectionPhase, "Cartesian Cerveny reflection phase");
    // RefCoef.f90 makes its table-domain decision in REAL(4), then forms the
    // interpolation weight from the original REAL(8) angle.  A table with a
    // zero endpoint can therefore produce a tiny negative extrapolated
    // amplitude inside half a REAL(4) ULP.  Bellhop immediately marks that
    // point inactive via its amplitude cutoff, but retains it as the terminal
    // Beam%Nsteps point.  Preserve that terminal value while continuing to
    // reject a negative amplitude on any point that remains active.
    if (point.active && point.amplitude < 0.0) {
      throw ValidationError(
          "Cartesian Cerveny active amplitude must be non-negative");
    }
  }
  if (workspaceDepthCount != receivers.receiversPerRange() ||
      workspaceRangeCount != receivers.rangeCount()) {
    throw ValidationError(
        "Cartesian Cerveny workspace and receiver-grid sizes must match");
  }
  if (pressureWorkspace != nullptr) {
    for (const std::complex<double> pressure :
         pressureWorkspace->pressure()) {
      requireFiniteComplex(
          pressure, "Cartesian Cerveny existing workspace pressure");
    }
  } else {
    for (const double intensity : intensityWorkspace->intensity()) {
      if (!std::isfinite(intensity) || intensity < 0.0) {
        throw ValidationError(
            "Cartesian Cerveny existing workspace intensity must be "
            "finite and non-negative");
      }
    }
  }
  if (!std::isfinite(path.launchAngle)) {
    throw ValidationError(
        "Cartesian Cerveny launch angle must be finite");
  }
  double previousTravelTime = 0.0;
  for (const RayState& point : path.points) {
    if (!isFinite(point.position) || !isFinite(point.slowness) ||
        !std::isfinite(point.dynamicP[0U]) ||
        !std::isfinite(point.dynamicP[1U]) ||
        !std::isfinite(point.dynamicQ[0U]) ||
        !std::isfinite(point.dynamicQ[1U]) ||
        !std::isfinite(point.soundSpeed) ||
        !std::isfinite(point.realTravelTime)) {
      throw ValidationError(
          "Cartesian Cerveny ray path must contain only finite states");
    }
    if (point.soundSpeed <= 0.0) {
      throw ValidationError(
          "Cartesian Cerveny ray-state sound speed must be positive");
    }
    if (point.realTravelTime < previousTravelTime) {
      throw ValidationError(
          "Cartesian Cerveny ray-state travel time must be non-decreasing");
    }
    previousTravelTime = point.realTravelTime;
  }
  requireFiniteComplex(epsilon, "Cartesian Cerveny epsilon");
  if ((widthMode == BeamWidthMode::SpaceFilling ||
       widthMode == BeamWidthMode::MinimumWidth) &&
      (epsilon.real() != 0.0 || epsilon.imag() <= 0.0)) {
    throw ValidationError(
        "space-filling/minimum-width epsilon must be positive imaginary");
  }
  if (widthMode == BeamWidthMode::Wkb && epsilon.imag() != 0.0) {
    throw ValidationError("WKB epsilon must be real");
  }
  if (request.has_value() &&
      (request->receiverRangeIndex >= receivers.rangeCount() ||
       request->receiverDepthIndex >= receivers.receiversPerRange())) {
    throw ValidationError(
        "Cartesian Cerveny diagnostic receiver index is out of range");
  }
}

struct PrecomputedRayValues {
  std::vector<std::complex<double>> p;
  std::vector<std::complex<double>> q;
  std::vector<std::complex<double>> gamma;
  std::vector<int> kmah;
};

[[nodiscard]] PrecomputedRayValues precomputeRayValues(
    const RayPath& path, const GeometrySspEvaluator& soundSpeedProfile,
    std::complex<double> epsilon, std::size_t pointCount,
    BeamWidthMode widthMode) {
  PrecomputedRayValues values;
  values.p.reserve(pointCount);
  values.q.reserve(pointCount);
  values.gamma.reserve(pointCount);
  values.kmah.reserve(pointCount);

  std::size_t segmentIndex = 0U;
  for (std::size_t index = 0U; index < pointCount; ++index) {
    const RayState& point = path.points[index];
    const SoundSpeedSample sample =
        soundSpeedProfile.evaluate(point.position, segmentIndex);
    segmentIndex = sample.segmentIndex;

    const std::complex<double> p =
        point.dynamicP[0U] + epsilon * point.dynamicP[1U];
    const std::complex<double> q =
        point.dynamicQ[0U] + epsilon * point.dynamicQ[1U];
    const Vec2 tangent = point.soundSpeed * point.slowness;
    const Vec2 normal{
        .range = tangent.depth, .depth = -tangent.range};
    const double soundSpeedSquared =
        sample.soundSpeed * sample.soundSpeed;
    const double alongGradient =
        dot(sample.soundSpeedGradient, tangent);
    const double normalGradient =
        dot(sample.soundSpeedGradient, normal);
    const double tangentRangeSquared =
        tangent.range * tangent.range;
    const double tangentDepthSquared =
        tangent.depth * tangent.depth;

    std::complex<double> gamma{};
    if (q != std::complex<double>{}) {
      gamma =
          0.5 *
          (p / q * tangentRangeSquared +
           2.0 * normalGradient / soundSpeedSquared *
               tangent.depth * tangent.range -
           alongGradient / soundSpeedSquared *
               tangentDepthSquared);
    }
    requireFiniteComplex(p, "Cartesian Cerveny p");
    requireFiniteComplex(q, "Cartesian Cerveny q");
    requireFiniteComplex(gamma, "Cartesian Cerveny gamma");
    values.p.push_back(p);
    values.q.push_back(q);
    values.gamma.push_back(gamma);

    int kmah = index == 0U ? 1 : values.kmah.back();
    if (index != 0U) {
      kmah = updateCervenyKmah(
          values.q[index - 1U], q, kmah, widthMode);
    }
    values.kmah.push_back(kmah);
  }
  return values;
}

[[nodiscard]] CartesianCervenyImageDiagnostic evaluateImage(
    CervenyImageKind kind, double receiverDepth,
    double interpolatedDepth, double seaSurfaceDepth,
    double seabedDepth, double angularFrequency,
    double beamWindowSquared, double radiusMax,
    Vec2 interpolatedSlowness,
    std::complex<double> tau,
    std::complex<double> gamma, double amplitude,
    double reflectionPhase) {
  double deltaDepth = 0.0;
  double polarity = 1.0;
  switch (kind) {
    case CervenyImageKind::True:
      deltaDepth = receiverDepth - interpolatedDepth;
      polarity = 1.0;
      break;
    case CervenyImageKind::Surface:
      deltaDepth =
          -receiverDepth + 2.0 * seaSurfaceDepth -
          interpolatedDepth;
      polarity = -1.0;
      break;
    case CervenyImageKind::Bottom:
      deltaDepth =
          -receiverDepth + 2.0 * seabedDepth -
          interpolatedDepth;
      polarity = 1.0;
      break;
  }

  const double deltaSquared = deltaDepth * deltaDepth;
  const double windowMetric =
      -angularFrequency * gamma.imag() * deltaSquared;
  const bool windowPassed =
      windowMetric < beamWindowSquared;
  const double taper = cervenyHermiteTaper(
      deltaDepth, radiusMax, 2.0 * radiusMax);
  std::complex<double> exponential{};
  std::complex<double> contribution{};
  if (windowPassed) {
    const std::complex<double> phaseArgument =
        angularFrequency *
            (tau + interpolatedSlowness.depth * deltaDepth +
             gamma * deltaSquared) -
        reflectionPhase;
    exponential = negativeImaginaryExponential(phaseArgument);
    contribution =
        polarity * amplitude * taper * exponential;
  }
  requireFinite(windowMetric, "Cartesian Cerveny window metric");
  requireFinite(taper, "Cartesian Cerveny Hermite taper");
  requireFiniteComplex(
      exponential, "Cartesian Cerveny image exponential");
  requireFiniteComplex(
      contribution, "Cartesian Cerveny image contribution");
  return CartesianCervenyImageDiagnostic{
      .kind = kind,
      .deltaDepth = deltaDepth,
      .polarity = polarity,
      .windowMetric = windowMetric,
      .windowPassed = windowPassed,
      .hermiteTaper = taper,
      .exponential = exponential,
      .contribution = contribution};
}

[[nodiscard]] std::complex<double> evaluateImageContribution(
    CervenyImageKind kind, double receiverDepth,
    double interpolatedDepth, double seaSurfaceDepth,
    double seabedDepth, double angularFrequency,
    double beamWindowSquared, double radiusMax,
    Vec2 interpolatedSlowness,
    std::complex<double> tau,
    std::complex<double> gamma, double amplitude,
    double reflectionPhase) {
  double deltaDepth = 0.0;
  double polarity = 1.0;
  switch (kind) {
    case CervenyImageKind::True:
      deltaDepth = receiverDepth - interpolatedDepth;
      break;
    case CervenyImageKind::Surface:
      deltaDepth =
          -receiverDepth + 2.0 * seaSurfaceDepth -
          interpolatedDepth;
      polarity = -1.0;
      break;
    case CervenyImageKind::Bottom:
      deltaDepth =
          -receiverDepth + 2.0 * seabedDepth -
          interpolatedDepth;
      break;
  }

  const double deltaSquared = deltaDepth * deltaDepth;
  const double windowMetric =
      -angularFrequency * gamma.imag() * deltaSquared;
  requireFinite(
      windowMetric, "Cartesian Cerveny window metric");
  if (windowMetric >= beamWindowSquared) {
    return {};
  }

  const double taper = cervenyHermiteTaper(
      deltaDepth, radiusMax, 2.0 * radiusMax);
  if (taper == 0.0) {
    return {};
  }
  const std::complex<double> phaseArgument =
      angularFrequency *
          (tau + interpolatedSlowness.depth * deltaDepth +
           gamma * deltaSquared) -
      reflectionPhase;
  const std::complex<double> contribution =
      polarity * amplitude * taper *
      negativeImaginaryExponential(phaseArgument);
  requireFiniteComplex(
      contribution, "Cartesian Cerveny image contribution");
  return contribution;
}

}  // namespace

int updateCervenyKmah(std::complex<double> qLeft,
                      std::complex<double> qRight,
                      int currentKmah,
                      BeamWidthMode widthMode) {
  requireFiniteComplex(qLeft, "Cerveny branch-cut left q");
  requireFiniteComplex(qRight, "Cerveny branch-cut right q");
  if (currentKmah != -1 && currentKmah != 1) {
    throw ValidationError("Cerveny KMAH must be -1 or +1");
  }
  if (widthMode == BeamWidthMode::Wkb) {
    const double leftReal = qLeft.real();
    const double rightReal = qRight.real();
    if ((leftReal < 0.0 && rightReal >= 0.0) ||
        (leftReal > 0.0 && rightReal <= 0.0)) {
      return -currentKmah;
    }
    return currentKmah;
  }
  if (widthMode != BeamWidthMode::SpaceFilling &&
      widthMode != BeamWidthMode::MinimumWidth) {
    throw ValidationError("unknown Cerveny beam-width mode");
  }
  if (qRight.real() < 0.0) {
    const double leftImaginary = qLeft.imag();
    const double rightImaginary = qRight.imag();
    if ((leftImaginary < 0.0 && rightImaginary >= 0.0) ||
        (leftImaginary > 0.0 && rightImaginary <= 0.0)) {
      return -currentKmah;
    }
  }
  return currentKmah;
}

double cervenyHermiteTaper(double offset, double fullValueRadius,
                           double zeroValueRadius) {
  requireFinite(offset, "Hermite offset");
  requireFinite(fullValueRadius, "Hermite full-value radius");
  requireFinite(zeroValueRadius, "Hermite zero-value radius");
  if (fullValueRadius < 0.0 ||
      zeroValueRadius <= fullValueRadius) {
    throw ValidationError(
        "Hermite radii must satisfy 0 <= full < zero");
  }

  const double absoluteOffset = std::abs(offset);
  if (absoluteOffset <= fullValueRadius) {
    return 1.0;
  }
  if (absoluteOffset >= zeroValueRadius) {
    return 0.0;
  }
  const double coordinate =
      (absoluteOffset - fullValueRadius) /
      (zeroValueRadius - fullValueRadius);
  const double complement = 1.0 - coordinate;
  const double complementSquared = complement * complement;
  return (1.0 + 2.0 * coordinate) *
         complementSquared;
}

CartesianCervenyInfluence::CartesianCervenyInfluence(
    Environment environment, ReceiverGrid receivers,
    CartesianCervenySettings settings, BeamWidthMode widthMode,
    SourceGeometry sourceGeometry, SimulationRunMode runMode)
    : environment_(std::move(environment)),
      receivers_(std::move(receivers)),
      settings_(settings),
      widthMode_(widthMode),
      sourceGeometry_(sourceGeometry),
      runMode_(runMode),
      soundSpeedProfile_(environment_.soundSpeedProfile()),
      receiverRangeDelta_(
          receivers_.rangeCount() >= 2U
              ? receivers_.ranges()[1U] - receivers_.ranges()[0U]
              : 0.0) {
  if (settings_.imageCount == 0U ||
      settings_.imageCount > 3U) {
    throw ValidationError(
        "Cartesian Cerveny image count must lie in [1, 3]");
  }
  if (settings_.beamWindow <= 0) {
    throw ValidationError(
        "Cartesian Cerveny beam window must be positive");
  }
  switch (widthMode_) {
    case BeamWidthMode::SpaceFilling:
    case BeamWidthMode::MinimumWidth:
    case BeamWidthMode::Wkb:
      break;
    default:
      throw ValidationError("unknown Cartesian Cerveny beam-width mode");
  }
  switch (sourceGeometry_) {
    case SourceGeometry::Point:
    case SourceGeometry::Line:
      break;
    default:
      throw ValidationError("unknown Cartesian Cerveny source geometry");
  }
  switch (runMode_) {
    case SimulationRunMode::CoherentTransmissionLoss:
    case SimulationRunMode::IncoherentTransmissionLoss:
    case SimulationRunMode::SemiCoherentTransmissionLoss:
      break;
    case SimulationRunMode::RayTrace:
      throw ValidationError(
          "Cartesian Cerveny influence does not support ray-trace mode");
    default:
      throw ValidationError("unknown Cartesian Cerveny run mode");
  }
  validateUniformReceiverRanges(receivers_, receiverRangeDelta_);
}

std::optional<CartesianCervenyDiagnostic>
CartesianCervenyInfluence::accumulate(
    FrequencyWorkspace& workspace, const RayPath& path,
    const RayFrequencyState& frequencyState,
    std::complex<double> epsilon,
    std::optional<CartesianCervenyDiagnosticRequest>
        diagnosticRequest) const {
  if (runMode_ != SimulationRunMode::CoherentTransmissionLoss) {
    throw ValidationError(
        "complex-pressure accumulation requires coherent TL mode");
  }
  return accumulateImpl(
      &workspace, nullptr, path, frequencyState, epsilon,
      diagnosticRequest);
}

std::optional<CartesianCervenyDiagnostic>
CartesianCervenyInfluence::accumulateIntensity(
    IntensityWorkspace& workspace, const RayPath& path,
    const RayFrequencyState& frequencyState,
    std::complex<double> epsilon,
    std::optional<CartesianCervenyDiagnosticRequest>
        diagnosticRequest) const {
  if (runMode_ != SimulationRunMode::IncoherentTransmissionLoss &&
      runMode_ != SimulationRunMode::SemiCoherentTransmissionLoss) {
    throw ValidationError(
        "intensity accumulation requires incoherent or semi-coherent TL "
        "mode");
  }
  return accumulateImpl(
      nullptr, &workspace, path, frequencyState, epsilon,
      diagnosticRequest);
}

std::optional<CartesianCervenyDiagnostic>
CartesianCervenyInfluence::accumulateImpl(
    FrequencyWorkspace* pressureWorkspace,
    IntensityWorkspace* intensityWorkspace, const RayPath& path,
    const RayFrequencyState& frequencyState,
    std::complex<double> epsilon,
    std::optional<CartesianCervenyDiagnosticRequest>
        diagnosticRequest) const {
  validateAccumulateInput(
      pressureWorkspace, intensityWorkspace, path, frequencyState, epsilon,
      receivers_,
      widthMode_, diagnosticRequest);

  std::optional<CartesianCervenyDiagnostic> diagnostic;
  if (diagnosticRequest.has_value()) {
    diagnostic.emplace();
    diagnostic->receiverRangeIndex =
        diagnosticRequest->receiverRangeIndex;
    diagnostic->receiverDepthIndex =
        diagnosticRequest->receiverDepthIndex;
  }

  std::size_t activePrefixPointCount = path.points.size();
  for (std::size_t index = 0U;
       index < frequencyState.points.size(); ++index) {
    if (!frequencyState.points[index].active) {
      activePrefixPointCount = index + 1U;
      break;
    }
  }
  const PrecomputedRayValues ray = precomputeRayValues(
      path, soundSpeedProfile_, epsilon, activePrefixPointCount, widthMode_);
  const double angularFrequency =
      2.0 * std::numbers::pi * frequencyState.frequency;
  const double radiusMax =
      30.0 * path.points.front().soundSpeed /
      frequencyState.frequency;
  const double beamWindowSquared =
      static_cast<double>(settings_.beamWindow) *
      static_cast<double>(settings_.beamWindow);
  const double ratio =
      sourceGeometry_ == SourceGeometry::Line
          ? 1.0
          : std::sqrt(std::abs(std::cos(path.launchAngle)));
  const std::vector<double>& receiverRanges =
      receivers_.ranges();

  for (std::size_t rightIndex = 2U;
       rightIndex < activePrefixPointCount; ++rightIndex) {
    const std::size_t leftIndex = rightIndex - 1U;
    const double leftRange =
        path.points[leftIndex].position.range;
    const double rightRange =
        path.points[rightIndex].position.range;
    if (rightRange > receiverRanges.back()) {
      return diagnostic;
    }
    if (std::abs(rightRange - leftRange) <
        1000.0 * floatingSpacing(rightRange)) {
      continue;
    }
    // active means "may continue from this point".  The first inactive point
    // is still the terminal point retained by legacy Beam%Nsteps, so the
    // segment ending there remains eligible; only a false left endpoint
    // suppresses the geometry suffix.
    if (!frequencyState.points[leftIndex].active) {
      continue;
    }

    const std::size_t firstUpper =
        fortranUpperRangeIndex(
            leftRange, receiverRanges, receiverRangeDelta_);
    const std::size_t secondUpper =
        fortranUpperRangeIndex(
            rightRange, receiverRanges, receiverRangeDelta_);
    if (firstUpper >= secondUpper) {
      continue;
    }

    for (std::size_t oneBasedRange = firstUpper + 1U;
         oneBasedRange <= secondUpper; ++oneBasedRange) {
      const std::size_t rangeIndex = oneBasedRange - 1U;
      const double weight =
          (receiverRanges[rangeIndex] - leftRange) /
          (rightRange - leftRange);
      const Vec2 position =
          path.points[leftIndex].position +
          weight * (path.points[rightIndex].position -
                    path.points[leftIndex].position);
      const Vec2 slowness =
          path.points[leftIndex].slowness +
          weight * (path.points[rightIndex].slowness -
                    path.points[leftIndex].slowness);
      const double soundSpeed =
          path.points[leftIndex].soundSpeed +
          weight * (path.points[rightIndex].soundSpeed -
                    path.points[leftIndex].soundSpeed);
      const std::complex<double> q =
          ray.q[leftIndex] +
          weight * (ray.q[rightIndex] - ray.q[leftIndex]);
      const std::complex<double> tau =
          frequencyState.points[leftIndex].complexTravelTime +
          weight *
              (frequencyState.points[rightIndex].complexTravelTime -
               frequencyState.points[leftIndex].complexTravelTime);
      const std::complex<double> gamma =
          ray.gamma[leftIndex] +
          weight *
              (ray.gamma[rightIndex] - ray.gamma[leftIndex]);
      if (gamma.imag() > 0.0) {
        continue;
      }

      const std::complex<double> principal =
          ratio *
          std::sqrt(soundSpeed * std::abs(epsilon) / q);
      int finalKmah = ray.kmah[leftIndex];
      finalKmah = updateCervenyKmah(
          ray.q[leftIndex], q, finalKmah, widthMode_);
      const std::complex<double> corrected =
          finalKmah < 0 ? -principal : principal;
      requireFiniteComplex(
          principal, "Cartesian Cerveny principal constant");
      requireFiniteComplex(
          corrected, "Cartesian Cerveny corrected constant");

      for (std::size_t depthIndex = 0U;
           depthIndex < receivers_.receiversPerRange(); ++depthIndex) {
        // InfluenceCervenyCart in the 2-D Origin tree allocates one pressure
        // row for an irregular grid but still reads Pos%Rz(iz), where iz is
        // always one, instead of Pos%Rz(ir).  Preserve that observable legacy
        // behavior for CC; the complete coordinate vectors remain in the SHD
        // header for compatibility with the irregular file layout.
        const double receiverDepth =
            receivers_.isIrregular()
                ? receivers_.depths().front()
                : receivers_.depthAt(depthIndex, rangeIndex);
        const bool captureDiagnostic =
            diagnosticRequest.has_value() &&
            diagnosticRequest->receiverRangeIndex == rangeIndex &&
            diagnosticRequest->receiverDepthIndex == depthIndex;
        std::complex<double> imageSum{};
        std::array<CartesianCervenyImageDiagnostic, 3> images;
        if (captureDiagnostic) {
          images = {};
          for (std::size_t imageIndex = 0U;
               imageIndex < settings_.imageCount; ++imageIndex) {
            const CervenyImageKind kind =
                imageIndex == 0U
                    ? CervenyImageKind::True
                    : (imageIndex == 1U
                           ? CervenyImageKind::Surface
                           : CervenyImageKind::Bottom);
            images[imageIndex] = evaluateImage(
                kind, receiverDepth, position.depth,
                environment_.seaSurface().depth(),
                environment_.seabed().depth(), angularFrequency,
                beamWindowSquared, radiusMax, slowness, tau, gamma,
                frequencyState.points[rightIndex].amplitude,
                frequencyState.points[rightIndex].reflectionPhase);
            imageSum += images[imageIndex].contribution;
          }
        } else {
          for (std::size_t imageIndex = 0U;
               imageIndex < settings_.imageCount; ++imageIndex) {
            const CervenyImageKind kind =
                imageIndex == 0U
                    ? CervenyImageKind::True
                    : (imageIndex == 1U
                           ? CervenyImageKind::Surface
                           : CervenyImageKind::Bottom);
            imageSum += evaluateImageContribution(
                kind, receiverDepth, position.depth,
                environment_.seaSurface().depth(),
                environment_.seabed().depth(), angularFrequency,
                beamWindowSquared, radiusMax, slowness, tau, gamma,
                frequencyState.points[rightIndex].amplitude,
                frequencyState.points[rightIndex].reflectionPhase);
          }
        }
        const std::complex<double> contribution =
            corrected * imageSum;
        requireFiniteComplex(
            contribution, "Cartesian Cerveny final contribution");
        double intensityIncrement = 0.0;
        if (pressureWorkspace != nullptr) {
          const std::complex<double> updatedPressure =
              pressureWorkspace->at(depthIndex, rangeIndex) + contribution;
          requireFiniteComplex(
              updatedPressure,
              "Cartesian Cerveny accumulated workspace pressure");
          pressureWorkspace->at(depthIndex, rangeIndex) = updatedPressure;
        } else {
          // Origin first sums the true/surface/bottom images coherently for
          // one beam, then forms ABS(z)**2 before adding different beams.
          // Preserve that operation order; std::norm has a different
          // rounding and overflow path.
          const double magnitude = std::abs(contribution);
          intensityIncrement = magnitude * magnitude;
          if (!std::isfinite(magnitude) ||
              !std::isfinite(intensityIncrement) ||
              intensityIncrement < 0.0) {
            throw ValidationError(
                "Cartesian Cerveny intensity increment must be finite and "
                "non-negative");
          }
          // IntensityWorkspace::add validates the sum before committing, so
          // an overflowing accumulation leaves this receiver cell unchanged.
          intensityWorkspace->add(
              depthIndex, rangeIndex, intensityIncrement);
        }

        if (captureDiagnostic) {
          ++diagnostic->evaluationCount;
          if (diagnostic->evaluationCount == 1U) {
            diagnostic->evaluated = true;
            diagnostic->leftPointIndex = leftIndex;
            diagnostic->rightPointIndex = rightIndex;
            diagnostic->kmahLeft = ray.kmah[leftIndex];
            diagnostic->kmahFinal = finalKmah;
            diagnostic->interpolationWeight = weight;
            diagnostic->interpolatedPosition = position;
            diagnostic->interpolatedSlowness = slowness;
            diagnostic->interpolatedSoundSpeed = soundSpeed;
            diagnostic->rightAmplitude =
                frequencyState.points[rightIndex].amplitude;
            diagnostic->rightPhase =
                frequencyState.points[rightIndex].reflectionPhase;
            diagnostic->epsilonLeft = epsilon;
            diagnostic->pLeft = ray.p[leftIndex];
            diagnostic->pRight = ray.p[rightIndex];
            diagnostic->qLeft = ray.q[leftIndex];
            diagnostic->qRight = ray.q[rightIndex];
            diagnostic->qInterpolated = q;
            diagnostic->tauInterpolated = tau;
            diagnostic->gammaLeft = ray.gamma[leftIndex];
            diagnostic->gammaRight = ray.gamma[rightIndex];
            diagnostic->gammaInterpolated = gamma;
            diagnostic->constantPrincipal = principal;
            diagnostic->constantCorrected = corrected;
            diagnostic->images = images;
            diagnostic->rawImageSum = imageSum;
            diagnostic->finalContribution = contribution;
            diagnostic->intensityIncrement = intensityIncrement;
          }
        }
      }
    }
  }
  return diagnostic;
}

}  // namespace bellhop
