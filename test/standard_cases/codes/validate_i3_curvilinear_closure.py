#!/usr/bin/env python3
"""Validate the complete F2CPP I3 curvilinear geometry and field closure."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from compare_f2cpp_geometry_oracle import compare
from compare_fields import STANDARD_CASES_ROOT, compare_files


EXPECTED_LAUNCH_COUNT = 459


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def aggregate_oracle_sha256(directories: list[Path]) -> str:
    digest = hashlib.sha256()
    paths: list[Path] = []
    for directory in directories:
        oracle = directory / "fortran-oracle"
        paths.extend(
            oracle / name
            for name in (
                "manifest.json",
                "ray_points.csv",
                "reflection_events.csv",
            )
        )
    for path in sorted(paths):
        digest.update(path.read_bytes())
    return digest.hexdigest()


def validate(
    oracle_root: Path,
    f2cpp_probe: Path,
    origin_shd: Path,
    f2cpp_shd: Path,
    rayreuse_probe: Path | None = None,
    rayreuse_shd: Path | None = None,
) -> dict[str, object]:
    directories = sorted(
        (path for path in oracle_root.glob("a[0-9][0-9][0-9]")
         if path.is_dir()),
        key=lambda path: int(path.name[1:]),
    )
    expected_names = [
        f"a{index:03d}" for index in range(1, EXPECTED_LAUNCH_COUNT + 1)
    ]
    if [path.name for path in directories] != expected_names:
        raise ValueError(
            f"expected contiguous a001..a{EXPECTED_LAUNCH_COUNT:03d} "
            "oracle directories"
        )

    totals = {"points": 0, "integrated_steps": 0, "reflection_events": 0}
    termination_counts: dict[str, int] = {}
    worst: dict[str, object] = {
        "scaled_error": 0.0,
        "absolute_error": 0.0,
        "field": "",
        "point_index": 0,
        "launch_angle_index": 0,
    }
    rayreuse_totals = {"points": 0, "integrated_steps": 0, "reflection_events": 0}
    rayreuse_termination_counts: dict[str, int] = {}
    rayreuse_worst: dict[str, object] = {
        "scaled_error": 0.0,
        "absolute_error": 0.0,
        "field": "",
        "point_index": 0,
        "launch_angle_index": 0,
    }
    for launch_index, directory in enumerate(directories, start=1):
        oracle = directory / "fortran-oracle"
        manifest = json.loads(
            (oracle / "manifest.json").read_text(encoding="utf-8")
        )
        if int(manifest["launch_angle_index"]) != launch_index:
            raise ValueError(
                f"{directory.name}: launch-angle index does not match path"
            )
        result = compare(oracle, f2cpp_probe, "i3-curvilinear")
        totals["points"] += int(manifest["point_count"])
        totals["integrated_steps"] += int(manifest["integrated_step_count"])
        totals["reflection_events"] += int(manifest["reflection_event_count"])
        reason = str(manifest["termination_reason"])
        termination_counts[reason] = termination_counts.get(reason, 0) + 1
        candidate = result["worst_comparison"]
        if float(candidate["scaled_error"]) > float(worst["scaled_error"]):
            worst = dict(candidate)
            worst["launch_angle_index"] = launch_index

        if rayreuse_probe is not None:
            rr_result = compare(oracle, rayreuse_probe, "i3-curvilinear")
            rayreuse_totals["points"] += int(manifest["point_count"])
            rayreuse_totals["integrated_steps"] += int(manifest["integrated_step_count"])
            rayreuse_totals["reflection_events"] += int(manifest["reflection_event_count"])
            rr_reason = str(manifest["termination_reason"])
            rayreuse_termination_counts[rr_reason] = rayreuse_termination_counts.get(rr_reason, 0) + 1
            rr_candidate = rr_result["worst_comparison"]
            if float(rr_candidate["scaled_error"]) > float(rayreuse_worst["scaled_error"]):
                rayreuse_worst = dict(rr_candidate)
                rayreuse_worst["launch_angle_index"] = launch_index

    tolerance_path = STANDARD_CASES_ROOT / "codes" / "tolerances.toml"
    field_passed, field_metrics = compare_files(
        origin_shd,
        f2cpp_shd,
        0,
        0,
        tolerance_path,
    )
    if not field_passed:
        raise ValueError(f"final-field comparison failed: {field_metrics}")

    origin_rr_metrics = None
    f2cpp_rr_metrics = None
    if rayreuse_shd is not None:
        origin_rr_passed, origin_rr_metrics = compare_files(
            origin_shd,
            rayreuse_shd,
            0,
            0,
            tolerance_path,
        )
        if not origin_rr_passed:
            raise ValueError(f"origin-rayreuse field comparison failed: {origin_rr_metrics}")

        f2cpp_rr_passed, f2cpp_rr_metrics = compare_files(
            f2cpp_shd,
            rayreuse_shd,
            0,
            0,
            tolerance_path,
        )
        if not f2cpp_rr_passed:
            raise ValueError(f"f2cpp-rayreuse field comparison failed: {f2cpp_rr_metrics}")

    sha256_map = {
        "fortran_oracle_aggregate": aggregate_oracle_sha256(directories),
        "f2cpp_probe": sha256(f2cpp_probe),
        "origin_field": sha256(origin_shd),
        "f2cpp_field": sha256(f2cpp_shd),
    }

    result_payload: dict[str, object] = {
        "schema": "bellhop.f2cpp.i3_curvilinear_closure_validation",
        "schema_version": 2 if (rayreuse_probe is not None or rayreuse_shd is not None) else 1,
        "status": "passed",
        "launch_angle_count": len(directories),
        "geometry": {
            "passed": len(directories),
            "failed": 0,
            "totals": totals,
            "termination_reason_counts": termination_counts,
            "worst_comparison": worst,
        },
        "field": {"passed": True, **field_metrics},
        "origin_f2cpp_field": {"passed": True, **field_metrics},
    }

    if rayreuse_probe is not None:
        result_payload["rayreuse_geometry"] = {
            "passed": len(directories),
            "failed": 0,
            "totals": rayreuse_totals,
            "termination_reason_counts": rayreuse_termination_counts,
            "worst_comparison": rayreuse_worst,
        }
        sha256_map["rayreuse_probe"] = sha256(rayreuse_probe)

    if rayreuse_shd is not None:
        result_payload["origin_rayreuse_field"] = {"passed": True, **origin_rr_metrics}
        result_payload["f2cpp_rayreuse_field"] = {"passed": True, **f2cpp_rr_metrics}
        sha256_map["rayreuse_field"] = sha256(rayreuse_shd)

    result_payload["sha256"] = sha256_map
    return result_payload


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--oracle-root", type=Path, required=True)
    parser.add_argument("--f2cpp-probe", type=Path, required=True)
    parser.add_argument("--origin-shd", type=Path, required=True)
    parser.add_argument("--f2cpp-shd", type=Path, required=True)
    parser.add_argument("--rayreuse-probe", type=Path)
    parser.add_argument("--rayreuse-shd", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = validate(
        args.oracle_root,
        args.f2cpp_probe,
        args.origin_shd,
        args.f2cpp_shd,
        rayreuse_probe=args.rayreuse_probe,
        rayreuse_shd=args.rayreuse_shd,
    )
    rendered = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.output is not None:
        args.output.write_text(rendered, encoding="utf-8")
    print(rendered, end="")


if __name__ == "__main__":
    main()
