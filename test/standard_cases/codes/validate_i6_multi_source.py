#!/usr/bin/env python3
"""Validate I6 multi-source SHD slices against Origin Bellhop."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sys

import numpy as np

from compare_fields import (
    STANDARD_CASES_ROOT,
    compare_files,
    decoded_complex64_payload,
)


PLOTREAD_ROOT = STANDARD_CASES_ROOT.parent / "PlotRead"
sys.path.insert(0, str(PLOTREAD_ROOT))

from bellhop_io_py.shd import ShdReader


CASE_ID = "multi_source_depths"
PROFILES = {
    "single": (1000.0,),
    "broadband_smoke": (1000.0, 2000.0),
}
# The RayReuse leg covers the single profile. Multi-source RayReuse broadband
# runs report "Trace passes = Nfreq x NSz" under the frozen FP-2F semantics,
# which the standard-case runner broadband validation does not model yet
# (F08 scope); Origin/F2CPP keep validating both profiles.
RAYREUSE_PROFILES = ("single",)
EXPECTED_SOURCE_DEPTHS = np.asarray((20.0, 50.0, 80.0))
MINIMUM_SLICE_DIFFERENCE = 1.0e-6


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


def load_profile(
    results_root: Path,
    version: str,
    profile: str,
    expected_frequencies: tuple[float, ...],
    executable: Path,
) -> list[tuple[Path, Path]]:
    profile_root = results_root / version / CASE_ID / profile
    manifest_path = profile_root / "run_manifest.json"
    if not manifest_path.is_file():
        raise ValueError(f"{manifest_path}: run manifest is absent")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if (
        manifest.get("version") != version
        or manifest.get("case_id") != CASE_ID
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

    output: list[tuple[Path, Path]] = []
    for run in runs:
        environment = profile_root / run["environment_file"]
        shade = profile_root / run["shade_file"]
        for artifact in (environment, shade):
            if not artifact.is_file() or artifact.stat().st_size == 0:
                raise ValueError(f"{manifest_path}: missing {artifact}")
            require_current(artifact, executable, manifest_path)
        output.append((environment, shade))
    return output


def generation_commands(
    rayreuse_executable: Path | None = None,
) -> list[str]:
    executables = {
        "origin": "Bellhop_origin/bin/bellhop",
        "f2cpp": "Bellhop_F2CPP/build/release/bellhop_f2cpp",
    }
    versions = ("origin", "f2cpp")
    if rayreuse_executable is not None:
        executables["rayreuse"] = (
            "Bellhop_RayReuse/build/release/bellhop_rayreuse"
        )
        versions = ("origin", "f2cpp", "rayreuse")
    return [
        "python3 test/standard_cases/codes/standard_cases.py test "
        f"--version {version} --case {CASE_ID} --profile {profile} "
        f"--executable {executables[version]} "
        "--results-root <results-root>"
        for version in versions
        for profile in (
            RAYREUSE_PROFILES if version == "rayreuse" else PROFILES
        )
    ]


def validate(
    results_root: Path,
    origin_executable: Path,
    f2cpp_executable: Path,
    rayreuse_executable: Path | None = None,
) -> dict[str, object]:
    executables = {
        "origin": origin_executable.resolve(),
        "f2cpp": f2cpp_executable.resolve(),
    }
    if rayreuse_executable is not None:
        executables["rayreuse"] = rayreuse_executable.resolve()
    if executables["origin"] == executables["f2cpp"]:
        raise ValueError("Origin and F2CPP executable paths must differ")
    if not all(path.is_file() for path in executables.values()):
        raise ValueError("an expected executable does not exist")
    executable_hashes = {
        version: sha256(path) for version, path in executables.items()
    }
    if executable_hashes["origin"] == executable_hashes["f2cpp"]:
        raise ValueError("Origin and F2CPP executable hashes must differ")
    if "rayreuse" in executables and (
        len(set(executables.values())) != len(executables)
        or len(set(executable_hashes.values())) != len(executable_hashes)
    ):
        raise ValueError(
            "Origin, F2CPP, and RayReuse executables must be distinct"
        )

    loaded: dict[str, dict[str, list[tuple[Path, Path]]]] = {
        "origin": {},
        "f2cpp": {},
    }
    input_hashes: dict[str, dict[str, str]] = {"origin": {}, "f2cpp": {}}
    field_paths: dict[str, list[Path]] = {"origin": [], "f2cpp": []}
    for version in ("origin", "f2cpp"):
        for profile, frequencies in PROFILES.items():
            runs = load_profile(
                results_root, version, profile, frequencies,
                executables[version],
            )
            loaded[version][profile] = runs
            input_hashes[version][profile] = aggregate_sha256(
                [environment for environment, _ in runs]
            )
            field_paths[version].extend(shade for _, shade in runs)
    if input_hashes["origin"] != input_hashes["f2cpp"]:
        raise ValueError("Origin and F2CPP rendered ENV inputs differ")

    # Optional RayReuse leg. RayReuse broadband runs share one SHD across the
    # frequency slices, so candidate fields are always read at the run's
    # frequency index instead of slice 0.
    loaded_rayreuse: dict[str, list[tuple[Path, Path]]] = {}
    if "rayreuse" in executables:
        for profile in RAYREUSE_PROFILES:
            loaded_rayreuse[profile] = load_profile(
                results_root, "rayreuse", profile, PROFILES[profile],
                executables["rayreuse"],
            )
        field_paths["rayreuse"] = [
            shade for runs in loaded_rayreuse.values()
            for _, shade in runs
        ]

    comparisons: dict[str, dict[str, float | bool]] = {}
    rayreuse_comparisons: dict[str, dict[str, float | bool]] = {}
    payload_exact: dict[str, dict[str, object]] = {}
    slice_guards: dict[str, dict[str, float | bool]] = {}
    tolerance_path = STANDARD_CASES_ROOT / "codes" / "tolerances.toml"
    for profile, frequencies in PROFILES.items():
        for index, frequency in enumerate(frequencies):
            origin_shade = loaded["origin"][profile][index][1]
            f2cpp_shade = loaded["f2cpp"][profile][index][1]
            passed, metrics = compare_files(
                origin_shade, f2cpp_shade, 0, 0, tolerance_path
            )
            if not passed:
                raise ValueError(
                    f"{profile}/{frequency:g}Hz field mismatch: {metrics}"
                )
            comparisons[f"{profile}/{frequency:g}Hz"] = {
                "passed": True,
                **metrics,
            }
            if loaded_rayreuse and profile in loaded_rayreuse:
                rayreuse_shade = loaded_rayreuse[profile][index][1]
                passed, metrics = compare_files(
                    origin_shade, rayreuse_shade, 0, index, tolerance_path
                )
                if not passed:
                    raise ValueError(
                        f"rayreuse/{profile}/{frequency:g}Hz field "
                        f"mismatch: {metrics}"
                    )
                rayreuse_comparisons[f"{profile}/{frequency:g}Hz"] = {
                    "passed": True,
                    **metrics,
                }
                f2cpp_payload = decoded_complex64_payload(
                    f2cpp_shade, 0
                )
                rayreuse_payload = decoded_complex64_payload(
                    rayreuse_shade, index
                )
                payload_exact[f"{profile}/{frequency:g}Hz"] = {
                    "passed": f2cpp_payload == rayreuse_payload,
                    "f2cpp_bytes": len(f2cpp_payload),
                    "rayreuse_bytes": len(rayreuse_payload),
                }
                if f2cpp_payload != rayreuse_payload:
                    raise ValueError(
                        f"rayreuse/{profile}/{frequency:g}Hz F2CPP payload "
                        "difference (expected zero difference)"
                    )
            for version, shade, slice_index in (
                ("origin", origin_shade, 0),
                ("f2cpp", f2cpp_shade, 0),
                *(
                    (("rayreuse", loaded_rayreuse[profile][index][1], index),)
                    if loaded_rayreuse and profile in loaded_rayreuse
                    else ()
                ),
            ):
                field = ShdReader(shade).read(
                    frequency_index=slice_index
                )
                if not np.array_equal(
                    field.header.source_depths_m, EXPECTED_SOURCE_DEPTHS
                ):
                    raise ValueError(
                        f"{version}/{profile}/{frequency:g}Hz source depths "
                        "are not the sorted Origin vector"
                    )
                if field.pressure.shape != (1, 3, 11, 51):
                    raise ValueError(
                        f"{version}/{profile}/{frequency:g}Hz unexpected "
                        f"pressure shape {field.pressure.shape}"
                    )
                pairwise = [
                    float(np.max(np.abs(field.pressure[:, left] -
                                        field.pressure[:, right])))
                    for left, right in ((0, 1), (1, 2), (0, 2))
                ]
                minimum_difference = min(pairwise)
                if (
                    not np.isfinite(minimum_difference)
                    or minimum_difference <= MINIMUM_SLICE_DIFFERENCE
                ):
                    raise ValueError(
                        f"{version}/{profile}/{frequency:g}Hz source slices "
                        f"appear aliased ({minimum_difference})"
                    )
                slice_guards[f"{version}/{profile}/{frequency:g}Hz"] = {
                    "passed": True,
                    "minimum_pairwise_max_pressure_difference": (
                        minimum_difference
                    ),
                }

    for version in ("origin", "f2cpp"):
        single = ShdReader(loaded[version]["single"][0][1]).read().pressure
        broadband = ShdReader(
            loaded[version]["broadband_smoke"][0][1]
        ).read().pressure
        if not np.array_equal(single, broadband):
            raise ValueError(
                f"{version}: single and broadband 1000Hz slices differ"
            )
    if "broadband_smoke" in loaded_rayreuse:
        single = ShdReader(loaded_rayreuse["single"][0][1]).read().pressure
        broadband = ShdReader(
            loaded_rayreuse["broadband_smoke"][0][1]
        ).read(frequency_index=0).pressure
        if not np.array_equal(single, broadband):
            raise ValueError(
                "rayreuse: single and broadband 1000Hz slices differ"
            )

    result: dict[str, object] = {
        "schema": "bellhop.f2cpp.i6_multi_source_validation",
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
        "case": {
            "id": CASE_ID,
            "profiles": {
                name: list(values) for name, values in PROFILES.items()
            },
            "source_depths_m": EXPECTED_SOURCE_DEPTHS.tolist(),
            "shd_pressure_shape": [1, 3, 11, 51],
        },
        "origin_f2cpp_field_comparisons": comparisons,
        "independent_source_slice_guards": slice_guards,
        "provenance_guards": {
            "distinct_executable_paths": True,
            "distinct_executable_hashes": True,
            "results_not_older_than_executables": True,
            "origin_f2cpp_rendered_env_inputs_equal": True,
            "sorted_source_depth_vector_exact": True,
            "source_major_pressure_shape_exact": True,
            "single_broadband_1000hz_repeatable": True,
            "field_comparison_slice_count": len(comparisons),
            "independent_source_guard_count": len(slice_guards),
        },
        "generation": {
            "case_commands": generation_commands(rayreuse_executable),
            "validator_command": (
                "python3 test/standard_cases/codes/"
                "validate_i6_multi_source.py "
                "--results-root <results-root> "
                "--origin-executable Bellhop_origin/bin/bellhop "
                "--f2cpp-executable "
                "Bellhop_F2CPP/build/release/bellhop_f2cpp"
            ),
        },
        "sha256": {
            version: executable_hashes[version]
            for version in executable_hashes
        },
    }
    result["sha256"]["rendered_env_aggregates"] = input_hashes["origin"]
    for version in ("origin", "f2cpp"):
        result["sha256"][f"{version}_field_aggregate"] = aggregate_sha256(
            field_paths[version]
        )
    if loaded_rayreuse:
        result["origin_rayreuse_field_comparisons"] = rayreuse_comparisons
        result["f2cpp_rayreuse_payload_exact"] = payload_exact
        result["sha256"]["rayreuse_field_aggregate"] = aggregate_sha256(
            field_paths["rayreuse"]
        )
        result["generation"]["validator_command"] += (
            " --rayreuse-executable "
            "Bellhop_RayReuse/build/release/bellhop_rayreuse"
        )
        result["provenance_guards"]["f2cpp_rayreuse_payload_zero_difference"] = (
            all(entry["passed"] for entry in payload_exact.values())
        )
    return result


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
