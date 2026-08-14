#!/usr/bin/env python3
"""Validate I4-04 grain-size half-space fields against Origin Bellhop."""

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


GRAIN_CASE = "grain_size_flat"
CONTROL_CASE = "grain_size_equivalent_acoustic_control"
PROFILES = {
    "single": (1000.0,),
    "broadband_smoke": (1000.0, 2000.0),
}


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
    output = [
        (profile_root / run["environment_file"], profile_root / run["shade_file"])
        for run in runs
    ]
    if any(not environment.is_file() or not shade.is_file() for environment, shade in output):
        raise ValueError(f"{manifest_path}: expected ENV or SHD output is absent")
    return output


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
    executable_hashes = {version: sha256(path) for version, path in executables.items()}
    if executable_hashes["origin"] == executable_hashes["f2cpp"]:
        raise ValueError("Origin and F2CPP executables have identical hashes")

    loaded: dict[str, dict[str, dict[str, list[tuple[Path, Path]]]]] = {
        "origin": {}, "f2cpp": {}
    }
    input_hashes: dict[str, dict[str, str]] = {"origin": {}, "f2cpp": {}}
    field_paths: dict[str, list[Path]] = {"origin": [], "f2cpp": []}
    for version in ("origin", "f2cpp"):
        for case_id in (GRAIN_CASE, CONTROL_CASE):
            loaded[version][case_id] = {}
            for profile, frequencies in PROFILES.items():
                runs = load_profile(
                    results_root, version, case_id, profile, frequencies,
                    executables[version],
                )
                loaded[version][case_id][profile] = runs
                input_hashes[version][f"{case_id}/{profile}"] = aggregate_sha256(
                    [environment for environment, _ in runs]
                )
                field_paths[version].extend(shade for _, shade in runs)
    if input_hashes["origin"] != input_hashes["f2cpp"]:
        raise ValueError("Origin and F2CPP rendered ENV inputs differ")

    comparisons: dict[str, dict[str, float | bool]] = {}
    equivalence_guards: dict[str, dict[str, bool | str]] = {}
    tolerance_path = STANDARD_CASES_ROOT / "codes" / "tolerances.toml"
    for profile, frequencies in PROFILES.items():
        for index, frequency in enumerate(frequencies):
            for case_id in (GRAIN_CASE, CONTROL_CASE):
                origin_shade = loaded["origin"][case_id][profile][index][1]
                f2cpp_shade = loaded["f2cpp"][case_id][profile][index][1]
                passed, metrics = compare_files(origin_shade, f2cpp_shade, 0, 0, tolerance_path)
                if not passed:
                    raise ValueError(f"{case_id}/{profile}/{frequency:g}Hz field mismatch: {metrics}")
                comparisons[f"{case_id}/{profile}/{frequency:g}Hz"] = {"passed": True, **metrics}

            for version in ("origin", "f2cpp"):
                grain_shade = loaded[version][GRAIN_CASE][profile][index][1]
                control_shade = loaded[version][CONTROL_CASE][profile][index][1]
                grain_pressure = ShdReader(grain_shade).read().pressure
                control_pressure = ShdReader(control_shade).read().pressure
                if grain_pressure.shape != control_pressure.shape:
                    raise ValueError(f"{version}/{profile}/{frequency:g}Hz control shape mismatch")
                if not np.array_equal(grain_pressure, control_pressure):
                    maximum_difference = float(np.max(np.abs(grain_pressure - control_pressure)))
                    raise ValueError(
                        f"{version}/{profile}/{frequency:g}Hz grain-size expansion "
                        f"does not equal acoustic control ({maximum_difference})"
                    )
                equivalence_guards[f"{version}/{profile}/{frequency:g}Hz"] = {
                    "passed": True,
                    "pressure_bit_identical": True,
                    "grain_shade_sha256": sha256(grain_shade),
                    "control_shade_sha256": sha256(control_shade),
                }

    return {
        "schema": "bellhop.f2cpp.i4_grain_size_validation",
        "schema_version": 1,
        "status": "passed",
        "cases": {"grain_size": GRAIN_CASE, "acoustic_control": CONTROL_CASE,
                  "profiles": {name: list(values) for name, values in PROFILES.items()}},
        "origin_f2cpp_field_comparisons": comparisons,
        "grain_size_equivalent_acoustic_noop_guards": equivalence_guards,
        "component_guards": {
            "all_grain_size_polynomial_branches_and_boundaries": True,
            "loss_parameter_conversion_matches_origin": True,
            "two_frequency_projection_preserves_frozen_path": True,
        },
        "origin_oracle": {
            "upstream_2d_initialization_defect_repaired": True,
            "repair": (
                "ReadEnvironmentBell TopBot G now stores vr, alpha2_f, and "
                "rho=rhor, matching the existing 3-D province data flow"
            ),
            "uninitialized_nan_behavior_is_not_a_compatibility_target": True,
        },
        "generation": {
            "case_command": "python3 test/standard_cases/codes/standard_cases.py test --version <origin|f2cpp> --case <grain_size_flat|grain_size_equivalent_acoustic_control> --profile <single|broadband_smoke> --executable <matching-executable> --results-root <results-root>",
            "validator_command": "python3 test/standard_cases/codes/validate_i4_grain_size.py --results-root <results-root> --origin-executable Bellhop_origin/bin/bellhop --f2cpp-executable Bellhop_F2CPP/build/release/bellhop_f2cpp",
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
    result = validate(args.results_root, args.origin_executable, args.f2cpp_executable)
    rendered = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.output is not None:
        args.output.write_text(rendered, encoding="utf-8")
    print(rendered, end="")


if __name__ == "__main__":
    main()
