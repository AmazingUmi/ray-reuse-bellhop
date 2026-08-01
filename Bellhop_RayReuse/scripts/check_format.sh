#!/usr/bin/env bash

set -euo pipefail

script_directory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_root="$(cd "${script_directory}/.." && pwd)"

if [[ -n "${CLANG_FORMAT:-}" ]]; then
  formatter="${CLANG_FORMAT}"
elif command -v clang-format >/dev/null 2>&1; then
  formatter="$(command -v clang-format)"
elif command -v xcrun >/dev/null 2>&1 &&
    xcrun -f clang-format >/dev/null 2>&1; then
  formatter="$(xcrun -f clang-format)"
else
  echo "clang-format is required for the format gate" >&2
  exit 2
fi

mapfile_compatible_find() {
  find \
    "${project_root}/app" \
    "${project_root}/include" \
    "${project_root}/src" \
    "${project_root}/tests" \
    -type f \( -name '*.cpp' -o -name '*.hpp' \) \
    -print0
}

files=()
while IFS= read -r -d '' file; do
  files+=("${file}")
done < <(mapfile_compatible_find)

if [[ "${#files[@]}" -eq 0 ]]; then
  echo "no C++ files found for the format gate" >&2
  exit 2
fi

"${formatter}" --dry-run --Werror "${files[@]}"
echo "Bellhop_RayReuse format gate passed"
