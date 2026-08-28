#include "rayreuse/numerics/cubic_spline_coefficients.hpp"

#include <array>
#include <cmath>
#include <cstddef>

#include "rayreuse/error.hpp"

namespace rayreuse {
namespace {

bool finiteComplex(std::complex<double> value) {
  return std::isfinite(value.real()) && std::isfinite(value.imag());
}

}  // namespace

std::vector<ComplexSplinePolynomial> computeCubicSplineCoefficients(
    const std::vector<double>& nodes,
    const std::vector<std::complex<double>>& values) {
  if (nodes.size() != values.size() || nodes.size() < 2U) {
    throw ValidationError(
        "cubic spline requires equal node/value arrays with at least two "
        "points");
  }
  const std::size_t nodeCount = nodes.size();
  std::vector<std::array<std::complex<double>, 4U>> work(nodeCount);
  for (std::size_t node = 0U; node < nodeCount; ++node) {
    if (!std::isfinite(nodes[node]) || !finiteComplex(values[node])) {
      throw ValidationError("cubic spline nodes and values must be finite");
    }
    work[node][0U] = values[node];
    if (node == 0U) {
      continue;
    }
    const double interval = nodes[node] - nodes[node - 1U];
    if (!std::isfinite(interval) || interval <= 0.0) {
      throw ValidationError("cubic spline nodes must be strictly increasing");
    }
    work[node][2U] = interval;
    work[node][3U] = (values[node] - values[node - 1U]) / interval;
    if (!finiteComplex(work[node][3U])) {
      throw ValidationError("cubic spline secant slope must be finite");
    }
  }

  // Direct translation of splinec.f90::CSPLINE with IBCBEG=IBCEND=0.
  // Preserving the original elimination order avoids needless oracle drift.
  if (nodeCount > 2U) {
    work[0U][3U] = work[2U][2U];
    work[0U][2U] = work[1U][2U] + work[2U][2U];
    work[0U][1U] =
        ((work[1U][2U] + 2.0 * work[0U][2U]) * work[1U][3U] *
             work[2U][2U] +
         work[1U][2U] * work[1U][2U] * work[2U][3U]) /
        work[0U][2U];
  } else {
    work[0U][3U] = 1.0;
    work[0U][2U] = 1.0;
    work[0U][1U] = 2.0 * work[1U][3U];
  }

  for (std::size_t node = 1U; node + 1U < nodeCount; ++node) {
    const std::complex<double> factor =
        -work[node + 1U][2U] / work[node - 1U][3U];
    work[node][1U] =
        factor * work[node - 1U][1U] +
        3.0 * (work[node][2U] * work[node + 1U][3U] +
               work[node + 1U][2U] * work[node][3U]);
    work[node][3U] =
        factor * work[node - 1U][2U] +
        2.0 * (work[node][2U] + work[node + 1U][2U]);
  }

  if (nodeCount == 2U) {
    work[1U][1U] = work[1U][3U];
  } else if (nodeCount == 3U) {
    work[2U][1U] = 2.0 * work[2U][3U];
    work[2U][3U] = 1.0;
    const std::complex<double> factor = -1.0 / work[1U][3U];
    work[2U][3U] = factor * work[1U][2U] + work[2U][3U];
    work[2U][1U] =
        (factor * work[1U][1U] + work[2U][1U]) / work[2U][3U];
  } else {
    const std::size_t last = nodeCount - 1U;
    std::complex<double> factor =
        work[last - 1U][2U] + work[last][2U];
    work[last][1U] =
        ((work[last][2U] + 2.0 * factor) * work[last][3U] *
             work[last - 1U][2U] +
         work[last][2U] * work[last][2U] *
             (work[last - 1U][0U] - work[last - 2U][0U]) /
             work[last - 1U][2U]) /
        factor;
    factor = -factor / work[last - 1U][3U];
    work[last][3U] = work[last - 1U][2U];
    work[last][3U] =
        factor * work[last - 1U][2U] + work[last][3U];
    work[last][1U] =
        (factor * work[last - 1U][1U] + work[last][1U]) /
        work[last][3U];
  }

  for (std::size_t node = nodeCount - 1U; node-- > 0U;) {
    work[node][1U] =
        (work[node][1U] - work[node][2U] * work[node + 1U][1U]) /
        work[node][3U];
  }

  std::vector<ComplexSplinePolynomial> coefficients;
  coefficients.reserve(nodeCount - 1U);
  for (std::size_t node = 1U; node < nodeCount; ++node) {
    const std::size_t segment = node - 1U;
    const std::complex<double> interval = work[node][2U];
    const std::complex<double> secant =
        (work[node][0U] - work[segment][0U]) / interval;
    const std::complex<double> derivativeSum =
        work[segment][1U] + work[node][1U] - 2.0 * secant;
    const std::complex<double> curvature =
        2.0 * (secant - work[segment][1U] - derivativeSum) / interval;
    const std::complex<double> thirdDerivative =
        (derivativeSum / interval) * (6.0 / interval);
    const ComplexSplinePolynomial polynomial{
        .value = work[segment][0U],
        .derivative = work[segment][1U],
        .curvature = curvature,
        .thirdDerivative = thirdDerivative};
    if (!finiteComplex(polynomial.value) ||
        !finiteComplex(polynomial.derivative) ||
        !finiteComplex(polynomial.curvature) ||
        !finiteComplex(polynomial.thirdDerivative)) {
      throw ValidationError("cubic spline coefficient must be finite");
    }
    coefficients.push_back(polynomial);
  }
  return coefficients;
}

}  // namespace rayreuse
