#!/usr/bin/env python3
"""Validate I4-01 attenuation-unit fields against Origin Bellhop."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sys

import numpy as np

from compare_fields import STANDARD_CASES_ROOT, compare_files


PLOTREAD_ROOT = STANDARD_CASES_ROOT.parent / "PlotRead"
sys.path.insert(0, str(PLOTREAD_ROOT))

from bellhop_io_py.shd import ShdReader


UNIT_SUFFIXES = ("n", "f", "m", "w", "q", "l")
EXPECTED_FREQUENCY_HZ = 5000.0


def aggregate_sha256(paths: list[Path]) -> str:
    digest = hashlib.sha256()
    for path in sorted(paths):
        digest.update(path.read_bytes())
    return digest.hexdigest()


def load_single_run(
    results_root: Path,
    version: str,
    case_id: str,
    expected_executable: Path,
) -> tuple[Path, Path, Path]:
    case_root = results_root / version / case_id / "single"
    manifest_path = case_root / "run_manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if (
        manifest["version"] != version
        or manifest["case_id"] != case_id
        or manifest["profile"] != "single"
        or manifest["last_stage"] != "test"
    ):
        raise ValueError(f"{manifest_path}: run identity mismatch")
    manifest_executable = Path(manifest["executable"]).resolve()
    if manifest_executable != expected_executable:
        raise ValueError(
            f"{manifest_path}: executable {manifest_executable} does not "
            f"match expected {expected_executable}"
        )
    runs = manifest["runs"]
    if len(runs) != 1 or float(runs[0]["frequency_hz"]) != EXPECTED_FREQUENCY_HZ:
        raise ValueError(f"{manifest_path}: expected one 5 kHz run")
    if runs[0]["status"] != "passed":
        raise ValueError(f"{manifest_path}: run is not passed")
    environment_path = case_root / runs[0]["environment_file"]
    shade_path = case_root / runs[0]["shade_file"]
    return manifest_path, environment_path, shade_path


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
    for version, executable in executables.items():
        if not executable.is_file():
            raise ValueError(f"{version} executable does not exist: {executable}")
    executable_hashes = {
        version: hashlib.sha256(executable.read_bytes()).hexdigest()
        for version, executable in executables.items()
    }
    if executable_hashes["origin"] == executable_hashes["f2cpp"]:
        raise ValueError(
            "Origin and F2CPP executables must have distinct content hashes"
        )

    comparisons: dict[str, dict[str, float | bool]] = {}
    pressures: dict[str, dict[str, np.ndarray]] = {
        "origin": {},
        "f2cpp": {},
    }
    output_paths: dict[str, list[Path]] = {"origin": [], "f2cpp": []}
    rendered_environments: list[Path] = []
    for suffix in UNIT_SUFFIXES:
        case_id = f"attenuation_unit_{suffix}"
        paths: dict[str, tuple[Path, Path, Path]] = {}
        for version in ("origin", "f2cpp"):
            paths[version] = load_single_run(
                results_root, version, case_id, executables[version]
            )
            manifest_path, environment_path, shade_path = paths[version]
            output_paths[version].append(shade_path)
            if version == "origin":
                rendered_environments.append(environment_path)
            pressures[version][suffix] = ShdReader(shade_path).read().pressure

        passed, metrics = compare_files(
            paths["origin"][2],
            paths["f2cpp"][2],
            0,
            0,
            STANDARD_CASES_ROOT / "codes" / "tolerances.toml",
        )
        if not passed:
            raise ValueError(f"{case_id}: Origin/F2CPP field mismatch: {metrics}")
        comparisons[case_id] = {"passed": True, **metrics}

    for version, values in pressures.items():
        reference = values["n"]
        for suffix in UNIT_SUFFIXES[1:]:
            if not np.array_equal(reference, values[suffix]):
                raise ValueError(
                    f"{version}: equivalent 5 kHz units differ for {suffix}"
                )

    return {
        "schema": "bellhop.f2cpp.i4_attenuation_unit_validation",
        "schema_version": 1,
        "status": "passed",
        "frequency_hz": EXPECTED_FREQUENCY_HZ,
        "equivalent_attenuation_np_per_m": 1.0e-5,
        "units": [suffix.upper() for suffix in UNIT_SUFFIXES],
        "origin_f2cpp_field_comparisons": comparisons,
        "cross_unit_fields_bit_identical": {
            "origin": True,
            "f2cpp": True,
        },
        "sha256": {
            "origin_executable": executable_hashes["origin"],
            "f2cpp_executable": executable_hashes["f2cpp"],
            "rendered_environment_aggregate": aggregate_sha256(
                rendered_environments
            ),
            "origin_field_aggregate": aggregate_sha256(
                output_paths["origin"]
            ),
            "f2cpp_field_aggregate": aggregate_sha256(
                output_paths["f2cpp"]
            ),
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
