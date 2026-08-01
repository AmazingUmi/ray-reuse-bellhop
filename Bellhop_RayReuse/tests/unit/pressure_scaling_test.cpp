#include "rayreuse/field/pressure_scaling.hpp"

#include <bit>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>

#include "rayreuse/error.hpp"
#include "support/test_harness.hpp"

namespace {

using rayreuse::FrequencyWorkspace;
using rayreuse::ReceiverGrid;
using rayreuse::scaleCoherentCartesianPointPressure;
using rayreuse::ValidationError;
using rayreuse::test::Context;

void checkComplexNear(Context& context, std::complex<double> actual,
                      std::complex<double> expected, double tolerance,
                      const std::string& message) {
  context.checkNear(actual.real(), expected.real(), tolerance,
                    message + " real");
  context.checkNear(actual.imag(), expected.imag(), tolerance,
                    message + " imaginary");
}

void testSmallMatrix(Context& context) {
  const ReceiverGrid receivers({10.0, 20.0}, {0.0, 1000.0, 4000.0});
  FrequencyWorkspace workspace(100.0, receivers);
  workspace.at(0U, 0U) = {1.0, 2.0};
  workspace.at(0U, 1U) = {3.0, 4.0};
  workspace.at(0U, 2U) = {5.0, 6.0};
  workspace.at(1U, 0U) = {-1.0, 0.5};
  workspace.at(1U, 1U) = {-3.0, -4.0};
  workspace.at(1U, 2U) = {2.0, 1.0};

  scaleCoherentCartesianPointPressure(workspace, receivers, 0.001, 1500.0);

  checkComplexNear(context, workspace.at(0U, 0U), {}, 0.0,
                   "zero range multiplies the first depth by zero");
  checkComplexNear(context, workspace.at(1U, 0U), {}, 0.0,
                   "zero range multiplies every depth by zero");
  checkComplexNear(context, workspace.at(0U, 1U),
                   {-6.3245553203367584e-7, -8.4327404271156782e-7}, 1.0e-21,
                   "1000 m row-zero scale");
  checkComplexNear(context, workspace.at(1U, 1U),
                   {6.3245553203367584e-7, 8.4327404271156782e-7}, 1.0e-21,
                   "1000 m row-one scale");
  checkComplexNear(context, workspace.at(0U, 2U),
                   {-5.2704627669472990e-7, -6.3245553203367584e-7}, 1.0e-21,
                   "4000 m row-zero scale");
  checkComplexNear(context, workspace.at(1U, 2U),
                   {-2.1081851067789195e-7, -1.0540925533894598e-7}, 1.0e-21,
                   "4000 m row-one scale");
}

void testO1ContributionAnchors(Context& context) {
  constexpr double directSpacing = 5.8372215785763523e-4;
  const ReceiverGrid directReceivers({500.0}, {0.0, 1080.0});
  FrequencyWorkspace direct(50.0, directReceivers);
  direct.at(0U, 1U) = {3.10088407099787347e1, -2.03824548128207503e1};
  scaleCoherentCartesianPointPressure(direct, directReceivers, directSpacing,
                                      1500.0);
  checkComplexNear(context, direct.at(0U, 1U),
                   {-2.5964118030924930e-6, 1.7066502661925371e-6}, 1.0e-20,
                   "direct O1 contribution scaling");

  const float directLegacyFactor = static_cast<float>(-8.3731340599810301e-8);
  context.check(std::bit_cast<std::uint32_t>(directLegacyFactor) == 0xb3b3cfcbU,
                "direct legacy SNGL factor bits");

  constexpr double munkSpacing = 7.09312989298996810e-4;
  constexpr double munkSourceSoundSpeed = 1501.38000000000011;
  const ReceiverGrid munkReceivers({575.0},
                                   {0.0, 16600.0, 59800.0000000000073});
  FrequencyWorkspace munk(50.0, munkReceivers);
  munk.at(0U, 1U) = {1.93208362125029010, -3.82389713319614799e1};
  munk.at(0U, 2U) = {4.84915532011030948e1, -1.36951960913375501e1};
  scaleCoherentCartesianPointPressure(munk, munkReceivers, munkSpacing,
                                      munkSourceSoundSpeed);
  checkComplexNear(context, munk.at(0U, 1U),
                   {-5.0096128433638360e-8, 9.9148111290158276e-7}, 1.0e-20,
                   "Munk focus O1 contribution scaling");
  checkComplexNear(context, munk.at(0U, 2U),
                   {-6.6244189637775524e-7, 1.8708973153296501e-7}, 1.0e-20,
                   "Munk negative-KMAH O1 contribution scaling");

  const float focusLegacyFactor = static_cast<float>(-2.5928550857038036e-8);
  const float branchLegacyFactor = static_cast<float>(-1.3660975007966664e-8);
  context.check(std::bit_cast<std::uint32_t>(focusLegacyFactor) == 0xb2deb97cU,
                "Munk focus legacy SNGL factor bits");
  context.check(std::bit_cast<std::uint32_t>(branchLegacyFactor) == 0xb26ab19aU,
                "Munk branch legacy SNGL factor bits");
}

void testValidation(Context& context) {
  const ReceiverGrid receivers({10.0}, {0.0, 1000.0});

  context.expectThrows<ValidationError>(
      [&] {
        FrequencyWorkspace workspace(50.0, receivers);
        scaleCoherentCartesianPointPressure(workspace, receivers, 0.0, 1500.0);
      },
      "zero launch-angle spacing is rejected");
  context.expectThrows<ValidationError>(
      [&] {
        FrequencyWorkspace workspace(50.0, receivers);
        scaleCoherentCartesianPointPressure(
            workspace, receivers, std::numeric_limits<double>::quiet_NaN(),
            1500.0);
      },
      "non-finite launch-angle spacing is rejected");
  context.expectThrows<ValidationError>(
      [&] {
        FrequencyWorkspace workspace(50.0, receivers);
        scaleCoherentCartesianPointPressure(workspace, receivers, 0.001,
                                            -1500.0);
      },
      "non-positive source sound speed is rejected");
  context.expectThrows<ValidationError>(
      [&] {
        FrequencyWorkspace workspace(50.0,
                                     ReceiverGrid({10.0, 20.0}, {0.0, 1000.0}));
        scaleCoherentCartesianPointPressure(workspace, receivers, 0.001,
                                            1500.0);
      },
      "workspace and receiver-grid size mismatch is rejected");
  context.expectThrows<ValidationError>(
      [&] {
        FrequencyWorkspace workspace(50.0, receivers);
        workspace.at(0U, 1U) = {std::numeric_limits<double>::infinity(), 0.0};
        scaleCoherentCartesianPointPressure(workspace, receivers, 0.001,
                                            1500.0);
      },
      "non-finite input pressure is rejected");

  const ReceiverGrid overflowReceivers({10.0}, {0.0, 1.0});
  FrequencyWorkspace overflow(100.0, overflowReceivers);
  const double maximum = std::numeric_limits<double>::max();
  overflow.at(0U, 0U) = {maximum, maximum};
  overflow.at(0U, 1U) = {maximum, maximum};
  context.expectThrows<ValidationError>(
      [&] {
        scaleCoherentCartesianPointPressure(overflow, overflowReceivers, 1.0,
                                            1.0);
      },
      "scaling overflow is rejected");
  context.check(
      overflow.at(0U, 0U) == std::complex<double>{maximum, maximum} &&
          overflow.at(0U, 1U) == std::complex<double>{maximum, maximum},
      "failed scaling leaves the workspace unchanged");
}

}  // namespace

int main() {
  Context context;
  testSmallMatrix(context);
  testO1ContributionAnchors(context);
  testValidation(context);

  if (context.failureCount() != 0) {
    std::cerr << context.failureCount()
              << " pressure-scaling assertion(s) failed\n";
    return 1;
  }
  std::cout << "All Bellhop RayReuse pressure-scaling tests passed\n";
  return 0;
}
