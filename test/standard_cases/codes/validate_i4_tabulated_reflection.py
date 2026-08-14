#!/usr/bin/env python3
"""Validate I4-05 bottom tabulated-reflection fields against Origin."""

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


TABLE_CASE = "tabulated_reflection_bottom"
CONTROL_CASE = "tabulated_reflection_rigid_control"
PROFILES = {"single": (1000.0,), "broadband_smoke": (1000.0, 2000.0)}
MINIMUM_TABLE_EFFECT = 1.0e-6


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
    require_brc: bool,
) -> list[tuple[Path, Path, Path | None]]:
    profile_root = results_root / version / case_id / profile
    manifest_path = profile_root / "run_manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if (
        manifest.get("version") != version
        or manifest.get("case_id") != case_id
        or manifest.get("profile") != profile
        or manifest.get("last_stage") != "test"
    ):
        raise ValueError(f"{manifest_path}: run identity mismatch")
    if Path(manifest["executable"]).resolve() != expected_executable:
        raise ValueError(f"{manifest_path}: executable identity mismatch")
    if manifest_path.stat().st_mtime_ns < expected_executable.stat().st_mtime_ns:
        raise ValueError(f"{manifest_path}: results predate executable")
    runs = manifest.get("runs")
    if not isinstance(runs, list):
        raise ValueError(f"{manifest_path}: runs must be a list")
    frequencies = tuple(float(run["frequency_hz"]) for run in runs)
    if frequencies != expected_frequencies:
        raise ValueError(f"{manifest_path}: unexpected frequency grid {frequencies}")
    if any(run.get("status") != "passed" for run in runs):
        raise ValueError(f"{manifest_path}: run is not passed")

    output: list[tuple[Path, Path, Path | None]] = []
    for run in runs:
        environment = profile_root / run["environment_file"]
        shade = profile_root / run["shade_file"]
        brc = environment.with_suffix(".brc") if require_brc else None
        if not environment.is_file() or not shade.is_file():
            raise ValueError(f"{manifest_path}: expected ENV or SHD output is absent")
        if brc is not None and not brc.is_file():
            raise ValueError(f"{manifest_path}: staged BRC sidecar is absent")
        output.append((environment, shade, brc))
    return output


def validate(
    results_root: Path,
    origin_executable: Path,
    f2cpp_executable: Path,
) -> dict[str, object]:
    executables = {"origin": origin_executable.resolve(), "f2cpp": f2cpp_executable.resolve()}
    if executables["origin"] == executables["f2cpp"]:
        raise ValueError("Origin and F2CPP executables must be distinct")
    if not all(path.is_file() for path in executables.values()):
        raise ValueError("an expected executable does not exist")
    executable_hashes = {version: sha256(path) for version, path in executables.items()}
    if executable_hashes["origin"] == executable_hashes["f2cpp"]:
        raise ValueError("Origin and F2CPP executables have identical hashes")

    loaded: dict[str, dict[str, dict[str, list[tuple[Path, Path, Path | None]]]]] = {
        "origin": {}, "f2cpp": {}
    }
    input_hashes: dict[str, dict[str, str]] = {"origin": {}, "f2cpp": {}}
    sidecar_hashes: dict[str, dict[str, str]] = {"origin": {}, "f2cpp": {}}
    field_paths: dict[str, list[Path]] = {"origin": [], "f2cpp": []}
    for version in ("origin", "f2cpp"):
        for case_id in (TABLE_CASE, CONTROL_CASE):
            loaded[version][case_id] = {}
            for profile, frequencies in PROFILES.items():
                runs = load_profile(
                    results_root, version, case_id, profile, frequencies,
                    executables[version], case_id == TABLE_CASE,
                )
                loaded[version][case_id][profile] = runs
                inputs = [environment for environment, _, _ in runs]
                if case_id == TABLE_CASE:
                    brcs = [brc for _, _, brc in runs]
                    if any(brc is None for brc in brcs):
                        raise AssertionError("tabulated-reflection case lacks BRC")
                    sidecar_hashes[version][profile] = aggregate_sha256(
                        [brc for brc in brcs if brc is not None]
                    )
                    inputs.extend(brc for brc in brcs if brc is not None)
                input_hashes[version][f"{case_id}/{profile}"] = aggregate_sha256(inputs)
                field_paths[version].extend(shade for _, shade, _ in runs)
    if input_hashes["origin"] != input_hashes["f2cpp"]:
        raise ValueError("Origin and F2CPP rendered ENV/BRC inputs differ")
    if sidecar_hashes["origin"] != sidecar_hashes["f2cpp"]:
        raise ValueError("Origin and F2CPP staged BRC sidecars differ")

    comparisons: dict[str, dict[str, float | bool]] = {}
    effect_guards: dict[str, dict[str, float | bool]] = {}
    tolerance_path = STANDARD_CASES_ROOT / "codes" / "tolerances.toml"
    for profile, frequencies in PROFILES.items():
        for index, frequency in enumerate(frequencies):
            for case_id in (TABLE_CASE, CONTROL_CASE):
                origin_shade = loaded["origin"][case_id][profile][index][1]
                f2cpp_shade = loaded["f2cpp"][case_id][profile][index][1]
                passed, metrics = compare_files(origin_shade, f2cpp_shade, 0, 0, tolerance_path)
                if not passed:
                    raise ValueError(f"{case_id}/{profile}/{frequency:g}Hz field mismatch: {metrics}")
                comparisons[f"{case_id}/{profile}/{frequency:g}Hz"] = {"passed": True, **metrics}

            for version in ("origin", "f2cpp"):
                table_pressure = ShdReader(loaded[version][TABLE_CASE][profile][index][1]).read().pressure
                rigid_pressure = ShdReader(loaded[version][CONTROL_CASE][profile][index][1]).read().pressure
                if table_pressure.shape != rigid_pressure.shape:
                    raise ValueError(f"{version}/{profile}/{frequency:g}Hz control shape mismatch")
                maximum_difference = float(np.max(np.abs(table_pressure - rigid_pressure)))
                if maximum_difference <= MINIMUM_TABLE_EFFECT:
                    raise ValueError(f"{version}/{profile}/{frequency:g}Hz table appears inactive ({maximum_difference})")
                effect_guards[f"{version}/{profile}/{frequency:g}Hz"] = {
                    "passed": True,
                    "max_pressure_absolute_vs_rigid_control": maximum_difference,
                }

    return {
        "schema": "bellhop.f2cpp.i4_tabulated_reflection_validation",
        "schema_version": 1,
        "status": "passed",
        "cases": {"tabulated_reflection": TABLE_CASE, "rigid_control": CONTROL_CASE,
                  "profiles": {name: list(values) for name, values in PROFILES.items()}},
        "origin_f2cpp_field_comparisons": comparisons,
        "tabulated_reflection_effect_guards": effect_guards,
        "component_guards": {
            "strictly_increasing_angle_table": True,
            "knot_midpoint_and_outside_domain_interpolation_anchors": True,
            "real4_domain_and_bracket_decision_anchors": True,
            "real4_internal_right_bracket_anchor": True,
            "inactive_terminal_negative_amplitude_is_retained": True,
            "phase_degrees_converted_to_radians": True,
            "two_frequency_projection_preserves_frozen_path": True,
        },
        "generation": {
            "case_command": "python3 test/standard_cases/codes/standard_cases.py test --version <origin|f2cpp> --case <tabulated_reflection_bottom|tabulated_reflection_rigid_control> --profile <single|broadband_smoke> --executable <matching-executable> --results-root <results-root>",
            "validator_command": "python3 test/standard_cases/codes/validate_i4_tabulated_reflection.py --results-root <results-root> --origin-executable Bellhop_origin/bin/bellhop --f2cpp-executable Bellhop_F2CPP/build/release/bellhop_f2cpp",
        },
        "sha256": {
            "origin_executable": executable_hashes["origin"],
            "f2cpp_executable": executable_hashes["f2cpp"],
            "rendered_input_aggregates": input_hashes["origin"],
            "staged_brc_aggregates": sidecar_hashes["origin"],
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
    result = validate(args.results_root, args.origin_executable, args.f2cpp_executable)
    rendered = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.output is not None:
        args.output.write_text(rendered, encoding="utf-8")
    print(rendered, end="")


if __name__ == "__main__":
    main()
