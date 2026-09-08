#!/usr/bin/env python3
"""Reproducible benchmark runner for Bellhop Broadband execution modes."""

from __future__ import annotations

import argparse
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import resource
import shutil
import statistics
import subprocess
import sys
import tempfile
import time
from typing import Any, Iterable, Sequence


CODES_ROOT = Path(__file__).resolve().parent
STANDARD_CASES_ROOT = CODES_ROOT.parent
PROJECT_ROOT = STANDARD_CASES_ROOT.parents[1]
DEFAULT_EXECUTABLE = (
    PROJECT_ROOT
    / "Bellhop_Broadband"
    / "build"
    / "release"
    / "bellhop_broadband"
)
DEFAULT_OUTPUT = (
    PROJECT_ROOT
    / "Bellhop_Broadband"
    / "build"
    / "benchmarks"
    / "broadband_benchmark.json"
)
EXECUTION_MODES = ("nonreuse", "reuse")
REUSE_MODES = ("serial", "frequency", "range")
# v4 separates execution/reuse modes and unifies the requested worker axis.
# Historical v2/v3 reports are preserved and are not read by this writer.
SCHEMA_VERSION = 4

sys.path.insert(0, str(CODES_ROOT))

import numpy as np

from case_model import CaseDefinition, discover_cases
from standard_cases import (
    format_frequency_csv,
    stage_companion_files,
    validate_broadband_output,
)


@dataclass(frozen=True)
class BenchmarkConfiguration:
    execution_mode: str
    reuse_mode: str | None = None
    reuse_workers: int | None = None
    output_queue_capacity: int | None = None
    memory_budget_mib: int | None = None

    def __post_init__(self) -> None:
        if self.execution_mode not in EXECUTION_MODES:
            raise ValueError(f"unknown execution mode: {self.execution_mode}")
        if self.execution_mode == "nonreuse":
            if self.reuse_mode is not None or self.reuse_workers is not None:
                raise ValueError("nonreuse does not accept reuse mode/workers")
        elif self.reuse_mode not in REUSE_MODES:
            raise ValueError(f"unknown reuse mode: {self.reuse_mode}")
        elif self.reuse_mode == "serial":
            if self.reuse_workers is not None:
                raise ValueError("Serial Reuse does not accept reuse workers")
        elif self.reuse_workers is None or self.reuse_workers <= 0:
            raise ValueError("Frequency/Range Reuse require positive reuse workers")

    @property
    def identifier(self) -> str:
        if self.execution_mode == "nonreuse":
            return "nonreuse"
        if self.reuse_mode == "serial":
            return "reuse-serial"
        if self.reuse_mode == "range":
            return f"reuse-range-w{self.reuse_workers}"
        memory = (
            "unlimited"
            if self.memory_budget_mib is None
            else f"{self.memory_budget_mib}MiB"
        )
        return (
            f"reuse-frequency-w{self.reuse_workers}"
            f"-q{self.output_queue_capacity}-m{memory}"
        )


def positive_integer(value: str) -> int:
    try:
        parsed = int(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError(
            f"expected a positive integer, got {value!r}"
        ) from error
    if parsed <= 0:
        raise argparse.ArgumentTypeError(
            f"expected a positive integer, got {value!r}"
        )
    return parsed


def nonnegative_integer(value: str) -> int:
    try:
        parsed = int(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError(
            f"expected a non-negative integer, got {value!r}"
        ) from error
    if parsed < 0:
        raise argparse.ArgumentTypeError(
            f"expected a non-negative integer, got {value!r}"
        )
    return parsed


def _parse_modes(value: str, allowed: Sequence[str], layer: str) -> tuple[str, ...]:
    modes = tuple(part.strip() for part in value.split(",") if part.strip())
    if not modes:
        raise argparse.ArgumentTypeError(f"at least one {layer} mode is required")
    unknown = tuple(mode for mode in modes if mode not in allowed)
    if unknown:
        raise argparse.ArgumentTypeError(
            f"unknown {layer} mode(s): {', '.join(unknown)}"
        )
    if len(set(modes)) != len(modes):
        raise argparse.ArgumentTypeError(f"{layer} modes must be unique")
    return modes


def parse_execution_modes(value: str) -> tuple[str, ...]:
    return _parse_modes(value, EXECUTION_MODES, "execution")


def parse_reuse_modes(value: str) -> tuple[str, ...]:
    return _parse_modes(value, REUSE_MODES, "reuse")


def parse_positive_integer_csv(value: str) -> tuple[int, ...]:
    parts = tuple(part.strip() for part in value.split(","))
    if not parts or any(not part for part in parts):
        raise argparse.ArgumentTypeError(
            "expected a comma-separated list of positive integers"
        )
    values = tuple(positive_integer(part) for part in parts)
    if len(set(values)) != len(values):
        raise argparse.ArgumentTypeError(
            "reuse worker counts must be unique"
        )
    return values


def parse_frequency_csv(value: str) -> tuple[float, ...]:
    parts = tuple(part.strip() for part in value.split(","))
    if not parts or any(not part for part in parts):
        raise argparse.ArgumentTypeError(
            "expected a comma-separated list of frequencies in Hz"
        )
    try:
        frequencies = tuple(float(part) for part in parts)
    except ValueError as error:
        raise argparse.ArgumentTypeError(
            f"frequency values must be numeric: {value!r}"
        ) from error
    if len(frequencies) < 2:
        raise argparse.ArgumentTypeError(
            "a broadband frequency override needs at least two frequencies"
        )
    if any(
        not math.isfinite(frequency) or frequency <= 0.0
        for frequency in frequencies
    ):
        raise argparse.ArgumentTypeError(
            "frequency values must be finite and positive"
        )
    if any(
        current >= following
        for current, following in zip(frequencies, frequencies[1:])
    ):
        raise argparse.ArgumentTypeError(
            "frequency values must be strictly increasing"
        )
    return frequencies


def expand_configurations(
    execution_modes: Sequence[str],
    reuse_modes: Sequence[str],
    reuse_workers: Sequence[int] | None,
    output_queue_capacity: int,
    memory_budget_mib: int | None,
) -> tuple[BenchmarkConfiguration, ...]:
    if not execution_modes:
        raise ValueError("at least one execution mode is required")
    if output_queue_capacity not in (1, 2):
        raise ValueError("output queue capacity must be 1 or 2")
    if memory_budget_mib is not None and memory_budget_mib <= 0:
        raise ValueError("memory budget must be positive")
    for modes, allowed, layer in (
        (execution_modes, EXECUTION_MODES, "execution"),
        (reuse_modes, REUSE_MODES, "reuse"),
    ):
        if len(set(modes)) != len(modes):
            raise ValueError(f"duplicate {layer} mode")
        if any(mode not in allowed for mode in modes):
            raise ValueError(f"unknown {layer} mode")
    if "reuse" in execution_modes and not reuse_modes:
        raise ValueError("at least one reuse mode is required")
    if reuse_workers is not None:
        if not reuse_workers or any(count <= 0 for count in reuse_workers):
            raise ValueError("reuse worker counts must be positive")
        if len(set(reuse_workers)) != len(reuse_workers):
            raise ValueError("reuse worker counts must be unique")
        if "reuse" not in execution_modes or not any(
            mode in ("frequency", "range") for mode in reuse_modes
        ):
            raise ValueError("reuse workers require Frequency or Range Reuse")

    configurations: list[BenchmarkConfiguration] = []
    for execution_mode in execution_modes:
        if execution_mode == "nonreuse":
            configurations.append(BenchmarkConfiguration("nonreuse"))
            continue
        for reuse_mode in reuse_modes:
            if reuse_mode == "serial":
                configurations.append(BenchmarkConfiguration("reuse", "serial"))
                continue
            # Preserve the existing route defaults when no worker CSV is given.
            workers = reuse_workers if reuse_workers is not None else (
                (8,) if reuse_mode == "frequency" else (1,)
            )
            for count in workers:
                configurations.append(BenchmarkConfiguration(
                    execution_mode="reuse",
                    reuse_mode=reuse_mode,
                    reuse_workers=count,
                    output_queue_capacity=(
                        output_queue_capacity if reuse_mode == "frequency" else None
                    ),
                    memory_budget_mib=(
                        memory_budget_mib if reuse_mode == "frequency" else None
                    ),
                ))
    return tuple(configurations)


def normalize_max_rss_kib(
    raw_max_rss: int | float,
    platform_name: str | None = None,
) -> int:
    """Normalize resource.ru_maxrss to KiB.

    Darwin reports bytes and Linux reports KiB. Rounding upward avoids
    understating the peak for synthetic/non-integral values while preserving
    the integer values returned by getrusage.
    """

    raw = float(raw_max_rss)
    if not math.isfinite(raw) or raw < 0.0:
        raise ValueError("max RSS must be a finite non-negative value")
    system = (platform_name or platform.system()).lower()
    if system not in ("darwin", "linux"):
        raise ValueError(
            f"unsupported platform for ru_maxrss normalization: {system}"
        )
    divisor = 1024.0 if system == "darwin" else 1.0
    return int(math.ceil(raw / divisor))


def _parse_nonnegative_float(value: str, field_name: str) -> float:
    try:
        parsed = float(value)
    except ValueError as error:
        raise ValueError(
            f"PRT field {field_name!r} is not numeric: {value!r}"
        ) from error
    if not math.isfinite(parsed) or parsed < 0.0:
        raise ValueError(
            f"PRT field {field_name!r} must be finite and non-negative"
        )
    return parsed


def _parse_nonnegative_integer(value: str, field_name: str) -> int:
    try:
        parsed = int(value)
    except ValueError as error:
        raise ValueError(
            f"PRT field {field_name!r} is not an integer: {value!r}"
        ) from error
    if parsed < 0:
        raise ValueError(f"PRT field {field_name!r} must be non-negative")
    return parsed


def parse_prt_metrics(
    contents: str,
    expected_execution_mode: str | None = None,
    expected_reuse_mode: str | None = None,
) -> dict[str, Any]:
    fields: dict[str, str] = {}
    for raw_line in contents.splitlines():
        if " = " not in raw_line:
            continue
        name, value = (part.strip() for part in raw_line.split(" = ", 1))
        if name in fields:
            raise ValueError(f"duplicate PRT field: {name!r}")
        fields[name] = value

    required = (
        "execution mode",
        "Trace passes",
        "Trace seconds",
        "Project seconds",
        "Influence seconds",
        "Scale seconds",
        "SHD seconds",
        "Total solver and product seconds",
    )
    missing = tuple(name for name in required if name not in fields)
    if missing:
        raise ValueError(f"missing PRT field(s): {', '.join(missing)}")

    execution_mode = fields["execution mode"]
    reuse_mode = fields.get("reuse mode")
    if execution_mode not in {"broadband nonreuse", "broadband reuse"}:
        raise ValueError(f"unknown PRT execution mode marker: {execution_mode!r}")
    if execution_mode == "broadband reuse":
        if reuse_mode not in REUSE_MODES:
            raise ValueError(f"unknown PRT reuse mode marker: {reuse_mode!r}")
    elif reuse_mode is not None:
        raise ValueError(f"unexpected PRT reuse mode with {execution_mode!r}")
    if expected_execution_mode is not None:
        if expected_execution_mode not in EXECUTION_MODES:
            raise ValueError(f"unknown expected execution mode: {expected_execution_mode}")
        if execution_mode != f"broadband {expected_execution_mode}":
            raise ValueError("PRT execution mode does not match request")
        if reuse_mode != expected_reuse_mode:
            raise ValueError("PRT reuse mode does not match request")
    elif expected_reuse_mode is not None:
        raise ValueError("expected reuse mode requires expected execution mode")

    wall_fields = tuple(name for name in fields if name.endswith(" wall seconds"))
    if wall_fields != ("Solver wall seconds",):
        raise ValueError("PRT must contain exactly one Solver wall seconds field")

    metrics: dict[str, Any] = {
        "execution_mode_marker": execution_mode,
        "reuse_mode_marker": reuse_mode,
        "trace_passes": _parse_nonnegative_integer(
            fields["Trace passes"], "Trace passes"
        ),
        "trace_seconds": _parse_nonnegative_float(
            fields["Trace seconds"], "Trace seconds"
        ),
        "project_seconds": _parse_nonnegative_float(
            fields["Project seconds"], "Project seconds"
        ),
        "influence_seconds": _parse_nonnegative_float(
            fields["Influence seconds"], "Influence seconds"
        ),
        "scale_seconds": _parse_nonnegative_float(
            fields["Scale seconds"], "Scale seconds"
        ),
        "solver_wall_seconds": _parse_nonnegative_float(
            fields[wall_fields[0]], wall_fields[0]
        ),
        "solver_wall_field": wall_fields[0],
        "shd_seconds": _parse_nonnegative_float(
            fields["SHD seconds"], "SHD seconds"
        ),
        "total_solver_and_product_seconds": _parse_nonnegative_float(
            fields["Total solver and product seconds"],
            "Total solver and product seconds",
        ),
    }

    optional_integer_fields = {
        "ray count": "ray_count",
        "total ray count": "total_ray_count",
        "ray point count": "ray_point_count",
        "total ray point count": "total_ray_point_count",
        "ray cache bytes": "ray_cache_bytes",
        "cumulative ray cache bytes": "cumulative_ray_cache_bytes",
        "peak ray cache bytes": "peak_ray_cache_bytes",
        "requested reuse worker count": "requested_reuse_worker_count",
        "effective reuse worker count": "effective_reuse_worker_count",
        "active frequency limit": "active_frequency_limit",
        "output queue capacity": "output_queue_capacity",
        "peak queued results": "peak_queued_results",
        "estimated workspace bytes": "estimated_workspace_bytes",
        "estimated peak memory bytes": "estimated_peak_memory_bytes",
        "memory budget bytes": "memory_budget_bytes",
        "Influence ray accumulations": "influence_ray_accumulations",
        "Influence validated ray points": (
            "influence_validated_ray_points"
        ),
        "Influence validated workspace values": (
            "influence_validated_workspace_values"
        ),
        "Influence active ray points": "influence_active_ray_points",
        "Influence segment candidates": "influence_segment_candidates",
        "Influence eligible segments": "influence_eligible_segments",
        "Influence receiver range evaluations": (
            "influence_receiver_range_evaluations"
        ),
        "Influence receiver depth evaluations": (
            "influence_receiver_depth_evaluations"
        ),
        "Influence image evaluations": "influence_image_evaluations",
        "Influence window rejections": "influence_window_rejections",
        "Influence taper rejections": "influence_taper_rejections",
        "Influence nonzero image contributions": (
            "influence_nonzero_image_contributions"
        ),
        "Influence geometry segment evaluations": (
            "influence_geometry_segment_evaluations"
        ),
        "Influence geometry range evaluations": (
            "influence_geometry_range_evaluations"
        ),
        "Influence geometry depth evaluations": (
            "influence_geometry_depth_evaluations"
        ),
        "Influence geometry image geometry evaluations": (
            "influence_geometry_image_geometry_evaluations"
        ),
        "Influence frequency range kernel evaluations": (
            "influence_frequency_range_kernel_evaluations"
        ),
        "Influence frequency image kernel evaluations": (
            "influence_frequency_image_kernel_evaluations"
        ),
    }
    for prt_name, result_name in optional_integer_fields.items():
        if prt_name in fields:
            metrics[result_name] = _parse_nonnegative_integer(
                fields[prt_name], prt_name
            )
    optional_float_fields = {
        "Influence validation seconds": "influence_validation_seconds",
        "Influence precompute seconds": "influence_precompute_seconds",
        "Influence hot loop seconds": "influence_hot_loop_seconds",
    }
    for prt_name, result_name in optional_float_fields.items():
        if prt_name in fields:
            metrics[result_name] = _parse_nonnegative_float(
                fields[prt_name], prt_name
            )
    if "frequency task count" in fields:
        task_count = _parse_nonnegative_integer(
            fields["frequency task count"], "frequency task count"
        )
        frequency_tasks = []
        for index in range(task_count):
            prefix = f"frequency task {index} "
            task_fields = {
                "frequency_hz": "frequency Hz",
                "project_seconds": "Project seconds",
                "influence_seconds": "Influence seconds",
                "scale_seconds": "Scale seconds",
                "total_seconds": "total seconds",
            }
            missing_task_fields = tuple(
                prefix + prt_name
                for prt_name in task_fields.values()
                if prefix + prt_name not in fields
            )
            if missing_task_fields:
                raise ValueError(
                    "missing frequency-task PRT field(s): "
                    + ", ".join(missing_task_fields)
                )
            frequency_tasks.append(
                {
                    result_name: _parse_nonnegative_float(
                        fields[prefix + prt_name],
                        prefix + prt_name,
                    )
                    for result_name, prt_name in task_fields.items()
                }
            )
        metrics["frequency_tasks"] = frequency_tasks
    metrics["completed_successfully"] = (
        "Bellhop Broadband completed successfully" in contents.splitlines()
    )
    return metrics


def validate_prt_metrics(
    metrics: dict[str, Any],
    configuration: BenchmarkConfiguration,
    frequency_count: int,
    expect_frequency_tasks: bool = False,
) -> None:
    if frequency_count < 2:
        raise ValueError("broadband PRT validation requires two frequencies")
    if not metrics.get("completed_successfully"):
        raise ValueError("PRT successful-completion marker is missing")

    expected_trace_passes = (
        frequency_count
        if configuration.execution_mode == "nonreuse"
        else 1
    )
    if metrics["trace_passes"] != expected_trace_passes:
        raise ValueError(
            f"PRT trace passes {metrics['trace_passes']} != "
            f"{expected_trace_passes}"
        )

    if metrics["solver_wall_field"] != "Solver wall seconds":
        raise ValueError("PRT wall field must be Solver wall seconds")

    required_by_mode = {
        "nonreuse": (
            "total_ray_count",
            "total_ray_point_count",
            "cumulative_ray_cache_bytes",
            "peak_ray_cache_bytes",
        ),
        "serial": ("ray_count", "ray_point_count", "ray_cache_bytes"),
        "range": (
            "ray_count",
            "ray_point_count",
            "ray_cache_bytes",
            "requested_reuse_worker_count",
            "effective_reuse_worker_count",
        ),
        "frequency": (
            "ray_count",
            "ray_point_count",
            "ray_cache_bytes",
            "requested_reuse_worker_count",
            "active_frequency_limit",
            "output_queue_capacity",
            "peak_queued_results",
            "estimated_workspace_bytes",
            "estimated_peak_memory_bytes",
            "memory_budget_bytes",
        ),
    }
    missing = tuple(
        name
        for name in required_by_mode[configuration.reuse_mode or configuration.execution_mode]
        if name not in metrics
    )
    if missing:
        raise ValueError(
            "PRT is missing mode-specific metric(s): "
            + ", ".join(missing)
        )
    frequency_tasks = metrics.get("frequency_tasks")
    if expect_frequency_tasks:
        if configuration.reuse_mode != "frequency":
            raise ValueError(
                "frequency-task profiling requires the reuse-frequency route"
            )
        if frequency_tasks is None:
            raise ValueError("PRT frequency-task timings are missing")
        if len(frequency_tasks) != frequency_count:
            raise ValueError(
                "PRT frequency-task timing count does not match request"
            )
    elif frequency_tasks is not None:
        raise ValueError(
            "unexpected PRT frequency-task timings without profiling"
        )

    if configuration.reuse_mode == "range":
        expected_requested_workers = configuration.reuse_workers
        if (
            metrics["requested_reuse_worker_count"]
            != expected_requested_workers
        ):
            raise ValueError(
                "PRT requested reuse worker count does not match request"
            )
        if not 1 <= metrics["effective_reuse_worker_count"] <= (
            expected_requested_workers
        ):
            raise ValueError(
                "PRT effective reuse worker count is outside valid bounds"
            )
        return
    if configuration.reuse_mode != "frequency":
        return
    if (
        metrics["requested_reuse_worker_count"]
        != configuration.reuse_workers
    ):
        raise ValueError("PRT requested reuse worker count does not match request")
    if (
        metrics["output_queue_capacity"]
        != configuration.output_queue_capacity
    ):
        raise ValueError("PRT output queue capacity does not match request")
    if not 1 <= metrics["active_frequency_limit"] <= min(
        configuration.reuse_workers or 0,
        frequency_count,
    ):
        raise ValueError("PRT active frequency limit is outside valid bounds")
    if (
        metrics["peak_queued_results"]
        > configuration.output_queue_capacity
    ):
        raise ValueError("PRT peak queued results exceeds queue capacity")
    expected_budget_bytes = (
        0
        if configuration.memory_budget_mib is None
        else configuration.memory_budget_mib * 1024 * 1024
    )
    if metrics["memory_budget_bytes"] != expected_budget_bytes:
        raise ValueError("PRT memory budget does not match request")
    if (
        expected_budget_bytes > 0
        and metrics["estimated_peak_memory_bytes"] > expected_budget_bytes
    ):
        raise ValueError("PRT estimated peak memory exceeds memory budget")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def summarize_samples(samples: Sequence[dict[str, Any]]) -> dict[str, Any]:
    if not samples:
        raise ValueError("cannot summarize an empty sample sequence")
    metric_names = (
        "real_seconds",
        "max_rss_kib",
        "trace_seconds",
        "project_seconds",
        "influence_seconds",
        "scale_seconds",
        "solver_wall_seconds",
        "shd_seconds",
        "total_solver_and_product_seconds",
    )
    summary: dict[str, Any] = {"sample_count": len(samples)}
    for name in metric_names:
        values: list[float] = []
        for sample in samples:
            source = (
                sample
                if name in ("real_seconds", "max_rss_kib")
                else sample["prt"]
            )
            if name not in source:
                raise ValueError(f"sample is missing metric {name!r}")
            value = float(source[name])
            if not math.isfinite(value) or value < 0.0:
                raise ValueError(f"sample metric {name!r} is invalid")
            values.append(value)
        summary[name] = {
            "median": statistics.median(values),
            "min": min(values),
            "max": max(values),
        }
    return summary


def require_identical_sample_hashes(
    samples: Sequence[dict[str, Any]],
    configuration_name: str,
) -> str:
    if not samples:
        raise ValueError(
            f"{configuration_name}: no measured samples are available"
        )
    hashes = {str(sample["shd_sha256"]) for sample in samples}
    if len(hashes) != 1:
        raise RuntimeError(
            f"{configuration_name}: repeated SHD outputs are not "
            f"byte-identical: {sorted(hashes)}"
        )
    return next(iter(hashes))


def require_cross_configuration_hashes(
    configuration_results: Sequence[dict[str, Any]],
) -> str:
    if not configuration_results:
        raise ValueError("no configuration results are available")
    hashes = {
        str(result["representative_shd_sha256"])
        for result in configuration_results
    }
    if len(hashes) != 1:
        details = ", ".join(
            f"{result['configuration']['identifier']}="
            f"{result['representative_shd_sha256']}"
            for result in configuration_results
        )
        raise RuntimeError(
            f"SHD outputs differ across benchmark configurations: {details}"
        )
    return next(iter(hashes))


def require_identical_environment_hashes(
    configuration_results: Sequence[dict[str, Any]],
) -> str:
    hashes = {
        str(sample["environment_sha256"])
        for result in configuration_results
        for sample in (*result["warmups"], *result["samples"])
    }
    if not hashes:
        raise ValueError("no benchmark samples are available")
    if len(hashes) != 1:
        raise RuntimeError(
            f"benchmark environments are not byte-identical: {sorted(hashes)}"
        )
    return next(iter(hashes))


def add_speedups_vs_nonreuse(
    configuration_results: Sequence[dict[str, Any]],
) -> None:
    baselines = [
        result
        for result in configuration_results
        if result["configuration"]["execution_mode"] == "nonreuse"
    ]
    if not baselines:
        for result in configuration_results:
            result["speedup_vs_nonreuse"] = None
        return
    baseline_seconds = float(
        baselines[0]["summary"]["real_seconds"]["median"]
    )
    if baseline_seconds <= 0.0:
        raise ValueError("nonreuse median real time must be positive")
    for result in configuration_results:
        median_seconds = float(result["summary"]["real_seconds"]["median"])
        if median_seconds <= 0.0:
            raise ValueError("configuration median real time must be positive")
        result["speedup_vs_nonreuse"] = baseline_seconds / median_seconds


def rotated_indices(count: int, round_index: int) -> tuple[int, ...]:
    if count <= 0:
        raise ValueError("configuration count must be positive")
    offset = round_index % count
    return tuple((*range(offset, count), *range(0, offset)))


def _run_git(arguments: Sequence[str]) -> str | None:
    completed = subprocess.run(
        ["git", *arguments],
        cwd=PROJECT_ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    if completed.returncode != 0:
        return None
    return completed.stdout.strip()


def repository_metadata() -> dict[str, Any]:
    commit = _run_git(("rev-parse", "HEAD"))
    commit_tree = _run_git(("rev-parse", "HEAD^{tree}"))
    status = _run_git(("status", "--porcelain"))
    return {
        "root": str(PROJECT_ROOT),
        "commit": commit,
        "commit_tree": commit_tree,
        "dirty": None if status is None else bool(status),
    }


def build_metadata(executable: Path) -> dict[str, Any]:
    stat = executable.stat()
    return {
        "executable": str(executable),
        "sha256": sha256_file(executable),
        "size_bytes": stat.st_size,
        "modified_time_ns": stat.st_mtime_ns,
    }


def _tool_version(command: str) -> dict[str, Any]:
    executable = shutil.which(command)
    if executable is None:
        return {"executable": None, "version": None}
    completed = subprocess.run(
        [executable, "--version"],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    first_line = (
        completed.stdout.splitlines()[0] if completed.stdout else None
    )
    return {
        "executable": executable,
        "version": first_line if completed.returncode == 0 else None,
    }


def _physical_memory_bytes() -> int | None:
    try:
        return int(os.sysconf("SC_PAGE_SIZE")) * int(
            os.sysconf("SC_PHYS_PAGES")
        )
    except (AttributeError, OSError, ValueError):
        return None


def runtime_metadata() -> dict[str, Any]:
    return {
        "platform": {
            "system": platform.system(),
            "release": platform.release(),
            "machine": platform.machine(),
            "description": platform.platform(),
            "processor": platform.processor() or None,
            "logical_cpu_count": os.cpu_count(),
            "physical_memory_bytes": _physical_memory_bytes(),
        },
        "python": {
            "version": platform.python_version(),
            "implementation": platform.python_implementation(),
            "executable": sys.executable,
        },
        "numpy": {"version": np.__version__},
        "conda_environment": os.environ.get("CONDA_DEFAULT_ENV"),
        "host_tools": {
            "cmake": _tool_version("cmake"),
            "cxx": _tool_version("c++"),
        },
    }


def write_json_atomic(path: Path, value: dict[str, Any]) -> None:
    file_descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{path.name}.",
        suffix=".tmp",
        dir=path.parent,
    )
    temporary_path = Path(temporary_name)
    try:
        with os.fdopen(file_descriptor, "w", encoding="utf-8") as stream:
            json.dump(
                value,
                stream,
                ensure_ascii=False,
                indent=2,
                allow_nan=False,
            )
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        temporary_path.replace(path)
    except BaseException:
        temporary_path.unlink(missing_ok=True)
        raise


def remove_temporary_tree(path: Path) -> None:
    try:
        shutil.rmtree(path)
    except FileNotFoundError:
        # Some mounted filesystems can report a directory entry that has
        # already disappeared while rmtree is walking a freshly used tree.
        shutil.rmtree(path, ignore_errors=True)


def _sample_command(
    executable: Path,
    file_root: str,
    frequencies: Sequence[float],
    configuration: BenchmarkConfiguration,
    profile_frequency_tasks: bool = False,
) -> list[str]:
    command = [
        str(executable),
        file_root,
        "--frequencies-hz",
        format_frequency_csv(frequencies),
    ]
    if configuration.execution_mode == "nonreuse":
        command.extend(("--execution-mode", "nonreuse"))
    elif configuration.reuse_mode == "serial":
        command.extend(
            ("--execution-mode", "reuse", "--reuse-mode", "serial")
        )
    elif configuration.reuse_mode == "frequency":
        command.extend(
            (
                "--execution-mode",
                "reuse",
                "--reuse-mode",
                "frequency",
                "--reuse-workers",
                str(configuration.reuse_workers),
            )
        )
        command.extend(
            (
                "--output-queue-capacity",
                str(configuration.output_queue_capacity),
            )
        )
        if configuration.memory_budget_mib is not None:
            command.extend(
                (
                    "--memory-budget-mib",
                    str(configuration.memory_budget_mib),
                )
            )
        if profile_frequency_tasks:
            command.append("--profile-frequency-tasks")
    else:
        # Range Reuse always makes its worker request explicit.
        command.extend(
            (
                "--execution-mode",
                "reuse",
                "--reuse-mode",
                "range",
                "--reuse-workers",
                str(configuration.reuse_workers),
            )
        )
    return command


def run_internal_sample(request_path: Path) -> int:
    request = json.loads(request_path.read_text(encoding="utf-8"))
    required = {
        "command",
        "working_directory",
        "print_path",
        "shade_path",
        "configuration",
        "frequency_count",
        "profile_frequency_tasks",
        "sample_output",
    }
    missing = required - set(request)
    if missing:
        raise ValueError(
            f"internal sample request is missing: {', '.join(sorted(missing))}"
        )

    working_directory = Path(request["working_directory"])
    print_path = Path(request["print_path"])
    shade_path = Path(request["shade_path"])
    for output in (print_path, shade_path):
        if output.exists():
            output.unlink()

    begin = time.perf_counter()
    completed = subprocess.run(
        [str(value) for value in request["command"]],
        cwd=working_directory,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )
    real_seconds = time.perf_counter() - begin
    raw_max_rss = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
    if completed.returncode != 0:
        raise RuntimeError(
            "solver command failed with exit code "
            f"{completed.returncode}\nstdout:\n{completed.stdout}\n"
            f"stderr:\n{completed.stderr}"
        )
    if not print_path.is_file() or not shade_path.is_file():
        raise RuntimeError("solver did not produce both PRT and SHD outputs")

    configuration = BenchmarkConfiguration(**request["configuration"])
    prt_metrics = parse_prt_metrics(
        print_path.read_text(encoding="utf-8", errors="strict"),
        configuration.execution_mode,
        configuration.reuse_mode,
    )
    validate_prt_metrics(
        prt_metrics,
        configuration,
        int(request["frequency_count"]),
        bool(request["profile_frequency_tasks"]),
    )
    rss_system = platform.system()
    sample = {
        "real_seconds": real_seconds,
        "max_rss_raw": raw_max_rss,
        "max_rss_raw_unit": "bytes" if rss_system == "Darwin" else "KiB",
        "max_rss_kib": normalize_max_rss_kib(raw_max_rss, rss_system),
        "rss_measurement": "resource.RUSAGE_CHILDREN.ru_maxrss",
        "prt": prt_metrics,
        "shd_sha256": sha256_file(shade_path),
    }
    Path(request["sample_output"]).write_text(
        json.dumps(sample, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return 0


def _run_isolated_sample(
    *,
    executable: Path,
    definition: CaseDefinition,
    frequencies: tuple[float, ...],
    configuration: BenchmarkConfiguration,
    run_directory: Path,
    sample_kind: str,
    sample_index: int,
    profile_frequency_tasks: bool,
) -> dict[str, Any]:
    run_directory.mkdir(parents=True)
    file_root = "broadband_benchmark"
    environment_path = run_directory / f"{file_root}.env"
    print_path = run_directory / f"{file_root}.prt"
    shade_path = run_directory / f"{file_root}.shd"
    environment_path.write_text(
        definition.render_origin_environment(
            frequencies[0],
            definition.shared_launch_angle_count(frequencies),
        ),
        encoding="utf-8",
    )
    # Companion boundary files (.ati/.bty) must sit next to the env with the
    # same file root, exactly as the standard_cases runner stages them;
    # geometry cases (e.g. the hat boundary pair) fail to launch otherwise.
    stage_companion_files(definition, run_directory, file_root)
    environment_sha256 = sha256_file(environment_path)

    request_path = run_directory / "sample_request.json"
    sample_output = run_directory / "sample_result.json"
    request = {
        "command": _sample_command(
            executable, file_root, frequencies, configuration,
            profile_frequency_tasks,
        ),
        "working_directory": str(run_directory),
        "print_path": str(print_path),
        "shade_path": str(shade_path),
        "configuration": asdict(configuration),
        "frequency_count": len(frequencies),
        "profile_frequency_tasks": profile_frequency_tasks,
        "sample_output": str(sample_output),
    }
    request_path.write_text(
        json.dumps(request, indent=2) + "\n", encoding="utf-8"
    )
    completed = subprocess.run(
        [
            sys.executable,
            str(Path(__file__).resolve()),
            "--internal-sample-request",
            str(request_path),
        ],
        cwd=PROJECT_ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )
    if completed.returncode != 0:
        raise RuntimeError(
            f"{definition.case_id}/{configuration.identifier}/"
            f"{sample_kind}-{sample_index}: isolated sample failed\n"
            f"stdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
        )
    if not sample_output.is_file():
        raise RuntimeError("isolated sample did not write its result JSON")

    validate_broadband_output(
        definition,
        frequencies,
        configuration.execution_mode,
        print_path,
        shade_path,
        reuse_mode=configuration.reuse_mode,
    )
    sample = json.loads(sample_output.read_text(encoding="utf-8"))
    sample.update(
        {
            "kind": sample_kind,
            "index": sample_index,
            "environment_sha256": environment_sha256,
        }
    )
    return sample


def benchmark_case(
    *,
    definition: CaseDefinition,
    profile_name: str,
    configurations: Sequence[BenchmarkConfiguration],
    repeats: int,
    warmups: int,
    executable: Path,
    temporary_root: Path,
    require_cross_mode_identity: bool,
    profile_frequency_tasks: bool,
    frequencies_override: tuple[float, ...] | None = None,
) -> dict[str, Any]:
    if profile_name not in definition.profiles:
        raise ValueError(
            f"{definition.case_id}: profile {profile_name!r} is not defined"
        )
    if frequencies_override is not None:
        frequencies = frequencies_override
        frequency_source = "explicit --frequencies-csv override"
    else:
        frequencies = definition.frequencies(profile_name)
        frequency_source = profile_name
    if len(frequencies) < 2:
        raise ValueError(
            f"{definition.case_id}/{profile_name}: benchmark profile "
            "must contain at least two frequencies"
        )
    if repeats <= 0 or warmups < 0:
        raise ValueError("repeats must be positive and warmups non-negative")

    if not configurations:
        raise ValueError("at least one benchmark configuration is required")

    configuration_results: list[dict[str, Any]] = []
    configuration_roots: list[Path] = []
    for configuration_index, configuration in enumerate(configurations):
        configuration_root = (
            temporary_root
            / definition.case_id
            / profile_name
            / f"{configuration_index:02d}-{configuration.identifier}"
        )
        configuration_roots.append(configuration_root)
        configuration_results.append(
            {
                "configuration": {
                    "identifier": configuration.identifier,
                    **asdict(configuration),
                },
                "warmups": [],
                "samples": [],
            }
        )

    sample_order: list[dict[str, Any]] = []
    for sample_kind, sample_count, round_offset in (
        ("warmup", warmups, 0),
        ("measured", repeats, warmups),
    ):
        for sample_index in range(sample_count):
            order = rotated_indices(
                len(configurations),
                round_offset + sample_index,
            )
            sample_order.append(
                {
                    "kind": sample_kind,
                    "index": sample_index,
                    "configuration_identifiers": [
                        configurations[index].identifier for index in order
                    ],
                }
            )
            for configuration_index in order:
                configuration = configurations[configuration_index]
                result = configuration_results[configuration_index]
                print(
                    f"[{definition.case_id}/{profile_name}] "
                    f"{sample_kind} {sample_index + 1}/{sample_count}: "
                    f"{configuration.identifier}",
                    flush=True,
                )
                current_sample = _run_isolated_sample(
                    executable=executable,
                    definition=definition,
                    frequencies=frequencies,
                    configuration=configuration,
                    run_directory=(
                        configuration_roots[configuration_index]
                        / (
                            f"{sample_kind}-"
                            f"{sample_index:03d}"
                        )
                    ),
                    sample_kind=sample_kind,
                    sample_index=sample_index,
                    profile_frequency_tasks=profile_frequency_tasks,
                )
                target = (
                    result["warmups"]
                    if sample_kind == "warmup"
                    else result["samples"]
                )
                target.append(current_sample)

    for configuration_result in configuration_results:
        representative_hash = require_identical_sample_hashes(
            [
                *configuration_result["warmups"],
                *configuration_result["samples"],
            ],
            configuration_result["configuration"]["identifier"],
        )
        configuration_result["representative_shd_sha256"] = (
            representative_hash
        )
        configuration_result["summary"] = summarize_samples(
            configuration_result["samples"]
        )

    common_environment_hash = require_identical_environment_hashes(
        configuration_results
    )
    common_hash = None
    if require_cross_mode_identity:
        common_hash = require_cross_configuration_hashes(
            configuration_results
        )
    add_speedups_vs_nonreuse(configuration_results)
    return {
        "case_id": definition.case_id,
        "description": definition.description,
        "profile": profile_name,
        "frequency_source": frequency_source,
        "frequencies_hz": frequencies,
        "frequency_count": len(frequencies),
        "design_frequency_hz": max(frequencies),
        "launch_angle_counts": definition.launch_angle_counts(frequencies),
        "shared_launch_angle_count": (
            definition.shared_launch_angle_count(frequencies)
        ),
        "cross_configuration_shd_identity_required": (
            require_cross_mode_identity
        ),
        "environment_sha256": common_environment_hash,
        "common_shd_sha256": common_hash,
        "sample_order": sample_order,
        "configurations": configuration_results,
    }


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Benchmark Bellhop Broadband execution modes with "
            "isolated peak-RSS samples."
        )
    )
    parser.add_argument(
        "--case",
        action="append",
        dest="cases",
        help="standard case id; repeat for multiple cases",
    )
    parser.add_argument("--profile", default="broadband_regression")
    parser.add_argument(
        "--frequencies-csv",
        type=parse_frequency_csv,
        metavar="CSV",
        help=(
            "override the profile frequency grid with an explicit "
            "strictly-increasing CSV in Hz (for example '50,250'); the "
            "profile still selects the case's declared broadband setup"
        ),
    )
    parser.add_argument(
        "--execution-modes", type=parse_execution_modes,
        default=parse_execution_modes("nonreuse,reuse"), metavar="CSV",
    )
    parser.add_argument(
        "--reuse-modes", type=parse_reuse_modes,
        default=parse_reuse_modes("serial,frequency"), metavar="CSV",
    )
    parser.add_argument("--repeats", type=positive_integer, default=3)
    parser.add_argument("--warmups", type=nonnegative_integer, default=1)
    parser.add_argument(
        "--reuse-workers", type=parse_positive_integer_csv, metavar="CSV",
        help=("worker counts shared by Frequency and Range Reuse; omitted "
              "preserves Frequency=8 and Range=1"),
    )
    parser.add_argument(
        "--queue",
        type=positive_integer,
        choices=(1, 2),
        default=2,
        dest="output_queue_capacity",
    )
    parser.add_argument(
        "--memory-budget-mib",
        type=positive_integer,
    )
    parser.add_argument(
        "--profile-frequency-tasks",
        action="store_true",
        help=(
            "record per-frequency Project/Influence/Scale timings; "
            "requires reuse-frequency-only modes"
        ),
    )
    parser.add_argument(
        "--executable",
        type=Path,
        default=DEFAULT_EXECUTABLE,
    )
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument(
        "--machine-label",
        help=(
            "human-readable hardware label, for example "
            "'Apple M4 MacBook Air, 10 cores, 16 GiB'"
        ),
    )
    parser.add_argument(
        "--allow-dirty",
        action="store_true",
        help="allow a benchmark from a Git worktree with uncommitted changes",
    )
    parser.add_argument(
        "--no-cross-mode-shd-check",
        action="store_true",
        help="allow SHD hashes to differ between requested configurations",
    )
    parser.add_argument(
        "--internal-sample-request",
        type=Path,
        help=argparse.SUPPRESS,
    )
    return parser


def _validate_case_selection(
    definitions: dict[str, CaseDefinition],
    requested_cases: Iterable[str],
) -> tuple[CaseDefinition, ...]:
    names = tuple(requested_cases)
    if len(set(names)) != len(names):
        raise ValueError("case ids must be unique")
    unknown = tuple(name for name in names if name not in definitions)
    if unknown:
        raise ValueError(f"unknown cases: {', '.join(unknown)}")
    return tuple(definitions[name] for name in names)


def main(argv: Sequence[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        if args.internal_sample_request is not None:
            return run_internal_sample(
                args.internal_sample_request.resolve()
            )

        executable = args.executable.resolve()
        if not executable.is_file():
            raise FileNotFoundError(
                f"Broadband executable not found: {executable}"
            )
        if (executable.stat().st_mode & 0o111) == 0:
            raise PermissionError(
                f"Broadband executable is not executable: {executable}"
            )
        output = args.output.resolve()
        if output.exists() and not output.is_file():
            raise ValueError(f"output path is not a regular file: {output}")
        output.parent.mkdir(parents=True, exist_ok=True)
        git_metadata = repository_metadata()
        if git_metadata["dirty"] is None:
            raise RuntimeError("unable to determine Git worktree status")
        if git_metadata["dirty"] and not args.allow_dirty:
            raise RuntimeError(
                "Git worktree is dirty; commit changes or pass --allow-dirty "
                "for an explicitly non-formal run"
            )

        definitions = discover_cases(STANDARD_CASES_ROOT / "cases")
        if not args.cases:
            raise ValueError("at least one --case is required")
        selected_cases = _validate_case_selection(definitions, args.cases)
        configurations = expand_configurations(
            args.execution_modes,
            args.reuse_modes,
            args.reuse_workers,
            args.output_queue_capacity,
            args.memory_budget_mib,
        )
        if args.profile_frequency_tasks and any(
            configuration.reuse_mode != "frequency"
            for configuration in configurations
        ):
            raise ValueError(
                "--profile-frequency-tasks requires "
                "--execution-modes reuse --reuse-modes frequency"
            )

        temporary_parent = Path(
            tempfile.mkdtemp(
                prefix="broadband-benchmark-",
            )
        )
        try:
            cases = [
                benchmark_case(
                    definition=definition,
                    profile_name=args.profile,
                    configurations=configurations,
                    repeats=args.repeats,
                    warmups=args.warmups,
                    executable=executable,
                    temporary_root=temporary_parent,
                    require_cross_mode_identity=(
                        not args.no_cross_mode_shd_check
                    ),
                    profile_frequency_tasks=args.profile_frequency_tasks,
                    frequencies_override=args.frequencies_csv,
                )
                for definition in selected_cases
            ]
        finally:
            remove_temporary_tree(temporary_parent)

        report = {
            "schema_version": SCHEMA_VERSION,
            "generated_at_utc": datetime.now(timezone.utc).isoformat(),
            "git": git_metadata,
            "build": build_metadata(executable),
            **runtime_metadata(),
            "benchmark": {
                "repeats": args.repeats,
                "warmups": args.warmups,
                "frequencies_csv_override": (
                    None
                    if args.frequencies_csv is None
                    else format_frequency_csv(args.frequencies_csv)
                ),
                "configuration_order": [
                    configuration.identifier
                    for configuration in configurations
                ],
                "sample_order_policy": "rotated-per-round",
                "primary_cross_mode_metric": "external real_seconds",
                "rss_measurement": (
                    "isolated helper resource.RUSAGE_CHILDREN.ru_maxrss"
                ),
                "allow_dirty": args.allow_dirty,
                "machine_label": args.machine_label,
                "cross_mode_shd_identity_required": (
                    not args.no_cross_mode_shd_check
                ),
                "profile_frequency_tasks": args.profile_frequency_tasks,
                "execution_modes": list(args.execution_modes),
                "reuse_modes": list(args.reuse_modes),
                "reuse_workers": (None if args.reuse_workers is None else list(args.reuse_workers)),
            },
            "cases": cases,
        }
        write_json_atomic(output, report)
        print(f"Bellhop Broadband benchmark written to {output}")
        return 0
    except (OSError, RuntimeError, ValueError) as error:
        parser.exit(1, f"benchmark_broadband.py: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
