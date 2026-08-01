#!/usr/bin/env bash

set -euo pipefail

script_directory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_root="$(cd "${script_directory}/.." && pwd)"

build_jobs="${RAYREUSE_BUILD_JOBS:-}"
parallel_arguments=()
if [[ -n "${build_jobs}" ]]; then
  if [[ ! "${build_jobs}" =~ ^[1-9][0-9]*$ ]]; then
    echo "RAYREUSE_BUILD_JOBS must be a positive integer" >&2
    exit 2
  fi
  parallel_arguments=(--parallel "${build_jobs}")
fi

python_mode="${RAYREUSE_PYTHON_MODE:-conda-py}"
case "${python_mode}" in
  conda-py)
    python_command=(conda run -n py python)
    ;;
  system)
    python_command=(python)
    ;;
  *)
    echo "RAYREUSE_PYTHON_MODE must be 'conda-py' or 'system'" >&2
    exit 2
    ;;
esac

"${script_directory}/check_format.sh"

(
  cd "${project_root}"
  cmake --preset static-analysis
  "${python_command[@]}" "${script_directory}/run_static_analysis.py" \
    "${project_root}/build/static-analysis/compile_commands.json"

  cmake --preset package
  cmake --build --preset package "${parallel_arguments[@]}"
)

install_root="$(mktemp -d "${TMPDIR:-/tmp}/bellhop-rayreuse-install.XXXXXX")"
cleanup_install() {
  rm -rf -- "${install_root}"
}
trap cleanup_install EXIT

cmake --install "${project_root}/build/package" --prefix "${install_root}"
version_output="$("${install_root}/bin/bellhop_rayreuse" --version)"
if [[ ! "${version_output}" =~ ^Bellhop\ RayReuse\ [0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "installed executable returned an invalid version: ${version_output}" >&2
  exit 1
fi

version="${version_output##* }"
package_path="${project_root}/build/package/bellhop-rayreuse-${version}-$(uname -s)-$(uname -m).tar.gz"
if [[ ! -f "${package_path}" ]]; then
  echo "expected versioned TGZ package is missing: ${package_path}" >&2
  exit 1
fi
cmake -E sha256sum "${package_path}"

echo "Bellhop_RayReuse engineering gate passed (${version_output})"
