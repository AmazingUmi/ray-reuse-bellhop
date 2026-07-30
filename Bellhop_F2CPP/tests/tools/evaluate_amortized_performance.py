#!/usr/bin/env python3
"""Evaluate the trace-once performance gate from F2CPP PRT files."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
import sys


@dataclass(frozen=True)
class Timing:
    case_id: str
    trace_seconds: float
    project_seconds: float
    influence_seconds: float
    scale_seconds: float
    shd_seconds: float
    ray_count: int
    ray_point_count: int
    ray_cache_bytes: int

    @property
    def frequency_seconds(self) -> float:
        return (
            self.project_seconds
            + self.influence_seconds
            + self.scale_seconds
            + self.shd_seconds
        )


@dataclass(frozen=True)
class AmortizedResult:
    timing: Timing
    frequency_count: int
    repeated_seconds: float
    reused_seconds: float
    amortized_seconds_per_frequency: float
    savings_percent: float


_FIELDS = {
    "ray count": ("ray_count", int),
    "ray point count": ("ray_point_count", int),
    "ray cache bytes": ("ray_cache_bytes", int),
    "Trace seconds": ("trace_seconds", float),
    "Project seconds": ("project_seconds", float),
    "Influence seconds": ("influence_seconds", float),
    "Scale seconds": ("scale_seconds", float),
    "SHD seconds": ("shd_seconds", float),
}


def parse_prt(path: Path) -> Timing:
    values: dict[str, int | float] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if "=" not in line:
            continue
        key, raw_value = (part.strip() for part in line.split("=", 1))
        field = _FIELDS.get(key)
        if field is None:
            continue
        name, conversion = field
        values[name] = conversion(raw_value)

    missing = sorted(
        name for name, _ in _FIELDS.values() if name not in values
    )
    if missing:
        raise ValueError(f"{path}: missing PRT fields: {', '.join(missing)}")

    timing = Timing(
        case_id=path.parents[2].name,
        trace_seconds=float(values["trace_seconds"]),
        project_seconds=float(values["project_seconds"]),
        influence_seconds=float(values["influence_seconds"]),
        scale_seconds=float(values["scale_seconds"]),
        shd_seconds=float(values["shd_seconds"]),
        ray_count=int(values["ray_count"]),
        ray_point_count=int(values["ray_point_count"]),
        ray_cache_bytes=int(values["ray_cache_bytes"]),
    )
    if (
        timing.trace_seconds <= 0.0
        or timing.frequency_seconds <= 0.0
        or timing.ray_count <= 0
        or timing.ray_point_count <= 0
        or timing.ray_cache_bytes <= 0
    ):
        raise ValueError(f"{path}: timing and cache values must be positive")
    return timing


def evaluate(timing: Timing, frequency_count: int) -> AmortizedResult:
    if frequency_count < 2:
        raise ValueError("frequency_count must be at least two")
    repeated = frequency_count * (
        timing.trace_seconds + timing.frequency_seconds
    )
    reused = (
        timing.trace_seconds
        + frequency_count * timing.frequency_seconds
    )
    savings = 100.0 * (repeated - reused) / repeated
    return AmortizedResult(
        timing=timing,
        frequency_count=frequency_count,
        repeated_seconds=repeated,
        reused_seconds=reused,
        amortized_seconds_per_frequency=reused / frequency_count,
        savings_percent=savings,
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Compare repeated single-frequency tracing with a modeled "
            "trace-once frozen-cache run."
        )
    )
    parser.add_argument("prt", nargs="+", type=Path)
    parser.add_argument("--frequency-count", type=int, default=16)
    parser.add_argument(
        "--minimum-savings-percent", type=float, default=1.0
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    if args.minimum_savings_percent < 0.0:
        raise ValueError("minimum savings must be non-negative")

    results = [
        evaluate(parse_prt(path), args.frequency_count)
        for path in args.prt
    ]
    print(
        "| case | repeated / s | trace-once / s | "
        "amortized / s·freq⁻¹ | savings |"
    )
    print("|---|---:|---:|---:|---:|")
    failed = False
    for result in sorted(results, key=lambda item: item.timing.case_id):
        print(
            f"| `{result.timing.case_id}` "
            f"| `{result.repeated_seconds:.3f}` "
            f"| `{result.reused_seconds:.3f}` "
            f"| `{result.amortized_seconds_per_frequency:.3f}` "
            f"| `{result.savings_percent:.2f}%` |"
        )
        failed = (
            failed
            or result.savings_percent + 1.0e-12
            < args.minimum_savings_percent
        )
    return 1 if failed else 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        print(error, file=sys.stderr)
        raise SystemExit(2) from error
