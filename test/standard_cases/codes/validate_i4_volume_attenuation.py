#!/usr/bin/env python3
"""Validate I4-02 volume-attenuation fields against Origin Bellhop."""

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


CASE_FREQUENCIES = {
    "volume_attenuation_francois_garrison": {
        "single": (5000.0,),
        "broadband_smoke": (5000.0, 10000.0),
    },
    "volume_attenuation_biological": {
        "single": (5000.0,),
        "broadband_smoke": (2500.0, 5000.0),
    },
}
CONTROL_CASE = "constant_speed_no_attenuation_5khz"
CONTROL_FREQUENCIES = (5000.0,)
MINIMUM_NOOP_DIFFERENCE = 1.0e-6


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
    manifest_executable = Path(manifest["executable"]).resolve()
    if manifest_executable != expected_executable:
        raise ValueError(
            f"{manifest_path}: executable {manifest_executable} does not "
            f"match expected {expected_executable}"
        )
    if manifest_path.stat().st_mtime_ns < expected_executable.stat().st_mtime_ns:
        raise ValueError(
            f"{manifest_path}: results predate the expected executable; rerun"
        )
    runs = manifest["runs"]
    actual_frequencies = tuple(float(run["frequency_hz"]) for run in runs)
    if actual_frequencies != expected_frequencies:
        raise ValueError(
            f"{manifest_path}: frequencies {actual_frequencies} do not "
            f"match expected {expected_frequencies}"
        )
    result: list[tuple[Path, Path]] = []
    for run in runs:
        if run["status"] != "passed":
            raise ValueError(f"{manifest_path}: run is not passed")
        result.append(
            (
                profile_root / run["environment_file"],
                profile_root / run["shade_file"],
            )
        )
    return result


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
        version: sha256(executable)
        for version, executable in executables.items()
    }
    if executable_hashes["origin"] == executable_hashes["f2cpp"]:
        raise ValueError(
            "Origin and F2CPP executables must have distinct content hashes"
        )

    paths: dict[str, dict[str, dict[str, list[tuple[Path, Path]]]]] = {
        "origin": {},
        "f2cpp": {},
    }
    rendered_environments: list[Path] = []
    field_paths: dict[str, list[Path]] = {"origin": [], "f2cpp": []}
    for version in ("origin", "f2cpp"):
        for case_id, profiles in CASE_FREQUENCIES.items():
            paths[version][case_id] = {}
            for profile, frequencies in profiles.items():
                loaded = load_profile(
                    results_root,
                    version,
                    case_id,
                    profile,
                    frequencies,
                    executables[version],
                )
                paths[version][case_id][profile] = loaded
                field_paths[version].extend(shade for _, shade in loaded)
                if version == "origin":
                    rendered_environments.extend(env for env, _ in loaded)

    comparisons: dict[str, dict[str, float | bool]] = {}
    tolerance_path = STANDARD_CASES_ROOT / "codes" / "tolerances.toml"
    for case_id, profiles in CASE_FREQUENCIES.items():
        for profile, frequencies in profiles.items():
            for index, frequency in enumerate(frequencies):
                origin_shade = paths["origin"][case_id][profile][index][1]
                f2cpp_shade = paths["f2cpp"][case_id][profile][index][1]
                passed, metrics = compare_files(
                    origin_shade, f2cpp_shade, 0, 0, tolerance_path
                )
                if not passed:
                    raise ValueError(
                        f"{case_id}/{profile}/{frequency:g}Hz field mismatch: "
                        f"{metrics}"
                    )
                comparisons[
                    f"{case_id}/{profile}/{frequency:g}Hz"
                ] = {"passed": True, **metrics}

    noop_guards: dict[str, dict[str, float | bool]] = {}
    for version in ("origin", "f2cpp"):
        control = load_profile(
            results_root,
            version,
            CONTROL_CASE,
            "single",
            CONTROL_FREQUENCIES,
            executables[version],
        )[0][1]
        field_paths[version].append(control)
        control_pressure = ShdReader(control).read().pressure
        for case_id in CASE_FREQUENCIES:
            candidate_path = paths[version][case_id]["single"][0][1]
            candidate_pressure = ShdReader(candidate_path).read().pressure
            if candidate_pressure.shape != control_pressure.shape:
                raise ValueError(
                    f"{version}/{case_id}: control field shape mismatch"
                )
            maximum_difference = float(
                np.max(np.abs(candidate_pressure - control_pressure))
            )
            if maximum_difference <= MINIMUM_NOOP_DIFFERENCE:
                raise ValueError(
                    f"{version}/{case_id}: volume attenuation appears to be "
                    f"a no-op ({maximum_difference})"
                )
            noop_guards[f"{version}/{case_id}"] = {
                "passed": True,
                "max_pressure_absolute_vs_lossless": maximum_difference,
            }

    return {
        "schema": "bellhop.f2cpp.i4_volume_attenuation_validation",
        "schema_version": 1,
        "status": "passed",
        "cases": {
            case_id: {
                profile: list(frequencies)
                for profile, frequencies in profiles.items()
            }
            for case_id, profiles in CASE_FREQUENCIES.items()
        },
        "origin_f2cpp_field_comparisons": comparisons,
        "lossless_noop_guards": noop_guards,
        "sha256": {
            "origin_executable": executable_hashes["origin"],
            "f2cpp_executable": executable_hashes["f2cpp"],
            "rendered_environment_aggregate": aggregate_sha256(
                rendered_environments
            ),
            "origin_field_aggregate": aggregate_sha256(
                field_paths["origin"]
            ),
            "f2cpp_field_aggregate": aggregate_sha256(
                field_paths["f2cpp"]
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
