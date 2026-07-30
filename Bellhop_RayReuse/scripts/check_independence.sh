#!/usr/bin/env bash

set -euo pipefail

script_directory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_root="$(cd "${script_directory}/.." && pwd)"

forbidden_pattern='Bellhop_F2CPP|bellhop_f2cpp|add_subdirectory.*F2CPP|include_directories.*F2CPP'

if grep -R -nE \
    --include='CMakeLists.txt' \
    --include='*.cmake' \
    --include='*.cpp' \
    --include='*.hpp' \
    "${forbidden_pattern}" \
    "${project_root}"; then
  echo "RayReuse source or CMake contains an F2CPP dependency" >&2
  exit 1
fi

for build_directory in \
    "${project_root}/build/debug" \
    "${project_root}/build/release"; do
  if [[ ! -d "${build_directory}" ]]; then
    continue
  fi
  if grep -R -nE \
      --include='link.txt' \
      --include='flags.make' \
      --include='DependInfo.cmake' \
      'Bellhop_F2CPP|bellhop_f2cpp' \
      "${build_directory}"; then
    echo "RayReuse build metadata contains an F2CPP dependency" >&2
    exit 1
  fi
done

release_executable="${project_root}/build/release/bellhop_rayreuse"
if [[ -x "${release_executable}" ]]; then
  if command -v otool >/dev/null 2>&1; then
    if otool -L "${release_executable}" |
        grep -E 'Bellhop_F2CPP|bellhop_f2cpp'; then
      echo "RayReuse executable links an F2CPP library" >&2
      exit 1
    fi
  elif command -v ldd >/dev/null 2>&1; then
    if ldd "${release_executable}" |
        grep -E 'Bellhop_F2CPP|bellhop_f2cpp'; then
      echo "RayReuse executable links an F2CPP library" >&2
      exit 1
    fi
  fi
fi

echo "Bellhop_RayReuse independence checks passed"
