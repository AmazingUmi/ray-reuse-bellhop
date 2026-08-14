#pragma once

#include <complex>
#include <vector>

namespace bellhop {

struct ComplexCubicPolynomial {
  std::complex<double> constant{};
  std::complex<double> linear{};
  std::complex<double> quadratic{};
  std::complex<double> cubic{};
};

// Bellhop pchipMod-compatible coefficients. Real and imaginary derivative
// limiters are applied independently, matching the original complex routine.
[[nodiscard]] std::vector<ComplexCubicPolynomial> computePchipCoefficients(
    const std::vector<double>& nodes,
    const std::vector<std::complex<double>>& values);

}  // namespace bellhop
