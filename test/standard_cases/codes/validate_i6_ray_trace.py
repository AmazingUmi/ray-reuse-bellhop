#!/usr/bin/env python3
"""Compare Origin and F2CPP 2-D ``.ray`` outputs semantically."""

from __future__ import annotations

import argparse
from dataclasses import asdict, dataclass
import hashlib
import json
import math
from pathlib import Path
import re


CASE_ID = "ray_trace_vacuum_rigid"
PROFILE = "single"
EXPECTED_FREQUENCY_HZ = 250.0
EXPECTED_SOURCE_COUNT = 2
EXPECTED_LAUNCH_ANGLES_DEG = (-60.0, -30.0, 0.0, 30.0, 60.0)
EXPECTED_TOP_DEPTH_M = 0.0
EXPECTED_BOTTOM_DEPTH_M = 100.0
DEFAULT_ABSOLUTE_TOLERANCE_M = 1.0e-7
DEFAULT_RELATIVE_TOLERANCE = 1.0e-10


@dataclass(frozen=True)
class RayHeader:
    title: str
    frequency_hz: float
    source_counts: tuple[int, int, int]
    launch_counts: tuple[int, int]
    top_depth_m: float
    bottom_depth_m: float
    plot_type: str


@dataclass(frozen=True)
class RayRecord:
    launch_angle_deg: float
    top_bounces: int
    bottom_bounces: int
    points_m: tuple[tuple[float, float], ...]


@dataclass(frozen=True)
class RayOutput:
    header: RayHeader
    rays: tuple[RayRecord, ...]


class _LineReader:
    def __init__(self, path: Path) -> None:
        self.path = path
        self.lines = [
            line.strip()
            for line in path.read_text(encoding="utf-8").splitlines()
            if line.strip()
        ]
        self.index = 0

    def read(self, label: str) -> str:
        if self.index >= len(self.lines):
            raise ValueError(f"{self.path}: missing {label}")
        line = self.lines[self.index]
        self.index += 1
        return line

    def require_end(self) -> None:
        if self.index != len(self.lines):
            raise ValueError(
                f"{self.path}: unexpected trailing ray-file records"
            )


def _float_token(token: str, path: Path, label: str) -> float:
    try:
        value = float(token.replace("D", "E").replace("d", "e"))
    except ValueError as error:
        raise ValueError(f"{path}: invalid {label}: {token!r}") from error
    if not math.isfinite(value):
        raise ValueError(f"{path}: non-finite {label}")
    return value


def _float_line(
    reader: _LineReader, count: int, label: str
) -> tuple[float, ...]:
    tokens = reader.read(label).replace(",", " ").split()
    if len(tokens) != count:
        raise ValueError(
            f"{reader.path}: {label} requires {count} value(s)"
        )
    return tuple(
        _float_token(token, reader.path, label) for token in tokens
    )


def _integer_line(
    reader: _LineReader, count: int, label: str
) -> tuple[int, ...]:
    values = _float_line(reader, count, label)
    integers = tuple(int(value) for value in values)
    if any(float(integer) != value for integer, value in zip(integers, values)):
        raise ValueError(f"{reader.path}: {label} requires integer values")
    return integers


def _quoted_line(reader: _LineReader, label: str) -> str:
    line = reader.read(label)
    match = re.fullmatch(r"(['\"])(.*)\1", line)
    if match is None:
        raise ValueError(f"{reader.path}: {label} must be quoted")
    return match.group(2).strip()


def parse_ray(path: Path) -> RayOutput:
    if not path.is_file() or path.stat().st_size == 0:
        raise ValueError(f"missing or empty ray file: {path}")
    reader = _LineReader(path)
    title = _quoted_line(reader, "title")
    frequency_hz = _float_line(reader, 1, "frequency")[0]
    source_counts = _integer_line(reader, 3, "source counts")
    launch_counts = _integer_line(reader, 2, "launch counts")
    top_depth_m = _float_line(reader, 1, "top depth")[0]
    bottom_depth_m = _float_line(reader, 1, "bottom depth")[0]
    plot_type = _quoted_line(reader, "plot type").lower()
    if any(value <= 0 for value in (*source_counts, *launch_counts)):
        raise ValueError(f"{path}: source and launch counts must be positive")

    ray_count = math.prod(source_counts) * math.prod(launch_counts)
    rays: list[RayRecord] = []
    for ray_index in range(ray_count):
        angle = _float_line(
            reader, 1, f"ray {ray_index} launch angle"
        )[0]
        point_count, top_bounces, bottom_bounces = _integer_line(
            reader, 3, f"ray {ray_index} counts"
        )
        if point_count <= 0 or top_bounces < 0 or bottom_bounces < 0:
            raise ValueError(f"{path}: ray {ray_index} has invalid counts")
        points = tuple(
            _float_line(reader, 2, f"ray {ray_index} point {point_index}")
            for point_index in range(point_count)
        )
        rays.append(
            RayRecord(
                launch_angle_deg=angle,
                top_bounces=top_bounces,
                bottom_bounces=bottom_bounces,
                points_m=points,
            )
        )
    reader.require_end()
    return RayOutput(
        header=RayHeader(
            title=title,
            frequency_hz=frequency_hz,
            source_counts=source_counts,
            launch_counts=launch_counts,
            top_depth_m=top_depth_m,
            bottom_depth_m=bottom_depth_m,
            plot_type=plot_type,
        ),
        rays=tuple(rays),
    )


def _require_close(
    left: float,
    right: float,
    label: str,
    *,
    absolute_tolerance: float,
    relative_tolerance: float,
) -> None:
    if not math.isclose(
        left,
        right,
        abs_tol=absolute_tolerance,
        rel_tol=relative_tolerance,
    ):
        raise ValueError(f"{label}: {left!r} != {right!r}")


def validate_fixture_semantics(ray: RayOutput, label: str) -> None:
    header = ray.header
    if header.source_counts != (1, 1, EXPECTED_SOURCE_COUNT):
        raise ValueError(f"{label}: unexpected source-major dimensions")
    if header.launch_counts != (len(EXPECTED_LAUNCH_ANGLES_DEG), 1):
        raise ValueError(f"{label}: unexpected launch dimensions")
    if header.plot_type != "rz":
        raise ValueError(f"{label}: expected 2-D rz plot type")
    _require_close(
        header.frequency_hz,
        EXPECTED_FREQUENCY_HZ,
        f"{label}: frequency",
        absolute_tolerance=1.0e-12,
        relative_tolerance=1.0e-12,
    )
    _require_close(
        header.top_depth_m,
        EXPECTED_TOP_DEPTH_M,
        f"{label}: top depth",
        absolute_tolerance=1.0e-12,
        relative_tolerance=1.0e-12,
    )
    _require_close(
        header.bottom_depth_m,
        EXPECTED_BOTTOM_DEPTH_M,
        f"{label}: bottom depth",
        absolute_tolerance=1.0e-12,
        relative_tolerance=1.0e-12,
    )

    launch_count = len(EXPECTED_LAUNCH_ANGLES_DEG)
    for source_index in range(EXPECTED_SOURCE_COUNT):
        source_rays = ray.rays[
            source_index * launch_count : (source_index + 1) * launch_count
        ]
        actual_angles = tuple(item.launch_angle_deg for item in source_rays)
        for launch_index, (actual, expected) in enumerate(
            zip(actual_angles, EXPECTED_LAUNCH_ANGLES_DEG)
        ):
            _require_close(
                actual,
                expected,
                f"{label}: source {source_index} launch {launch_index}",
                absolute_tolerance=1.0e-10,
                relative_tolerance=1.0e-12,
            )
    if not any(item.top_bounces > 0 for item in ray.rays):
        raise ValueError(f"{label}: fixture did not exercise a top reflection")
    if not any(item.bottom_bounces > 0 for item in ray.rays):
        raise ValueError(f"{label}: fixture did not exercise a bottom reflection")


def compare_ray_outputs(
    reference: RayOutput,
    candidate: RayOutput,
    *,
    absolute_tolerance: float = DEFAULT_ABSOLUTE_TOLERANCE_M,
    relative_tolerance: float = DEFAULT_RELATIVE_TOLERANCE,
) -> dict[str, object]:
    if absolute_tolerance < 0.0 or relative_tolerance < 0.0:
        raise ValueError("ray comparison tolerances must be non-negative")
    if reference.header.title != candidate.header.title:
        raise ValueError("ray title mismatch")
    if reference.header.source_counts != candidate.header.source_counts:
        raise ValueError("ray source-count mismatch")
    if reference.header.launch_counts != candidate.header.launch_counts:
        raise ValueError("ray launch-count mismatch")
    if reference.header.plot_type != candidate.header.plot_type:
        raise ValueError("ray plot-type mismatch")
    for name in ("frequency_hz", "top_depth_m", "bottom_depth_m"):
        _require_close(
            getattr(reference.header, name),
            getattr(candidate.header, name),
            f"ray header {name}",
            absolute_tolerance=absolute_tolerance,
            relative_tolerance=relative_tolerance,
        )
    if len(reference.rays) != len(candidate.rays):
        raise ValueError("ray record-count mismatch")

    point_count = 0
    max_absolute_coordinate_error_m = 0.0
    for ray_index, (left, right) in enumerate(
        zip(reference.rays, candidate.rays)
    ):
        _require_close(
            left.launch_angle_deg,
            right.launch_angle_deg,
            f"ray {ray_index} launch angle",
            absolute_tolerance=absolute_tolerance,
            relative_tolerance=relative_tolerance,
        )
        if (
            left.top_bounces != right.top_bounces
            or left.bottom_bounces != right.bottom_bounces
        ):
            raise ValueError(f"ray {ray_index} bounce-count mismatch")
        if len(left.points_m) != len(right.points_m):
            raise ValueError(f"ray {ray_index} point-count mismatch")
        point_count += len(left.points_m)
        for point_index, (left_point, right_point) in enumerate(
            zip(left.points_m, right.points_m)
        ):
            for axis, left_value, right_value in zip(
                ("r", "z"), left_point, right_point
            ):
                max_absolute_coordinate_error_m = max(
                    max_absolute_coordinate_error_m,
                    abs(left_value - right_value),
                )
                _require_close(
                    left_value,
                    right_value,
                    f"ray {ray_index} point {point_index} {axis}",
                    absolute_tolerance=absolute_tolerance,
                    relative_tolerance=relative_tolerance,
                )
    return {
        "passed": True,
        "ray_count": len(reference.rays),
        "point_count": point_count,
        "top_bounce_count": sum(item.top_bounces for item in reference.rays),
        "bottom_bounce_count": sum(
            item.bottom_bounces for item in reference.rays
        ),
        "max_absolute_coordinate_error_m": (
            max_absolute_coordinate_error_m
        ),
        "absolute_tolerance_m": absolute_tolerance,
        "relative_tolerance": relative_tolerance,
    }


def semantic_sha256(ray: RayOutput) -> str:
    normalized = json.dumps(
        asdict(ray),
        sort_keys=True,
        separators=(",", ":"),
        allow_nan=False,
    ).encode("utf-8")
    return hashlib.sha256(normalized).hexdigest()


def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _load_result(
    results_root: Path, version: str, executable: Path
) -> tuple[Path, Path, Path, Path]:
    profile_root = results_root / version / CASE_ID / PROFILE
    manifest_path = profile_root / "run_manifest.json"
    if not manifest_path.is_file():
        raise ValueError(f"{manifest_path}: run manifest is absent")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if (
        manifest.get("version") != version
        or manifest.get("case_id") != CASE_ID
        or manifest.get("profile") != PROFILE
        or manifest.get("last_stage") != "test"
        or manifest.get("output_kind") != "ray"
    ):
        raise ValueError(f"{manifest_path}: run identity mismatch")
    if Path(str(manifest.get("executable"))).resolve() != executable:
        raise ValueError(f"{manifest_path}: executable identity mismatch")
    runs = manifest.get("runs")
    if not isinstance(runs, list) or len(runs) != 1:
        raise ValueError(f"{manifest_path}: expected one ray run")
    run = runs[0]
    if (
        run.get("frequency_index") != 0
        or float(run.get("frequency_hz", 0.0)) != EXPECTED_FREQUENCY_HZ
        or run.get("status") != "passed"
        or run.get("shade_file") is not None
        or not isinstance(run.get("ray_file"), str)
        or not isinstance(run.get("print_file"), str)
    ):
        raise ValueError(f"{manifest_path}: invalid ray run record")
    environment_path = profile_root / str(run["environment_file"])
    print_path = profile_root / str(run["print_file"])
    ray_path = profile_root / str(run["ray_file"])
    unexpected_shade = environment_path.with_suffix(".shd")
    if unexpected_shade.exists():
        raise ValueError(
            f"{manifest_path}: ray run unexpectedly produced {unexpected_shade}"
        )
    for artifact in (manifest_path, environment_path, print_path, ray_path):
        if not artifact.is_file() or artifact.stat().st_size == 0:
            raise ValueError(f"{manifest_path}: missing {artifact}")
        if artifact.stat().st_mtime_ns < executable.stat().st_mtime_ns:
            raise ValueError(f"{manifest_path}: {artifact.name} is stale")
    return manifest_path, environment_path, print_path, ray_path


def generation_commands(rayreuse_executable: Path | None = None) -> list[str]:
    pairs = [
        ("origin", "Bellhop_origin/bin/bellhop"),
        ("f2cpp", "Bellhop_F2CPP/build/release/bellhop_f2cpp"),
    ]
    if rayreuse_executable is not None:
        pairs.append(
            ("rayreuse", "Bellhop_RayReuse/build/release/bellhop_rayreuse")
        )
    return [
        "python3 test/standard_cases/codes/standard_cases.py test "
        f"--version {version} --case {CASE_ID} --profile {PROFILE} "
        f"--executable {executable} --results-root <results-root>"
        for version, executable in pairs
    ]


def validate(
    results_root: Path,
    origin_executable: Path,
    f2cpp_executable: Path,
    rayreuse_executable: Path | None = None,
    *,
    absolute_tolerance: float = DEFAULT_ABSOLUTE_TOLERANCE_M,
    relative_tolerance: float = DEFAULT_RELATIVE_TOLERANCE,
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
        version: _sha256(path) for version, path in executables.items()
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

    loaded = {
        version: _load_result(results_root, version, executable)
        for version, executable in executables.items()
    }
    if _sha256(loaded["origin"][1]) != _sha256(loaded["f2cpp"][1]):
        raise ValueError("Origin and F2CPP rendered ENV inputs differ")
    if "rayreuse" in executables and (
        _sha256(loaded["origin"][1]) != _sha256(loaded["rayreuse"][1])
    ):
        raise ValueError("Origin and RayReuse rendered ENV inputs differ")
    rays = {
        version: parse_ray(paths[3]) for version, paths in loaded.items()
    }
    for version, output in rays.items():
        validate_fixture_semantics(output, version)
    comparison = compare_ray_outputs(
        rays["origin"],
        rays["f2cpp"],
        absolute_tolerance=absolute_tolerance,
        relative_tolerance=relative_tolerance,
    )
    rayreuse_comparison: dict[str, object] | None = None
    origin_rayreuse_comparison: dict[str, object] | None = None
    if "rayreuse" in executables:
        rayreuse_comparison = compare_ray_outputs(
            rays["f2cpp"],
            rays["rayreuse"],
            absolute_tolerance=absolute_tolerance,
            relative_tolerance=relative_tolerance,
        )
        origin_rayreuse_comparison = compare_ray_outputs(
            rays["origin"],
            rays["rayreuse"],
            absolute_tolerance=absolute_tolerance,
            relative_tolerance=relative_tolerance,
        )
    result: dict[str, object] = {
        "schema": "bellhop.f2cpp.i6_ray_trace_validation",
        "schema_version": 1,
        "status": "passed",
        "case": {
            "id": CASE_ID,
            "frequency_hz": EXPECTED_FREQUENCY_HZ,
            "source_count": EXPECTED_SOURCE_COUNT,
            "launch_angles_deg": list(EXPECTED_LAUNCH_ANGLES_DEG),
            "ordering": "source-major then launch-angle-major",
        },
        "comparison": comparison,
        "semantic_sha256": {
            version: semantic_sha256(output)
            for version, output in rays.items()
        },
        "executables": {
            version: {"path": str(path), "sha256": executable_hashes[version]}
            for version, path in executables.items()
        },
        "provenance_guards": {
            "distinct_executable_paths": True,
            "distinct_executable_hashes": True,
            "results_not_older_than_executables": True,
            "origin_f2cpp_rendered_env_inputs_equal": True,
            "ray_files_and_print_logs_present": True,
            "no_shd_artifacts": True,
        },
        "generation": {
            "case_commands": generation_commands(rayreuse_executable),
            "validator_command": (
                "python3 test/standard_cases/codes/validate_i6_ray_trace.py "
                "--results-root <results-root> "
                "--origin-executable Bellhop_origin/bin/bellhop "
                "--f2cpp-executable "
                "Bellhop_F2CPP/build/release/bellhop_f2cpp"
            ),
        },
    }
    if rayreuse_comparison is not None:
        result["f2cpp_vs_rayreuse_comparison"] = rayreuse_comparison
        result["origin_vs_rayreuse_comparison"] = origin_rayreuse_comparison
        result["provenance_guards"][
            "origin_rayreuse_rendered_env_inputs_equal"
        ] = True
        result["generation"]["validator_command"] += (
            " --rayreuse-executable "
            "Bellhop_RayReuse/build/release/bellhop_rayreuse"
        )
    return result


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--results-root", type=Path, required=True)
    parser.add_argument("--origin-executable", type=Path, required=True)
    parser.add_argument("--f2cpp-executable", type=Path, required=True)
    parser.add_argument("--rayreuse-executable", type=Path)
    parser.add_argument(
        "--absolute-tolerance-m",
        type=float,
        default=DEFAULT_ABSOLUTE_TOLERANCE_M,
    )
    parser.add_argument(
        "--relative-tolerance",
        type=float,
        default=DEFAULT_RELATIVE_TOLERANCE,
    )
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = validate(
        args.results_root,
        args.origin_executable,
        args.f2cpp_executable,
        args.rayreuse_executable,
        absolute_tolerance=args.absolute_tolerance_m,
        relative_tolerance=args.relative_tolerance,
    )
    rendered = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.output is not None:
        args.output.write_text(rendered, encoding="utf-8")
    print(rendered, end="")


if __name__ == "__main__":
    main()
