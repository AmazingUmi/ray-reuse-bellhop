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

output="${RAYREUSE_INTERMEDIATE_OUTPUT:-${project_root}/test/standard_cases/results/intermediate_state_matrix.json}"

"${python_command[@]}" \
  "${project_root}/test/standard_cases/codes/intermediate_state_matrix.py" \
  --origin-executable "${project_root}/Bellhop_origin/bin/bellhop" \
  --f2cpp-probe "${project_root}/Bellhop_F2CPP/build/release/bellhop_f2cpp_geometry_oracle_probe" \
  --rayreuse-probe "${project_root}/Bellhop_RayReuse/build/release/bellhop_rayreuse_geometry_oracle_probe" \
  --output "${output}"

echo "Bellhop three-model intermediate-state gate passed"
