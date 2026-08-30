#include "rayreuse/numerics/pchip_coefficients.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

#include "rayreuse/error.hpp"

namespace rayreuse {
namespace {

bool finiteComplex(std::complex<double> value) {
  return std::isfinite(value.real()) && std::isfinite(value.imag());
}

double limitInterior(double firstSlope, double secondSlope, double estimate) {
  if (firstSlope * secondSlope <= 0.0) {
    return 0.0;
  }
  if (firstSlope > 0.0) {
    return std::min(std::max(estimate, 0.0),
                    3.0 * std::min(firstSlope, secondSlope));
  }
  return std::max(std::min(estimate, 0.0),
                  3.0 * std::max(firstSlope, secondSlope));
}

double limitLeftEndpoint(double firstSlope, double secondSlope,
                         double estimate) {
  if (firstSlope * estimate <= 0.0) {
    return 0.0;
  }
  if (firstSlope * secondSlope <= 0.0 &&
      std::abs(estimate) > std::abs(3.0 * firstSlope)) {
    return 3.0 * firstSlope;
  }
  return estimate;
}

double limitRightEndpoint(double firstSlope, double secondSlope,
                          double estimate) {
  if (secondSlope * estimate <= 0.0) {
    return 0.0;
  }
  if (firstSlope * secondSlope <= 0.0 &&
      std::abs(estimate) > std::abs(3.0 * secondSlope)) {
    return 3.0 * secondSlope;
  }
  return estimate;
}

template <typename Limiter>
std::complex<double> limitParts(std::complex<double> firstSlope,
                                std::complex<double> secondSlope,
                                std::complex<double> estimate,
                                Limiter limiter) {
  return {limiter(firstSlope.real(), secondSlope.real(), estimate.real()),
          limiter(firstSlope.imag(), secondSlope.imag(), estimate.imag())};
}

std::vector<std::complex<double>> clampedSplineDerivatives(
    const std::vector<double>& intervals,
    const std::vector<std::complex<double>>& secants,
    std::complex<double> leftDerivative, std::complex<double> rightDerivative) {
  const std::size_t nodeCount = secants.size() + 1U;
  std::vector<std::complex<double>> derivatives(nodeCount);
  derivatives.front() = leftDerivative;
  derivatives.back() = rightDerivative;
  if (nodeCount <= 2U) {
    return derivatives;
  }

  // Direct translation of splinec.f90::CSPLINE with first-derivative
  // boundary conditions. Keeping its elimination order also keeps the
  // coefficient oracle insensitive to an algebraically equivalent solver's
  // different rounding.
  std::vector<std::array<std::complex<double>, 4U>> work(nodeCount);
  for (std::size_t node = 1U; node < nodeCount; ++node) {
    work[node][2U] = intervals[node - 1U];
    work[node][3U] = secants[node - 1U];
  }
  work.front()[1U] = leftDerivative;
  work.front()[2U] = 0.0;
  work.front()[3U] = 1.0;
  work.back()[1U] = rightDerivative;

  for (std::size_t node = 1U; node + 1U < nodeCount; ++node) {
    const std::complex<double> factor =
        -work[node + 1U][2U] / work[node - 1U][3U];
    work[node][1U] = factor * work[node - 1U][1U] +
                     3.0 * (work[node][2U] * work[node + 1U][3U] +
                            work[node + 1U][2U] * work[node][3U]);
    work[node][3U] = factor * work[node - 1U][2U] +
                     2.0 * (work[node][2U] + work[node + 1U][2U]);
  }
  for (std::size_t node = nodeCount - 1U; node-- > 0U;) {
    work[node][1U] = (work[node][1U] - work[node][2U] * work[node + 1U][1U]) /
                     work[node][3U];
  }
  for (std::size_t node = 0U; node < nodeCount; ++node) {
    derivatives[node] = work[node][1U];
  }
  return derivatives;
}

}  // namespace

std::vector<ComplexCubicPolynomial> computePchipCoefficients(
    const std::vector<double>& nodes,
    const std::vector<std::complex<double>>& values) {
  if (nodes.size() != values.size() || nodes.size() < 2U) {
    throw ValidationError(
        "PCHIP requires equal node/value arrays with at least two points");
  }

  const std::size_t nodeCount = nodes.size();
  std::vector<double> intervals(nodeCount - 1U);
  std::vector<std::complex<double>> secants(nodeCount - 1U);
  for (std::size_t index = 0U; index + 1U < nodeCount; ++index) {
    intervals[index] = nodes[index + 1U] - nodes[index];
    if (!std::isfinite(nodes[index]) || !std::isfinite(values[index].real()) ||
        !std::isfinite(values[index].imag()) ||
        !std::isfinite(intervals[index]) || intervals[index] <= 0.0) {
      throw ValidationError(
          "PCHIP nodes and values must be finite with increasing nodes");
    }
    secants[index] = (values[index + 1U] - values[index]) / intervals[index];
    if (!finiteComplex(secants[index])) {
      throw ValidationError("PCHIP secant slope must be finite");
    }
  }
  if (!std::isfinite(nodes.back()) || !std::isfinite(values.back().real()) ||
      !std::isfinite(values.back().imag())) {
    throw ValidationError("PCHIP nodes and values must be finite");
  }

  if (nodeCount == 2U) {
    return {{.constant = values[0U],
             .linear = secants[0U],
             .quadratic = 0.0,
             .cubic = 0.0}};
  }

  std::vector<std::complex<double>> derivatives(nodeCount);
  {
    const double firstInterval = intervals[0U];
    const double secondInterval = intervals[1U];
    const std::complex<double> leftEstimate =
        ((2.0 * firstInterval + secondInterval) * secants[0U] -
         firstInterval * secants[1U]) /
        (firstInterval + secondInterval);
    const std::size_t last = intervals.size() - 1U;
    const double penultimateInterval = intervals[last - 1U];
    const double lastInterval = intervals[last];
    const std::complex<double> rightEstimate =
        (-lastInterval * secants[last - 1U] +
         (penultimateInterval + 2.0 * lastInterval) * secants[last]) /
        (penultimateInterval + lastInterval);
    const std::complex<double> leftDerivative =
        limitParts(secants[0U], secants[1U], leftEstimate, limitLeftEndpoint);
    const std::complex<double> rightDerivative = limitParts(
        secants[last - 1U], secants[last], rightEstimate, limitRightEndpoint);
    derivatives = clampedSplineDerivatives(intervals, secants, leftDerivative,
                                           rightDerivative);
    derivatives.front() = leftDerivative;
    derivatives.back() = rightDerivative;
    for (std::size_t node = 1U; node + 1U < nodeCount; ++node) {
      derivatives[node] = limitParts(secants[node - 1U], secants[node],
                                     derivatives[node], limitInterior);
    }
  }

  std::vector<ComplexCubicPolynomial> coefficients;
  coefficients.reserve(nodeCount - 1U);
  for (std::size_t index = 0U; index + 1U < nodeCount; ++index) {
    const double interval = intervals[index];
    const std::complex<double> difference = values[index + 1U] - values[index];
    const ComplexCubicPolynomial polynomial{
        .constant = values[index],
        .linear = derivatives[index],
        .quadratic = (3.0 * difference - interval * (2.0 * derivatives[index] +
                                                     derivatives[index + 1U])) /
                     (interval * interval),
        .cubic = (interval * (derivatives[index] + derivatives[index + 1U]) -
                  2.0 * difference) /
                 (interval * interval * interval)};
    if (!finiteComplex(polynomial.constant) ||
        !finiteComplex(polynomial.linear) ||
        !finiteComplex(polynomial.quadratic) ||
        !finiteComplex(polynomial.cubic)) {
      throw ValidationError("PCHIP polynomial coefficient must be finite");
    }
    coefficients.push_back(polynomial);
  }
  return coefficients;
}

}  // namespace rayreuse
