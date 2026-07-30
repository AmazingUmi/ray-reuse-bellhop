#pragma once

#include <cmath>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>

namespace rayreuse::test {

class Context {
 public:
  void check(bool condition, std::string_view message) {
    if (!condition) {
      ++failureCount_;
      std::cerr << "FAIL: " << message << '\n';
    }
  }

  void checkNear(double actual, double expected, double tolerance,
                 std::string_view message) {
    std::ostringstream detail;
    detail << message << " (actual="
           << std::setprecision(std::numeric_limits<double>::max_digits10)
           << actual << ", expected=" << expected
           << ", tolerance=" << tolerance << ')';
    check(std::abs(actual - expected) <= tolerance, detail.str());
  }

  template <typename ExpectedException, typename Function>
  void expectThrows(Function&& function, std::string_view message) {
    try {
      function();
    } catch (const ExpectedException&) {
      return;
    } catch (const std::exception& error) {
      ++failureCount_;
      std::cerr << "FAIL: " << message << " (unexpected exception: "
                << error.what() << ")\n";
      return;
    } catch (...) {
      ++failureCount_;
      std::cerr << "FAIL: " << message << " (unknown exception)\n";
      return;
    }

    ++failureCount_;
    std::cerr << "FAIL: " << message << " (no exception)\n";
  }

  [[nodiscard]] int failureCount() const noexcept { return failureCount_; }

 private:
  int failureCount_{};
};

}  // namespace rayreuse::test
