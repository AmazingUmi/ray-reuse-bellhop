#!/usr/bin/env python3
"""Validate ATT-01/ATT-02 attenuation-unit products across implementations."""

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


UNIT_SUFFIXES = ("n", "f", "m", "w", "q", "l")
PROFILES = {
    "single": (5000.0,),
    "broadband_smoke": (4000.0, 5000.0),
}
VERSIONS = ("origin", "f2cpp", "rayreuse")


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def aggregate_sha256(paths: list[Path]) -> str:
    digest = hashlib.sha256()
    for path in sorted(paths, key=str):
        digest.update(path.read_bytes())
    return digest.hexdigest()


def load_profile(
    results_root: Path,
    version: str,
    case_id: str,
    profile: str,
    expected_frequencies: tuple[float, ...],
    executable: Path,
) -> list[tuple[Path, Path]]:
    root = results_root / version / case_id / profile
    manifest_path = root / "run_manifest.json"
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
    if Path(manifest["executable"]).resolve() != executable:
        raise ValueError(f"{manifest_path}: executable identity mismatch")
    if manifest_path.stat().st_mtime_ns < executable.stat().st_mtime_ns:
        raise ValueError(f"{manifest_path}: results predate executable")
    runs = manifest.get("runs", [])
    frequencies = tuple(float(run["frequency_hz"]) for run in runs)
    if frequencies != expected_frequencies:
        raise ValueError(
            f"{manifest_path}: frequencies {frequencies} != {expected_frequencies}"
        )
    products: list[tuple[Path, Path]] = []
    for run in runs:
        if run.get("status") != "passed":
            raise ValueError(f"{manifest_path}: run is not passed")
        environment = root / run["environment_file"]
        shade = root / run["shade_file"]
        for artifact in (environment, shade):
            if not artifact.is_file() or artifact.stat().st_size == 0:
                raise ValueError(f"{manifest_path}: missing artifact {artifact}")
        products.append((environment, shade))
    return products


def validate(
    results_root: Path,
    origin_executable: Path,
    f2cpp_executable: Path,
    rayreuse_executable: Path | None = None,
) -> dict[str, object]:
    supplied = {"origin": origin_executable, "f2cpp": f2cpp_executable}
    if rayreuse_executable is not None:
        supplied["rayreuse"] = rayreuse_executable
    executables = {name: path.resolve() for name, path in supplied.items()}
    if len(set(executables.values())) != len(executables):
        raise ValueError("implementation executable paths must be distinct")
    for name, path in executables.items():
        if not path.is_file():
            raise ValueError(f"{name} executable does not exist: {path}")
    executable_hashes = {
        name: sha256(path) for name, path in executables.items()
    }
    if len(set(executable_hashes.values())) != len(executable_hashes):
        raise ValueError(
            "implementation executables must have distinct content hashes"
        )

    paths: dict[str, dict[str, dict[str, list[tuple[Path, Path]]]]] = {
        v: {} for v in executables
    }
    field_paths: dict[str, list[Path]] = {v: [] for v in executables}
    environments: list[Path] = []
    for version, executable in executables.items():
        for suffix in UNIT_SUFFIXES:
            case_id = f"attenuation_unit_{suffix}"
            paths[version][case_id] = {}
            for profile, frequencies in PROFILES.items():
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
                environments.extend(env for env, _ in loaded)

    # Validate rendered input environments consistency across implementations
    for suffix in UNIT_SUFFIXES:
        case_id = f"attenuation_unit_{suffix}"
        # Single profile: Origin, F2CPP, and RayReuse must have identical rendered ENV bytes
        origin_single_env = paths["origin"][case_id]["single"][0][0].read_bytes()
        f2cpp_single_env = paths["f2cpp"][case_id]["single"][0][0].read_bytes()
        if origin_single_env != f2cpp_single_env:
            raise ValueError(
                f"{case_id}/single: origin and f2cpp rendered env bytes differ"
            )
        if "rayreuse" in executables:
            rayreuse_single_env = paths["rayreuse"][case_id]["single"][0][0].read_bytes()
            if rayreuse_single_env != origin_single_env:
                raise ValueError(
                    f"{case_id}/single: rayreuse rendered env bytes differ from origin"
                )

        # Broadband profile: Origin and F2CPP per-frequency rendered ENVs must match
        origin_smoke_envs = [
            env.read_bytes() for env, _ in paths["origin"][case_id]["broadband_smoke"]
        ]
        f2cpp_smoke_envs = [
            env.read_bytes() for env, _ in paths["f2cpp"][case_id]["broadband_smoke"]
        ]
        if origin_smoke_envs != f2cpp_smoke_envs:
            raise ValueError(
                f"{case_id}/broadband_smoke: origin and f2cpp rendered env bytes differ"
            )

    tolerance_path = STANDARD_CASES_ROOT / "codes" / "tolerances.toml"
    comparisons: dict[str, dict[str, object]] = {}
    versions = tuple(executables)
    gating_passed_count = 0
    non_gating_count = 0
    for suffix in UNIT_SUFFIXES:
        case_id = f"attenuation_unit_{suffix}"
        for profile, frequencies in PROFILES.items():
            fmax = max(frequencies)
            for index, frequency in enumerate(frequencies):
                # Origin/F2CPP products are one file per run; RayReuse is one
                # broadband SHD, hence its frequency slice is selected below.
                version_shades = {}
                for version in executables:
                    loaded = paths[version][case_id][profile]
                    shade = (
                        loaded[0][1]
                        if version == "rayreuse"
                        else loaded[index][1]
                    )
                    version_shades[version] = shade
                for left, right in itertools.combinations(versions, 2):
                    is_gating = (
                        (left, right) == ("origin", "rayreuse")
                        or (right, left) == ("origin", "rayreuse")
                        or frequency == fmax
                        or profile == "single"
                    )
                    passed, metrics = compare_files(
                        version_shades[left],
                        version_shades[right],
                        0 if left != "rayreuse" else index,
                        0 if right != "rayreuse" else index,
                        tolerance_path,
                    )
                    if is_gating:
                        if not passed:
                            raise ValueError(
                                f"{case_id}/{profile}/{frequency:g}Hz {left}/{right} mismatch: {metrics}"
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

    cross_unit: dict[str, bool] = {}
    for version in executables:
        for profile, frequencies in PROFILES.items():
            index = frequencies.index(5000.0)
            reference = None
            for suffix in UNIT_SUFFIXES:
                case_id = f"attenuation_unit_{suffix}"
                loaded = paths[version][case_id][profile]
                shade = (
                    loaded[0][1]
                    if version == "rayreuse"
                    else loaded[index][1]
                )
                pressure = ShdReader(shade).read(
                    frequency_index=index if version == "rayreuse" else 0
                ).pressure
                if reference is None:
                    reference = pressure
                elif not np.array_equal(reference, pressure):
                    raise ValueError(
                        f"{version}/{profile}: equivalent 5 kHz units differ ({suffix})"
                    )
            cross_unit[f"{version}/{profile}/5000Hz"] = True

            if profile == "broadband_smoke":
                # At 4 kHz, linear-in-frequency units (F, W, Q, L) are mutually bit-identical,
                # and constant units (N, M) are mutually bit-identical.
                ref_linear = None
                for suffix in ("f", "w", "q", "l"):
                    case_id = f"attenuation_unit_{suffix}"
                    p4k = ShdReader(
                        paths[version][case_id][profile][0][1]
                    ).read(
                        frequency_index=0 if version == "rayreuse" else 0
                    ).pressure
                    if ref_linear is None:
                        ref_linear = p4k
                    elif not np.array_equal(ref_linear, p4k):
                        raise ValueError(
                            f"{version}/broadband_smoke: 4 kHz linear units differ ({suffix})"
                        )
                ref_const = None
                for suffix in ("n", "m"):
                    case_id = f"attenuation_unit_{suffix}"
                    p4k = ShdReader(
                        paths[version][case_id][profile][0][1]
                    ).read(
                        frequency_index=0 if version == "rayreuse" else 0
                    ).pressure
                    if ref_const is None:
                        ref_const = p4k
                    elif not np.array_equal(ref_const, p4k):
                        raise ValueError(
                            f"{version}/broadband_smoke: 4 kHz constant units differ ({suffix})"
                        )
                # W at 4 kHz must differ from W at 5 kHz
                p5k_shade = (
                    paths[version]["attenuation_unit_w"][profile][0][1]
                    if version == "rayreuse"
                    else paths[version]["attenuation_unit_w"][profile][1][1]
                )
                p5k = ShdReader(p5k_shade).read(
                    frequency_index=1 if version == "rayreuse" else 0
                ).pressure
                if np.array_equal(ref_linear, p5k):
                    raise ValueError(
                        f"{version}/broadband_smoke: W 4 kHz equals 5 kHz (no frequency scaling)"
                    )
                cross_unit[
                    f"{version}/broadband_smoke/4000Hz_linear_units_identical"
                ] = True
                cross_unit[
                    f"{version}/broadband_smoke/4000Hz_constant_units_identical"
                ] = True
                cross_unit[
                    f"{version}/broadband_smoke/w_frequency_scaling_verified"
                ] = True

    return {
        "schema": "bellhop.rayreuse.i4_attenuation_unit_validation",
        "schema_version": 2,
        "status": "passed",
        "profiles": PROFILES,
        "units": [s.upper() for s in UNIT_SUFFIXES],
        "total_pairwise_comparisons": len(comparisons),
        "gating_passed_comparisons": gating_passed_count,
        "non_gating_comparisons": non_gating_count,
        "pairwise_field_comparisons": comparisons,
        "cross_unit_fields_bit_identical": cross_unit,
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
                environments
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
    rendered = (
        json.dumps(
            validate(
                args.results_root,
                args.origin_executable,
                args.f2cpp_executable,
                args.rayreuse_executable,
            ),
            indent=2,
            sort_keys=True,
        )
        + "\n"
    )
    if args.output is not None:
        args.output.write_text(rendered, encoding="utf-8")
    print(rendered, end="")


if __name__ == "__main__":
    main()
