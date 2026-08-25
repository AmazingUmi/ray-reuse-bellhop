#!/usr/bin/env python3
"""Validate I6 directional source beam-pattern fields against Origin."""

from __future__ import annotations

import argparse
import bisect
import hashlib
import json
import math
from pathlib import Path
import sys

import numpy as np

from compare_fields import STANDARD_CASES_ROOT, compare_files


PLOTREAD_ROOT = STANDARD_CASES_ROOT.parent / "PlotRead"
sys.path.insert(0, str(PLOTREAD_ROOT))

from bellhop_io_py.shd import ShdReader


DIRECTIONAL_CASE = "source_beam_pattern_directional"
CONTROL_CASE = "source_beam_pattern_omni_control"
CASES = (DIRECTIONAL_CASE, CONTROL_CASE)
PROFILES = {
    "single": (1000.0,),
    "broadband_smoke": (1000.0, 2000.0),
}
MINIMUM_DIRECTIONAL_EFFECT = 1.0e-6
EXPECTED_SBP_POINTS = (
    (-180.0, -20.0),
    (-60.0, -18.0),
    (-15.0, -3.0),
    (20.0, 0.0),
    (60.0, -12.0),
    (180.0, -20.0),
)
ANCHOR_ANGLES_DEGREES = (-200.0, -60.0, -37.5, 0.0, 60.0, 200.0)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def aggregate_sha256(paths: list[Path]) -> str:
    digest = hashlib.sha256()
    for path in sorted(paths, key=lambda item: str(item)):
        digest.update(path.read_bytes())
    return digest.hexdigest()


def require_current(artifact: Path, executable: Path, manifest: Path) -> None:
    if artifact.stat().st_mtime_ns < executable.stat().st_mtime_ns:
        raise ValueError(f"{manifest}: {artifact.name} predates executable")


def canonical_sbp() -> Path:
    return (
        STANDARD_CASES_ROOT / "cases" / DIRECTIONAL_CASE / "origin.sbp"
    )


def read_sbp(path: Path) -> tuple[tuple[float, float], ...]:
    lines = [
        line.split("!", 1)[0].strip()
        for line in path.read_text(encoding="utf-8").splitlines()
    ]
    lines = [line for line in lines if line]
    if not lines:
        raise ValueError(f"{path}: empty source beam-pattern table")
    try:
        count = int(lines[0])
        points = tuple(tuple(map(float, line.split())) for line in lines[1:])
    except ValueError as error:
        raise ValueError(f"{path}: malformed source beam-pattern table") from error
    if count < 2 or len(points) != count:
        raise ValueError(f"{path}: source beam-pattern count mismatch")
    if any(len(point) != 2 for point in points):
        raise ValueError(f"{path}: each source pattern row needs two values")
    if any(not all(math.isfinite(value) for value in point) for point in points):
        raise ValueError(f"{path}: source beam-pattern value is non-finite")
    if any(points[index - 1][0] >= points[index][0]
           for index in range(1, len(points))):
        raise ValueError(f"{path}: source beam-pattern angles are not increasing")
    return points


def interpolated_amplitude(
    points: tuple[tuple[float, float], ...], angle_degrees: float
) -> float:
    angles = [point[0] for point in points]
    amplitudes = [10.0 ** (point[1] / 20.0) for point in points]
    left = max(0, min(bisect.bisect_left(angles, angle_degrees) - 1,
                      len(points) - 2))
    fraction = (
        (angle_degrees - angles[left]) /
        (angles[left + 1] - angles[left])
    )
    return ((1.0 - fraction) * amplitudes[left] +
            fraction * amplitudes[left + 1])


def component_anchors() -> dict[str, float]:
    points = read_sbp(canonical_sbp())
    if points != EXPECTED_SBP_POINTS:
        raise ValueError("canonical SBP points differ from the frozen fixture")
    anchors = {
        f"{angle:g}deg": interpolated_amplitude(points, angle)
        for angle in ANCHOR_ANGLES_DEGREES
    }
    midpoint_db_then_convert = 10.0 ** (-10.5 / 20.0)
    if math.isclose(
        anchors["-37.5deg"], midpoint_db_then_convert,
        rel_tol=1.0e-12, abs_tol=1.0e-12,
    ):
        raise ValueError("SBP midpoint does not distinguish interpolation order")
    return anchors


def load_profile(
    results_root: Path,
    version: str,
    case_id: str,
    profile: str,
    expected_frequencies: tuple[float, ...],
    executable: Path,
) -> list[tuple[Path, Path, Path | None]]:
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
    if Path(manifest["executable"]).resolve() != executable:
        raise ValueError(f"{manifest_path}: executable identity mismatch")
    require_current(manifest_path, executable, manifest_path)
    frequencies = tuple(
        float(value) for value in manifest.get("frequencies_hz", ())
    )
    runs = manifest.get("runs")
    if frequencies != expected_frequencies or not isinstance(runs, list):
        raise ValueError(f"{manifest_path}: frequency grid mismatch")
    if tuple(float(run["frequency_hz"]) for run in runs) != frequencies:
        raise ValueError(f"{manifest_path}: run frequency grid mismatch")
    if any(
        run.get("frequency_index") != index
        or run.get("status") != "passed"
        for index, run in enumerate(runs)
    ):
        raise ValueError(f"{manifest_path}: run order/status mismatch")

    require_sbp = case_id == DIRECTIONAL_CASE
    output: list[tuple[Path, Path, Path | None]] = []
    for run in runs:
        environment = profile_root / run["environment_file"]
        shade = profile_root / run["shade_file"]
        sbp = environment.with_suffix(".sbp")
        for artifact in (environment, shade):
            if not artifact.is_file() or artifact.stat().st_size == 0:
                raise ValueError(f"{manifest_path}: missing {artifact}")
            require_current(artifact, executable, manifest_path)
        if require_sbp:
            if not sbp.is_file() or sbp.stat().st_size == 0:
                raise ValueError(f"{manifest_path}: staged SBP is absent")
            require_current(sbp, executable, manifest_path)
            if sha256(sbp) != sha256(canonical_sbp()):
                raise ValueError(f"{manifest_path}: staged SBP differs")
            staged_sbp: Path | None = sbp
        else:
            if sbp.exists():
                raise ValueError(f"{manifest_path}: omni control staged an SBP")
            staged_sbp = None
        output.append((environment, shade, staged_sbp))
    return output


def generation_commands() -> list[str]:
    executables = {
        "origin": "Bellhop_origin/bin/bellhop",
        "f2cpp": "Bellhop_F2CPP/build/release/bellhop_f2cpp",
    }
    return [
        "python3 test/standard_cases/codes/standard_cases.py test "
        f"--version {version} --case {case_id} --profile {profile} "
        f"--executable {executables[version]} "
        "--results-root <results-root>"
        for version in ("origin", "f2cpp")
        for case_id in CASES
        for profile in PROFILES
    ]


def validate(
    results_root: Path,
    origin_executable: Path,
    f2cpp_executable: Path,
) -> dict[str, object]:
    anchors = component_anchors()
    executables = {
        "origin": origin_executable.resolve(),
        "f2cpp": f2cpp_executable.resolve(),
    }
    if executables["origin"] == executables["f2cpp"]:
        raise ValueError("Origin and F2CPP executable paths must differ")
    if not all(path.is_file() for path in executables.values()):
        raise ValueError("an expected executable does not exist")
    executable_hashes = {
        version: sha256(path) for version, path in executables.items()
    }
    if executable_hashes["origin"] == executable_hashes["f2cpp"]:
        raise ValueError("Origin and F2CPP executable hashes must differ")

    loaded: dict[
        str, dict[str, dict[str, list[tuple[Path, Path, Path | None]]]]
    ] = {"origin": {}, "f2cpp": {}}
    input_hashes: dict[str, dict[str, str]] = {"origin": {}, "f2cpp": {}}
    sidecar_hashes: dict[str, dict[str, str]] = {"origin": {}, "f2cpp": {}}
    field_paths: dict[str, list[Path]] = {"origin": [], "f2cpp": []}
    for version in ("origin", "f2cpp"):
        for case_id in CASES:
            loaded[version][case_id] = {}
            for profile, frequencies in PROFILES.items():
                runs = load_profile(
                    results_root, version, case_id, profile, frequencies,
                    executables[version],
                )
                loaded[version][case_id][profile] = runs
                inputs: list[Path] = []
                sidecars: list[Path] = []
                for environment, shade, sbp in runs:
                    inputs.append(environment)
                    field_paths[version].append(shade)
                    if sbp is not None:
                        inputs.append(sbp)
                        sidecars.append(sbp)
                key = f"{case_id}/{profile}"
                input_hashes[version][key] = aggregate_sha256(inputs)
                if sidecars:
                    sidecar_hashes[version][profile] = aggregate_sha256(
                        sidecars
                    )
    if input_hashes["origin"] != input_hashes["f2cpp"]:
        raise ValueError("Origin and F2CPP rendered ENV/SBP inputs differ")
    if sidecar_hashes["origin"] != sidecar_hashes["f2cpp"]:
        raise ValueError("Origin and F2CPP staged SBP sidecars differ")

    comparisons: dict[str, dict[str, float | bool]] = {}
    effect_guards: dict[str, dict[str, float | bool]] = {}
    tolerance_path = STANDARD_CASES_ROOT / "codes" / "tolerances.toml"
    for profile, frequencies in PROFILES.items():
        for index, frequency in enumerate(frequencies):
            for case_id in CASES:
                origin_shade = loaded["origin"][case_id][profile][index][1]
                f2cpp_shade = loaded["f2cpp"][case_id][profile][index][1]
                passed, metrics = compare_files(
                    origin_shade, f2cpp_shade, 0, 0, tolerance_path
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
                directional = ShdReader(
                    loaded[version][DIRECTIONAL_CASE][profile][index][1]
                ).read().pressure
                control = ShdReader(
                    loaded[version][CONTROL_CASE][profile][index][1]
                ).read().pressure
                if directional.shape != control.shape:
                    raise ValueError("directional/control field shape mismatch")
                maximum_difference = float(
                    np.max(np.abs(directional - control))
                )
                if (
                    not np.isfinite(maximum_difference)
                    or maximum_difference <= MINIMUM_DIRECTIONAL_EFFECT
                ):
                    raise ValueError(
                        f"{version}/{profile}/{frequency:g}Hz source "
                        f"beam pattern appears inactive ({maximum_difference})"
                    )
                effect_guards[f"{version}/{profile}/{frequency:g}Hz"] = {
                    "passed": True,
                    "max_pressure_absolute_vs_omni_control": (
                        maximum_difference
                    ),
                }

    for version in ("origin", "f2cpp"):
        for case_id in CASES:
            single = ShdReader(
                loaded[version][case_id]["single"][0][1]
            ).read().pressure
            broadband = ShdReader(
                loaded[version][case_id]["broadband_smoke"][0][1]
            ).read().pressure
            if not np.array_equal(single, broadband):
                raise ValueError(
                    f"{version}/{case_id}: repeated 1000Hz fields differ"
                )

    return {
        "schema": "bellhop.f2cpp.i6_source_beam_pattern_validation",
        "schema_version": 1,
        "status": "passed",
        "cases": {
            "directional": DIRECTIONAL_CASE,
            "omnidirectional_control": CONTROL_CASE,
            "profiles": {
                name: list(values) for name, values in PROFILES.items()
            },
        },
        "origin_f2cpp_field_comparisons": comparisons,
        "directional_effect_guards": effect_guards,
        "component_guards": {
            "strictly_increasing_angle_table": True,
            "db_converted_to_linear_amplitude_before_interpolation": True,
            "origin_strict_lower_bracket_and_first_last_segment_extrapolation": True,
            "interpolation_amplitude_anchors": anchors,
        },
        "provenance_guards": {
            "distinct_executable_paths": True,
            "distinct_executable_hashes": True,
            "results_not_older_than_executables": True,
            "origin_f2cpp_rendered_env_sbp_inputs_equal": True,
            "staged_sbp_sidecars_equal_canonical_source": True,
            "omni_control_has_no_sbp_sidecar": True,
            "single_broadband_1000hz_repeatable": True,
            "field_comparison_slice_count": len(comparisons),
            "directional_effect_guard_count": len(effect_guards),
        },
        "generation": {
            "case_commands": generation_commands(),
            "validator_command": (
                "python3 test/standard_cases/codes/"
                "validate_i6_source_beam_pattern.py "
                "--results-root <results-root> "
                "--origin-executable Bellhop_origin/bin/bellhop "
                "--f2cpp-executable "
                "Bellhop_F2CPP/build/release/bellhop_f2cpp "
                "--output Bellhop_F2CPP/doc/reports/validation/"
                "i6_source_beam_pattern_report.json"
            ),
        },
        "sha256": {
            "origin_executable": executable_hashes["origin"],
            "f2cpp_executable": executable_hashes["f2cpp"],
            "canonical_sbp": sha256(canonical_sbp()),
            "rendered_env_sbp_aggregates": input_hashes["origin"],
            "staged_sbp_aggregates": sidecar_hashes["origin"],
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
