#!/usr/bin/env python3
"""Validate I5 2-D quadrilateral SSP fields against Origin Bellhop."""

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


RANGE_DEPENDENT_CASE = "q_range_dependent_cross_gradient"
CONTROL_CASE = "q_range_independent_control"
CASES = (RANGE_DEPENDENT_CASE, CONTROL_CASE)
PROFILES = {
    "single": (1000.0,),
    "broadband_smoke": (1000.0, 2000.0),
}
MINIMUM_RANGE_DEPENDENT_EFFECT = 1.0e-6
CONTROL_TOLERANCE_FILE = "tolerances_i5_q_control.toml"


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def aggregate_sha256(paths: list[Path]) -> str:
    digest = hashlib.sha256()
    for path in sorted(paths, key=lambda item: str(item)):
        digest.update(path.read_bytes())
    return digest.hexdigest()


def require_not_older(
    artifact: Path,
    executable: Path,
    manifest_path: Path,
) -> None:
    if artifact.stat().st_mtime_ns < executable.stat().st_mtime_ns:
        raise ValueError(
            f"{manifest_path}: {artifact.name} predates executable"
        )


def canonical_ssp(case_id: str) -> Path:
    return STANDARD_CASES_ROOT / "cases" / case_id / "origin.ssp"


def load_profile(
    results_root: Path,
    version: str,
    case_id: str,
    profile: str,
    expected_frequencies: tuple[float, ...],
    expected_executable: Path,
) -> list[tuple[Path, Path, Path]]:
    profile_root = results_root / version / case_id / profile
    manifest_path = profile_root / "run_manifest.json"
    if not manifest_path.is_file():
        raise ValueError(f"{manifest_path}: run manifest is absent")
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
    require_not_older(manifest_path, expected_executable, manifest_path)

    manifest_frequencies = tuple(
        float(value) for value in manifest.get("frequencies_hz", ())
    )
    if manifest_frequencies != expected_frequencies:
        raise ValueError(
            f"{manifest_path}: unexpected manifest frequency grid "
            f"{manifest_frequencies}"
        )
    runs = manifest.get("runs")
    if not isinstance(runs, list):
        raise ValueError(f"{manifest_path}: runs must be a list")
    frequencies = tuple(float(run["frequency_hz"]) for run in runs)
    if frequencies != expected_frequencies:
        raise ValueError(
            f"{manifest_path}: unexpected run frequency grid {frequencies}"
        )
    if any(
        run.get("frequency_index") != index
        for index, run in enumerate(runs)
    ):
        raise ValueError(f"{manifest_path}: frequency indices are not ordered")
    if any(run.get("status") != "passed" for run in runs):
        raise ValueError(f"{manifest_path}: run is not passed")

    canonical = canonical_ssp(case_id)
    if not canonical.is_file():
        raise ValueError(f"{canonical}: canonical SSP sidecar is absent")
    canonical_hash = sha256(canonical)
    output: list[tuple[Path, Path, Path]] = []
    for run in runs:
        environment = profile_root / run["environment_file"]
        shade = profile_root / run["shade_file"]
        sidecar = environment.with_suffix(".ssp")
        for artifact in (environment, sidecar, shade):
            if not artifact.is_file() or artifact.stat().st_size == 0:
                raise ValueError(
                    f"{manifest_path}: expected non-empty artifact is absent: "
                    f"{artifact}"
                )
            require_not_older(artifact, expected_executable, manifest_path)
        if sha256(sidecar) != canonical_hash:
            raise ValueError(
                f"{manifest_path}: staged SSP differs from canonical sidecar"
            )
        output.append((environment, shade, sidecar))
    return output


def generation_commands() -> list[str]:
    commands: list[str] = []
    executable_for = {
        "origin": "Bellhop_origin/bin/bellhop",
        "f2cpp": "Bellhop_F2CPP/build/release/bellhop_f2cpp",
    }
    for version in ("origin", "f2cpp"):
        for case_id in CASES:
            for profile in PROFILES:
                commands.append(
                    "python3 test/standard_cases/codes/standard_cases.py "
                    f"test --version {version} --case {case_id} "
                    f"--profile {profile} "
                    f"--executable {executable_for[version]} "
                    "--results-root <results-root>"
                )
    return commands


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
            "Origin and F2CPP executables have identical hashes"
        )

    canonical_sidecar_hashes = {
        case_id: sha256(canonical_ssp(case_id)) for case_id in CASES
    }
    if (
        canonical_sidecar_hashes[RANGE_DEPENDENT_CASE]
        == canonical_sidecar_hashes[CONTROL_CASE]
    ):
        raise ValueError(
            "range-dependent and control canonical SSP sidecars are identical"
        )

    loaded: dict[
        str,
        dict[str, dict[str, list[tuple[Path, Path, Path]]]],
    ] = {"origin": {}, "f2cpp": {}}
    input_hashes: dict[str, dict[str, str]] = {
        "origin": {},
        "f2cpp": {},
    }
    sidecar_hashes: dict[str, dict[str, str]] = {
        "origin": {},
        "f2cpp": {},
    }
    field_paths: dict[str, list[Path]] = {"origin": [], "f2cpp": []}
    for version in ("origin", "f2cpp"):
        for case_id in CASES:
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
                key = f"{case_id}/{profile}"
                rendered_inputs: list[Path] = []
                staged_sidecars: list[Path] = []
                for environment, shade, sidecar in runs:
                    rendered_inputs.extend((environment, sidecar))
                    staged_sidecars.append(sidecar)
                    field_paths[version].append(shade)
                input_hashes[version][key] = aggregate_sha256(
                    rendered_inputs
                )
                sidecar_hashes[version][key] = aggregate_sha256(
                    staged_sidecars
                )

    if input_hashes["origin"] != input_hashes["f2cpp"]:
        raise ValueError("Origin and F2CPP rendered ENV/SSP inputs differ")
    if sidecar_hashes["origin"] != sidecar_hashes["f2cpp"]:
        raise ValueError("Origin and F2CPP staged SSP sidecars differ")

    comparisons: dict[str, dict[str, float | bool]] = {}
    effect_guards: dict[str, dict[str, float | bool]] = {}
    default_tolerance_path = (
        STANDARD_CASES_ROOT / "codes" / "tolerances.toml"
    )
    control_tolerance_path = (
        STANDARD_CASES_ROOT / "codes" / CONTROL_TOLERANCE_FILE
    )
    for profile, frequencies in PROFILES.items():
        for index, frequency in enumerate(frequencies):
            for case_id in CASES:
                origin_shade = loaded["origin"][case_id][profile][index][1]
                f2cpp_shade = loaded["f2cpp"][case_id][profile][index][1]
                tolerance_path = (
                    default_tolerance_path
                    if case_id == RANGE_DEPENDENT_CASE
                    else control_tolerance_path
                )
                passed, metrics = compare_files(
                    origin_shade,
                    f2cpp_shade,
                    0,
                    0,
                    tolerance_path,
                )
                if not passed:
                    raise ValueError(
                        f"{case_id}/{profile}/{frequency:g}Hz field "
                        f"mismatch: {metrics}"
                    )
                comparisons[f"{case_id}/{profile}/{frequency:g}Hz"] = {
                    "passed": True,
                    **metrics,
                }

            for version in ("origin", "f2cpp"):
                dependent_pressure = ShdReader(
                    loaded[version][RANGE_DEPENDENT_CASE][profile][index][1]
                ).read().pressure
                control_pressure = ShdReader(
                    loaded[version][CONTROL_CASE][profile][index][1]
                ).read().pressure
                if dependent_pressure.shape != control_pressure.shape:
                    raise ValueError(
                        f"{version}/{profile}/{frequency:g}Hz control shape "
                        "mismatch"
                    )
                maximum_difference = float(
                    np.max(np.abs(dependent_pressure - control_pressure))
                )
                if (
                    not np.isfinite(maximum_difference)
                    or maximum_difference <= MINIMUM_RANGE_DEPENDENT_EFFECT
                ):
                    raise ValueError(
                        f"{version}/{profile}/{frequency:g}Hz "
                        "range-dependent SSP appears inactive "
                        f"({maximum_difference})"
                    )
                effect_guards[f"{version}/{profile}/{frequency:g}Hz"] = {
                    "passed": True,
                    "max_pressure_absolute_vs_q_control": maximum_difference,
                }

    return {
        "schema": "bellhop.f2cpp.i5_quadrilateral_ssp_validation",
        "schema_version": 1,
        "status": "passed",
        "executables": {
            version: {
                "path": str(path),
                "sha256": executable_hashes[version],
                "mtime_ns": path.stat().st_mtime_ns,
            }
            for version, path in executables.items()
        },
        "cases": {
            "range_dependent": RANGE_DEPENDENT_CASE,
            "range_independent_control": CONTROL_CASE,
            "profiles": {
                name: list(values) for name, values in PROFILES.items()
            },
        },
        "origin_f2cpp_field_comparisons": comparisons,
        "range_dependent_effect_guards": effect_guards,
        "provenance_guards": {
            "distinct_executable_paths": True,
            "distinct_executable_hashes": True,
            "results_not_older_than_executables": True,
            "origin_f2cpp_rendered_env_ssp_inputs_equal": True,
            "origin_f2cpp_staged_ssp_sidecars_equal": True,
            "staged_ssp_sidecars_equal_canonical_sources": True,
            "range_dependent_and_control_ssp_sources_distinct": True,
            "field_comparison_slice_count": len(comparisons),
            "effect_guard_count": len(effect_guards),
        },
        "compatibility_tolerances": {
            "range_dependent_case": "codes/tolerances.toml",
            "range_independent_control": (
                f"codes/{CONTROL_TOLERANCE_FILE}"
            ),
            "control_exception_reason": (
                "the constant-Q control is an effect/no-op guard; coherent "
                "near-zero cells amplify sub-micropressure differences, "
                "while the range-dependent feature case keeps the default "
                "strict field gate"
            ),
        },
        "generation": {
            "case_commands": generation_commands(),
            "validator_command": (
                "python3 "
                "test/standard_cases/codes/validate_i5_quadrilateral_ssp.py "
                "--results-root <results-root> "
                "--origin-executable Bellhop_origin/bin/bellhop "
                "--f2cpp-executable "
                "Bellhop_F2CPP/build/release/bellhop_f2cpp "
                "--output "
                "Bellhop_F2CPP/doc/reports/validation/"
                "i5_quadrilateral_ssp_report.json"
            ),
        },
        "sha256": {
            "origin_executable": executable_hashes["origin"],
            "f2cpp_executable": executable_hashes["f2cpp"],
            "canonical_ssp_sidecars": canonical_sidecar_hashes,
            "rendered_env_ssp_aggregates": input_hashes["origin"],
            "staged_ssp_aggregates": sidecar_hashes["origin"],
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
