"""Execution-mode route showcase: one multi-frequency ENV, four TL routes."""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
from typing import Sequence

import numpy as np


CODES_ROOT = Path(__file__).resolve().parent
DEMO_ROOT = CODES_ROOT.parent
PROJECT_ROOT = DEMO_ROOT.parent
PLOTREAD_ROOT = PROJECT_ROOT / "test" / "PlotRead"
sys.path.insert(0, str(PLOTREAD_ROOT))
sys.path.insert(0, str(CODES_ROOT))

from bellhop_io_py.shd import PressureField, ShdReader
from bellhop_io_py.plotting import transmission_loss
from reliability import (
    ModelOutput,
    common_tl_limits,
    draw_tl,
    field_plane,
    output_paths,
)
from rayreuse_multifrequency import parse_indexes


ROUTES = ("nonreuse", "serial", "frequency", "range")
ROUTE_LABELS = {
    "nonreuse": "NonReuse",
    "serial": "Reuse serial",
    "frequency": "Reuse frequency",
    "range": "Reuse range",
}
ROUTE_DIRECTORIES = {
    "nonreuse": "nonreuse",
    "serial": "reuse_serial",
    "frequency": "reuse_frequency",
    "range": "reuse_range",
}
PARALLEL_ROUTES = ("frequency", "range")
DEFAULT_EXECUTABLE = (
    PROJECT_ROOT
    / "Bellhop_Broadband"
    / "build"
    / "release"
    / "bellhop_broadband"
)
TIMING_LINE = re.compile(
    r"^(?P<label>[A-Za-z][A-Za-z0-9 /]*?) seconds = (?P<value>[0-9.eE+-]+)$"
)
PRESSURE_FLOOR = 1.0e-37


def parse_routes(value: str) -> tuple[str, ...]:
    routes = tuple(item.strip() for item in value.split(",") if item.strip())
    if not routes:
        raise ValueError("at least one route is required")
    unknown = sorted(set(routes) - set(ROUTES))
    if unknown:
        raise ValueError(f"unknown routes: {', '.join(unknown)}")
    if len(set(routes)) != len(routes):
        raise ValueError("routes must not contain duplicates")
    if routes[0] != "nonreuse":
        raise ValueError("nonreuse must be the first route (difference reference)")
    return routes


def parse_workers(value: int) -> int:
    if value < 1:
        raise ValueError("workers must be a positive integer")
    return value


def route_arguments(route: str, workers: int) -> list[str]:
    if route == "nonreuse":
        return ["--execution-mode", "nonreuse"]
    arguments = ["--execution-mode", "reuse", "--reuse-mode", route]
    if route in PARALLEL_ROUTES:
        arguments += ["--reuse-workers", str(parse_workers(workers))]
    return arguments


def route_directory(route: str, workers: int) -> str:
    if route in PARALLEL_ROUTES:
        return f"{ROUTE_DIRECTORIES[route]}_w{parse_workers(workers)}"
    return ROUTE_DIRECTORIES[route]


def route_label(route: str, workers: int) -> str:
    if route in PARALLEL_ROUTES:
        return f"{ROUTE_LABELS[route]} (w{parse_workers(workers)})"
    return ROUTE_LABELS[route]


def expected_prt_markers(route: str) -> tuple[str, ...]:
    if route == "nonreuse":
        return ("execution mode = broadband nonreuse",)
    return (
        "execution mode = broadband reuse",
        f"reuse mode = {route}",
    )


def parse_timings(print_text: str) -> dict[str, float]:
    timings: dict[str, float] = {}
    for line in print_text.splitlines():
        match = TIMING_LINE.match(line.strip())
        if match is not None:
            timings[match.group("label")] = float(match.group("value"))
    return timings


def validate_environment(environment: Path, executable: Path) -> None:
    if not environment.is_file():
        raise FileNotFoundError(f"multi-frequency ENV not found: {environment}")
    if environment.suffix.lower() != ".env":
        raise ValueError("execution-mode input must be a .env file")
    if not executable.is_file():
        raise FileNotFoundError(f"RayReuse executable not found: {executable}")


def validate_route_result(
    results_root: Path, route: str, workers: int, environment: Path
) -> tuple[ModelOutput, ShdReader, str]:
    output = output_paths(
        results_root, route_directory(route, workers), environment.stem
    )
    if not output.print_log.is_file() or not output.shade.is_file():
        raise FileNotFoundError(
            f"{route}: PRT/SHD result is missing under {output.root.parent}"
        )
    print_text = output.print_log.read_text(errors="replace")
    if "FATAL ERROR" in print_text:
        raise RuntimeError(f"{route}: RayReuse reported FATAL ERROR")
    for marker in expected_prt_markers(route):
        if marker not in print_text:
            raise RuntimeError(f"{route}: PRT marker missing: {marker}")
    reader = ShdReader(output.shade)
    if reader.header.dimensions[0] < 2:
        raise RuntimeError(f"{route}: showcase result is not multi-frequency")
    for index in range(reader.header.dimensions[0]):
        pressure = reader.read(frequency_index=index).pressure
        if not np.isfinite(pressure).all() or not np.any(pressure):
            raise RuntimeError(f"{route}: invalid pressure at frequency index {index}")
    return output, reader, print_text


def run_routes(
    *,
    environment: Path,
    executable: Path,
    routes: tuple[str, ...],
    workers: int,
    results_root: Path,
) -> Path:
    validate_environment(environment, executable)
    records: dict[str, dict[str, object]] = {}
    for route in routes:
        route_directory_name = route_directory(route, workers)
        route_root = results_root / route_directory_name
        route_root.mkdir(parents=True, exist_ok=True)
        output = output_paths(results_root, route_directory_name, environment.stem)
        shutil.copy2(environment, output.environment)
        for stale_path in (output.print_log, output.shade):
            if stale_path.exists():
                stale_path.unlink()

        print(
            f"[Bellhop Broadband/{route}] {output.environment.name} -> "
            f"{output.shade.name}",
            flush=True,
        )
        started = time.perf_counter()
        subprocess.run(
            [
                str(executable),
                output.root.name,
                *route_arguments(route, workers),
            ],
            cwd=route_root,
            check=True,
        )
        elapsed = time.perf_counter() - started
        _, reader, print_text = validate_route_result(
            results_root, route, workers, environment
        )
        timings = parse_timings(print_text)
        if "Total solver and product" not in timings:
            raise RuntimeError(f"{route}: total timing line missing from PRT")
        records[route] = {
            "directory": route_directory_name,
            "arguments": route_arguments(route, workers),
            "environment": str(output.environment),
            "print_log": str(output.print_log),
            "shade": str(output.shade),
            "frequencies_hz": [
                float(value) for value in reader.header.frequencies_hz
            ],
            "elapsed_seconds": elapsed,
            "prt_timings_seconds": timings,
            "status": "passed",
        }

    report = {
        "schema": "bellhop.execution_modes.run",
        "schema_version": 1,
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "executable": str(executable),
        "source_environment": str(environment),
        "reuse_workers": workers,
        "routes": records,
    }
    report_path = results_root / "run_summary.json"
    report_path.write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    print(report_path)
    return report_path


def load_route_fields(
    *,
    environment: Path,
    routes: tuple[str, ...],
    workers: int,
    results_root: Path,
) -> dict[str, ShdReader]:
    readers: dict[str, ShdReader] = {}
    frequencies: dict[str, tuple[float, ...]] = {}
    for route in routes:
        _, reader, _ = validate_route_result(
            results_root, route, workers, environment
        )
        readers[route] = reader
        frequencies[route] = tuple(
            float(value) for value in reader.header.frequencies_hz
        )
    unique_frequencies = set(frequencies.values())
    if len(unique_frequencies) != 1:
        raise ValueError("the route SHD files do not use the same frequency list")
    return readers


def difference_metrics(
    field: PressureField, reference: PressureField
) -> dict[str, float]:
    pressure = field_plane(field)
    reference_pressure = field_plane(reference)
    valid = np.abs(reference_pressure) > PRESSURE_FLOOR
    loss = transmission_loss(pressure)
    reference_loss = transmission_loss(reference_pressure)
    tl_difference = np.where(valid, loss - reference_loss, np.nan)
    finite = np.abs(tl_difference[np.isfinite(tl_difference)])
    return {
        "max_abs_tl_difference_db": float(np.max(finite)) if finite.size else 0.0,
        "max_abs_pressure_difference": float(
            np.max(np.abs(pressure - reference_pressure))
        ),
    }


def route_comparison_metrics(
    readers: dict[str, ShdReader], routes: tuple[str, ...]
) -> dict[str, dict[str, object]]:
    frequency_count = readers[routes[0]].header.dimensions[0]
    metrics: dict[str, dict[str, object]] = {}
    for route in routes[1:]:
        per_frequency: list[dict[str, float]] = []
        for index in range(frequency_count):
            reference = readers["nonreuse"].read(frequency_index=index)
            field = readers[route].read(frequency_index=index)
            per_frequency.append(difference_metrics(field, reference))
        metrics[route] = {
            "reference": "nonreuse",
            "per_frequency": per_frequency,
            "max_abs_tl_difference_db": max(
                values["max_abs_tl_difference_db"] for values in per_frequency
            ),
            "max_abs_pressure_difference": max(
                values["max_abs_pressure_difference"] for values in per_frequency
            ),
        }
    return metrics


def save_tl_comparison(
    *,
    readers: dict[str, ShdReader],
    routes: tuple[str, ...],
    workers: int,
    indexes: tuple[int, ...],
    figures_root: Path,
    stem: str,
    dpi: int,
) -> Path:
    import matplotlib.pyplot as plt

    fields = [
        readers[route].read(frequency_index=index)
        for index in indexes
        for route in routes
    ]
    limits = common_tl_limits(fields)
    figure, axes = plt.subplots(
        len(indexes),
        len(routes),
        figsize=(4.4 * len(routes), 3.8 * len(indexes)),
        squeeze=False,
        layout="constrained",
    )
    artist = None
    for row, index in enumerate(indexes):
        for column, route in enumerate(routes):
            field = readers[route].read(frequency_index=index)
            title = f"{route_label(route, workers)} — {field.frequency_hz:g} Hz"
            artist = draw_tl(axes[row][column], field, title, limits)
    assert artist is not None
    figure.colorbar(
        artist,
        ax=axes.ravel().tolist(),
        orientation="horizontal",
        shrink=0.72,
        pad=0.08,
        label="Transmission loss (dB)",
    )
    figure.suptitle("Bellhop Broadband — execution modes on one multi-frequency ENV")
    output = figures_root / f"{stem}_tl_comparison.png"
    figure.savefig(output, dpi=dpi, bbox_inches="tight")
    plt.close(figure)
    return output


def save_route_differences(
    *,
    readers: dict[str, ShdReader],
    routes: tuple[str, ...],
    workers: int,
    indexes: tuple[int, ...],
    figures_root: Path,
    stem: str,
    dpi: int,
) -> Path:
    import matplotlib.pyplot as plt

    candidates = tuple(route for route in routes if route != "nonreuse")
    if not candidates:
        raise ValueError("difference figure requires at least one reuse route")
    differences: dict[tuple[int, str], np.ndarray] = {}
    values: list[np.ndarray] = []
    for index in indexes:
        reference = readers["nonreuse"].read(frequency_index=index)
        reference_pressure = field_plane(reference)
        reference_loss = transmission_loss(reference_pressure)
        for route in candidates:
            field = readers[route].read(frequency_index=index)
            valid = np.abs(reference_pressure) > PRESSURE_FLOOR
            difference = np.where(
                valid,
                transmission_loss(field_plane(field)) - reference_loss,
                np.nan,
            )
            differences[(index, route)] = difference
            finite = np.abs(difference[np.isfinite(difference)])
            if finite.size:
                values.append(finite)
    limit = max(
        1.0e-3,
        float(np.percentile(np.concatenate(values), 99.0)) if values else 0.0,
    )

    figure, axes = plt.subplots(
        len(indexes),
        len(candidates),
        figsize=(6.0 * len(candidates), 4.6 * len(indexes)),
        squeeze=False,
        layout="constrained",
    )
    artist = None
    for row, index in enumerate(indexes):
        for column, route in enumerate(candidates):
            axis = axes[row][column]
            difference = differences[(index, route)]
            field = readers[route].read(frequency_index=index)
            artist = axis.pcolormesh(
                field.header.receiver_ranges_m / 1000.0,
                field.header.receiver_depths_m[: difference.shape[0]],
                difference,
                shading="auto",
                cmap="coolwarm",
                vmin=-limit,
                vmax=limit,
            )
            axis.invert_yaxis()
            axis.set_xlabel("Range (km)")
            axis.set_ylabel("Depth (m)")
            finite = np.abs(difference[np.isfinite(difference)])
            maximum = float(np.max(finite)) if finite.size else 0.0
            axis.set_title(
                f"{route_label(route, workers)} − NonReuse @ "
                f"{field.frequency_hz:g} Hz\nmax |ΔTL| = {maximum:.4g} dB"
            )
    assert artist is not None
    figure.colorbar(
        artist,
        ax=axes.ravel().tolist(),
        orientation="horizontal",
        shrink=0.72,
        pad=0.08,
        label="TL difference (dB)",
    )
    output = figures_root / f"{stem}_tl_difference.png"
    figure.savefig(output, dpi=dpi, bbox_inches="tight")
    plt.close(figure)
    return output


def save_route_timings(
    *,
    routes: tuple[str, ...],
    workers: int,
    results_root: Path,
    figures_root: Path,
    stem: str,
    dpi: int,
) -> Path:
    import matplotlib.pyplot as plt

    run_summary = json.loads((results_root / "run_summary.json").read_text())
    totals: list[float] = []
    traces: list[float] = []
    for route in routes:
        timings = run_summary["routes"][route]["prt_timings_seconds"]
        totals.append(timings["Total solver and product"])
        traces.append(timings.get("Trace", 0.0))

    labels = [route_label(route, workers) for route in routes]
    positions = np.arange(len(routes))
    figure, (total_axis, trace_axis) = plt.subplots(
        1, 2, figsize=(11.0, 4.6), layout="constrained"
    )
    for axis, values, color, ylabel, title in (
        (
            total_axis,
            totals,
            "#0072B2",
            "Wall-clock solver and product seconds",
            "Total wall time per route",
        ),
        (
            trace_axis,
            traces,
            "#E69F00",
            "Trace phase seconds",
            "Trace phase: reuse traces once,\nnonreuse re-traces per frequency",
        ),
    ):
        axis.bar(positions, values, color=color)
        for position, value in zip(positions, values, strict=True):
            axis.text(position, value, f"{value:.2f} s", ha="center", va="bottom")
        axis.set_xticks(positions, labels, rotation=20, ha="right")
        axis.set_ylabel(ylabel)
        axis.set_ylim(0.0, max(values) * 1.18)
        axis.set_title(title)
    figure.suptitle("Execution-mode route timings (PRT)")
    output = figures_root / f"{stem}_timings.png"
    figure.savefig(output, dpi=dpi, bbox_inches="tight")
    plt.close(figure)
    return output


def plot_execution_modes(
    *,
    environment: Path,
    routes: tuple[str, ...],
    workers: int,
    results_root: Path,
    figures_root: Path,
    index_text: str,
    dpi: int,
) -> Path:
    readers = load_route_fields(
        environment=environment,
        routes=routes,
        workers=workers,
        results_root=results_root,
    )
    indexes = parse_indexes(index_text, readers[routes[0]].header.dimensions[0])
    figures_root.mkdir(parents=True, exist_ok=True)
    stem = environment.stem
    comparison_figure = save_tl_comparison(
        readers=readers,
        routes=routes,
        workers=workers,
        indexes=indexes,
        figures_root=figures_root,
        stem=stem,
        dpi=dpi,
    )
    difference_figure = save_route_differences(
        readers=readers,
        routes=routes,
        workers=workers,
        indexes=indexes,
        figures_root=figures_root,
        stem=stem,
        dpi=dpi,
    )
    timings_figure = save_route_timings(
        routes=routes,
        workers=workers,
        results_root=results_root,
        figures_root=figures_root,
        stem=stem,
        dpi=dpi,
    )

    metrics = route_comparison_metrics(readers, routes)
    summary = {
        "schema": "bellhop.execution_modes.figures",
        "schema_version": 1,
        "environment": str(environment),
        "routes": list(routes),
        "reuse_workers": workers,
        "frequencies_hz": [
            float(value) for value in readers[routes[0]].header.frequencies_hz
        ],
        "selected_frequency_indexes": list(indexes),
        "comparisons_to_nonreuse": metrics,
        "figures": {
            "tl_comparison": str(comparison_figure),
            "tl_difference": str(difference_figure),
            "timings": str(timings_figure),
        },
    }
    summary_path = figures_root / f"{stem}_summary.json"
    summary_path.write_text(
        json.dumps(summary, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    print(comparison_figure)
    print(difference_figure)
    print(timings_figure)
    print(summary_path)
    return summary_path


def add_common_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "--environment",
        type=Path,
        default=(
            DEMO_ROOT
            / "cases"
            / "rayreuse_multifrequency"
            / "munk_rayreuse_multifrequency.env"
        ),
    )
    parser.add_argument("--executable", type=Path, default=DEFAULT_EXECUTABLE)
    parser.add_argument("--routes", default=",".join(ROUTES))
    parser.add_argument("--workers", type=int, default=2)
    parser.add_argument(
        "--results-root",
        type=Path,
        default=DEMO_ROOT / "results" / "execution_modes",
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Run one multi-frequency ENV through the execution-mode routes "
            "and compare TL, differences, and phase timings."
        )
    )
    commands = parser.add_subparsers(dest="command", required=True)
    for command in ("check", "run", "plot", "show"):
        command_parser = commands.add_parser(command)
        add_common_arguments(command_parser)
        if command in ("plot", "show"):
            command_parser.add_argument("--plot-indexes", default="2")
            command_parser.add_argument(
                "--figures-root",
                type=Path,
                default=DEMO_ROOT / "figures" / "execution_modes",
            )
            command_parser.add_argument("--dpi", type=int, default=180)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    arguments = build_parser().parse_args(argv)
    try:
        routes = parse_routes(arguments.routes)
        workers = parse_workers(arguments.workers)
        environment = arguments.environment.expanduser().resolve()
        executable = arguments.executable.expanduser().resolve()
        results_root = arguments.results_root.expanduser().resolve()
        if arguments.command == "check":
            validate_environment(environment, executable)
            print(f"environment: READY ({environment})")
            for route in routes:
                print(
                    f"{route}: {route_directory(route, workers)} "
                    f"({' '.join(route_arguments(route, workers))})"
                )
            return 0
        if arguments.command in ("run", "show"):
            run_routes(
                environment=environment,
                executable=executable,
                routes=routes,
                workers=workers,
                results_root=results_root,
            )
        if arguments.command in ("plot", "show"):
            plot_execution_modes(
                environment=environment,
                routes=routes,
                workers=workers,
                results_root=results_root,
                figures_root=arguments.figures_root.expanduser().resolve(),
                index_text=arguments.plot_indexes,
                dpi=arguments.dpi,
            )
        return 0
    except (
        FileNotFoundError,
        RuntimeError,
        subprocess.CalledProcessError,
        ValueError,
    ) as error:
        print(f"execution-modes showcase: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
