#!/usr/bin/env python3
"""Validate I4-02 volume-attenuation fields across implementations."""

from __future__ import annotations

import argparse
import hashlib
import itertools
import json
from pathlib import Path
import sys

import numpy as np

from compare_fields import STANDARD_CASES_ROOT, compare_files


PLOTREAD_ROOT = STANDARD_CASES_ROOT.parent / "PlotRead"
sys.path.insert(0, str(PLOTREAD_ROOT))

from bellhop_io_py.shd import ShdReader


CASE_FREQUENCIES = {
    "constant_speed_thorp": {
        "single": (5000.0,),
        "broadband_smoke": (1000.0, 5000.0),
        "broadband_regression": (
            1000.0,
            1600.0,
            2200.0,
            2800.0,
            3400.0,
            4000.0,
            4600.0,
            5200.0,
            5800.0,
            6400.0,
            7000.0,
            7600.0,
            8200.0,
            8800.0,
            9400.0,
            10000.0,
        ),
    },
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

EXPECTED_THORP_HASHES = {
    "single": "27450009cbc6861ffc8f89e127432c09c852ca34af47e8a057e7d218db3f48ea",
    "broadband_smoke": "1ddd8171315750ddf754136191bc9a7aa3e5b0747cc0b43286acabc74305a7f3",
    "broadband_regression": "c8ef3fad90e32753991b021eb804f4f26046c04783ab50a602a2983f26dcbcd2",
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
    if not manifest_path.is_file():
        raise ValueError(f"missing manifest: {manifest_path}")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if (
        manifest.get("version"),
        manifest.get("case_id"),
        manifest.get("profile"),
        manifest.get("last_stage"),
    ) != (version, case_id, profile, "test"):
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
    runs = manifest.get("runs", [])
    actual_frequencies = tuple(float(run["frequency_hz"]) for run in runs)
    if actual_frequencies != expected_frequencies:
        raise ValueError(
            f"{manifest_path}: frequencies {actual_frequencies} do not "
            f"match expected {expected_frequencies}"
        )
    result: list[tuple[Path, Path]] = []
    for run in runs:
        if run.get("status") != "passed":
            raise ValueError(f"{manifest_path}: run is not passed")
        environment = profile_root / run["environment_file"]
        shade = profile_root / run["shade_file"]
        for artifact in (environment, shade):
            if not artifact.is_file() or artifact.stat().st_size == 0:
                raise ValueError(f"{manifest_path}: missing artifact {artifact}")
        result.append((environment, shade))
    return result


def validate(
    results_root: Path,
    origin_executable: Path,
    f2cpp_executable: Path,
    rayreuse_executable: Path | None = None,
) -> dict[str, object]:
    supplied = {"origin": origin_executable, "f2cpp": f2cpp_executable}
    if rayreuse_executable is not None:
        supplied["rayreuse"] = rayreuse_executable
    executables = {
        version: executable.resolve()
        for version, executable in supplied.items()
    }
    if len(set(executables.values())) != len(executables):
        raise ValueError("implementation executable paths must be distinct")
    for version, executable in executables.items():
        if not executable.is_file():
            raise ValueError(
                f"{version} executable does not exist: {executable}"
            )
    executable_hashes = {
        version: sha256(executable)
        for version, executable in executables.items()
    }
    if len(set(executable_hashes.values())) != len(executable_hashes):
        raise ValueError(
            "implementation executables must have distinct content hashes"
        )

    paths: dict[str, dict[str, dict[str, list[tuple[Path, Path]]]]] = {
        version: {} for version in executables
    }
    field_paths: dict[str, list[Path]] = {
        version: [] for version in executables
    }
    rendered_environments: list[Path] = []
    for version, executable in executables.items():
        for case_id, profiles in CASE_FREQUENCIES.items():
            paths[version][case_id] = {}
            for profile, frequencies in profiles.items():
                loaded = load_profile(
                    results_root,
                    version,
                    case_id,
                    profile,
                    frequencies,
                    executable,
                )
                paths[version][case_id][profile] = loaded
                field_paths[version].extend(shade for _, shade in loaded)
                rendered_environments.extend(env for env, _ in loaded)

    # Validate rendered input environments consistency across implementations
    for case_id, profiles in CASE_FREQUENCIES.items():
        for profile in profiles:
            origin_envs = [
                env.read_bytes()
                for env, _ in paths["origin"][case_id][profile]
            ]
            f2cpp_envs = [
                env.read_bytes()
                for env, _ in paths["f2cpp"][case_id][profile]
            ]
            if origin_envs != f2cpp_envs:
                raise ValueError(
                    f"{case_id}/{profile}: origin and f2cpp rendered env bytes differ"
                )
            if profile == "single" and "rayreuse" in executables:
                rayreuse_env = paths["rayreuse"][case_id]["single"][0][0].read_bytes()
                if rayreuse_env != origin_envs[0]:
                    raise ValueError(
                        f"{case_id}/single: rayreuse rendered env bytes differ from origin"
                    )

    tolerance_path = STANDARD_CASES_ROOT / "codes" / "tolerances.toml"
    comparisons: dict[str, dict[str, object]] = {}
    versions = tuple(executables)
    gating_passed_count = 0
    non_gating_count = 0
    for case_id, profiles in CASE_FREQUENCIES.items():
        for profile, frequencies in profiles.items():
            fmax = max(frequencies)
            for index, frequency in enumerate(frequencies):
                # Origin/F2CPP have one SHD per run; RayReuse has one broadband SHD.
                shades = {
                    version: paths[version][case_id][profile][
                        0 if version == "rayreuse" else index
                    ][1]
                    for version in versions
                }
                for left, right in itertools.combinations(versions, 2):
                    is_gating = (
                        (left, right) == ("origin", "rayreuse")
                        or (right, left) == ("origin", "rayreuse")
                        or frequency == fmax
                        or profile == "single"
                    )
                    passed, metrics = compare_files(
                        shades[left],
                        shades[right],
                        index if left == "rayreuse" else 0,
                        index if right == "rayreuse" else 0,
                        tolerance_path,
                    )
                    if is_gating:
                        if not passed:
                            raise ValueError(
                                f"{case_id}/{profile}/{frequency:g}Hz "
                                f"{left}/{right} mismatch: {metrics}"
                            )
                        gating_passed_count += 1
                    else:
                        non_gating_count += 1
                    comparisons[
                        f"{case_id}/{profile}/{frequency:g}Hz/{left}_{right}"
                    ] = {
                        "passed": passed,
                        "gating": is_gating,
                        "metrics": metrics,
                        **(
                            {}
                            if is_gating
                            else {
                                "non_gating_reason": "F2CPP D-02 replans from current single frequency; only fmax matches shared-fmax launch fan"
                            }
                        ),
                    }

    noop_guards: dict[str, dict[str, float | bool]] = {}
    for version in versions:
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

    # Assert RayReuse Thorp SHD hashes match H00 baseline
    if "rayreuse" in executables:
        for profile, expected_hash in EXPECTED_THORP_HASHES.items():
            actual_shd = paths["rayreuse"]["constant_speed_thorp"][profile][0][1]
            actual_hash = sha256(actual_shd)
            if actual_hash != expected_hash:
                raise ValueError(
                    f"RayReuse Thorp {profile} SHD hash {actual_hash} != baseline {expected_hash}"
                )

    return {
        "schema": "bellhop.rayreuse.i4_volume_attenuation_validation",
        "schema_version": 2,
        "status": "passed",
        "cases": {
            case_id: {
                profile: list(frequencies)
                for profile, frequencies in profiles.items()
            }
            for case_id, profiles in CASE_FREQUENCIES.items()
        },
        "total_pairwise_comparisons": len(comparisons),
        "gating_passed_comparisons": gating_passed_count,
        "non_gating_comparisons": non_gating_count,
        "pairwise_field_comparisons": comparisons,
        "lossless_noop_guards": noop_guards,
        "thorp_baseline_hashes_verified": "rayreuse" in executables,
        "executables": {
            v: {
                "path": str(p),
                "sha256": executable_hashes[v],
                "mtime_ns": p.stat().st_mtime_ns,
            }
            for v, p in executables.items()
        },
        "sha256": {
            "rendered_environment_aggregate": aggregate_sha256(
                rendered_environments
            ),
            **{
                v + "_field_aggregate": aggregate_sha256(field_paths[v])
                for v in executables
            },
        },
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--results-root", type=Path, required=True)
    parser.add_argument("--origin-executable", type=Path, required=True)
    parser.add_argument("--f2cpp-executable", type=Path, required=True)
    parser.add_argument("--rayreuse-executable", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = validate(
        args.results_root,
        args.origin_executable,
        args.f2cpp_executable,
        args.rayreuse_executable,
    )
    rendered = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.output is not None:
        args.output.write_text(rendered, encoding="utf-8")
    print(rendered, end="")


if __name__ == "__main__":
    main()
