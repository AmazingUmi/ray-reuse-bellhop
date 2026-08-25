#!/usr/bin/env bash

set -euo pipefail

script_directory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_root="$(cd "${script_directory}/../.." && pwd)"

python_executable="${PYTHON:-python3}"
if ! command -v "${python_executable}" >/dev/null 2>&1; then
  echo "Python interpreter not found: ${python_executable}" >&2
  exit 2
fi
python_command=("${python_executable}")

repetitions="${RAYREUSE_MICROBENCH_REPETITIONS:-3}"
warmups="${RAYREUSE_MICROBENCH_WARMUPS:-1}"
output="${RAYREUSE_MICROBENCH_OUTPUT:-${project_root}/test/standard_cases/results/single_thread_microbenchmark.json}"

"${python_command[@]}" \
  "${project_root}/test/standard_cases/codes/microbenchmark_models.py" \
  --origin-executable "${project_root}/Bellhop_origin/bin/bellhop" \
  --f2cpp-executable "${project_root}/Bellhop_F2CPP/build/release/bellhop_f2cpp" \
  --rayreuse-executable "${project_root}/Bellhop_RayReuse/build/release/bellhop_rayreuse" \
  --case constant_speed_direct \
  --case munk_cerveny_cc \
  --warmups "${warmups}" \
  --repetitions "${repetitions}" \
  --output "${output}"

echo "Bellhop three-model single-thread microbenchmark completed"
