#!/usr/bin/env bash

# Engineering gate: heavy checks that must not block fast push/PR
# feedback. Runs the Debug sanitizer CTest, the isolated Release
# rebuild, formatting, static analysis and the package/install smoke.

set -euo pipefail

script_directory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_root="$(cd "${script_directory}/.." && pwd)"
repository_root="$(cd "${project_root}/.." && pwd)"

build_jobs="${BROADBAND_BUILD_JOBS:-}"
parallel_arguments=()
if [[ -n "${build_jobs}" ]]; then
  if [[ ! "${build_jobs}" =~ ^[1-9][0-9]*$ ]]; then
    echo "BROADBAND_BUILD_JOBS must be a positive integer" >&2
    exit 2
  fi
  parallel_arguments=(--parallel "${build_jobs}")
fi

ctest_parallelism=()
if [[ -n "${BROADBAND_CTEST_JOBS:-}" ]]; then
  if [[ ! "${BROADBAND_CTEST_JOBS}" =~ ^[1-9][0-9]*$ ]]; then
    echo "BROADBAND_CTEST_JOBS must be a positive integer" >&2
    exit 2
  fi
  ctest_parallelism=(--parallel "${BROADBAND_CTEST_JOBS}")
fi

python_executable="${PYTHON:-python3}"
if ! command -v "${python_executable}" >/dev/null 2>&1; then
  echo "Python interpreter not found: ${python_executable}" >&2
  exit 2
fi
python_command=("${python_executable}")

"${script_directory}/check_format.sh"

(
  cd "${project_root}"
  cmake --preset debug
  cmake --build --preset debug "${parallel_arguments[@]}"
  ctest --preset debug "${ctest_parallelism[@]}" --output-on-failure

  cmake --preset static-analysis
  "${python_command[@]}" "${script_directory}/run_static_analysis.py" \
    "${project_root}/build/static-analysis/compile_commands.json"

  cmake --preset package
  cmake --build --preset package "${parallel_arguments[@]}"
)

isolation_root="$(
  mktemp -d "${TMPDIR:-/tmp}/bellhop-broadband-isolated.XXXXXX"
)"
cleanup_isolation() {
  rm -rf -- "${isolation_root}"
}
trap cleanup_isolation EXIT

mkdir -p \
  "${isolation_root}/Bellhop_Broadband" \
  "${isolation_root}/test/standard_cases/cases"
rsync -a \
  --exclude build \
  --exclude '._*' \
  "${project_root}/" \
  "${isolation_root}/Bellhop_Broadband/"
rsync -a \
  --exclude '._*' \
  "${repository_root}/test/standard_cases/cases/" \
  "${isolation_root}/test/standard_cases/cases/"

(
  cd "${isolation_root}/Bellhop_Broadband"
  cmake --preset release
  cmake --build --preset release "${parallel_arguments[@]}"
  ctest --preset release "${ctest_parallelism[@]}" --output-on-failure
)

install_root="$(mktemp -d "${TMPDIR:-/tmp}/bellhop-broadband-install.XXXXXX")"
cleanup_install() {
  rm -rf -- "${install_root}"
}
trap cleanup_install EXIT

cmake --install "${project_root}/build/package" --prefix "${install_root}"
version_output="$("${install_root}/bin/bellhop_broadband" --version)"
if [[ ! "${version_output}" =~ ^Bellhop\ Broadband\ [0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "installed executable returned an invalid version: ${version_output}" >&2
  exit 1
fi

version="${version_output##* }"
package_path="${project_root}/build/package/bellhop-broadband-${version}-$(uname -s)-$(uname -m).tar.gz"
if [[ ! -f "${package_path}" ]]; then
  echo "expected versioned TGZ package is missing: ${package_path}" >&2
  exit 1
fi
cmake -E sha256sum "${package_path}"

echo "Bellhop_Broadband engineering gate passed (${version_output})"
