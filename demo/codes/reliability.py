"""Reliability showcase: three Bellhop implementations and execution routes."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
from typing import Sequence


CODES_ROOT = Path(__file__).resolve().parent
DEMO_ROOT = CODES_ROOT.parent
PROJECT_ROOT = DEMO_ROOT.parent
PLOTREAD_ROOT = PROJECT_ROOT / "test" / "PlotRead"
STANDARD_CODES_ROOT = PROJECT_ROOT / "test" / "standard_cases" / "codes"
sys.path.insert(0, str(PLOTREAD_ROOT))
sys.path.insert(0, str(STANDARD_CODES_ROOT))

import numpy as np

from bellhop_io_py.plotting import transmission_loss
from bellhop_io_py.shd import PressureField, ShdReader
from compare_fields import compare_files


VERSIONS = ("origin", "f2cpp", "broadband")
VERSION_LABELS = {
    "origin": "Original Bellhop",
    "f2cpp": "Bellhop F2CPP",
    "broadband": "Bellhop Broadband",
}
DEFAULT_EXECUTABLES = {
    "origin": PROJECT_ROOT / "Bellhop_origin" / "bin" / "bellhop",
    "f2cpp": (
        PROJECT_ROOT
        / "Bellhop_F2CPP"
        / "build"
        / "release"
        / "bellhop_f2cpp"
    ),
    "broadband": (
        PROJECT_ROOT
        / "Bellhop_Broadband"
        / "build"
        / "release"
        / "bellhop_broadband"
    ),
}
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
TIMING_LINE = re.compile(
    r"^(?P<label>[A-Za-z][A-Za-z0-9 /]*?) seconds = (?P<value>[0-9.eE+-]+)$"
)
PRESSURE_FLOOR = 1.0e-37
DEFAULT_ROUTE_ENVIRONMENT = (
    DEMO_ROOT
    / "cases"
    / "reliability"
    / "munk_rayreuse_multifrequency.env"
)


@dataclass(frozen=True)
class ModelOutput:
    version: str
    root: Path
    environment: Path
    print_log: Path
    shade: Path


def parse_versions(value: str) -> tuple[str, ...]:
    versions = tuple(item.strip() for item in value.split(",") if item.strip())
    if not versions:
        raise ValueError("at least one version is required")
    unknown = sorted(set(versions) - set(VERSIONS))
    if unknown:
        raise ValueError(f"unknown versions: {', '.join(unknown)}")
    if len(set(versions)) != len(versions):
        raise ValueError("versions must not contain duplicates")
    return versions


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


def executable_paths(arguments: argparse.Namespace) -> dict[str, Path]:
    overrides = {
        "origin": arguments.origin_executable,
        "f2cpp": arguments.f2cpp_executable,
        "broadband": arguments.broadband_executable,
    }
    return {
        version: (
            overrides[version].expanduser().resolve()
            if overrides[version] is not None
            else DEFAULT_EXECUTABLES[version]
        )
        for version in VERSIONS
    }


def output_paths(
    results_root: Path, version: str, environment_stem: str
) -> ModelOutput:
    root = results_root / version / environment_stem
    return ModelOutput(
        version=version,
        root=root,
        environment=root.with_suffix(".env"),
        print_log=root.with_suffix(".prt"),
        shade=root.with_suffix(".shd"),
    )


def validate_inputs(
    environment: Path,
    versions: tuple[str, ...],
    executables: dict[str, Path],
) -> None:
    if not environment.is_file():
        raise FileNotFoundError(f"Munk environment not found: {environment}")
    if environment.suffix.lower() != ".env":
        raise ValueError("the showcase input must be a .env file")
    missing = [
        f"{version}: {executables[version]}"
        for version in versions
        if not executables[version].is_file()
    ]
    if missing:
        raise FileNotFoundError("missing executables:\n  " + "\n  ".join(missing))


def validate_output(output: ModelOutput) -> PressureField:
    if not output.print_log.is_file() or output.print_log.stat().st_size == 0:
        raise RuntimeError(f"missing PRT output: {output.print_log}")
    print_text = output.print_log.read_text(errors="replace")
    if "FATAL ERROR" in print_text:
        raise RuntimeError(f"{output.version} reported FATAL ERROR")
    if not output.shade.is_file() or output.shade.stat().st_size == 0:
        raise RuntimeError(f"missing SHD output: {output.shade}")
    reader = ShdReader(output.shade)
    if reader.header.dimensions[0] != 1:
        raise RuntimeError(f"{output.version}: showcase expects one frequency")
    field = reader.read(frequency_index=0)
    if not np.isfinite(field.pressure).all() or not np.any(field.pressure):
        raise RuntimeError(f"{output.version}: invalid pressure field")
    return field


def run_models(
    *,
    environment: Path,
    versions: tuple[str, ...],
    results_root: Path,
    executables: dict[str, Path],
) -> Path:
    validate_inputs(environment, versions, executables)
    records: dict[str, dict[str, object]] = {}
    for version in versions:
        version_directory = results_root / version
        version_directory.mkdir(parents=True, exist_ok=True)
        output = output_paths(results_root, version, environment.stem)
        shutil.copy2(environment, output.environment)
        for stale_path in (output.print_log, output.shade):
            if stale_path.exists():
                stale_path.unlink()

        print(
            f"[{VERSION_LABELS[version]}] {output.environment.name} -> "
            f"{output.shade.name}",
            flush=True,
        )
        started = time.perf_counter()
        subprocess.run(
            [str(executables[version]), output.root.name],
            cwd=version_directory,
            check=True,
        )
        elapsed = time.perf_counter() - started
        field = validate_output(output)
        records[version] = {
            "executable": str(executables[version]),
            "environment": str(output.environment),
            "print_log": str(output.print_log),
            "shade": str(output.shade),
            "frequency_hz": field.frequency_hz,
            "elapsed_seconds": elapsed,
            "status": "passed",
        }

    report = {
        "schema": "bellhop.direct_showcase.run",
        "schema_version": 1,
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "source_environment": str(environment),
        "versions": records,
    }
    report_path = results_root / "run_summary.json"
    report_path.write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    print(report_path)
    return report_path


def field_plane(field: PressureField) -> np.ndarray:
    return field.pressure[0, 0]


def common_tl_limits(fields: Sequence[PressureField]) -> tuple[float, float]:
    valid_values: list[np.ndarray] = []
    for field in fields:
        pressure = field_plane(field)
        loss = transmission_loss(pressure)
        valid = np.isfinite(loss) & (np.abs(pressure) > 1.0e-37)
        if np.any(valid):
            valid_values.append(loss[valid])
    if not valid_values:
        return 0.0, 120.0
    combined = np.concatenate(valid_values)
    upper = float(5.0 * np.ceil(np.percentile(combined, 98.0) / 5.0))
    return max(0.0, upper - 60.0), upper


def draw_tl(axes, field: PressureField, label: str, limits: tuple[float, float]):
    loss = transmission_loss(field_plane(field))
    ranges_km = field.header.receiver_ranges_m / 1000.0
    depths_m = field.header.receiver_depths_m[: loss.shape[0]]
    artist = axes.pcolormesh(
        ranges_km,
        depths_m,
        loss,
        shading="auto",
        cmap="viridis_r",
        vmin=limits[0],
        vmax=limits[1],
    )
    axes.invert_yaxis()
    axes.set_xlabel("Range (km)")
    axes.set_ylabel("Depth (m)")
    axes.set_title(label)
    return artist


def save_field_comparison(
    *,
    fields: dict[str, PressureField],
    versions: tuple[str, ...],
    figures_root: Path,
    stem: str,
    dpi: int,
) -> Path:
    import matplotlib.pyplot as plt

    limits = common_tl_limits(tuple(fields.values()))
    figure, axes = plt.subplots(
        1,
        len(versions),
        figsize=(5.2 * len(versions), 4.8),
        squeeze=False,
        layout="constrained",
    )
    artist = None
    for axis, version in zip(axes[0], versions, strict=True):
        artist = draw_tl(axis, fields[version], VERSION_LABELS[version], limits)
    assert artist is not None
    figure.colorbar(
        artist,
        ax=list(axes[0]),
        orientation="horizontal",
        shrink=0.72,
        pad=0.08,
        label="Transmission loss (dB)",
    )
    title = fields[versions[0]].header.title.rstrip().replace("_", " ")
    frequency_hz = fields[versions[0]].frequency_hz
    figure.suptitle(f"{title} — {frequency_hz:g} Hz")
    suffix = versions[0] if len(versions) == 1 else "comparison"
    output = figures_root / f"{stem}_{suffix}.png"
    figure.savefig(output, dpi=dpi, bbox_inches="tight")
    plt.close(figure)
    return output


def save_difference_comparison(
    *,
    fields: dict[str, PressureField],
    candidates: tuple[str, ...],
    figures_root: Path,
    stem: str,
    dpi: int,
) -> Path:
    import matplotlib.pyplot as plt

    reference = field_plane(fields["origin"])
    reference_tl = transmission_loss(reference)
    differences: dict[str, np.ndarray] = {}
    values: list[np.ndarray] = []
    for version in candidates:
        difference = transmission_loss(field_plane(fields[version])) - reference_tl
        difference = np.where(np.abs(reference) > 1.0e-10, difference, np.nan)
        differences[version] = difference
        finite = np.abs(difference[np.isfinite(difference)])
        if finite.size:
            values.append(finite)
    limit = max(
        1.0e-3,
        float(np.percentile(np.concatenate(values), 99.0)) if values else 0.0,
    )

    figure, axes = plt.subplots(
        1,
        len(candidates),
        figsize=(6.0 * len(candidates), 4.8),
        squeeze=False,
        layout="constrained",
    )
    artist = None
    for axis, version in zip(axes[0], candidates, strict=True):
        field = fields[version]
        difference = differences[version]
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
            f"{VERSION_LABELS[version]} − Original\n"
            f"max |ΔTL| = {maximum:.4g} dB"
        )
    assert artist is not None
    figure.colorbar(
        artist,
        ax=list(axes[0]),
        orientation="horizontal",
        shrink=0.72,
        pad=0.08,
        label="TL difference (dB)",
    )
    output = figures_root / f"{stem}_difference.png"
    figure.savefig(output, dpi=dpi, bbox_inches="tight")
    plt.close(figure)
    return output


def plot_results(
    *,
    environment: Path,
    versions: tuple[str, ...],
    results_root: Path,
    figures_root: Path,
    dpi: int,
) -> Path:
    figures_root.mkdir(parents=True, exist_ok=True)
    outputs = {
        version: output_paths(results_root, version, environment.stem)
        for version in versions
    }
    fields = {version: validate_output(output) for version, output in outputs.items()}
    frequencies = {field.frequency_hz for field in fields.values()}
    if len(frequencies) != 1:
        raise ValueError("the three SHD files do not use the same frequency")
    frequency_hz = frequencies.pop()
    frequency_text = format(frequency_hz, ".12g").replace(".", "p")
    stem = f"{environment.stem}_{frequency_text}Hz"
    field_figure = save_field_comparison(
        fields=fields,
        versions=versions,
        figures_root=figures_root,
        stem=stem,
        dpi=dpi,
    )

    metrics: dict[str, object] = {}
    difference_figure: Path | None = None
    candidates = tuple(version for version in versions if version != "origin")
    if "origin" in versions and candidates:
        reference = outputs["origin"]
        for version in candidates:
            passed, values = compare_files(
                reference.shade,
                outputs[version].shade,
                0,
                0,
                STANDARD_CODES_ROOT / "tolerances.toml",
            )
            metrics[version] = {"passed": passed, **values}
        difference_figure = save_difference_comparison(
            fields=fields,
            candidates=candidates,
            figures_root=figures_root,
            stem=stem,
            dpi=dpi,
        )

    summary = {
        "schema": "bellhop.direct_showcase.figures",
        "schema_version": 1,
        "environment": str(environment),
        "frequency_hz": frequency_hz,
        "versions": list(versions),
        "field_figure": str(field_figure),
        "difference_figure": (
            str(difference_figure) if difference_figure is not None else None
        ),
        "comparisons_to_origin": metrics,
    }
    summary_suffix = f"_{versions[0]}" if len(versions) == 1 else ""
    summary_path = figures_root / f"{stem}{summary_suffix}_summary.json"
    summary_path.write_text(
        json.dumps(summary, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    print(field_figure)
    if difference_figure is not None:
        print(difference_figure)
    print(summary_path)
    return summary_path


def validate_route_result(
    results_root: Path, route: str, workers: int, environment: Path
) -> ShdReader:
    output = output_paths(
        results_root, route_directory(route, workers), environment.stem
    )
    if not output.print_log.is_file() or not output.shade.is_file():
        raise FileNotFoundError(
            f"{route}: PRT/SHD result is missing under {output.root.parent}"
        )
    print_text = output.print_log.read_text(errors="replace")
    if "FATAL ERROR" in print_text:
        raise RuntimeError(f"{route}: Bellhop Broadband reported FATAL ERROR")
    for marker in expected_prt_markers(route):
        if marker not in print_text:
            raise RuntimeError(f"{route}: PRT marker missing: {marker}")
    reader = ShdReader(output.shade)
    if reader.header.dimensions[0] < 2:
        raise RuntimeError(f"{route}: route comparison requires a multi-frequency ENV")
    for index in range(reader.header.dimensions[0]):
        pressure = reader.read(frequency_index=index).pressure
        if not np.isfinite(pressure).all() or not np.any(pressure):
            raise RuntimeError(f"{route}: invalid pressure at frequency index {index}")
    return reader


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
        per_frequency = [
            difference_metrics(
                readers[route].read(frequency_index=index),
                readers["nonreuse"].read(frequency_index=index),
            )
            for index in range(frequency_count)
        ]
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


def plot_execution_routes(
    *,
    environment: Path,
    workers: int,
    results_root: Path,
    figures_root: Path,
    frequency_index: int,
    dpi: int,
) -> Path:
    readers = {
        route: validate_route_result(results_root, route, workers, environment)
        for route in ROUTES
    }
    frequency_sets = {
        tuple(float(value) for value in reader.header.frequencies_hz)
        for reader in readers.values()
    }
    if len(frequency_sets) != 1:
        raise ValueError("the route SHD files do not use the same frequency list")
    frequency_count = readers[ROUTES[0]].header.dimensions[0]
    if not 0 <= frequency_index < frequency_count:
        raise ValueError(
            f"frequency index out of range: {frequency_index} "
            f"(0..{frequency_count - 1})"
        )
    fields = {
        route: readers[route].read(frequency_index=frequency_index)
        for route in ROUTES
    }
    limits = common_tl_limits(tuple(fields.values()))

    import matplotlib.pyplot as plt

    figure, axes = plt.subplots(
        2,
        2,
        figsize=(10.4, 9.2),
        squeeze=False,
        layout="constrained",
    )
    artist = None
    for axis, route in zip(axes.flat, ROUTES, strict=True):
        artist = draw_tl(axis, fields[route], route_label(route, workers), limits)
    assert artist is not None
    figure.colorbar(
        artist,
        ax=list(axes.flat),
        orientation="horizontal",
        shrink=0.72,
        pad=0.06,
        label="Transmission loss (dB)",
    )
    frequency_hz = fields[ROUTES[0]].frequency_hz
    title = fields[ROUTES[0]].header.title.rstrip().replace("_", " ")
    figure.suptitle(
        f"{title} — Broadband execution routes — {frequency_hz:g} Hz"
    )
    figures_root.mkdir(parents=True, exist_ok=True)
    frequency_text = format(frequency_hz, ".12g").replace(".", "p")
    stem = f"{environment.stem}_{frequency_text}Hz"
    output = figures_root / f"{stem}_routes_comparison.png"
    figure.savefig(output, dpi=dpi, bbox_inches="tight")
    plt.close(figure)

    summary = {
        "schema": "bellhop.reliability.routes_figures",
        "schema_version": 2,
        "environment": str(environment),
        "routes": [
            {
                "route": route,
                "directory": route_directory(route, workers),
                "arguments": route_arguments(route, workers),
            }
            for route in ROUTES
        ],
        "reuse_workers": workers,
        "frequencies_hz": [
            float(value) for value in readers[ROUTES[0]].header.frequencies_hz
        ],
        "selected_frequency_index": frequency_index,
        "selected_frequency_hz": frequency_hz,
        "figure": str(output),
        "comparisons_to_nonreuse": route_comparison_metrics(readers, ROUTES),
    }
    summary_path = figures_root / f"{stem}_routes_summary.json"
    summary_path.write_text(
        json.dumps(summary, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    print(output)
    print(summary_path)
    return summary_path


def add_common_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "--environment",
        type=Path,
        default=DEMO_ROOT / "cases" / "reliability" / "munk_cerveny_cc.env",
    )
    parser.add_argument("--versions", default=",".join(VERSIONS))
    parser.add_argument(
        "--results-root",
        type=Path,
        default=DEMO_ROOT / "results" / "reliability",
    )
    parser.add_argument("--origin-executable", type=Path)
    parser.add_argument("--f2cpp-executable", type=Path)
    parser.add_argument("--broadband-executable", type=Path)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Directly run one ENV with the three Bellhop executables."
    )
    commands = parser.add_subparsers(dest="command", required=True)
    for command in ("check", "run", "plot", "show"):
        command_parser = commands.add_parser(command)
        add_common_arguments(command_parser)
        if command in ("plot", "show"):
            command_parser.add_argument(
                "--figures-root",
                type=Path,
                default=DEMO_ROOT / "figures" / "reliability",
            )
            command_parser.add_argument("--dpi", type=int, default=180)
    routes_parser = commands.add_parser(
        "routes",
        help=(
            "Plot the 2x2 TL comparison of the execution-mode routes "
            "from a speed run."
        ),
    )
    routes_parser.add_argument(
        "--environment", type=Path, default=DEFAULT_ROUTE_ENVIRONMENT
    )
    routes_parser.add_argument("--workers", type=int, default=4)
    routes_parser.add_argument("--frequency-index", type=int, default=2)
    routes_parser.add_argument(
        "--results-root",
        type=Path,
        default=DEMO_ROOT / "results" / "speed",
    )
    routes_parser.add_argument(
        "--figures-root", type=Path, default=DEMO_ROOT / "figures" / "reliability"
    )
    routes_parser.add_argument("--dpi", type=int, default=180)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    arguments = build_parser().parse_args(argv)
    try:
        if arguments.command == "routes":
            workers = parse_workers(arguments.workers)
            environment = arguments.environment.expanduser().resolve()
            results_root = arguments.results_root.expanduser().resolve()
            plot_execution_routes(
                environment=environment,
                workers=workers,
                results_root=results_root,
                figures_root=arguments.figures_root.expanduser().resolve(),
                frequency_index=arguments.frequency_index,
                dpi=arguments.dpi,
            )
            return 0
        versions = parse_versions(arguments.versions)
        environment = arguments.environment.expanduser().resolve()
        results_root = arguments.results_root.expanduser().resolve()
        executables = executable_paths(arguments)
        if arguments.command == "check":
            validate_inputs(environment, versions, executables)
            print(f"environment: READY ({environment})")
            for version in versions:
                print(f"{version}: READY ({executables[version]})")
            return 0
        if arguments.command in ("run", "show"):
            run_models(
                environment=environment,
                versions=versions,
                results_root=results_root,
                executables=executables,
            )
        if arguments.command in ("plot", "show"):
            plot_results(
                environment=environment,
                versions=versions,
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
        print(f"showcase: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
