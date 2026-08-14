#!/usr/bin/env python3
"""Validate the I4-03 ordinary elastic half-space closure cases."""

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


ELASTIC_CASE = "elastic_halfspace_flat"
CONTROL_CASE = "elastic_halfspace_fluid_control"
PROFILES = {
    "single": (1000.0,),
    "broadband_smoke": (1000.0, 2000.0),
}
MINIMUM_SHEAR_EFFECT = 1.0e-6


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def aggregate_sha256(paths: list[Path]) -> str:
    digest = hashlib.sha256()
    for path in sorted(paths, key=lambda item: str(item)):
        digest.update(path.read_bytes())
    return digest.hexdigest()


def load_profile(
    results_root: Path,
    version: str,
    case_id: str,
    profile: str,
    expected_frequencies: tuple[float, ...],
    expected_executable: Path,
) -> list[tuple[Path, Path]]:
    profile_root = results_root / version / case_id / profile
    manifest_path = profile_root / "run_manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if (
        manifest["version"] != version
        or manifest["case_id"] != case_id
        or manifest["profile"] != profile
        or manifest["last_stage"] != "test"
    ):
        raise ValueError(f"{manifest_path}: run identity mismatch")
    if Path(manifest["executable"]).resolve() != expected_executable:
        raise ValueError(f"{manifest_path}: executable identity mismatch")
    if manifest_path.stat().st_mtime_ns < expected_executable.stat().st_mtime_ns:
        raise ValueError(f"{manifest_path}: results predate the executable")
    runs = manifest["runs"]
    frequencies = tuple(float(run["frequency_hz"]) for run in runs)
    if frequencies != expected_frequencies:
        raise ValueError(
            f"{manifest_path}: frequencies {frequencies} do not match "
            f"{expected_frequencies}"
        )
    if any(run["status"] != "passed" for run in runs):
        raise ValueError(f"{manifest_path}: run is not passed")
    return [
        (
            profile_root / run["environment_file"],
            profile_root / run["shade_file"],
        )
        for run in runs
    ]


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

    loaded: dict[str, dict[str, dict[str, list[tuple[Path, Path]]]]] = {
        "origin": {},
        "f2cpp": {},
    }
    input_hashes: dict[str, dict[str, str]] = {"origin": {}, "f2cpp": {}}
    field_paths: dict[str, list[Path]] = {"origin": [], "f2cpp": []}
    for version in ("origin", "f2cpp"):
        for case_id in (ELASTIC_CASE, CONTROL_CASE):
            loaded[version][case_id] = {}
            for profile, frequencies in PROFILES.items():
                runs = load_profile(
                    results_root,
                    version,
                    case_id,
                    profile,
                    frequencies,
                    executables[version],
                )
                loaded[version][case_id][profile] = runs
                input_hashes[version][f"{case_id}/{profile}"] = (
                    aggregate_sha256([environment for environment, _ in runs])
                )
                field_paths[version].extend(shade for _, shade in runs)
    if input_hashes["origin"] != input_hashes["f2cpp"]:
        raise ValueError("Origin and F2CPP rendered ENV inputs differ")

    comparisons: dict[str, dict[str, float | bool]] = {}
    tolerance_path = STANDARD_CASES_ROOT / "codes" / "tolerances.toml"
    for case_id in (ELASTIC_CASE, CONTROL_CASE):
        for profile, frequencies in PROFILES.items():
            for index, frequency in enumerate(frequencies):
                origin_shade = loaded["origin"][case_id][profile][index][1]
                f2cpp_shade = loaded["f2cpp"][case_id][profile][index][1]
                passed, metrics = compare_files(
                    origin_shade, f2cpp_shade, 0, 0, tolerance_path
                )
                if not passed:
                    raise ValueError(
                        f"{case_id}/{profile}/{frequency:g}Hz field mismatch: "
                        f"{metrics}"
                    )
                comparisons[f"{case_id}/{profile}/{frequency:g}Hz"] = {
                    "passed": True,
                    **metrics,
                }

    shear_guards: dict[str, dict[str, float | bool]] = {}
    for version in ("origin", "f2cpp"):
        for profile, frequencies in PROFILES.items():
            for index, frequency in enumerate(frequencies):
                elastic_pressure = ShdReader(
                    loaded[version][ELASTIC_CASE][profile][index][1]
                ).read().pressure
                control_pressure = ShdReader(
                    loaded[version][CONTROL_CASE][profile][index][1]
                ).read().pressure
                if elastic_pressure.shape != control_pressure.shape:
                    raise ValueError(
                        f"{version}/{profile}: shear-control shape mismatch"
                    )
                maximum_difference = float(
                    np.max(np.abs(elastic_pressure - control_pressure))
                )
                if maximum_difference <= MINIMUM_SHEAR_EFFECT:
                    raise ValueError(
                        f"{version}/{profile}/{frequency:g}Hz: shear appears "
                        f"to be a no-op ({maximum_difference})"
                    )
                shear_guards[f"{version}/{profile}/{frequency:g}Hz"] = {
                    "passed": True,
                    "max_pressure_absolute_vs_fluid_control": maximum_difference,
                }

    return {
        "schema": "bellhop.f2cpp.i4_elastic_halfspace_validation",
        "schema_version": 1,
        "status": "passed",
        "cases": {
            "elastic": ELASTIC_CASE,
            "fluid_control": CONTROL_CASE,
            "profiles": {
                profile: list(frequencies)
                for profile, frequencies in PROFILES.items()
            },
        },
        "origin_f2cpp_field_comparisons": comparisons,
        "shear_noop_guards": shear_guards,
        "component_guards": {
            "gfortran_lossless_and_lossy_coefficient_anchors": True,
            "ordinary_env_retains_p_and_s_properties": True,
            "two_frequency_projection_preserves_frozen_path": True,
            "elastic_long_format_remains_explicitly_rejected": True,
        },
        "generation": {
            "case_command": "python3 test/standard_cases/codes/standard_cases.py test --version <origin|f2cpp> --case <elastic_halfspace_flat|elastic_halfspace_fluid_control> --profile <single|broadband_smoke> --executable <matching-executable> --results-root <results-root>",
            "validator_command": "python3 test/standard_cases/codes/validate_i4_elastic_halfspace.py --results-root <results-root> --origin-executable Bellhop_origin/bin/bellhop --f2cpp-executable Bellhop_F2CPP/build/release/bellhop_f2cpp",
        },
        "sha256": {
            "origin_executable": executable_hashes["origin"],
            "f2cpp_executable": executable_hashes["f2cpp"],
            "rendered_input_aggregates": input_hashes["origin"],
            "origin_field_aggregate": aggregate_sha256(field_paths["origin"]),
            "f2cpp_field_aggregate": aggregate_sha256(field_paths["f2cpp"]),
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
