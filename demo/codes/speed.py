"""Speed showcase: run the execution-mode routes and compare run times."""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
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

from bellhop_io_py.shd import ShdReader
from reliability import (
    ROUTES,
    ModelOutput,
    expected_prt_markers,
    output_paths,
    parse_routes,
    parse_timings,
    parse_workers,
    route_arguments,
    route_directory,
    route_label,
)


DEFAULT_EXECUTABLE = (
    PROJECT_ROOT
    / "Bellhop_Broadband"
    / "build"
    / "release"
    / "bellhop_broadband"
)


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
        "schema": "bellhop.speed.run",
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


def load_run_summary(results_root: Path, routes: tuple[str, ...]) -> dict[str, object]:
    summary_path = results_root / "run_summary.json"
    if not summary_path.is_file():
        raise FileNotFoundError(
            f"run summary not found: {summary_path} (run the routes first)"
        )
    run_summary = json.loads(summary_path.read_text())
    records = run_summary.get("routes", {})
    missing = [route for route in routes if route not in records]
    if missing:
        raise ValueError(f"run summary is missing routes: {', '.join(missing)}")
    return run_summary


def save_speed_comparison(
    *,
    routes: tuple[str, ...],
    workers: int,
    run_summary: dict[str, object],
    figures_root: Path,
    stem: str,
    dpi: int,
) -> Path:
    import matplotlib.pyplot as plt

    records = run_summary["routes"]
    totals: list[float] = []
    traces: list[float] = []
    for route in routes:
        timings = records[route]["prt_timings_seconds"]
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
    figure.suptitle("Execution-mode route speed comparison (PRT timings)")
    output = figures_root / f"{stem}_speed_comparison.png"
    figure.savefig(output, dpi=dpi, bbox_inches="tight")
    plt.close(figure)
    return output


def plot_speed_comparison(
    *,
    environment: Path,
    routes: tuple[str, ...],
    workers: int,
    results_root: Path,
    figures_root: Path,
    dpi: int,
) -> Path:
    run_summary = load_run_summary(results_root, routes)
    figures_root.mkdir(parents=True, exist_ok=True)
    stem = environment.stem
    figure = save_speed_comparison(
        routes=routes,
        workers=workers,
        run_summary=run_summary,
        figures_root=figures_root,
        stem=stem,
        dpi=dpi,
    )

    records = run_summary["routes"]
    summary = {
        "schema": "bellhop.speed.figures",
        "schema_version": 1,
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "environment": str(environment),
        "routes": list(routes),
        "reuse_workers": workers,
        "timings": {
            route: {
                "elapsed_seconds": records[route]["elapsed_seconds"],
                "prt_timings_seconds": records[route]["prt_timings_seconds"],
            }
            for route in routes
        },
        "figure": str(figure),
    }
    summary_path = figures_root / f"{stem}_speed_summary.json"
    summary_path.write_text(
        json.dumps(summary, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    print(figure)
    print(summary_path)
    return summary_path


def add_common_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "--environment",
        type=Path,
        default=(
            DEMO_ROOT
            / "cases"
            / "reliability"
            / "munk_rayreuse_multifrequency.env"
        ),
    )
    parser.add_argument("--executable", type=Path, default=DEFAULT_EXECUTABLE)
    parser.add_argument("--routes", default=",".join(ROUTES))
    parser.add_argument("--workers", type=int, default=4)
    parser.add_argument(
        "--results-root",
        type=Path,
        default=DEMO_ROOT / "results" / "speed",
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Run one multi-frequency ENV through the execution-mode routes "
            "and compare their run times."
        )
    )
    commands = parser.add_subparsers(dest="command", required=True)
    for command in ("check", "run", "plot", "show"):
        command_parser = commands.add_parser(command)
        add_common_arguments(command_parser)
        if command in ("plot", "show"):
            command_parser.add_argument(
                "--figures-root",
                type=Path,
                default=DEMO_ROOT / "figures" / "speed",
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
            plot_speed_comparison(
                environment=environment,
                routes=routes,
                workers=workers,
                results_root=results_root,
                figures_root=arguments.figures_root.expanduser().resolve(),
                dpi=arguments.dpi,
            )
        return 0
    except (
        FileNotFoundError,
        RuntimeError,
        subprocess.CalledProcessError,
        ValueError,
    ) as error:
        print(f"speed showcase: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
