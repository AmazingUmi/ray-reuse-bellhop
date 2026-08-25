#!/usr/bin/env python3
"""Generate and compare the representative I5 quadrilateral ray oracle."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path

from compare_f2cpp_geometry_oracle import compare
from validate_ray_oracle import validate_oracle


REPOSITORY_ROOT = Path(__file__).resolve().parents[3]
RUNNER = Path(__file__).with_name("standard_cases.py")
CASE_ID = "q_range_dependent_cross_gradient"
CASE_ROOT = REPOSITORY_ROOT / "test" / "standard_cases" / "cases" / CASE_ID
ALPHA_INDEX = 150


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def generate(
    origin_executable: Path,
    f2cpp_probe: Path,
    output_root: Path,
) -> dict[str, object]:
    oracle_dir = output_root / "fortran-oracle"
    oracle_dir.mkdir(parents=True, exist_ok=True)
    run_root = output_root / "solver-results"
    environment = os.environ.copy()
    environment.update(
        {
            "BELLHOP_ORACLE_DIR": str(oracle_dir),
            "BELLHOP_ORACLE_ALPHA": str(ALPHA_INDEX),
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
            str(origin_executable.resolve()),
            "--results-root",
            str(run_root),
        ],
        cwd=REPOSITORY_ROOT,
        env=environment,
        check=True,
    )

    validation = validate_oracle(oracle_dir)
    comparison = compare(
        oracle_dir,
        f2cpp_probe,
        "i5-quadrilateral",
        expected_producer="f2cpp",
    )
    manifest = json.loads(
        (oracle_dir / "manifest.json").read_text(encoding="utf-8")
    )
    run_manifest = next(
        run_root.glob(f"origin/{CASE_ID}/single/run_manifest.json")
    )
    rendered_environment = next(
        run_root.glob(f"origin/{CASE_ID}/single/f000_1000Hz/*.env")
    )

    return {
        "schema": "bellhop.i5_quadrilateral_geometry_oracle",
        "schema_version": 1,
        "case": CASE_ID,
        "profile": "single",
        "source_index": 1,
        "launch_angle_index": ALPHA_INDEX,
        "launch_angle_rad": float(manifest["launch_angle_rad"]),
        "probe_configuration": "i5-quadrilateral",
        "generation": {
            "command": (
                "python3 test/standard_cases/codes/generate_i5_q_oracle.py "
                "--origin-executable Bellhop_origin/bin/bellhop "
                "--f2cpp-probe "
                "Bellhop_F2CPP/build/debug/"
                "bellhop_f2cpp_geometry_oracle_probe "
                "--output-root <temporary-output-root> "
                "--report-output "
                "Bellhop_F2CPP/doc/reports/validation/"
                "i5_q_geometry_oracle_report.json"
            ),
            "oracle_source_index": 1,
            "oracle_launch_angle_index": ALPHA_INDEX,
        },
        "configuration": {
            "depths_m": [0.0, 100.0],
            "ranges_m": [0.0, 350.0, 800.0],
            "speeds_depth_major_m_per_s": [
                [1500.0, 1540.0, 1580.0],
                [1500.0, 1520.0, 1540.0],
            ],
            "source_depth_m": 50.0,
            "step_m": 1.0,
            "range_limit_m": 710.0,
            "depth_limit_m": 101.0,
            "surface": "vacuum",
            "bottom": "rigid",
        },
        "fortran_oracle": validation,
        "f2cpp_comparison": comparison,
        "passed": True,
        "sha256": {
            "case_manifest": sha256(CASE_ROOT / "case.toml"),
            "environment_template": sha256(CASE_ROOT / "origin.env.in"),
            "ssp_matrix": sha256(CASE_ROOT / "origin.ssp"),
            "rendered_environment": sha256(rendered_environment),
            "origin_executable": sha256(origin_executable),
            "f2cpp_probe": sha256(f2cpp_probe),
            "run_manifest": sha256(run_manifest),
            "fortran_ray_points": sha256(oracle_dir / "ray_points.csv"),
            "f2cpp_ray_points": comparison["probe_csv_sha256"],
        },
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--origin-executable", type=Path, required=True)
    parser.add_argument("--f2cpp-probe", type=Path, required=True)
    parser.add_argument("--output-root", type=Path, required=True)
    parser.add_argument(
        "--report-output",
        type=Path,
        help="optional report path; oracle CSVs remain under output-root",
    )
    args = parser.parse_args()

    origin_executable = args.origin_executable.resolve()
    f2cpp_probe = args.f2cpp_probe.resolve()
    for name, path in (
        ("origin executable", origin_executable),
        ("F2CPP probe", f2cpp_probe),
    ):
        if not path.is_file():
            raise SystemExit(f"missing {name}: {path}")

    output_root = args.output_root.resolve()
    output_root.mkdir(parents=True, exist_ok=True)
    report = generate(origin_executable, f2cpp_probe, output_root)
    report_path = (
        args.report_output.resolve()
        if args.report_output is not None
        else output_root / "i5_q_geometry_oracle_report.json"
    )
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(
        "I5 QUADRILATERAL GEOMETRY ORACLE PASSED: "
        f"{report['f2cpp_comparison']['point_count']} points"
    )
    print(report_path)


if __name__ == "__main__":
    main()
