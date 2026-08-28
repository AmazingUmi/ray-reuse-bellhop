#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <limits>
#include <memory>
#include <utility>

#include "rayreuse/acoustics/attenuation.hpp"
#include "rayreuse/error.hpp"
#include "rayreuse/model/c_linear_frequency_ssp.hpp"
#include "rayreuse/model/c_linear_ssp.hpp"
#include "rayreuse/model/cubic_spline_frequency_ssp.hpp"
#include "rayreuse/model/cubic_spline_ssp.hpp"
#include "rayreuse/model/n2_linear_frequency_ssp.hpp"
#include "rayreuse/model/n2_linear_ssp.hpp"
#include "rayreuse/model/pchip_frequency_ssp.hpp"
#include "rayreuse/model/pchip_ssp.hpp"
#include "rayreuse/model/sound_speed_evaluator.hpp"
#include "support/munk_case_fixture.hpp"
#include "support/test_harness.hpp"

namespace {

using rayreuse::AttenuationUnit;
using rayreuse::CLinearFrequencySsp;
using rayreuse::CLinearSsp;
using rayreuse::CubicSplineFrequencySsp;
using rayreuse::CubicSplineSsp;
using rayreuse::FrequencySspEvaluator;
using rayreuse::GeometrySspEvaluator;
using rayreuse::N2LinearFrequencySsp;
using rayreuse::N2LinearSsp;
using rayreuse::PchipFrequencySsp;
using rayreuse::PchipSsp;
using rayreuse::SoundSpeedPoint;
using rayreuse::SoundSpeedProfile;
using rayreuse::SoundSpeedSample;
using rayreuse::SspGradientContinuity;
using rayreuse::SspInterpolationKind;
using rayreuse::sspGradientContinuity;
using rayreuse::ValidationError;
using rayreuse::Vec2;
using rayreuse::test::Context;

SoundSpeedProfile makePiecewiseProfile(
    SspInterpolationKind kind = SspInterpolationKind::CLinear) {
  return SoundSpeedProfile(
      {{.depth = 0.0, .soundSpeed = 1480.0, .density = 1000.0},
       {.depth = 100.0, .soundSpeed = 1500.0, .density = 1020.0},
       {.depth = 300.0, .soundSpeed = 1460.0, .density = 1060.0}},
      kind);
}

SoundSpeedProfile makeAttenuatingProfile(
    SspInterpolationKind kind = SspInterpolationKind::CLinear) {
  return SoundSpeedProfile(
      {{.depth = 0.0,
        .soundSpeed = 1480.0,
        .density = 1000.0,
        .attenuation =
            {.value = 0.1, .unit = AttenuationUnit::DecibelsPerWavelength}},
       {.depth = 100.0,
        .soundSpeed = 1500.0,
        .density = 1020.0,
        .attenuation =
            {.value = 0.2, .unit = AttenuationUnit::DecibelsPerWavelength}},
       {.depth = 300.0,
        .soundSpeed = 1460.0,
        .density = 1060.0,
        .attenuation =
            {.value = 0.3, .unit = AttenuationUnit::DecibelsPerWavelength}}},
      kind);
}

void checkSameSample(Context& context,
                     const SoundSpeedSample& expected,
                     const SoundSpeedSample& actual) {
  context.check(expected.soundSpeed == actual.soundSpeed,
                "evaluator preserves sound speed exactly");
  context.check(expected.imaginarySoundSpeed == actual.imaginarySoundSpeed,
                "evaluator preserves imaginary sound speed exactly");
  context.check(expected.soundSpeedGradient == actual.soundSpeedGradient,
                "evaluator preserves gradient exactly");
  context.check(expected.soundSpeedHessian == actual.soundSpeedHessian,
                "evaluator preserves Hessian exactly");
  context.check(expected.density == actual.density,
                "evaluator preserves density exactly");
  context.check(expected.segmentIndex == actual.segmentIndex,
                "evaluator preserves segment index exactly");
}

void testCLinearDispatchIsExact(Context& context) {
  const SoundSpeedProfile profile = makePiecewiseProfile(SspInterpolationKind::CLinear);
  const CLinearSsp concrete(profile);
  const GeometrySspEvaluator evaluator(profile);

  context.check(evaluator.interpolationKind() == SspInterpolationKind::CLinear,
                "evaluator reports C-linear interpolation");
  context.check(evaluator.gradientContinuity() ==
                    SspGradientContinuity::DiscontinuousAtNodes,
                "C-linear evaluator reports discontinuous gradient at nodes");
  context.check(evaluator.segmentCount() == concrete.segmentCount(),
                "evaluator preserves segment count");

  const Vec2 query{.range = 50.0, .depth = 50.0};
  checkSameSample(context, concrete.evaluate(query, 0U),
                  evaluator.evaluate(query, 0U));
  checkSameSample(context, concrete.evaluateAtSegment(query, 0U),
                  evaluator.evaluateAtSegment(query, 0U));
  context.check(concrete.locateSegment(150.0, 0U) ==
                    evaluator.locateSegment(150.0, 0U),
                "evaluator preserves segment location");

  const SoundSpeedProfile attProfile =
      makeAttenuatingProfile(SspInterpolationKind::CLinear);
  const CLinearFrequencySsp freqConcrete(attProfile, 1000.0);
  const FrequencySspEvaluator freqEvaluator(attProfile, 1000.0);

  context.check(freqEvaluator.interpolationKind() ==
                    SspInterpolationKind::CLinear,
                "frequency evaluator reports C-linear interpolation");
  context.check(freqEvaluator.gradientContinuity() ==
                    SspGradientContinuity::DiscontinuousAtNodes,
                "frequency evaluator reports discontinuous gradient at nodes");
  context.checkNear(freqEvaluator.frequency(), 1000.0, 0.0,
                "frequency evaluator reports frequency");
  context.check(freqEvaluator.isLossless() == freqConcrete.isLossless(),
                "frequency evaluator preserves lossless state");
  context.check(freqEvaluator.uniformComplexSoundSpeed() ==
                    freqConcrete.uniformComplexSoundSpeed(),
                "frequency evaluator preserves uniform complex speed");
  checkSameSample(context, freqConcrete.evaluate(query, 0U),
                  freqEvaluator.evaluate(query, 0U));
  checkSameSample(context, freqConcrete.evaluateAtSegment(query, 0U),
                  freqEvaluator.evaluateAtSegment(query, 0U));
}

void testPchipDispatchIsExact(Context& context) {
  const SoundSpeedProfile profile =
      makePiecewiseProfile(SspInterpolationKind::Pchip);
  const PchipSsp concrete(profile);
  const GeometrySspEvaluator evaluator(profile);

  context.check(evaluator.interpolationKind() == SspInterpolationKind::Pchip,
                "evaluator reports PCHIP interpolation");
  context.check(evaluator.gradientContinuity() ==
                    SspGradientContinuity::ContinuousAtNodes,
                "PCHIP evaluator reports continuous gradient at nodes");
  context.check(evaluator.segmentCount() == concrete.segmentCount(),
                "evaluator preserves segment count");

  const Vec2 query{.range = 50.0, .depth = 50.0};
  checkSameSample(context, concrete.evaluate(query, 0U),
                  evaluator.evaluate(query, 0U));
  checkSameSample(context, concrete.evaluateAtSegment(query, 0U),
                  evaluator.evaluateAtSegment(query, 0U));
  context.check(concrete.locateSegment(150.0, 0U) ==
                    evaluator.locateSegment(150.0, 0U),
                "evaluator preserves segment location");

  const SoundSpeedProfile attProfile =
      makeAttenuatingProfile(SspInterpolationKind::Pchip);
  const PchipFrequencySsp freqConcrete(attProfile, 50.0);
  const FrequencySspEvaluator freqEvaluator(attProfile, 50.0);

  context.check(freqEvaluator.interpolationKind() ==
                    SspInterpolationKind::Pchip,
                "frequency evaluator reports PCHIP interpolation");
  context.check(freqEvaluator.gradientContinuity() ==
                    SspGradientContinuity::ContinuousAtNodes,
                "frequency evaluator reports continuous gradient at nodes");
  context.checkNear(freqEvaluator.frequency(), 50.0, 0.0,
                "frequency evaluator reports frequency");
  context.check(freqEvaluator.isLossless() == freqConcrete.isLossless(),
                "frequency evaluator preserves lossless state");
  context.check(freqEvaluator.uniformComplexSoundSpeed() ==
                    freqConcrete.uniformComplexSoundSpeed(),
                "frequency evaluator preserves uniform complex speed");
  checkSameSample(context, freqConcrete.evaluate(query, 0U),
                  freqEvaluator.evaluate(query, 0U));
  checkSameSample(context, freqConcrete.evaluateAtSegment(query, 0U),
                  freqEvaluator.evaluateAtSegment(query, 0U));
}

void testN2LinearDispatchIsExact(Context& context) {
  context.check(
      sspGradientContinuity(SspInterpolationKind::N2Linear) ==
              SspGradientContinuity::DiscontinuousAtNodes &&
          sspGradientContinuity(SspInterpolationKind::CLinear) ==
              SspGradientContinuity::DiscontinuousAtNodes &&
          sspGradientContinuity(SspInterpolationKind::Pchip) ==
              SspGradientContinuity::ContinuousAtNodes,
      "N2/C interpolation kinds retain gradient jumps while PCHIP does not");

  const SoundSpeedProfile profile =
      makePiecewiseProfile(SspInterpolationKind::N2Linear);
  const N2LinearSsp concrete(profile);
  const GeometrySspEvaluator evaluator(profile);

  context.check(evaluator.interpolationKind() ==
                    SspInterpolationKind::N2Linear,
                "evaluator reports N2-linear interpolation");
  context.check(evaluator.gradientContinuity() ==
                    SspGradientContinuity::DiscontinuousAtNodes,
                "N2-linear evaluator reports discontinuous gradient at nodes");
  context.check(evaluator.segmentCount() == concrete.segmentCount(),
                "evaluator preserves segment count");

  const Vec2 query{.range = 50.0, .depth = 50.0};
  checkSameSample(context, concrete.evaluate(query, 0U),
                  evaluator.evaluate(query, 0U));
  checkSameSample(context, concrete.evaluateAtSegment(query, 0U),
                  evaluator.evaluateAtSegment(query, 0U));
  context.check(concrete.locateSegment(150.0, 0U) ==
                    evaluator.locateSegment(150.0, 0U),
                "evaluator preserves segment location");

  const SoundSpeedProfile attProfile =
      makeAttenuatingProfile(SspInterpolationKind::N2Linear);
  const N2LinearFrequencySsp freqConcrete(attProfile, 50.0);
  const FrequencySspEvaluator freqEvaluator(attProfile, 50.0);

  context.check(freqEvaluator.interpolationKind() ==
                    SspInterpolationKind::N2Linear,
                "frequency evaluator reports N2-linear interpolation");
  context.check(freqEvaluator.gradientContinuity() ==
                    SspGradientContinuity::DiscontinuousAtNodes,
                "N2-linear frequency evaluator reports discontinuous gradient "
                "at nodes");
  context.checkNear(freqEvaluator.frequency(), 50.0, 0.0,
                "frequency evaluator reports frequency");
  context.check(freqEvaluator.isLossless() == freqConcrete.isLossless(),
                "frequency evaluator preserves lossless state");
  context.check(freqEvaluator.uniformComplexSoundSpeed() ==
                    freqConcrete.uniformComplexSoundSpeed(),
                "frequency evaluator preserves uniform complex speed");
  checkSameSample(context, freqConcrete.evaluate(query, 0U),
                  freqEvaluator.evaluate(query, 0U));
  checkSameSample(context, freqConcrete.evaluateAtSegment(query, 0U),
                  freqEvaluator.evaluateAtSegment(query, 0U));

  context.check(
      freqEvaluator.evaluate(query, 0U).soundSpeedHessian.depthDepth != 0.0,
      "N2-linear frequency evaluator reports non-zero second derivative "
      "d2c/dz2");
}

void testCubicSplineDispatchIsExact(Context& context) {
  context.check(
      sspGradientContinuity(SspInterpolationKind::CubicSpline) ==
              SspGradientContinuity::ContinuousAtNodes &&
          sspGradientContinuity(SspInterpolationKind::Pchip) ==
              SspGradientContinuity::ContinuousAtNodes &&
          sspGradientContinuity(SspInterpolationKind::CLinear) ==
              SspGradientContinuity::DiscontinuousAtNodes &&
          sspGradientContinuity(SspInterpolationKind::N2Linear) ==
              SspGradientContinuity::DiscontinuousAtNodes,
      "cubic spline joins PCHIP as node-continuous while C/N2 keep jumps");

  const SoundSpeedProfile profile =
      makePiecewiseProfile(SspInterpolationKind::CubicSpline);
  const CubicSplineSsp concrete(profile);
  const GeometrySspEvaluator evaluator(profile);

  context.check(evaluator.interpolationKind() ==
                    SspInterpolationKind::CubicSpline,
                "evaluator reports cubic-spline interpolation");
  context.check(evaluator.gradientContinuity() ==
                    SspGradientContinuity::ContinuousAtNodes,
                "cubic-spline evaluator reports continuous gradient at nodes");
  context.check(evaluator.segmentCount() == concrete.segmentCount(),
                "evaluator preserves segment count");

  const Vec2 query{.range = 50.0, .depth = 50.0};
  checkSameSample(context, concrete.evaluate(query, 0U),
                  evaluator.evaluate(query, 0U));
  checkSameSample(context, concrete.evaluateAtSegment(query, 0U),
                  evaluator.evaluateAtSegment(query, 0U));
  context.check(concrete.locateSegment(150.0, 0U) ==
                    evaluator.locateSegment(150.0, 0U),
                "evaluator preserves segment location");

  const GeometrySspEvaluator cEvaluator(
      makePiecewiseProfile(SspInterpolationKind::CLinear));
  const GeometrySspEvaluator pchipEvaluator(
      makePiecewiseProfile(SspInterpolationKind::Pchip));
  const GeometrySspEvaluator n2Evaluator(
      makePiecewiseProfile(SspInterpolationKind::N2Linear));
  const SoundSpeedSample splineSample = evaluator.evaluate(query, 0U);
  context.check(
      splineSample.soundSpeed != cEvaluator.evaluate(query, 0U).soundSpeed &&
          splineSample.soundSpeed !=
              pchipEvaluator.evaluate(query, 0U).soundSpeed &&
          splineSample.soundSpeed !=
              n2Evaluator.evaluate(query, 0U).soundSpeed,
      "dispatched spline sample differs from C/P/N2 at the same query "
      "point, excluding a silent fallback backend");

  const SoundSpeedProfile attProfile =
      makeAttenuatingProfile(SspInterpolationKind::CubicSpline);
  const CubicSplineFrequencySsp freqConcrete(attProfile, 50.0);
  const FrequencySspEvaluator freqEvaluator(attProfile, 50.0);

  context.check(freqEvaluator.interpolationKind() ==
                    SspInterpolationKind::CubicSpline,
                "frequency evaluator reports cubic-spline interpolation");
  context.check(freqEvaluator.gradientContinuity() ==
                    SspGradientContinuity::ContinuousAtNodes,
                "cubic-spline frequency evaluator reports continuous gradient "
                "at nodes");
  context.checkNear(freqEvaluator.frequency(), 50.0, 0.0,
                "frequency evaluator reports frequency");
  context.check(freqEvaluator.isLossless() == freqConcrete.isLossless(),
                "frequency evaluator preserves lossless state");
  context.check(freqEvaluator.uniformComplexSoundSpeed() ==
                    freqConcrete.uniformComplexSoundSpeed(),
                "frequency evaluator preserves uniform complex speed");
  checkSameSample(context, freqConcrete.evaluate(query, 0U),
                  freqEvaluator.evaluate(query, 0U));
  checkSameSample(context, freqConcrete.evaluateAtSegment(query, 0U),
                  freqEvaluator.evaluateAtSegment(query, 0U));

  const FrequencySspEvaluator cFreqEvaluator(
      makeAttenuatingProfile(SspInterpolationKind::CLinear), 50.0);
  const FrequencySspEvaluator pchipFreqEvaluator(
      makeAttenuatingProfile(SspInterpolationKind::Pchip), 50.0);
  const FrequencySspEvaluator n2FreqEvaluator(
      makeAttenuatingProfile(SspInterpolationKind::N2Linear), 50.0);
  const SoundSpeedSample splineFreqSample = freqEvaluator.evaluate(query, 0U);
  context.check(
      splineFreqSample.soundSpeed !=
              cFreqEvaluator.evaluate(query, 0U).soundSpeed &&
          splineFreqSample.soundSpeed !=
              pchipFreqEvaluator.evaluate(query, 0U).soundSpeed &&
          splineFreqSample.soundSpeed !=
              n2FreqEvaluator.evaluate(query, 0U).soundSpeed,
      "dispatched spline frequency sample differs from C/P/N2, excluding a "
      "silent fallback backend");
}

void testPchipNonzeroCurvature(Context& context) {
  const auto munkEnvironment =
      rayreuse::test::makeMunkEnvironment(SspInterpolationKind::Pchip);
  const GeometrySspEvaluator evaluator(
      munkEnvironment.soundSpeedProfile());
  const auto sample = evaluator.evaluate(
      Vec2{.range = 0.0, .depth = 100.0}, 0U);
  context.check(sample.soundSpeedHessian.depthDepth != 0.0,
                "PCHIP evaluator evaluates non-zero second derivative d2c/dz2");
  context.checkNear(sample.soundSpeedHessian.depthDepth, 1.53910656288373468e-4,
                    2.0e-18, "PCHIP evaluator matches Munk curvature anchor");
}

}  // namespace

int main() {
  Context context;
  testCLinearDispatchIsExact(context);
  testPchipDispatchIsExact(context);
  testN2LinearDispatchIsExact(context);
  testCubicSplineDispatchIsExact(context);
  testPchipNonzeroCurvature(context);
  if (context.failureCount() != 0) {
    std::cerr << context.failureCount()
              << " sound-speed evaluator assertion(s) failed\n";
    return 1;
  }
  std::cout << "All Bellhop RayReuse sound-speed evaluator tests passed\n";
  return 0;
}
