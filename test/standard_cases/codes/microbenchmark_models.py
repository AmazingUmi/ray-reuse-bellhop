#!/usr/bin/env python3
"""Run comparable single-thread stage microbenchmarks for the three models."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import re
import shutil
import statistics
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Sequence


REPOSITORY_ROOT = Path(__file__).resolve().parents[3]
RUNNER = Path(__file__).with_name("standard_cases.py")
DEFAULT_CASES = ("constant_speed_direct", "munk_cerveny_cc")

FORTRAN_FIELDS = {
    "trace_seconds": "Stage Trace seconds",
    "influence_seconds": "Stage Influence seconds",
    "scale_seconds": "Stage Scale seconds",
    "output_seconds": "Stage Output seconds",
}
CXX_FIELDS = {
    "trace_seconds": "Trace seconds",
    "project_seconds": "Project seconds",
    "influence_seconds": "Influence seconds",
    "scale_seconds": "Scale seconds",
    "output_seconds": "SHD seconds",
}


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def parse_stage_timings(path: Path, model: str) -> dict[str, float]:
    fields = FORTRAN_FIELDS if model == "origin" else CXX_FIELDS
    values: dict[str, float] = {}
    text = path.read_text(encoding="utf-8", errors="replace")
    for field, label in fields.items():
        matches = re.findall(
            rf"^\s*{re.escape(label)}\s*=\s*"
            r"([+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[Ee][+-]?\d+)?)",
            text,
            flags=re.MULTILINE,
        )
        if len(matches) != 1:
            raise ValueError(
                f"{path}: expected one {label!r}, found {len(matches)}"
            )
        value = float(matches[0])
        if value < 0.0:
            raise ValueError(f"{path}: {label} must be non-negative")
        values[field] = value

    project = values.get("project_seconds", 0.0)
    values["formula_core_seconds"] = (
        values["trace_seconds"] + project + values["influence_seconds"]
    )
    values["reported_stage_seconds"] = sum(
        value
        for field, value in values.items()
        if field.endswith("_seconds")
        and field not in {"formula_core_seconds", "reported_stage_seconds"}
    )
    return values


def summarize_samples(
    samples: Sequence[dict[str, float]],
) -> dict[str, object]:
    if not samples:
        raise ValueError("timing samples must not be empty")
    fields = tuple(samples[0])
    if any(tuple(sample) != fields for sample in samples):
        raise ValueError("timing samples must have identical fields")
    return {
        "sample_count": len(samples),
        "median": {
            field: statistics.median(sample[field] for sample in samples)
            for field in fields
        },
        "samples": list(samples),
    }


def find_print_file(run_root: Path) -> Path:
    matches = sorted(
        path for path in run_root.rglob("*.prt") if not path.name.startswith("._")
    )
    if len(matches) != 1:
        raise ValueError(
            f"{run_root}: expected exactly one PRT, found {len(matches)}"
        )
    return matches[0]


def run_sample(
    *, model: str, case_id: str, executable: Path, run_root: Path
) -> dict[str, float]:
    environment = os.environ.copy()
    environment.update(
        {
            "BELLHOP_PROFILE_STAGES": "1",
            "OMP_NUM_THREADS": "1",
            "OPENBLAS_NUM_THREADS": "1",
            "VECLIB_MAXIMUM_THREADS": "1",
        }
    )
    subprocess.run(
        [
            sys.executable,
            str(RUNNER),
            "test",
            "--version",
            model,
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
    return parse_stage_timings(find_print_file(run_root), model)


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


def build_report(
    *,
    cases: Sequence[str],
    warmups: int,
    repetitions: int,
    executables: dict[str, Path],
    work_root: Path,
) -> dict[str, object]:
    results: list[dict[str, object]] = []
    for case_id in cases:
        model_names = tuple(executables)
        for warmup_index in range(warmups):
            for model in model_names:
                run_sample(
                    model=model,
                    case_id=case_id,
                    executable=executables[model],
                    run_root=(
                        work_root
                        / case_id
                        / model
                        / f"warmup-{warmup_index + 1}"
                    ),
                )

        samples_by_model: dict[str, list[dict[str, float]]] = {
            model: [] for model in model_names
        }
        schedule: list[list[str]] = []
        for repeat_index in range(repetitions):
            offset = repeat_index % len(model_names)
            order = model_names[offset:] + model_names[:offset]
            schedule.append(list(order))
            for model in order:
                samples_by_model[model].append(
                    run_sample(
                        model=model,
                        case_id=case_id,
                        executable=executables[model],
                        run_root=(
                            work_root
                            / case_id
                            / model
                            / f"repeat-{repeat_index + 1}"
                        ),
                    )
                )

        model_results: dict[str, dict[str, object]] = {}
        for model in model_names:
            model_results[model] = summarize_samples(samples_by_model[model])

        origin_core = float(
            model_results["origin"]["median"]["formula_core_seconds"]
        )
        ratios = {
            model: float(result["median"]["formula_core_seconds"])
            / origin_core
            for model, result in model_results.items()
            if model != "origin"
        }
        results.append(
            {
                "case_id": case_id,
                "measurement_schedule": schedule,
                "models": model_results,
                "formula_core_ratio_to_origin": ratios,
            }
        )

    return {
        "schema": "bellhop.single_thread_formula_microbenchmark",
        "schema_version": 1,
        "git": git_identity(),
        "platform": platform.platform(),
        "thread_contract": {
            "solver_threads": 1,
            "environment": {
                "OMP_NUM_THREADS": "1",
                "OPENBLAS_NUM_THREADS": "1",
                "VECLIB_MAXIMUM_THREADS": "1",
            },
        },
        "stage_mapping": {
            "origin_formula_core": "Trace + Influence",
            "cpp_formula_core": "Trace + Project + Influence",
            "note": (
                "Fortran keeps frequency projection inside trace/influence "
                "state ownership; C++ reports Project separately. Cross-model "
                "ratios therefore use the aggregate formula core. Raw Trace "
                "and Influence timings remain diagnostic only."
            ),
        },
        "executables": {
            model: {"path": str(path), "sha256": sha256_file(path)}
            for model, path in executables.items()
        },
        "warmups": warmups,
        "repetitions": repetitions,
        "results": results,
    }


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--origin-executable", type=Path, required=True)
    parser.add_argument("--f2cpp-executable", type=Path, required=True)
    parser.add_argument("--rayreuse-executable", type=Path, required=True)
    parser.add_argument("--case", action="append", dest="cases")
    parser.add_argument("--warmups", type=int, default=1)
    parser.add_argument("--repetitions", type=int, default=3)
    parser.add_argument("--work-root", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    if args.warmups < 0:
        raise SystemExit("--warmups must be non-negative")
    if args.repetitions < 1:
        raise SystemExit("--repetitions must be positive")
    executables = {
        "origin": args.origin_executable.resolve(),
        "f2cpp": args.f2cpp_executable.resolve(),
        "rayreuse": args.rayreuse_executable.resolve(),
    }
    missing = [str(path) for path in executables.values() if not path.is_file()]
    if missing:
        raise SystemExit(f"missing executable(s): {', '.join(missing)}")

    temporary_root: Path | None = None
    if args.work_root is None:
        parent = REPOSITORY_ROOT / "test/standard_cases/results/microbenchmark"
        parent.mkdir(parents=True, exist_ok=True)
        temporary_root = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
        work_root = temporary_root
    else:
        work_root = args.work_root.resolve()
        work_root.mkdir(parents=True, exist_ok=True)

    try:
        report = build_report(
            cases=tuple(args.cases or DEFAULT_CASES),
            warmups=args.warmups,
            repetitions=args.repetitions,
            executables=executables,
            work_root=work_root,
        )
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        print(f"Wrote single-thread microbenchmark: {args.output}")
        return 0
    finally:
        if temporary_root is not None:
            shutil.rmtree(temporary_root, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
