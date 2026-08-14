#!/usr/bin/env python3
"""Validate I6 irregular receiver SHD fields against Origin Bellhop."""

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


CASE_ID = "irregular_receiver_pairs"
PROFILES = {"single": (1000.0,), "broadband_smoke": (1000.0, 2000.0)}
EXPECTED_DEPTHS = np.asarray((20.0, 35.0, 50.0, 65.0, 80.0))
EXPECTED_RANGES = np.asarray((50.0, 200.0, 350.0, 500.0, 650.0))


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def aggregate_sha256(paths: list[Path]) -> str:
    digest = hashlib.sha256()
    for path in sorted(paths, key=lambda item: str(item)):
        digest.update(path.read_bytes())
    return digest.hexdigest()


def load_profile(
    root: Path, version: str, profile: str,
    frequencies: tuple[float, ...], executable: Path,
) -> list[tuple[Path, Path]]:
    profile_root = root / version / CASE_ID / profile
    manifest_path = profile_root / "run_manifest.json"
    if not manifest_path.is_file():
        raise ValueError(f"{manifest_path}: manifest is absent")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if (
        manifest.get("version") != version
        or manifest.get("case_id") != CASE_ID
        or manifest.get("profile") != profile
        or manifest.get("last_stage") != "test"
        or Path(manifest["executable"]).resolve() != executable
    ):
        raise ValueError(f"{manifest_path}: run identity mismatch")
    if manifest_path.stat().st_mtime_ns < executable.stat().st_mtime_ns:
        raise ValueError(f"{manifest_path}: results predate executable")
    runs = manifest.get("runs")
    if (
        tuple(float(value) for value in manifest.get("frequencies_hz", ()))
        != frequencies
        or not isinstance(runs, list)
        or tuple(float(run["frequency_hz"]) for run in runs) != frequencies
        or any(
            run.get("frequency_index") != index
            or run.get("status") != "passed"
            for index, run in enumerate(runs)
        )
    ):
        raise ValueError(f"{manifest_path}: frequency/order/status mismatch")
    output: list[tuple[Path, Path]] = []
    for run in runs:
        environment = profile_root / run["environment_file"]
        shade = profile_root / run["shade_file"]
        for artifact in (environment, shade):
            if not artifact.is_file() or artifact.stat().st_size == 0:
                raise ValueError(f"{manifest_path}: missing {artifact}")
            if artifact.stat().st_mtime_ns < executable.stat().st_mtime_ns:
                raise ValueError(f"{manifest_path}: {artifact.name} is stale")
        output.append((environment, shade))
    return output


def validate(root: Path, origin: Path, f2cpp: Path) -> dict[str, object]:
    executables = {"origin": origin.resolve(), "f2cpp": f2cpp.resolve()}
    if executables["origin"] == executables["f2cpp"]:
        raise ValueError("executable paths must differ")
    if not all(path.is_file() for path in executables.values()):
        raise ValueError("an executable is absent")
    executable_hashes = {key: sha256(path) for key, path in executables.items()}
    if executable_hashes["origin"] == executable_hashes["f2cpp"]:
        raise ValueError("executable hashes must differ")

    loaded: dict[str, dict[str, list[tuple[Path, Path]]]] = {
        "origin": {}, "f2cpp": {}
    }
    input_hashes: dict[str, dict[str, str]] = {"origin": {}, "f2cpp": {}}
    field_paths: dict[str, list[Path]] = {"origin": [], "f2cpp": []}
    for version in ("origin", "f2cpp"):
        for profile, frequencies in PROFILES.items():
            runs = load_profile(
                root, version, profile, frequencies, executables[version]
            )
            loaded[version][profile] = runs
            input_hashes[version][profile] = aggregate_sha256(
                [environment for environment, _ in runs]
            )
            field_paths[version].extend(shade for _, shade in runs)
    if input_hashes["origin"] != input_hashes["f2cpp"]:
        raise ValueError("Origin and F2CPP rendered ENV inputs differ")

    comparisons: dict[str, dict[str, float | bool]] = {}
    layout_guards: dict[str, dict[str, float | bool]] = {}
    tolerances = STANDARD_CASES_ROOT / "codes" / "tolerances.toml"
    for profile, frequencies in PROFILES.items():
        for index, frequency in enumerate(frequencies):
            shades = {
                version: loaded[version][profile][index][1]
                for version in ("origin", "f2cpp")
            }
            passed, metrics = compare_files(
                shades["origin"], shades["f2cpp"], 0, 0, tolerances
            )
            if not passed:
                raise ValueError(
                    f"{profile}/{frequency:g}Hz mismatch: {metrics}"
                )
            comparisons[f"{profile}/{frequency:g}Hz"] = {
                "passed": True, **metrics
            }
            for version, shade in shades.items():
                field = ShdReader(shade).read()
                if (
                    field.header.plot_type != "irregular"
                    or not np.array_equal(
                        field.header.receiver_depths_m, EXPECTED_DEPTHS
                    )
                    or not np.array_equal(
                        field.header.receiver_ranges_m, EXPECTED_RANGES
                    )
                    or field.pressure.shape != (1, 1, 1, 5)
                ):
                    raise ValueError(
                        f"{version}/{profile}/{frequency:g}Hz irregular "
                        "header/data layout mismatch"
                    )
                maximum_pressure = float(np.max(np.abs(field.pressure)))
                if not np.isfinite(maximum_pressure) or maximum_pressure <= 0.0:
                    raise ValueError(
                        f"{version}/{profile}/{frequency:g}Hz field is empty"
                    )
                layout_guards[f"{version}/{profile}/{frequency:g}Hz"] = {
                    "passed": True,
                    "maximum_pressure_magnitude": maximum_pressure,
                }

    for version in ("origin", "f2cpp"):
        single = ShdReader(loaded[version]["single"][0][1]).read().pressure
        repeat = ShdReader(
            loaded[version]["broadband_smoke"][0][1]
        ).read().pressure
        if not np.array_equal(single, repeat):
            raise ValueError(f"{version}: repeated 1000Hz slice differs")

    commands = [
        "python3 test/standard_cases/codes/standard_cases.py test "
        f"--version {version} --case {CASE_ID} --profile {profile} "
        f"--executable {'Bellhop_origin/bin/bellhop' if version == 'origin' else 'Bellhop_F2CPP/build/release/bellhop_f2cpp'} "
        "--results-root <results-root>"
        for version in ("origin", "f2cpp") for profile in PROFILES
    ]
    return {
        "schema": "bellhop.f2cpp.i6_irregular_receiver_validation",
        "schema_version": 1,
        "status": "passed",
        "executables": {
            version: {
                "path": str(path), "sha256": executable_hashes[version],
                "mtime_ns": path.stat().st_mtime_ns,
            }
            for version, path in executables.items()
        },
        "case": {
            "id": CASE_ID,
            "profiles": {key: list(value) for key, value in PROFILES.items()},
            "receiver_depths_m": EXPECTED_DEPTHS.tolist(),
            "receiver_ranges_m": EXPECTED_RANGES.tolist(),
            "pressure_shape": [1, 1, 1, 5],
            "legacy_cc_semantics": (
                "Origin InfluenceCervenyCart uses Rz(1) for every irregular "
                "range while retaining the complete paired axes in SHD"
            ),
        },
        "origin_f2cpp_field_comparisons": comparisons,
        "irregular_layout_nonempty_guards": layout_guards,
        "provenance_guards": {
            "distinct_executable_paths_and_hashes": True,
            "results_not_older_than_executables": True,
            "rendered_env_inputs_equal": True,
            "irregular_header_axes_and_record_shape_exact": True,
            "single_broadband_1000hz_repeatable": True,
            "field_comparison_slice_count": len(comparisons),
        },
        "generation": {
            "case_commands": commands,
            "validator_command": (
                "python3 test/standard_cases/codes/"
                "validate_i6_irregular_receivers.py "
                "--results-root <results-root> "
                "--origin-executable Bellhop_origin/bin/bellhop "
                "--f2cpp-executable Bellhop_F2CPP/build/release/bellhop_f2cpp "
                "--output Bellhop_F2CPP/doc/validation/"
                "i6_irregular_receivers_report.json"
            ),
        },
        "sha256": {
            "origin_executable": executable_hashes["origin"],
            "f2cpp_executable": executable_hashes["f2cpp"],
            "rendered_env_aggregates": input_hashes["origin"],
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
        args.results_root, args.origin_executable, args.f2cpp_executable
    )
    rendered = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.output is not None:
        args.output.write_text(rendered, encoding="utf-8")
    print(rendered, end="")


if __name__ == "__main__":
    main()
