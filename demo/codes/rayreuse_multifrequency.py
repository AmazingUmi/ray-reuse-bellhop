"""Direct multi-frequency RayReuse showcase and selected TL plots."""

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


CODES_ROOT = Path(__file__).resolve().parent
DEMO_ROOT = CODES_ROOT.parent
PROJECT_ROOT = DEMO_ROOT.parent
PLOTREAD_ROOT = PROJECT_ROOT / "test" / "PlotRead"
sys.path.insert(0, str(PLOTREAD_ROOT))
sys.path.insert(0, str(CODES_ROOT))

import numpy as np

from bellhop_io_py.shd import PressureField, ShdReader
from reliability import common_tl_limits, draw_tl, output_paths


EXECUTION_MODES = ("nonreuse", "reuse", "parallel")
DEFAULT_EXECUTABLE = (
    PROJECT_ROOT
    / "Bellhop_RayReuse"
    / "build"
    / "release"
    / "bellhop_rayreuse"
)
RESULT_VERSION = "rayreuse_multifrequency"


def parse_indexes(value: str, frequency_count: int) -> tuple[int, ...]:
    try:
        indexes = tuple(int(item.strip()) for item in value.split(",") if item.strip())
    except ValueError as error:
        raise ValueError("plot indexes must be comma-separated integers") from error
    if not indexes:
        raise ValueError("at least one plot index is required")
    if len(set(indexes)) != len(indexes):
        raise ValueError("plot indexes must not contain duplicates")
    invalid = [index for index in indexes if index < 0 or index >= frequency_count]
    if invalid:
        raise ValueError(f"plot indexes out of range: {invalid}")
    return indexes


def validate_environment(environment: Path, executable: Path) -> None:
    if not environment.is_file():
        raise FileNotFoundError(f"multi-frequency ENV not found: {environment}")
    if environment.suffix.lower() != ".env":
        raise ValueError("multi-frequency input must be a .env file")
    if not executable.is_file():
        raise FileNotFoundError(f"RayReuse executable not found: {executable}")


def validate_result(results_root: Path, environment: Path):
    output = output_paths(results_root, RESULT_VERSION, environment.stem)
    if not output.print_log.is_file() or not output.shade.is_file():
        raise FileNotFoundError("multi-frequency PRT/SHD result is missing")
    print_text = output.print_log.read_text(errors="replace")
    if "FATAL ERROR" in print_text:
        raise RuntimeError("RayReuse reported FATAL ERROR")
    reader = ShdReader(output.shade)
    if reader.header.dimensions[0] < 2:
        raise RuntimeError("RayReuse showcase result is not multi-frequency")
    for index in range(reader.header.dimensions[0]):
        pressure = reader.read(frequency_index=index).pressure
        if not np.isfinite(pressure).all() or not np.any(pressure):
            raise RuntimeError(f"invalid pressure at frequency index {index}")
    return output, reader, print_text


def run_multifrequency(
    *,
    environment: Path,
    executable: Path,
    execution_mode: str,
    results_root: Path,
) -> Path:
    validate_environment(environment, executable)
    version_directory = results_root / RESULT_VERSION
    version_directory.mkdir(parents=True, exist_ok=True)
    output = output_paths(results_root, RESULT_VERSION, environment.stem)
    shutil.copy2(environment, output.environment)
    for stale_path in (output.print_log, output.shade):
        if stale_path.exists():
            stale_path.unlink()

    print(
        f"[Bellhop RayReuse/{execution_mode}] {output.environment.name} -> "
        f"{output.shade.name}",
        flush=True,
    )
    started = time.perf_counter()
    subprocess.run(
        [
            str(executable),
            output.root.name,
            "--execution-mode",
            execution_mode,
        ],
        cwd=version_directory,
        check=True,
    )
    elapsed = time.perf_counter() - started
    _, reader, print_text = validate_result(results_root, environment)
    expected_marker = {
        "nonreuse": "execution mode = broadband non-reuse",
        "reuse": "execution mode = broadband reuse",
        "parallel": "execution mode = broadband parallel reuse",
    }[execution_mode]
    if expected_marker not in print_text:
        raise RuntimeError(f"PRT execution-mode marker missing: {expected_marker}")

    report = {
        "schema": "bellhop.rayreuse.multifrequency_run",
        "schema_version": 1,
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "executable": str(executable),
        "environment": str(output.environment),
        "print_log": str(output.print_log),
        "shade": str(output.shade),
        "execution_mode": execution_mode,
        "frequencies_hz": [
            float(value) for value in reader.header.frequencies_hz
        ],
        "elapsed_seconds": elapsed,
        "status": "passed",
    }
    report_path = version_directory / "run_summary.json"
    report_path.write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    print(report_path)
    return report_path


def plot_selected_frequencies(
    *,
    environment: Path,
    results_root: Path,
    figures_root: Path,
    index_text: str,
    dpi: int,
) -> Path:
    _, reader, _ = validate_result(results_root, environment)
    indexes = parse_indexes(index_text, reader.header.dimensions[0])
    fields: list[PressureField] = [
        reader.read(frequency_index=index) for index in indexes
    ]
    limits = common_tl_limits(fields)

    import matplotlib.pyplot as plt

    figure, axes = plt.subplots(
        1,
        len(indexes),
        figsize=(5.2 * len(indexes), 4.8),
        squeeze=False,
        layout="constrained",
    )
    artist = None
    for axis, index, field in zip(axes[0], indexes, fields, strict=True):
        title = f"f{index:03d} — {field.frequency_hz:g} Hz"
        artist = draw_tl(axis, field, title, limits)
    assert artist is not None
    figure.colorbar(
        artist,
        ax=list(axes[0]),
        orientation="horizontal",
        shrink=0.72,
        pad=0.08,
        label="Transmission loss (dB)",
    )
    figure.suptitle("Bellhop RayReuse — one multi-frequency SHD")
    figures_root.mkdir(parents=True, exist_ok=True)
    output = figures_root / f"{environment.stem}_selected_tl.png"
    figure.savefig(output, dpi=dpi, bbox_inches="tight")
    plt.close(figure)

    summary = {
        "schema": "bellhop.rayreuse.multifrequency_figures",
        "schema_version": 1,
        "environment": str(environment),
        "shade": str(
            output_paths(results_root, RESULT_VERSION, environment.stem).shade
        ),
        "frequencies_hz": [
            float(value) for value in reader.header.frequencies_hz
        ],
        "selected_frequency_indexes": list(indexes),
        "selected_frequencies_hz": [field.frequency_hz for field in fields],
        "figure": str(output),
    }
    summary_path = figures_root / f"{environment.stem}_summary.json"
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
        default=(
            DEMO_ROOT
            / "cases"
            / "rayreuse_multifrequency"
            / "munk_rayreuse_multifrequency.env"
        ),
    )
    parser.add_argument("--executable", type=Path, default=DEFAULT_EXECUTABLE)
    parser.add_argument(
        "--execution-mode", choices=EXECUTION_MODES, default="reuse"
    )
    parser.add_argument("--results-root", type=Path, default=DEMO_ROOT / "results")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Run a RayReuse multi-frequency ENV and plot selected slices."
    )
    commands = parser.add_subparsers(dest="command", required=True)
    for command in ("check", "run", "plot", "show"):
        command_parser = commands.add_parser(command)
        add_common_arguments(command_parser)
        if command in ("plot", "show"):
            command_parser.add_argument("--plot-indexes", default="0,2,4")
            command_parser.add_argument(
                "--figures-root",
                type=Path,
                default=DEMO_ROOT / "figures" / "rayreuse_multifrequency",
            )
            command_parser.add_argument("--dpi", type=int, default=180)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    arguments = build_parser().parse_args(argv)
    try:
        environment = arguments.environment.expanduser().resolve()
        executable = arguments.executable.expanduser().resolve()
        results_root = arguments.results_root.expanduser().resolve()
        if arguments.command == "check":
            validate_environment(environment, executable)
            print(f"environment: READY ({environment})")
            print(f"rayreuse: READY ({executable})")
            return 0
        if arguments.command in ("run", "show"):
            run_multifrequency(
                environment=environment,
                executable=executable,
                execution_mode=arguments.execution_mode,
                results_root=results_root,
            )
        if arguments.command in ("plot", "show"):
            plot_selected_frequencies(
                environment=environment,
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
        print(f"rayreuse multifrequency showcase: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
