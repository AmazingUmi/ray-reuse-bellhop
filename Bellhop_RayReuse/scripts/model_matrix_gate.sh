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

profiles="${RAYREUSE_MATRIX_PROFILES:-single,broadband_smoke}"
modes="${RAYREUSE_MATRIX_MODES:-nonreuse,reuse,parallel}"

cd "${repository_root}"
"${python_command[@]}" test/standard_cases/codes/model_matrix.py \
  --profiles "${profiles}" \
  --modes "${modes}"

echo "Bellhop three-model matrix gate passed"
