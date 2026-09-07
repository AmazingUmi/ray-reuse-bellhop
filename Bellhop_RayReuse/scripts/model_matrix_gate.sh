#!/usr/bin/env bash

set -euo pipefail

script_directory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repository_root="$(cd "${script_directory}/../.." && pwd)"

python_executable="${PYTHON:-python3}"
if ! command -v "${python_executable}" >/dev/null 2>&1; then
  echo "Python interpreter not found: ${python_executable}" >&2
  exit 2
fi
python_command=("${python_executable}")

profiles="${RAYREUSE_MATRIX_PROFILES:-single,broadband_smoke}"
execution_modes="${RAYREUSE_MATRIX_EXECUTION_MODES:-nonreuse,reuse}"
reuse_modes="${RAYREUSE_MATRIX_REUSE_MODES:-serial,frequency}"

cd "${repository_root}"
"${python_command[@]}" test/standard_cases/codes/model_matrix.py \
  --profiles "${profiles}" \
  --execution-modes "${execution_modes}" \
  --reuse-modes "${reuse_modes}"

echo "Bellhop three-model matrix gate passed"
