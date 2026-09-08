#!/usr/bin/env bash

# Fast quality gate: Release build + Release CTest + Python suites +
# independence check. Heavy checks (Debug sanitizer CTest, isolation
# rebuild, formatting, static analysis, packaging) live in
# engineering_gate.sh.

set -euo pipefail

script_directory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_root="$(cd "${script_directory}/.." && pwd)"
repository_root="$(cd "${project_root}/.." && pwd)"

python_executable="${PYTHON:-python3}"
if ! command -v "${python_executable}" >/dev/null 2>&1; then
  echo "Python interpreter not found: ${python_executable}" >&2
  exit 2
fi
python_command=("${python_executable}")

build_parallelism=()
if [[ -n "${BROADBAND_BUILD_JOBS:-}" ]]; then
  if [[ ! "${BROADBAND_BUILD_JOBS}" =~ ^[1-9][0-9]*$ ]]; then
    echo "BROADBAND_BUILD_JOBS must be a positive integer" >&2
    exit 2
  fi
  build_parallelism=("${BROADBAND_BUILD_JOBS}")
fi

ctest_parallelism=()
if [[ -n "${BROADBAND_CTEST_JOBS:-}" ]]; then
  if [[ ! "${BROADBAND_CTEST_JOBS}" =~ ^[1-9][0-9]*$ ]]; then
    echo "BROADBAND_CTEST_JOBS must be a positive integer" >&2
    exit 2
  fi
  ctest_parallelism=(--parallel "${BROADBAND_CTEST_JOBS}")
fi

(
  cd "${project_root}"
  cmake --preset release
  cmake --build --preset release --parallel "${build_parallelism[@]}"
  ctest --preset release "${ctest_parallelism[@]}" --output-on-failure
)

(
  cd "${repository_root}"
  PYTHONDONTWRITEBYTECODE=1 \
    "${python_command[@]}" -m unittest discover \
      -s test/standard_cases/codes/tests \
      -p 'test_*.py'
)

(
  plot_cache_root="$(
    mktemp -d "${TMPDIR:-/tmp}/bellhop-plotread-cache.XXXXXX"
  )"
  cleanup_plot_cache() {
    rm -rf -- "${plot_cache_root}"
  }
  trap cleanup_plot_cache EXIT
  cd "${repository_root}"
  MPLCONFIGDIR="${plot_cache_root}/matplotlib" \
    XDG_CACHE_HOME="${plot_cache_root}/xdg" \
    PYTHONDONTWRITEBYTECODE=1 \
    "${python_command[@]}" -m unittest discover \
      -s test/PlotRead/tests \
      -p 'test_*.py'
)

"${script_directory}/check_independence.sh"

echo "Bellhop_Broadband quality gate passed"
