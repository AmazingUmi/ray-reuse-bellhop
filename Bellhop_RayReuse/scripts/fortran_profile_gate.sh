#!/usr/bin/env bash

set -euo pipefail

script_directory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repository_root="$(cd "${script_directory}/../.." && pwd)"

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

result_root="$(mktemp -d "${TMPDIR:-/tmp}/bellhop-fortran-profile.XXXXXX")"
cleanup_results() {
  rm -rf -- "${result_root}"
}
trap cleanup_results EXIT

runner="${repository_root}/test/standard_cases/codes/standard_cases.py"
common_arguments=(
  test
  --version origin
  --case constant_speed_direct
  --profile single
)

BELLHOP_PROFILE_STAGES=0 "${python_command[@]}" "${runner}" \
  "${common_arguments[@]}" --results-root "${result_root}/default"
BELLHOP_PROFILE_STAGES=1 "${python_command[@]}" "${runner}" \
  "${common_arguments[@]}" --results-root "${result_root}/profiled"

relative_output="origin/constant_speed_direct/single/f000_50Hz"
default_root="${result_root}/default/${relative_output}"
profiled_root="${result_root}/profiled/${relative_output}"
default_shd="${default_root}/constant_speed_direct_f000_50Hz.shd"
profiled_shd="${profiled_root}/constant_speed_direct_f000_50Hz.shd"
default_prt="${default_root}/constant_speed_direct_f000_50Hz.prt"
profiled_prt="${profiled_root}/constant_speed_direct_f000_50Hz.prt"

cmp --silent "${default_shd}" "${profiled_shd}"
if grep -q 'Stage .* seconds' "${default_prt}"; then
  echo "default Fortran PRT unexpectedly contains stage timings" >&2
  exit 1
fi
for stage in Trace Influence Scale Output; do
  grep -q "Stage ${stage} seconds" "${profiled_prt}"
done

grep 'Stage .* seconds' "${profiled_prt}"
echo "Fortran stage-profile gate passed with byte-identical SHD"
