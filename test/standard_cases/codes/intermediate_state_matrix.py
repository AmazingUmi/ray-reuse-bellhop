#!/usr/bin/env python3
"""Generate Fortran ray oracles and compare F2CPP/RayReuse geometry states."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Sequence

from compare_f2cpp_geometry_oracle import compare
from validate_ray_oracle import validate_oracle


REPOSITORY_ROOT = Path(__file__).resolve().parents[3]
RUNNER = Path(__file__).with_name("standard_cases.py")
CASE_CONFIGURATIONS = {
    "constant_speed_direct": "direct",
    "constant_speed_vacuum_rigid": "vacuum-rigid",
    "munk_cerveny_cc": "munk",
    "munk_n2": "munk-n2",
    "munk_pchip": "munk-pchip",
}


def sha256_file(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def git_identity() -> dict[str, object]:
    revision = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=REPOSITORY_ROOT,
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()
    dirty = bool(
        subprocess.run(
            ["git", "status", "--porcelain"],
            cwd=REPOSITORY_ROOT,
            check=True,
            capture_output=True,
            text=True,
        ).stdout.strip()
    )
    return {"revision": revision, "dirty": dirty}


def generate_fortran_oracle(
    *, case_id: str, executable: Path, oracle_dir: Path, run_root: Path
) -> dict[str, object]:
    oracle_dir.mkdir(parents=True)
    environment = os.environ.copy()
    environment.update(
        {
            "BELLHOP_ORACLE_DIR": str(oracle_dir),
            "BELLHOP_ORACLE_ALPHA": "150",
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
            case_id,
            "--profile",
            "single",
            "--executable",
            str(executable),
            "--results-root",
            str(run_root),
        ],
        cwd=REPOSITORY_ROOT,
        env=environment,
        check=True,
    )
    return validate_oracle(oracle_dir)


def build_report(
    *,
    origin_executable: Path,
    f2cpp_probe: Path,
    rayreuse_probe: Path,
    cases: Sequence[str],
    work_root: Path,
) -> dict[str, object]:
    results: list[dict[str, object]] = []
    for case_id in cases:
        configuration = CASE_CONFIGURATIONS[case_id]
        case_root = work_root / case_id
        oracle_dir = case_root / "fortran-oracle"
        oracle_summary = generate_fortran_oracle(
            case_id=case_id,
            executable=origin_executable,
            oracle_dir=oracle_dir,
            run_root=case_root / "solver-results",
        )
        comparisons = {
            "f2cpp": compare(
                oracle_dir,
                f2cpp_probe,
                configuration,
                expected_producer="f2cpp",
            ),
            "rayreuse": compare(
                oracle_dir,
                rayreuse_probe,
                configuration,
                expected_producer="rayreuse",
            ),
        }
        cpp_identical = (
            comparisons["f2cpp"]["probe_csv_sha256"]
            == comparisons["rayreuse"]["probe_csv_sha256"]
        )
        if not cpp_identical:
            raise ValueError(f"{case_id}: F2CPP/RayReuse probe CSV differs")
        results.append(
            {
                "case_id": case_id,
                "probe_configuration": configuration,
                "fortran_oracle": oracle_summary,
                "comparisons": comparisons,
                "cpp_probe_byte_identical": cpp_identical,
            }
        )

    return {
        "schema": "bellhop.intermediate_geometry_matrix",
        "schema_version": 1,
        "git": git_identity(),
        "platform": platform.platform(),
        "executables": {
            "origin": {
                "path": str(origin_executable),
                "sha256": sha256_file(origin_executable),
            },
            "f2cpp_probe": {
                "path": str(f2cpp_probe),
                "sha256": sha256_file(f2cpp_probe),
            },
            "rayreuse_probe": {
                "path": str(rayreuse_probe),
                "sha256": sha256_file(rayreuse_probe),
            },
        },
        "selector": {"source_index": 1, "launch_angle_index": 150},
        "scope": (
            "frequency-independent ray geometry, dynamic p/q, real travel "
            "time, step quadrature subset, bounce sequence"
        ),
        "passed": True,
        "results": results,
    }


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--origin-executable", type=Path, required=True)
    parser.add_argument("--f2cpp-probe", type=Path, required=True)
    parser.add_argument("--rayreuse-probe", type=Path, required=True)
    parser.add_argument(
        "--case",
        action="append",
        choices=tuple(CASE_CONFIGURATIONS),
        dest="cases",
    )
    parser.add_argument("--work-root", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    paths = {
        "origin": args.origin_executable.resolve(),
        "f2cpp": args.f2cpp_probe.resolve(),
        "rayreuse": args.rayreuse_probe.resolve(),
    }
    missing = [str(path) for path in paths.values() if not path.is_file()]
    if missing:
        raise SystemExit(f"missing executable(s): {', '.join(missing)}")

    temporary_root: Path | None = None
    if args.work_root is None:
        parent = REPOSITORY_ROOT / "test/standard_cases/results/intermediate"
        parent.mkdir(parents=True, exist_ok=True)
        temporary_root = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
        work_root = temporary_root
    else:
        work_root = args.work_root.resolve()
        work_root.mkdir(parents=True, exist_ok=True)

    try:
        report = build_report(
            origin_executable=paths["origin"],
            f2cpp_probe=paths["f2cpp"],
            rayreuse_probe=paths["rayreuse"],
            cases=tuple(args.cases or CASE_CONFIGURATIONS),
            work_root=work_root,
        )
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        print(
            "INTERMEDIATE GEOMETRY MATRIX PASSED: "
            f"{len(report['results'])} case(s)"
        )
        print(f"Wrote intermediate-state report: {args.output}")
        return 0
    finally:
        if temporary_root is not None:
            shutil.rmtree(temporary_root, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
