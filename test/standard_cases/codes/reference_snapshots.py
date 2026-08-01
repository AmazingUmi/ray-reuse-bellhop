"""Create compact, reviewable numerical references from Bellhop SHD files."""

from __future__ import annotations

import argparse
import hashlib
import itertools
import json
import math
from pathlib import Path
import sys
from typing import Sequence


CODES_ROOT = Path(__file__).resolve().parent
STANDARD_CASES_ROOT = CODES_ROOT.parent
PROJECT_ROOT = STANDARD_CASES_ROOT.parents[1]
sys.path.insert(0, str(PROJECT_ROOT / "test" / "PlotRead"))

import numpy as np

from bellhop_io_py.shd import ShdReader


SCHEMA = "bellhop.standard_case.compact_reference"
SCHEMA_VERSION = 1


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def spaced_indices(size: int, maximum_count: int) -> tuple[int, ...]:
    if size <= 0:
        raise ValueError("axis size must be positive")
    if maximum_count <= 0:
        raise ValueError("maximum sample count must be positive")
    if maximum_count == 1:
        return (0,)
    if size <= maximum_count:
        return tuple(range(size))
    return tuple(
        sorted(
            {
                round(index * (size - 1) / (maximum_count - 1))
                for index in range(maximum_count)
            }
        )
    )


def select_samples(
    pressure: np.ndarray,
) -> list[tuple[tuple[int, int, int, int], tuple[str, ...]]]:
    if pressure.ndim != 4 or any(size <= 0 for size in pressure.shape):
        raise ValueError("pressure must have four non-empty dimensions")
    if not np.isfinite(pressure).all():
        raise ValueError("pressure contains non-finite values")
    if not np.any(pressure):
        raise ValueError("pressure is entirely zero")

    index_labels: dict[tuple[int, int, int, int], set[str]] = {}
    axes = (
        spaced_indices(pressure.shape[0], 3),
        spaced_indices(pressure.shape[1], 3),
        spaced_indices(pressure.shape[2], 3),
        spaced_indices(pressure.shape[3], 5),
    )
    for indices in itertools.product(*axes):
        index_labels.setdefault(indices, set()).add("grid")

    maximum_index = tuple(
        int(value)
        for value in np.unravel_index(
            int(np.argmax(np.abs(pressure))), pressure.shape
        )
    )
    index_labels.setdefault(maximum_index, set()).add("max_magnitude")
    return [
        (indices, tuple(sorted(labels)))
        for indices, labels in sorted(index_labels.items())
    ]


def create_snapshot(
    *,
    case_id: str,
    profile: str,
    oracle_version: str,
    source_revision: str,
    environment_path: Path,
    shade_path: Path,
    executable_path: Path,
    frequency_index: int = 0,
) -> dict[str, object]:
    for label, path in (
        ("environment", environment_path),
        ("shade", shade_path),
        ("executable", executable_path),
    ):
        if not path.is_file():
            raise FileNotFoundError(f"{label} file does not exist: {path}")
    if not case_id or not profile or not oracle_version or not source_revision:
        raise ValueError("snapshot identity fields must not be empty")

    reader = ShdReader(shade_path)
    field = reader.read(frequency_index=frequency_index)
    header = field.header
    samples: list[dict[str, object]] = []
    for indices, labels in select_samples(field.pressure):
        bearing_index, source_depth_index, depth_index, range_index = indices
        pressure = complex(field.pressure[indices])
        magnitude = abs(pressure)
        samples.append(
            {
                "id": (
                    f"b{bearing_index:03d}_sz{source_depth_index:03d}_"
                    f"rz{depth_index:03d}_rr{range_index:03d}"
                ),
                "selection": list(labels),
                "indices": {
                    "bearing": bearing_index,
                    "source_depth": source_depth_index,
                    "receiver_depth": depth_index,
                    "receiver_range": range_index,
                },
                "coordinates": {
                    "bearing_deg": float(
                        header.bearings_deg[bearing_index]
                    ),
                    "source_depth_m": float(
                        header.source_depths_m[source_depth_index]
                    ),
                    "receiver_depth_m": float(
                        header.receiver_depths_m[depth_index]
                    ),
                    "receiver_range_m": float(
                        header.receiver_ranges_m[range_index]
                    ),
                },
                "pressure": {
                    "real": pressure.real,
                    "imag": pressure.imag,
                    "magnitude": magnitude,
                    "transmission_loss_db": (
                        -20.0 * math.log10(magnitude)
                        if magnitude > 0.0
                        else None
                    ),
                },
            }
        )

    return {
        "schema": SCHEMA,
        "schema_version": SCHEMA_VERSION,
        "case_id": case_id,
        "profile": profile,
        "oracle_version": oracle_version,
        "source_revision": source_revision,
        "frequency_hz": field.frequency_hz,
        "frequency_index": frequency_index,
        "source_position_m": {
            "x": field.source_x_m,
            "y": field.source_y_m,
        },
        "sample_policy": {
            "bearing_max_count": 3,
            "source_depth_max_count": 3,
            "receiver_depth_max_count": 3,
            "receiver_range_max_count": 5,
            "include_max_magnitude": True,
        },
        "shd": {
            "title": header.title,
            "plot_type": header.plot_type,
            "dimensions": list(header.dimensions),
            "byte_order": header.byte_order,
        },
        "artifacts": {
            "environment": {
                "name": environment_path.name,
                "sha256": sha256_file(environment_path),
            },
            "shade": {
                "name": shade_path.name,
                "sha256": sha256_file(shade_path),
            },
            "executable": {
                "name": executable_path.name,
                "sha256": sha256_file(executable_path),
            },
        },
        "samples": samples,
    }


def create_from_manifest(
    manifest_path: Path, source_revision: str
) -> dict[str, object]:
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("schema_version") != 1:
        raise ValueError("unsupported run manifest schema")
    runs = manifest.get("runs")
    if not isinstance(runs, list) or len(runs) != 1:
        raise ValueError("compact reference generation requires one run")
    run = runs[0]
    if not isinstance(run, dict) or run.get("status") != "passed":
        raise ValueError("run manifest must describe one passed run")
    if manifest.get("profile") != "single":
        raise ValueError("initial compact references require profile 'single'")

    result_root = manifest_path.parent
    environment_name = run.get("environment_file")
    shade_name = run.get("shade_file")
    executable_name = manifest.get("executable")
    if not all(
        isinstance(value, str) and value
        for value in (environment_name, shade_name, executable_name)
    ):
        raise ValueError("run manifest lacks required artifact paths")
    return create_snapshot(
        case_id=str(manifest["case_id"]),
        profile=str(manifest["profile"]),
        oracle_version=str(manifest["version"]),
        source_revision=source_revision,
        environment_path=result_root / environment_name,
        shade_path=result_root / shade_name,
        executable_path=Path(executable_name),
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Create compact numerical references from passed runs."
    )
    subparsers = parser.add_subparsers(dest="command", required=True)
    generate = subparsers.add_parser("generate")
    generate.add_argument("manifest", type=Path)
    generate.add_argument("output", type=Path)
    generate.add_argument("--source-revision", required=True)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        snapshot = create_from_manifest(
            args.manifest.resolve(), args.source_revision
        )
        output = args.output.resolve()
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(
            json.dumps(
                snapshot,
                ensure_ascii=False,
                indent=2,
                sort_keys=True,
                allow_nan=False,
            )
            + "\n",
            encoding="utf-8",
        )
        print(f"Wrote compact reference: {output}")
        return 0
    except (FileNotFoundError, KeyError, OSError, ValueError) as error:
        print(f"reference_snapshots.py: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
