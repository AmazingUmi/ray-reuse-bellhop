#!/usr/bin/env python3
"""Generate and summarize the origin-only I3 curvilinear boundary oracle."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import os
import subprocess
import sys
from pathlib import Path

from validate_ray_oracle import validate_oracle


REPOSITORY_ROOT = Path(__file__).resolve().parents[3]
RUNNER = Path(__file__).with_name("standard_cases.py")
CASE_ID = "i3_curvilinear_oracle"
CASE_ROOT = REPOSITORY_ROOT / "test" / "standard_cases" / "cases" / CASE_ID
ANCHOR_FIELDS = (
    "event_index",
    "boundary",
    "boundary_condition",
    "boundary_segment_index",
    "pre_r_m",
    "pre_z_m",
    "post_r_m",
    "post_z_m",
    "tangent_r",
    "tangent_z",
    "normal_r",
    "normal_z",
    "boundary_curvature_per_m",
    "incident_t_r_s_per_m",
    "incident_t_z_s_per_m",
    "reflected_t_r_s_per_m",
    "reflected_t_z_s_per_m",
    "incident_p1",
    "incident_p2",
    "incident_q1",
    "incident_q2",
    "reflected_p1",
    "reflected_p2",
    "reflected_q1",
    "reflected_q2",
)
FULL_LAUNCH_FAN_SIZE = 459


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_boundary(path: Path) -> list[tuple[float, float]]:
    lines = [line.strip() for line in path.read_text(encoding="utf-8").splitlines()]
    if lines[0] != "C":
        raise ValueError(f"{path}: expected single-character C short format")
    count = int(lines[1])
    points = [tuple(map(float, line.split())) for line in lines[2:]]
    if len(points) != count:
        raise ValueError(f"{path}: point-count mismatch")
    return [(1000.0 * range_km, depth_m) for range_km, depth_m in points]


def segment_fraction(
    event: dict[str, str], points: list[tuple[float, float]]
) -> float:
    # Fortran adds an extrapolation node at index 1, so physical segment i
    # joins input points i-1 and i in Python's zero-based list.
    segment = int(event["boundary_segment_index"])
    if segment < 2 or segment > len(points):
        raise ValueError(f"event uses extrapolated segment {segment}")
    start = points[segment - 2]
    end = points[segment - 1]
    delta = (end[0] - start[0], end[1] - start[1])
    length_squared = delta[0] ** 2 + delta[1] ** 2
    position = (float(event["pre_r_m"]), float(event["pre_z_m"]))
    return (
        (position[0] - start[0]) * delta[0]
        + (position[1] - start[1]) * delta[1]
    ) / length_squared


def generate(
    executable: Path, output_root: Path, alpha_index: int,
    allow_partial_boundary_coverage: bool = False,
) -> dict[str, object]:
    oracle_dir = output_root / "fortran-oracle"
    oracle_dir.mkdir(parents=True, exist_ok=True)
    run_root = output_root / "solver-results"
    environment = os.environ.copy()
    environment.update(
        {
            "BELLHOP_ORACLE_DIR": str(oracle_dir),
            "BELLHOP_ORACLE_ALPHA": str(alpha_index),
            "BELLHOP_ORACLE_SOURCE": "1",
        }
    )
    subprocess.run(
        [
            sys.executable,
            str(RUNNER),
            "test",
            "--version",
            "origin",
            "--case",
            CASE_ID,
            "--profile",
            "single",
            "--executable",
            str(executable.resolve()),
            "--results-root",
            str(run_root),
        ],
        cwd=REPOSITORY_ROOT,
        env=environment,
        check=True,
    )

    validation = validate_oracle(
        oracle_dir, reflection_frame_mode="curvilinear_interpolated"
    )
    manifest = json.loads((oracle_dir / "manifest.json").read_text(encoding="utf-8"))
    with (oracle_dir / "reflection_events.csv").open(
        newline="", encoding="utf-8"
    ) as stream:
        events = list(csv.DictReader(stream))
    boundary_points = {
        "sea_surface": read_boundary(CASE_ROOT / "origin.ati"),
        "seabed": read_boundary(CASE_ROOT / "origin.bty"),
    }
    seen_boundaries = {event["boundary"] for event in events}
    if (not allow_partial_boundary_coverage and
            seen_boundaries != set(boundary_points)):
        raise ValueError(
            f"oracle must hit both boundaries, got {sorted(seen_boundaries)}"
        )

    anchors: list[dict[str, object]] = []
    for event in events:
        fraction = segment_fraction(event, boundary_points[event["boundary"]])
        if not 0.0 < fraction < 1.0:
            raise ValueError(
                f"event {event['event_index']} is not inside its physical "
                f"segment: {fraction}"
            )
        curvature = float(event["boundary_curvature_per_m"])
        if not math.isfinite(curvature) or curvature == 0.0:
            raise ValueError(
                f"event {event['event_index']} has zero/non-finite curvature"
            )
        for coordinate in ("r_m", "z_m"):
            pre = float(event[f"pre_{coordinate}"])
            post = float(event[f"post_{coordinate}"])
            if pre != post:
                raise ValueError(
                    f"event {event['event_index']} moved the reflection point"
                )
        anchor: dict[str, object] = {
            field: (
                event[field]
                if field in {"boundary", "boundary_condition"}
                else int(event[field])
                if field in {"event_index", "boundary_segment_index"}
                else float(event[field])
            )
            for field in ANCHOR_FIELDS
        }
        anchor["physical_segment_fraction"] = fraction
        anchors.append(anchor)

    run_manifest = next(run_root.glob(f"origin/{CASE_ID}/single/run_manifest.json"))
    rendered_env = next(run_root.glob(f"origin/{CASE_ID}/single/f000_100Hz/*.env"))
    return {
        "schema": "bellhop.fortran.i3_curvilinear_oracle_summary",
        "schema_version": 1,
        "case": CASE_ID,
        "profile": "single",
        "source_index": 1,
        "launch_angle_index": alpha_index,
        "launch_angle_rad": float(manifest["launch_angle_rad"]),
        "validation": validation,
        "coverage": {
            "boundaries": sorted(seen_boundaries),
            "all_events_on_physical_segment_interior": True,
            "all_events_have_nonzero_curvature": True,
            "anchors_include_interpolated_frame_slowness_and_pq": True,
        },
        "sha256": {
            "case_manifest": sha256(CASE_ROOT / "case.toml"),
            "environment_template": sha256(CASE_ROOT / "origin.env.in"),
            "rendered_environment": sha256(rendered_env),
            "altimetry": sha256(CASE_ROOT / "origin.ati"),
            "bathymetry": sha256(CASE_ROOT / "origin.bty"),
            "fortran_executable": sha256(executable),
            "run_manifest": sha256(run_manifest),
            "ray_points": sha256(oracle_dir / "ray_points.csv"),
            "reflection_events": sha256(oracle_dir / "reflection_events.csv"),
        },
        "reflection_anchors": anchors,
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--origin-executable", type=Path, required=True)
    parser.add_argument("--output-root", type=Path, required=True)
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument("--alpha-index", type=int)
    selection.add_argument(
        "--all-alpha-indices",
        action="store_true",
        help="generate a001..a459 oracle directories for the full launch fan",
    )
    parser.add_argument(
        "--allow-partial-boundary-coverage",
        action="store_true",
        help="allow matrix rays that hit only one boundary or no boundary",
    )
    args = parser.parse_args()
    alpha_indices = (
        range(1, FULL_LAUNCH_FAN_SIZE + 1)
        if args.all_alpha_indices
        else (args.alpha_index if args.alpha_index is not None else 150,)
    )
    for alpha_index in alpha_indices:
        output_root = (
            args.output_root / f"a{alpha_index:03d}"
            if args.all_alpha_indices
            else args.output_root
        )
        report = generate(
            args.origin_executable,
            output_root,
            alpha_index,
            args.allow_partial_boundary_coverage or args.all_alpha_indices,
        )
        report_path = output_root / "i3_curvilinear_oracle_report.json"
        report_path.write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        print(report_path)


if __name__ == "__main__":
    main()
