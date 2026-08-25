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
modes="${RAYREUSE_MATRIX_MODES:-nonreuse,reuse,parallel}"

cd "${repository_root}"
"${python_command[@]}" test/standard_cases/codes/model_matrix.py \
  --profiles "${profiles}" \
  --modes "${modes}"

echo "Bellhop three-model matrix gate passed"
