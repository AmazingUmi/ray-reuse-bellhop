"""Create compact, reviewable numerical references from Bellhop SHD files."""

from __future__ import annotations

import argparse
import cmath
import hashlib
import itertools
import json
import math
from pathlib import Path
import sys
import tomllib
from typing import Sequence


CODES_ROOT = Path(__file__).resolve().parent
STANDARD_CASES_ROOT = CODES_ROOT.parent
PROJECT_ROOT = STANDARD_CASES_ROOT.parents[1]
sys.path.insert(0, str(PROJECT_ROOT / "test" / "PlotRead"))

import numpy as np

from bellhop_io_py.shd import ShdReader


SCHEMA = "bellhop.standard_case.compact_reference"
SCHEMA_VERSION = 1
DEFAULT_TOLERANCES = CODES_ROOT / "tolerances.toml"


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


def load_reference(path: Path) -> dict[str, object]:
    document = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(document, dict):
        raise ValueError(f"{path}: reference must be a JSON object")
    if document.get("schema") != SCHEMA:
        raise ValueError(f"{path}: unsupported reference schema")
    if document.get("schema_version") != SCHEMA_VERSION:
        raise ValueError(f"{path}: unsupported reference schema version")
    samples = document.get("samples")
    if not isinstance(samples, list) or not samples:
        raise ValueError(f"{path}: reference samples must be a non-empty list")

    seen_ids: set[str] = set()
    for sample in samples:
        if not isinstance(sample, dict):
            raise ValueError(f"{path}: reference sample must be an object")
        sample_id = sample.get("id")
        if not isinstance(sample_id, str) or not sample_id:
            raise ValueError(f"{path}: reference sample needs an id")
        if sample_id in seen_ids:
            raise ValueError(f"{path}: duplicate sample id {sample_id!r}")
        seen_ids.add(sample_id)
        pressure = sample.get("pressure")
        if not isinstance(pressure, dict):
            raise ValueError(f"{path}: sample {sample_id} lacks pressure")
        try:
            value = complex(
                float(pressure["real"]), float(pressure["imag"])
            )
            stored_magnitude = float(pressure["magnitude"])
        except (KeyError, TypeError, ValueError) as error:
            raise ValueError(
                f"{path}: sample {sample_id} has invalid pressure values"
            ) from error
        magnitude = abs(value)
        if not math.isclose(
            magnitude,
            stored_magnitude,
            rel_tol=1.0e-15,
            abs_tol=1.0e-300,
        ):
            raise ValueError(
                f"{path}: sample {sample_id} magnitude is inconsistent"
            )
        stored_tl = pressure.get("transmission_loss_db")
        if magnitude == 0.0:
            if stored_tl is not None:
                raise ValueError(
                    f"{path}: zero sample {sample_id} must have null TL"
                )
        else:
            expected_tl = -20.0 * math.log10(magnitude)
            if not isinstance(stored_tl, (int, float)) or not math.isclose(
                float(stored_tl),
                expected_tl,
                rel_tol=1.0e-15,
                abs_tol=1.0e-12,
            ):
                raise ValueError(
                    f"{path}: sample {sample_id} TL is inconsistent"
                )
    return document


def load_tolerances(path: Path) -> dict[str, dict[str, float]]:
    with path.open("rb") as stream:
        raw = tomllib.load(stream)
    required = {
        "pressure": ("absolute", "relative", "relative_floor"),
        "transmission_loss": ("absolute_db", "pressure_floor"),
        "phase": ("absolute_rad", "pressure_floor"),
    }
    result: dict[str, dict[str, float]] = {}
    for section, names in required.items():
        values = raw.get(section)
        if not isinstance(values, dict):
            raise ValueError(f"missing tolerance section {section!r}")
        result[section] = {}
        for name in names:
            try:
                value = float(values[name])
            except (KeyError, TypeError, ValueError) as error:
                raise ValueError(
                    f"invalid tolerance {section}.{name}"
                ) from error
            if value < 0.0 or not math.isfinite(value):
                raise ValueError(
                    f"tolerance {section}.{name} must be finite and non-negative"
                )
            result[section][name] = value
    return result


def compare_pressure_values(
    reference: complex,
    candidate: complex,
    tolerances: dict[str, dict[str, float]],
) -> dict[str, float | bool | None]:
    reference_magnitude = abs(reference)
    candidate_magnitude = abs(candidate)
    pressure_rules = tolerances["pressure"]
    absolute_error = abs(candidate - reference)
    relative_error = absolute_error / max(
        reference_magnitude, pressure_rules["relative_floor"]
    )
    pressure_limit = (
        pressure_rules["absolute"]
        + pressure_rules["relative"] * reference_magnitude
    )
    pressure_passed = absolute_error <= pressure_limit

    tl_rules = tolerances["transmission_loss"]
    tl_compared = reference_magnitude > tl_rules["pressure_floor"]
    tl_error: float | None = None
    tl_passed = True
    if tl_compared:
        reference_tl = -20.0 * math.log10(reference_magnitude)
        candidate_tl = -20.0 * math.log10(
            max(candidate_magnitude, tl_rules["pressure_floor"])
        )
        tl_error = abs(candidate_tl - reference_tl)
        tl_passed = tl_error <= tl_rules["absolute_db"]

    phase_rules = tolerances["phase"]
    phase_compared = (
        reference_magnitude > phase_rules["pressure_floor"]
        and candidate_magnitude > phase_rules["pressure_floor"]
    )
    phase_error: float | None = None
    phase_passed = True
    if phase_compared:
        phase_error = abs(cmath.phase(candidate / reference))
        phase_passed = phase_error <= phase_rules["absolute_rad"]

    return {
        "pressure_absolute": absolute_error,
        "pressure_relative": relative_error,
        "pressure_limit": pressure_limit,
        "pressure_passed": pressure_passed,
        "tl_compared": tl_compared,
        "tl_difference_db": tl_error,
        "tl_passed": tl_passed,
        "phase_compared": phase_compared,
        "phase_difference_rad": phase_error,
        "phase_passed": phase_passed,
        "passed": pressure_passed and tl_passed and phase_passed,
    }


def validate_candidate(
    reference_path: Path,
    candidate_path: Path,
    candidate_frequency_index: int,
    tolerances_path: Path,
) -> tuple[bool, dict[str, object]]:
    reference = load_reference(reference_path)
    tolerances = load_tolerances(tolerances_path)
    reader = ShdReader(candidate_path)
    field = reader.read(frequency_index=candidate_frequency_index)

    expected_dimensions = tuple(int(value) for value in reference["shd"]["dimensions"])
    failures: list[dict[str, object]] = []
    if reader.header.dimensions != expected_dimensions:
        failures.append(
            {
                "id": "header",
                "reasons": [
                    f"dimensions {reader.header.dimensions} != {expected_dimensions}"
                ],
            }
        )
    expected_frequency = float(reference["frequency_hz"])
    if not math.isclose(
        field.frequency_hz,
        expected_frequency,
        rel_tol=1.0e-12,
        abs_tol=1.0e-9,
    ):
        failures.append(
            {
                "id": "header",
                "reasons": [
                    f"frequency {field.frequency_hz} != {expected_frequency}"
                ],
            }
        )

    maxima = {
        "pressure_absolute": 0.0,
        "pressure_relative": 0.0,
        "tl_difference_db": 0.0,
        "phase_difference_rad": 0.0,
    }
    compared = {"pressure": 0, "tl": 0, "phase": 0}
    axes = {
        "bearing": reader.header.bearings_deg,
        "source_depth": reader.header.source_depths_m,
        "receiver_depth": reader.header.receiver_depths_m,
        "receiver_range": reader.header.receiver_ranges_m,
    }
    for sample in reference["samples"]:
        sample_id = str(sample["id"])
        indices = sample["indices"]
        index_tuple = (
            int(indices["bearing"]),
            int(indices["source_depth"]),
            int(indices["receiver_depth"]),
            int(indices["receiver_range"]),
        )
        reasons: list[str] = []
        for name, axis in axes.items():
            index = int(indices[name])
            coordinate_name = (
                f"{name}_deg" if name == "bearing" else f"{name}_m"
            )
            expected_coordinate = float(
                sample["coordinates"][coordinate_name]
            )
            if not 0 <= index < axis.size:
                reasons.append(f"{name} index {index} is out of range")
            elif float(axis[index]) != expected_coordinate:
                reasons.append(
                    f"{name} coordinate {float(axis[index])} != {expected_coordinate}"
                )
        if reasons:
            failures.append({"id": sample_id, "reasons": reasons})
            continue

        reference_pressure = complex(
            float(sample["pressure"]["real"]),
            float(sample["pressure"]["imag"]),
        )
        candidate_pressure = complex(field.pressure[index_tuple])
        metrics = compare_pressure_values(
            reference_pressure, candidate_pressure, tolerances
        )
        compared["pressure"] += 1
        maxima["pressure_absolute"] = max(
            maxima["pressure_absolute"],
            float(metrics["pressure_absolute"]),
        )
        maxima["pressure_relative"] = max(
            maxima["pressure_relative"],
            float(metrics["pressure_relative"]),
        )
        if metrics["tl_compared"]:
            compared["tl"] += 1
            maxima["tl_difference_db"] = max(
                maxima["tl_difference_db"],
                float(metrics["tl_difference_db"]),
            )
        if metrics["phase_compared"]:
            compared["phase"] += 1
            maxima["phase_difference_rad"] = max(
                maxima["phase_difference_rad"],
                float(metrics["phase_difference_rad"]),
            )
        if not metrics["passed"]:
            if not metrics["pressure_passed"]:
                reasons.append("pressure tolerance exceeded")
            if not metrics["tl_passed"]:
                reasons.append("TL tolerance exceeded")
            if not metrics["phase_passed"]:
                reasons.append("phase tolerance exceeded")
            failures.append(
                {"id": sample_id, "reasons": reasons, "metrics": metrics}
            )

    report: dict[str, object] = {
        "reference": str(reference_path),
        "candidate": str(candidate_path),
        "case_id": reference["case_id"],
        "frequency_hz": expected_frequency,
        "sample_count": len(reference["samples"]),
        "compared": compared,
        "maxima": maxima,
        "failures": failures,
        "passed": not failures,
    }
    return not failures, report


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Create compact numerical references from passed runs."
    )
    subparsers = parser.add_subparsers(dest="command", required=True)
    generate = subparsers.add_parser("generate")
    generate.add_argument("manifest", type=Path)
    generate.add_argument("output", type=Path)
    generate.add_argument("--source-revision", required=True)
    check = subparsers.add_parser("check")
    check.add_argument("references", nargs="+", type=Path)
    validate = subparsers.add_parser("validate")
    validate.add_argument("reference", type=Path)
    validate.add_argument("candidate", type=Path)
    validate.add_argument("--candidate-frequency-index", type=int, default=0)
    validate.add_argument(
        "--tolerances", type=Path, default=DEFAULT_TOLERANCES
    )
    validate.add_argument("--report", type=Path)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        if args.command == "generate":
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
        if args.command == "check":
            for reference in args.references:
                load_reference(reference.resolve())
            print(f"Checked {len(args.references)} compact reference(s)")
            return 0
        if args.command == "validate":
            passed, report = validate_candidate(
                args.reference.resolve(),
                args.candidate.resolve(),
                args.candidate_frequency_index,
                args.tolerances.resolve(),
            )
            contents = json.dumps(
                report,
                ensure_ascii=False,
                indent=2,
                sort_keys=True,
                allow_nan=False,
            )
            print(contents)
            if args.report is not None:
                report_path = args.report.resolve()
                report_path.parent.mkdir(parents=True, exist_ok=True)
                report_path.write_text(contents + "\n", encoding="utf-8")
            return 0 if passed else 1
        raise AssertionError(f"unhandled command: {args.command}")
    except (
        FileNotFoundError,
        IndexError,
        KeyError,
        OSError,
        TypeError,
        ValueError,
    ) as error:
        print(f"reference_snapshots.py: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
