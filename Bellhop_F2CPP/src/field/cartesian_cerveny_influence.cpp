#include "bellhop/field/cartesian_cerveny_influence.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <numbers>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "bellhop/error.hpp"

namespace bellhop {
namespace {

constexpr std::array<CervenyImageKind, 3> kImageKinds{
    CervenyImageKind::True,
    CervenyImageKind::Surface,
    CervenyImageKind::Bottom,
};

[[nodiscard]] bool finiteComplex(
    std::complex<double> value) noexcept {
  return std::isfinite(value.real()) &&
         std::isfinite(value.imag());
}

[[gnu::always_inline]] inline void requireFinite(
    double value, std::string_view name) {
  if (!std::isfinite(value)) {
    throw ValidationError(
        std::string(name) + " must be finite");
  }
}

[[gnu::always_inline]] inline void requireFiniteComplex(
    std::complex<double> value, std::string_view name) {
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

[[gnu::always_inline]] inline double cervenyHermiteTaperHot(
    double offset, double fullValueRadius, double zeroValueRadius) {
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
  const double taper = cervenyHermiteTaperHot(
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

[[nodiscard, gnu::always_inline]] inline std::complex<double>
evaluateImageContribution(
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

  const double taper = cervenyHermiteTaperHot(
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

struct PreparedCartesianCervenyAccumulation {
  const RayPath& path;
  const RayFrequencyState& frequencyState;
  const PrecomputedRayValues& ray;
  std::span<std::complex<double>> pressureValues;
  IntensityWorkspace* intensityWorkspace;
  std::optional<CartesianCervenyDiagnostic>* diagnostic;
  const std::vector<double>& receiverRanges;
  const std::vector<double>& receiverDepths;
  std::size_t activePrefixPointCount;
  std::size_t receiverRangeCount;
  double receiverRangeDelta;
  bool irregularReceivers;
  double irregularReceiverDepth;
  double maximumReceiverRange;
  double seaSurfaceDepth;
  double seabedDepth;
  std::size_t imageCount;
  bool captureRequested;
  std::size_t requestedRangeIndex;
  std::size_t requestedDepthIndex;
  double angularFrequency;
  double radiusMax;
  double beamWindowSquared;
  double ratio;
  double epsilonMagnitude;
  std::complex<double> epsilon;
  BeamWidthMode widthMode;
};

[[gnu::noinline]] void accumulatePreparedDepthStripe(
    const PreparedCartesianCervenyAccumulation& prepared,
    std::size_t depthBegin, std::size_t depthEnd) {
  const RayPath& path = prepared.path;
  const RayFrequencyState& frequencyState = prepared.frequencyState;
  const PrecomputedRayValues& ray = prepared.ray;
  const std::vector<double>& receiverRanges = prepared.receiverRanges;
  const std::vector<double>& receiverDepths = prepared.receiverDepths;
  const std::span<std::complex<double>> pressureValues =
      prepared.pressureValues;
  IntensityWorkspace* const intensityWorkspace =
      prepared.intensityWorkspace;
  std::optional<CartesianCervenyDiagnostic>* const diagnosticState =
      prepared.diagnostic;
  const std::size_t activePrefixPointCount =
      prepared.activePrefixPointCount;
  const std::size_t receiverRangeCount =
      prepared.receiverRangeCount;
  const double receiverRangeDelta = prepared.receiverRangeDelta;
  const bool irregularReceivers = prepared.irregularReceivers;
  const double irregularReceiverDepth =
      prepared.irregularReceiverDepth;
  const double maximumReceiverRange =
      prepared.maximumReceiverRange;
  const double seaSurfaceDepth = prepared.seaSurfaceDepth;
  const double seabedDepth = prepared.seabedDepth;
  const std::size_t imageCount = prepared.imageCount;
  const bool captureRequested = prepared.captureRequested;
  const std::size_t requestedRangeIndex =
      prepared.requestedRangeIndex;
  const std::size_t requestedDepthIndex =
      prepared.requestedDepthIndex;
  const double angularFrequency = prepared.angularFrequency;
  const double radiusMax = prepared.radiusMax;
  const double beamWindowSquared = prepared.beamWindowSquared;
  const double ratio = prepared.ratio;
  const double epsilonMagnitude = prepared.epsilonMagnitude;
  const std::complex<double> epsilon = prepared.epsilon;
  const BeamWidthMode widthMode = prepared.widthMode;

  for (std::size_t rightIndex = 2U;
       rightIndex < activePrefixPointCount; ++rightIndex) {
    const std::size_t leftIndex = rightIndex - 1U;
    const RayState& leftPoint = path.points[leftIndex];
    const RayState& rightPoint = path.points[rightIndex];
    const RayFrequencyPoint& leftFrequencyPoint =
        frequencyState.points[leftIndex];
    const RayFrequencyPoint& rightFrequencyPoint =
        frequencyState.points[rightIndex];
    const double leftRange = leftPoint.position.range;
    const double rightRange = rightPoint.position.range;
    if (rightRange > maximumReceiverRange) {
      return;
    }
    if (std::abs(rightRange - leftRange) <
        1000.0 * floatingSpacing(rightRange)) {
      continue;
    }
    // active means "may continue from this point".  The first inactive point
    // is still the terminal point retained by legacy Beam%Nsteps, so the
    // segment ending there remains eligible; only a false left endpoint
    // suppresses the geometry suffix.
    if (!leftFrequencyPoint.active) {
      continue;
    }

    const std::size_t firstUpper = fortranUpperRangeIndex(
        leftRange, receiverRanges, receiverRangeDelta);
    const std::size_t secondUpper = fortranUpperRangeIndex(
        rightRange, receiverRanges, receiverRangeDelta);
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
          leftPoint.position +
          weight * (rightPoint.position - leftPoint.position);
      const Vec2 slowness =
          leftPoint.slowness +
          weight * (rightPoint.slowness - leftPoint.slowness);
      const double soundSpeed =
          leftPoint.soundSpeed +
          weight * (rightPoint.soundSpeed - leftPoint.soundSpeed);
      const std::complex<double> q =
          ray.q[leftIndex] +
          weight * (ray.q[rightIndex] - ray.q[leftIndex]);
      const std::complex<double> tau =
          leftFrequencyPoint.complexTravelTime +
          weight *
              (rightFrequencyPoint.complexTravelTime -
               leftFrequencyPoint.complexTravelTime);
      const std::complex<double> gamma =
          ray.gamma[leftIndex] +
          weight * (ray.gamma[rightIndex] - ray.gamma[leftIndex]);
      if (gamma.imag() > 0.0) {
        continue;
      }

      const std::complex<double> principal =
          ratio *
          std::sqrt(soundSpeed * epsilonMagnitude / q);
      int finalKmah = ray.kmah[leftIndex];
      finalKmah = updateCervenyKmah(
          ray.q[leftIndex], q, finalKmah, widthMode);
      const std::complex<double> corrected =
          finalKmah < 0 ? -principal : principal;
      requireFiniteComplex(
          principal, "Cartesian Cerveny principal constant");
      requireFiniteComplex(
          corrected, "Cartesian Cerveny corrected constant");

      for (std::size_t depthIndex = depthBegin;
           depthIndex < depthEnd; ++depthIndex) {
        // InfluenceCervenyCart in the 2-D Origin tree allocates one pressure
        // row for an irregular grid but still reads Pos%Rz(iz), where iz is
        // always one, instead of Pos%Rz(ir).  Preserve that observable legacy
        // behavior for CC; the complete coordinate vectors remain in the SHD
        // header for compatibility with the irregular file layout.
        const double receiverDepth =
            irregularReceivers
                ? irregularReceiverDepth
                : receiverDepths[depthIndex];
        const bool captureDiagnostic =
            captureRequested &&
            requestedRangeIndex == rangeIndex &&
            requestedDepthIndex == depthIndex;
        std::complex<double> imageSum{};
        std::array<CartesianCervenyImageDiagnostic, 3> images;
        if (captureDiagnostic) {
          images = {};
          for (std::size_t imageIndex = 0U;
               imageIndex < imageCount; ++imageIndex) {
            const CervenyImageKind kind = kImageKinds[imageIndex];
            images[imageIndex] = evaluateImage(
                kind, receiverDepth, position.depth,
                seaSurfaceDepth, seabedDepth,
                angularFrequency, beamWindowSquared,
                radiusMax, slowness, tau, gamma,
                rightFrequencyPoint.amplitude,
                rightFrequencyPoint.reflectionPhase);
            imageSum += images[imageIndex].contribution;
          }
        } else {
          for (std::size_t imageIndex = 0U;
               imageIndex < imageCount; ++imageIndex) {
            const CervenyImageKind kind = kImageKinds[imageIndex];
            imageSum += evaluateImageContribution(
                kind, receiverDepth, position.depth,
                seaSurfaceDepth, seabedDepth,
                angularFrequency, beamWindowSquared,
                radiusMax, slowness, tau, gamma,
                rightFrequencyPoint.amplitude,
                rightFrequencyPoint.reflectionPhase);
          }
        }
        const std::complex<double> contribution = corrected * imageSum;
        requireFiniteComplex(
            contribution, "Cartesian Cerveny final contribution");
        double intensityIncrement = 0.0;
        if (intensityWorkspace == nullptr) {
          std::complex<double>& pressure =
              pressureValues[
                  depthIndex * receiverRangeCount + rangeIndex];
          const std::complex<double> updatedPressure =
              pressure + contribution;
          requireFiniteComplex(
              updatedPressure,
              "Cartesian Cerveny accumulated workspace pressure");
          pressure = updatedPressure;
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
          CartesianCervenyDiagnostic& diagnostic =
              diagnosticState->value();
          ++diagnostic.evaluationCount;
          if (diagnostic.evaluationCount == 1U) {
            diagnostic.evaluated = true;
            diagnostic.leftPointIndex = leftIndex;
            diagnostic.rightPointIndex = rightIndex;
            diagnostic.kmahLeft = ray.kmah[leftIndex];
            diagnostic.kmahFinal = finalKmah;
            diagnostic.interpolationWeight = weight;
            diagnostic.interpolatedPosition = position;
            diagnostic.interpolatedSlowness = slowness;
            diagnostic.interpolatedSoundSpeed = soundSpeed;
            diagnostic.rightAmplitude = rightFrequencyPoint.amplitude;
            diagnostic.rightPhase = rightFrequencyPoint.reflectionPhase;
            diagnostic.epsilonLeft = epsilon;
            diagnostic.pLeft = ray.p[leftIndex];
            diagnostic.pRight = ray.p[rightIndex];
            diagnostic.qLeft = ray.q[leftIndex];
            diagnostic.qRight = ray.q[rightIndex];
            diagnostic.qInterpolated = q;
            diagnostic.tauInterpolated = tau;
            diagnostic.gammaLeft = ray.gamma[leftIndex];
            diagnostic.gammaRight = ray.gamma[rightIndex];
            diagnostic.gammaInterpolated = gamma;
            diagnostic.constantPrincipal = principal;
            diagnostic.constantCorrected = corrected;
            diagnostic.images = images;
            diagnostic.rawImageSum = imageSum;
            diagnostic.finalContribution = contribution;
            diagnostic.intensityIncrement = intensityIncrement;
          }
        }
      }
    }
  }
}

}  // namespace

class CartesianCervenyInfluence::DeterministicDepthTeam {
 public:
  using Task =
      std::function<void(std::size_t depthBegin, std::size_t depthEnd)>;

  DeterministicDepthTeam(std::size_t threadCount,
                         std::size_t depthCount) {
    tiles_.reserve(threadCount);
    workers_.reserve(threadCount);
    for (std::size_t index = 0U; index < threadCount; ++index) {
      tiles_.emplace_back(
          depthCount * index / threadCount,
          depthCount * (index + 1U) / threadCount);
    }
    try {
      for (std::size_t index = 0U; index < threadCount; ++index) {
        workers_.emplace_back([this, index] { workerLoop(index); });
      }
    } catch (...) {
      stopAndJoin();
      throw;
    }
  }

  ~DeterministicDepthTeam() { stopAndJoin(); }

  DeterministicDepthTeam(const DeterministicDepthTeam&) = delete;
  DeterministicDepthTeam& operator=(const DeterministicDepthTeam&) = delete;

  void run(Task task) {
    std::unique_lock lock(mutex_);
    if (stopping_) {
      throw BellhopError("Cartesian Cerveny depth team is stopped");
    }
    if (task_) {
      throw BellhopError("Cartesian Cerveny depth team is already active");
    }
    task_ = std::move(task);
    firstException_ = nullptr;
    completedWorkerCount_ = 0U;
    ++generation_;
    workAvailable_.notify_all();
    workCompleted_.wait(
        lock, [this] { return completedWorkerCount_ == workers_.size(); });
    const std::exception_ptr failure = firstException_;
    task_ = {};
    lock.unlock();
    if (failure != nullptr) {
      std::rethrow_exception(failure);
    }
  }

 private:
  void workerLoop(std::size_t workerIndex) noexcept {
    std::size_t observedGeneration = 0U;
    while (true) {
      std::unique_lock lock(mutex_);
      workAvailable_.wait(lock, [this, observedGeneration] {
        return stopping_ || generation_ != observedGeneration;
      });
      if (stopping_) {
        return;
      }
      observedGeneration = generation_;
      const Task* const task = &task_;
      const auto [depthBegin, depthEnd] = tiles_[workerIndex];
      lock.unlock();

      std::exception_ptr failure;
      try {
        (*task)(depthBegin, depthEnd);
      } catch (...) {
        failure = std::current_exception();
      }

      lock.lock();
      if (failure != nullptr && firstException_ == nullptr) {
        firstException_ = failure;
      }
      ++completedWorkerCount_;
      if (completedWorkerCount_ == workers_.size()) {
        workCompleted_.notify_one();
      }
    }
  }

  void stopAndJoin() noexcept {
    {
      std::lock_guard lock(mutex_);
      stopping_ = true;
      ++generation_;
    }
    workAvailable_.notify_all();
    for (std::thread& worker : workers_) {
      if (worker.joinable()) {
        worker.join();
      }
    }
  }

  std::mutex mutex_;
  std::condition_variable workAvailable_;
  std::condition_variable workCompleted_;
  std::vector<std::thread> workers_;
  std::vector<std::pair<std::size_t, std::size_t>> tiles_;
  Task task_;
  std::exception_ptr firstException_;
  std::size_t generation_{};
  std::size_t completedWorkerCount_{};
  bool stopping_{};
};

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
  return cervenyHermiteTaperHot(
      offset, fullValueRadius, zeroValueRadius);
}

CartesianCervenyInfluence::CartesianCervenyInfluence(
    Environment environment, ReceiverGrid receivers,
    CartesianCervenySettings settings, BeamWidthMode widthMode,
    SourceGeometry sourceGeometry, SimulationRunMode runMode,
    std::size_t threadCount)
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
              : 0.0),
      threadCount_(std::min(threadCount,
                            receivers_.receiversPerRange())) {
  if (threadCount == 0U ||
      threadCount > kMaximumCartesianCervenyThreadCount) {
    throw ValidationError(
        "Cartesian Cerveny thread count must lie in [1, 256]");
  }
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
  if (threadCount_ > 1U) {
    depthTeam_ = std::make_unique<DeterministicDepthTeam>(
        threadCount_, receivers_.receiversPerRange());
  }
}

CartesianCervenyInfluence::~CartesianCervenyInfluence() = default;

std::size_t CartesianCervenyInfluence::threadCount() const noexcept {
  return threadCount_;
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
  const double epsilonMagnitude = std::abs(epsilon);
  const std::vector<double>& receiverRanges =
      receivers_.ranges();
  const std::vector<double>& receiverDepths =
      receivers_.depths();
  const std::size_t receiverRangeCount = receiverRanges.size();
  const std::size_t receiversPerRange =
      receivers_.receiversPerRange();
  const bool irregularReceivers = receivers_.isIrregular();
  const double irregularReceiverDepth = receiverDepths.front();
  const double maximumReceiverRange = receiverRanges.back();
  const double seaSurfaceDepth =
      environment_.seaSurface().depth();
  const double seabedDepth = environment_.seabed().depth();
  const std::size_t imageCount = settings_.imageCount;
  const bool captureRequested = diagnosticRequest.has_value();
  const std::size_t requestedRangeIndex =
      captureRequested ? diagnosticRequest->receiverRangeIndex : 0U;
  const std::size_t requestedDepthIndex =
      captureRequested ? diagnosticRequest->receiverDepthIndex : 0U;
  std::span<std::complex<double>> pressureValues;
  if (pressureWorkspace != nullptr) {
    pressureValues = pressureWorkspace->pressure();
  }

  const PreparedCartesianCervenyAccumulation prepared{
      .path = path,
      .frequencyState = frequencyState,
      .ray = ray,
      .pressureValues = pressureValues,
      .intensityWorkspace = intensityWorkspace,
      .diagnostic = &diagnostic,
      .receiverRanges = receiverRanges,
      .receiverDepths = receiverDepths,
      .activePrefixPointCount = activePrefixPointCount,
      .receiverRangeCount = receiverRangeCount,
      .receiverRangeDelta = receiverRangeDelta_,
      .irregularReceivers = irregularReceivers,
      .irregularReceiverDepth = irregularReceiverDepth,
      .maximumReceiverRange = maximumReceiverRange,
      .seaSurfaceDepth = seaSurfaceDepth,
      .seabedDepth = seabedDepth,
      .imageCount = imageCount,
      .captureRequested = captureRequested,
      .requestedRangeIndex = requestedRangeIndex,
      .requestedDepthIndex = requestedDepthIndex,
      .angularFrequency = angularFrequency,
      .radiusMax = radiusMax,
      .beamWindowSquared = beamWindowSquared,
      .ratio = ratio,
      .epsilonMagnitude = epsilonMagnitude,
      .epsilon = epsilon,
      .widthMode = widthMode_,
  };
  if (depthTeam_ != nullptr && !captureRequested) {
    depthTeam_->run(
        [&prepared](std::size_t depthBegin, std::size_t depthEnd) {
          accumulatePreparedDepthStripe(
              prepared, depthBegin, depthEnd);
        });
  } else {
    accumulatePreparedDepthStripe(
        prepared, 0U, receiversPerRange);
  }
  return diagnostic;
}

}  // namespace bellhop
