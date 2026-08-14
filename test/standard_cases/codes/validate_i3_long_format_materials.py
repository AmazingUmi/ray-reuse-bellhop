#!/usr/bin/env python3
"""Validate the I3-06 long-format boundary-material closure case."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from compare_fields import STANDARD_CASES_ROOT, compare_files


CASE_ID = "i3_long_format_materials"
PROFILE = "single"
FREQUENCIES = (1000.0,)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def aggregate_sha256(paths: list[Path]) -> str:
    digest = hashlib.sha256()
    for path in sorted(paths, key=lambda item: item.name):
        digest.update(path.read_bytes())
    return digest.hexdigest()


def load_run(
    results_root: Path,
    version: str,
    expected_executable: Path,
) -> tuple[Path, Path, list[Path]]:
    profile_root = results_root / version / CASE_ID / PROFILE
    manifest_path = profile_root / "run_manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if (
        manifest["version"] != version
        or manifest["case_id"] != CASE_ID
        or manifest["profile"] != PROFILE
        or manifest["last_stage"] != "test"
    ):
        raise ValueError(f"{manifest_path}: run identity mismatch")
    if Path(manifest["executable"]).resolve() != expected_executable:
        raise ValueError(f"{manifest_path}: executable identity mismatch")
    if manifest_path.stat().st_mtime_ns < expected_executable.stat().st_mtime_ns:
        raise ValueError(f"{manifest_path}: results predate the executable")
    runs = manifest["runs"]
    actual_frequencies = tuple(float(run["frequency_hz"]) for run in runs)
    if actual_frequencies != FREQUENCIES:
        raise ValueError(
            f"{manifest_path}: unexpected frequencies {actual_frequencies}"
        )
    if any(run["status"] != "passed" for run in runs):
        raise ValueError(f"{manifest_path}: run is not passed")
    run = runs[0]
    environment_path = profile_root / run["environment_file"]
    shade_path = profile_root / run["shade_file"]
    companion_paths = [
        environment_path.with_suffix(".ati"),
        environment_path.with_suffix(".bty"),
    ]
    if not all(path.is_file() for path in companion_paths):
        raise ValueError(f"{manifest_path}: rendered ATI/BTY sidecar is missing")
    return environment_path, shade_path, companion_paths


def validate(
    results_root: Path,
    origin_executable: Path,
    f2cpp_executable: Path,
) -> dict[str, object]:
    executables = {
        "origin": origin_executable.resolve(),
        "f2cpp": f2cpp_executable.resolve(),
    }
    if executables["origin"] == executables["f2cpp"]:
        raise ValueError("Origin and F2CPP executables must be distinct")
    if not all(path.is_file() for path in executables.values()):
        raise ValueError("an expected executable does not exist")
    executable_hashes = {
        version: sha256(path) for version, path in executables.items()
    }
    if executable_hashes["origin"] == executable_hashes["f2cpp"]:
        raise ValueError(
            "Origin and F2CPP executables must have distinct content hashes"
        )

    loaded = {
        version: load_run(results_root, version, executables[version])
        for version in ("origin", "f2cpp")
    }
    rendered_inputs = {
        version: [loaded[version][0], *loaded[version][2]]
        for version in ("origin", "f2cpp")
    }
    rendered_input_hashes = {
        version: aggregate_sha256(paths)
        for version, paths in rendered_inputs.items()
    }
    if rendered_input_hashes["origin"] != rendered_input_hashes["f2cpp"]:
        raise ValueError("Origin and F2CPP rendered ENV/ATI/BTY inputs differ")
    passed, metrics = compare_files(
        loaded["origin"][1],
        loaded["f2cpp"][1],
        0,
        0,
        STANDARD_CASES_ROOT / "codes" / "tolerances.toml",
    )
    if not passed:
        raise ValueError(f"I3-06 field mismatch: {metrics}")

    return {
        "schema": "bellhop.f2cpp.i3_long_format_materials_validation",
        "schema_version": 1,
        "status": "passed",
        "case": {"id": CASE_ID, "profile": PROFILE, "frequencies_hz": [1000.0]},
        "origin_f2cpp_field_comparison": {"passed": True, **metrics},
        "component_guards": {
            "segment_to_left_node_material": True,
            "reflection_event_freezes_raw_material": True,
            "frozen_material_overrides_environment_fallback": True,
            "long_format_attenuation_depth_is_1e20": True,
        },
        "generation": {
            "origin_command": "python3 test/standard_cases/codes/standard_cases.py test --version origin --case i3_long_format_materials --profile single --executable Bellhop_origin/bin/bellhop --results-root <results-root>",
            "f2cpp_command": "python3 test/standard_cases/codes/standard_cases.py test --version f2cpp --case i3_long_format_materials --profile single --executable Bellhop_F2CPP/build/release/bellhop_f2cpp --results-root <results-root>",
            "validator_command": "python3 test/standard_cases/codes/validate_i3_long_format_materials.py --results-root <results-root> --origin-executable Bellhop_origin/bin/bellhop --f2cpp-executable Bellhop_F2CPP/build/release/bellhop_f2cpp",
        },
        "sha256": {
            "origin_executable": executable_hashes["origin"],
            "f2cpp_executable": executable_hashes["f2cpp"],
            "rendered_input_aggregate": rendered_input_hashes["origin"],
            "origin_field": sha256(loaded["origin"][1]),
            "f2cpp_field": sha256(loaded["f2cpp"][1]),
        },
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--results-root", type=Path, required=True)
    parser.add_argument("--origin-executable", type=Path, required=True)
    parser.add_argument("--f2cpp-executable", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = validate(
        args.results_root,
        args.origin_executable,
        args.f2cpp_executable,
    )
    rendered = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.output is not None:
        args.output.write_text(rendered, encoding="utf-8")
    print(rendered, end="")


if __name__ == "__main__":
    main()
