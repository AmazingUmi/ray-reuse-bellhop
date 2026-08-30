#pragma once

#include <complex>
#include <vector>

namespace rayreuse {

struct ComplexSplinePolynomial {
  std::complex<double> value{};
  std::complex<double> derivative{};
  std::complex<double> curvature{};
  std::complex<double> thirdDerivative{};
};

// Bellhop splinec.f90::CSPLINE coefficients with not-a-knot conditions at
// both endpoints. The returned polynomial uses ordinary powers, unlike the
// Fortran derivative/curvature/third-derivative storage convention.
[[nodiscard]] std::vector<ComplexSplinePolynomial>
computeCubicSplineCoefficients(const std::vector<double>& nodes,
                               const std::vector<std::complex<double>>& values);

}  // namespace rayreuse
