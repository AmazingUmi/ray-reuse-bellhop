#!/usr/bin/env bash

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
if [[ -n "${RAYREUSE_BUILD_JOBS:-}" ]]; then
  if [[ ! "${RAYREUSE_BUILD_JOBS}" =~ ^[1-9][0-9]*$ ]]; then
    echo "RAYREUSE_BUILD_JOBS must be a positive integer" >&2
    exit 2
  fi
  build_parallelism=("${RAYREUSE_BUILD_JOBS}")
fi

for preset in debug release; do
  (
    cd "${project_root}"
    cmake --preset "${preset}"
    cmake --build --preset "${preset}" --parallel "${build_parallelism[@]}"
    ctest --preset "${preset}" --output-on-failure
  )
done

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

isolation_root="$(
  mktemp -d "${TMPDIR:-/tmp}/bellhop-rayreuse-isolated.XXXXXX"
)"
cleanup_isolation() {
  rm -rf -- "${isolation_root}"
}
trap cleanup_isolation EXIT

mkdir -p \
  "${isolation_root}/Bellhop_RayReuse" \
  "${isolation_root}/test/standard_cases/cases"
rsync -a \
  --exclude build \
  --exclude '._*' \
  "${project_root}/" \
  "${isolation_root}/Bellhop_RayReuse/"
rsync -a \
  --exclude '._*' \
  "${repository_root}/test/standard_cases/cases/" \
  "${isolation_root}/test/standard_cases/cases/"

(
  cd "${isolation_root}/Bellhop_RayReuse"
  cmake --preset release
  cmake --build --preset release --parallel "${build_parallelism[@]}"
  ctest --preset release --output-on-failure
)

echo "Bellhop_RayReuse quality gate passed"
