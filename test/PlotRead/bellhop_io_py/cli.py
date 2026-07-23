"""Command-line interface for inspecting, plotting, and exporting SHD files."""

from __future__ import annotations

import argparse
from pathlib import Path
import sys

import numpy as np

from .plotting import plot_field
from .shd import PressureField, ShdFormatError, ShdReader, read_shd


def _add_selection_arguments(parser: argparse.ArgumentParser) -> None:
    frequency = parser.add_mutually_exclusive_group()
    frequency.add_argument("--frequency", type=float, help="nearest frequency in Hz")
    frequency.add_argument(
        "--frequency-index", type=int, help="zero-based frequency index"
    )
    parser.add_argument("--source-x-km", type=float)
    parser.add_argument("--source-y-km", type=float)


def _read_selection(arguments: argparse.Namespace) -> PressureField:
    return read_shd(
        arguments.file,
        frequency_hz=arguments.frequency,
        frequency_index=arguments.frequency_index,
        source_x_km=arguments.source_x_km,
        source_y_km=arguments.source_y_km,
    )


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="bellhop-shd", description="Read and plot Acoustic Toolbox SHD files"
    )
    commands = parser.add_subparsers(dest="command", required=True)

    info = commands.add_parser("info", help="print validated SHD metadata")
    info.add_argument("file", type=Path)

    plot = commands.add_parser("plot", help="plot one pressure-field slice")
    plot.add_argument("file", type=Path)
    _add_selection_arguments(plot)
    plot.add_argument("--bearing-index", type=int, default=0)
    plot.add_argument("--source-depth-index", type=int, default=0)
    plot.add_argument("--range-unit", choices=("m", "km"), default="km")
    plot.add_argument("-o", "--output", type=Path, help="write an image instead of opening a window")
    plot.add_argument("--dpi", type=int, default=160)

    export = commands.add_parser("export", help="export one slice as compressed NumPy data")
    export.add_argument("file", type=Path)
    export.add_argument("output", type=Path)
    _add_selection_arguments(export)
    return parser


def _print_info(path: Path) -> None:
    is_ascii = path.name.upper() == "ASCFIL" or path.suffix.lower() in {".asc", ".txt"}
    header = read_shd(path).header if is_ascii else ShdReader(path).header
    labels = (
        "frequency",
        "bearing",
        "source x",
        "source y",
        "source depth",
        "receiver depth",
        "receiver range",
    )
    print(f"title: {header.title}")
    print(f"plot type: {header.plot_type}")
    print(f"byte order: {header.byte_order}")
    print(f"record bytes: {header.record_bytes}")
    print("dimensions:")
    for label, size in zip(labels, header.dimensions, strict=True):
        print(f"  {label}: {size}")
    print(
        f"frequencies (Hz): {header.frequencies_hz[0]:g}"
        + (
            f" .. {header.frequencies_hz[-1]:g}"
            if header.frequencies_hz.size > 1
            else ""
        )
    )


def _export(field: PressureField, output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    np.savez_compressed(
        output,
        title=field.header.title,
        plot_type=field.header.plot_type,
        frequency_hz=field.frequency_hz,
        frequency_index=field.frequency_index,
        frequencies_hz=field.header.frequencies_hz,
        bearings_deg=field.header.bearings_deg,
        source_x_m=field.source_x_m,
        source_y_m=field.source_y_m,
        source_depths_m=field.header.source_depths_m,
        receiver_depths_m=field.header.receiver_depths_m,
        receiver_ranges_m=field.header.receiver_ranges_m,
        pressure=field.pressure,
    )


def main(argv: list[str] | None = None) -> int:
    arguments = _build_parser().parse_args(argv)
    try:
        if arguments.command == "info":
            _print_info(arguments.file)
        elif arguments.command == "plot":
            field = _read_selection(arguments)
            figure, _, _ = plot_field(
                field,
                bearing_index=arguments.bearing_index,
                source_depth_index=arguments.source_depth_index,
                range_unit=arguments.range_unit,
            )
            if arguments.output:
                arguments.output.parent.mkdir(parents=True, exist_ok=True)
                figure.savefig(arguments.output, dpi=arguments.dpi)
                print(arguments.output)
            else:
                import matplotlib.pyplot as plt

                plt.show()
        elif arguments.command == "export":
            field = _read_selection(arguments)
            _export(field, arguments.output)
            print(arguments.output)
    except (FileNotFoundError, ShdFormatError, ValueError, IndexError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    return 0
