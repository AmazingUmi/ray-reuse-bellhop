#!/usr/bin/env python3
"""Semantic, ordered Origin/F2CPP validation for I8 arrivals products.

The validator deliberately consumes the independent ARR readers rather than
the C++ writer.  It compares the complete source-major stream; it never sorts
records or pairs arrivals by nearest delay.
"""

from __future__ import annotations

import argparse
from dataclasses import fields
import hashlib
import json
import math
from pathlib import Path
import struct
import sys

from arrivals_io import ArrivalRecord, ArrivalsProduct, parse_ascii_arrivals, parse_binary_arrivals
from case_model import discover_cases


CASES = (
    "arrival_geometric_hat_ascii",
    "arrival_geometric_hat_binary",
    "arrival_geometric_hat_ray_centered",
    "arrival_geometric_hat_ray_centered_binary",
    "arrival_geometric_gaussian_irregular",
    "arrival_line_directional_multisource",
    "arrival_zero",
)
RAYREUSE_CASES = (
    "arrival_geometric_hat_ascii",
    "arrival_geometric_hat_binary",
    "arrival_geometric_hat_ray_centered",
    "arrival_geometric_hat_ray_centered_binary",
    "arrival_zero",
)
ULP_LIMIT = 8
CODES_ROOT = Path(__file__).resolve().parent
STANDARD_CASES_ROOT = CODES_ROOT.parent
PROJECT_ROOT = STANDARD_CASES_ROOT.parents[1]


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_hashes(references: tuple[str, ...]) -> dict[str, str]:
    paths = {reference.split(" ", 1)[0] for reference in references}
    result: dict[str, str] = {}
    for relative in sorted(paths):
        path = PROJECT_ROOT / relative
        if not path.is_file():
            raise ValueError(f"referenced Origin source is absent: {path}")
        result[relative] = sha256(path)
    return result


def float32_bits(value: float) -> int:
    return struct.unpack("<I", struct.pack("<f", float(value)))[0]


def float32_ulp(left: float, right: float) -> int:
    if not math.isfinite(left) or not math.isfinite(right):
        raise ValueError("non-finite comparison field")
    a, b = float32_bits(left), float32_bits(right)
    # Map IEEE bit patterns to monotonically ordered signed integers.
    a = (~a + 1) & 0xFFFFFFFF if a & 0x80000000 else a | 0x80000000
    b = (~b + 1) & 0xFFFFFFFF if b & 0x80000000 else b | 0x80000000
    return abs(a - b)


def float64_ulp(left: float, right: float) -> int:
    if not math.isfinite(left) or not math.isfinite(right):
        raise ValueError("non-finite binary64 comparison field")
    a = struct.unpack("<Q", struct.pack("<d", left))[0]
    b = struct.unpack("<Q", struct.pack("<d", right))[0]
    a = (~a + 1) & 0xFFFFFFFFFFFFFFFF if a & (1 << 63) else a | (1 << 63)
    b = (~b + 1) & 0xFFFFFFFFFFFFFFFF if b & (1 << 63) else b | (1 << 63)
    return abs(a - b)


def _manifest(results_root: Path, version: str, case_id: str) -> tuple[Path, dict[str, object]]:
    path = results_root / version / case_id / "single" / "run_manifest.json"
    if not path.is_file():
        raise ValueError(f"missing manifest: {path}")
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as error:
        raise ValueError(f"invalid manifest: {path}") from error
    if data.get("version") != version or data.get("case_id") != case_id:
        raise ValueError(f"{path}: manifest identity mismatch")
    if data.get("output_kind") not in {"arrivals_ascii", "arrivals_binary"}:
        raise ValueError(f"{path}: expected arrivals output kind")
    if data.get("last_stage") != "test":
        raise ValueError(f"{path}: product is not a fresh validated test run")
    return path, data


def _product_path(root: Path, manifest_path: Path, manifest: dict[str, object]) -> tuple[Path, Path]:
    runs = manifest.get("runs")
    if not isinstance(runs, list) or len(runs) != 1 or not isinstance(runs[0], dict):
        raise ValueError(f"{manifest_path}: expected exactly one frequency run")
    run = runs[0]
    raw_product = run.get("arrival_file")
    raw_env = run.get("environment_file")
    if not isinstance(raw_product, str) or not isinstance(raw_env, str):
        raise ValueError(f"{manifest_path}: arrival/environment record is absent")
    case_root = manifest_path.parent
    product, environment = case_root / raw_product, case_root / raw_env
    if not product.is_file() or not environment.is_file():
        raise ValueError(f"{manifest_path}: referenced product or environment is absent")
    expected_hash = run.get("product_sha256")
    if not isinstance(expected_hash, str) or sha256(product) != expected_hash:
        raise ValueError(f"{manifest_path}: product hash mismatch")
    return product, environment


def _require_fresh(product: Path, environment: Path, executable: Path, manifest_path: Path) -> None:
    if not executable.is_file():
        raise ValueError(f"missing executable: {executable}")
    newest_input = max(environment.stat().st_mtime_ns, executable.stat().st_mtime_ns)
    if product.stat().st_mtime_ns < newest_input:
        raise ValueError(f"{manifest_path}: stale product predates executable or rendered ENV")


def _require_executable_identity(manifest: dict[str, object], executable: Path, manifest_path: Path) -> None:
    if Path(str(manifest.get("executable"))).resolve() != executable.resolve():
        raise ValueError(f"{manifest_path}: executable identity mismatch")


def _parse(path: Path, kind: str, cell_count: int | None) -> ArrivalsProduct:
    if kind == "arrivals_ascii":
        return parse_ascii_arrivals(path, receiver_cell_count=cell_count)
    return parse_binary_arrivals(path, receiver_cell_count=cell_count)


def _require_equal_header(left: ArrivalsProduct, right: ArrivalsProduct, label: str) -> None:
    a, b = left.header, right.header
    if a.dimension != b.dimension or a.source_count != b.source_count or a.receiver_cell_count != b.receiver_cell_count:
        raise ValueError(f"{label}: ARR header structure differs")
    for name in ("frequency_hz", "source_x_m", "source_y_m", "source_depths_m", "receiver_depths_m", "receiver_ranges_m", "receiver_bearings_deg"):
        first, second = getattr(a, name), getattr(b, name)
        first_values = (first,) if isinstance(first, float) else first
        second_values = (second,) if isinstance(second, float) else second
        if len(first_values) != len(second_values):
            raise ValueError(f"{label}: ARR header {name} dimensions differ")
        pairs = zip(first_values, second_values)
        distance = float64_ulp if name == "receiver_ranges_m" else float32_ulp
        if any(distance(x, y) > (0 if name == "receiver_ranges_m" else ULP_LIMIT) for x, y in pairs):
            raise ValueError(f"{label}: ARR header {name} differs")


def compare_arrival_products(left: ArrivalsProduct, right: ArrivalsProduct, label: str) -> dict[str, object]:
    """Compare all ordered ARR semantics and return field-wise maximum ULPs."""
    _require_equal_header(left, right, label)
    if len(left.sources) != len(right.sources):
        raise ValueError(f"{label}: source body count differs")
    maxima = {field.name: {"ulp": 0, "location": None} for field in fields(ArrivalRecord) if field.name not in {"top_bounces", "bottom_bounces"}}
    records = 0
    for source_index, (a_source, b_source) in enumerate(zip(left.sources, right.sources)):
        if a_source.maximum_arrivals != b_source.maximum_arrivals:
            raise ValueError(f"{label}: source {source_index} maximum-arrivals differs")
        if len(a_source.cells) != len(b_source.cells):
            raise ValueError(f"{label}: source {source_index} receiver-cell count differs")
        for cell_index, (a_cell, b_cell) in enumerate(zip(a_source.cells, b_source.cells)):
            if a_cell.count != b_cell.count:
                raise ValueError(f"{label}: source {source_index} cell {cell_index} arrival count differs")
            for arrival_index, (a_arrival, b_arrival) in enumerate(zip(a_cell.arrivals, b_cell.arrivals)):
                if (a_arrival.top_bounces, a_arrival.bottom_bounces) != (b_arrival.top_bounces, b_arrival.bottom_bounces):
                    raise ValueError(f"{label}: source {source_index} cell {cell_index} arrival {arrival_index} bounce counts differ")
                for field, maximum in maxima.items():
                    distance = float32_ulp(getattr(a_arrival, field), getattr(b_arrival, field))
                    if distance > ULP_LIMIT:
                        raise ValueError(f"{label}: source {source_index} cell {cell_index} arrival {arrival_index} {field} differs by {distance} ULP")
                    if distance > maximum["ulp"]:
                        maximum["ulp"] = distance
                        maximum["location"] = [source_index, cell_index, arrival_index]
                records += 1
    return {"arrival_records": records, "maximum_float32_ulp": maxima}


def _path_classes(product: ArrivalsProduct) -> dict[str, int]:
    counts = {"direct": 0, "top": 0, "bottom": 0, "mixed": 0}
    for cell in product.cells:
        for arrival in cell.arrivals:
            key = "direct" if not (arrival.top_bounces or arrival.bottom_bounces) else "mixed" if arrival.top_bounces and arrival.bottom_bounces else "top" if arrival.top_bounces else "bottom"
            counts[key] += 1
    return counts


def _effects(
    products: dict[str, ArrivalsProduct], *, full_matrix: bool = True
) -> dict[str, object]:
    all_arrivals = [arrival for product in products.values() for cell in product.cells for arrival in cell.arrivals]
    if not all_arrivals:
        raise ValueError("arrival matrix produced no non-zero products")
    zero = products["arrival_zero"]
    if any(cell.count for cell in zero.cells):
        raise ValueError("zero-arrival case contains an arrival")
    if not any(arrival.top_bounces or arrival.bottom_bounces for arrival in all_arrivals):
        raise ValueError("arrival matrix did not observe a reflected path")
    if max((source.maximum_arrivals for product in products.values() for source in product.sources), default=0) < 2:
        raise ValueError("arrival matrix did not observe multi-arrival cells")
    if not full_matrix:
        return {
            "zero_cells": len(zero.cells),
            "reflected_arrivals": sum(
                bool(a.top_bounces or a.bottom_bounces)
                for a in all_arrivals
            ),
            "maximum_arrivals": max(
                (
                    source.maximum_arrivals
                    for product in products.values()
                    for source in product.sources
                ),
                default=0,
            ),
            "path_class_counts": {
                case_id: _path_classes(product)
                for case_id, product in products.items()
            },
            "scope": "rayreuse representative shared cases",
        }
    ray_centered = products["arrival_geometric_hat_ray_centered"]
    if not any(abs(arrival.phase_degrees) > 180.0 for cell in ray_centered.cells for arrival in cell.arrivals):
        raise ValueError("ray-centered case did not observe unwrapped/caustic phase")
    multi = products["arrival_line_directional_multisource"]
    if multi.source_count != 2 or not all(any(cell.count for cell in source.cells) for source in multi.sources):
        raise ValueError("line/directional multi-source case lacks separated non-empty source bodies")
    amplitudes = [arrival.amplitude for cell in multi.cells for arrival in cell.arrivals]
    if len({float32_bits(value) for value in amplitudes}) < 2:
        raise ValueError("line/directional case does not expose directional amplitude variation")
    irregular = products["arrival_geometric_gaussian_irregular"]
    if len(irregular.sources[0].cells) != 3:
        raise ValueError("irregular case did not retain the paired receiver layout")
    return {"zero_cells": len(zero.cells), "reflected_arrivals": sum(bool(a.top_bounces or a.bottom_bounces) for a in all_arrivals), "maximum_arrivals": max(source.maximum_arrivals for product in products.values() for source in product.sources), "line_directional_amplitude_values": len({float32_bits(value) for value in amplitudes}), "irregular_cells": len(irregular.sources[0].cells), "path_class_counts": {case_id: _path_classes(product) for case_id, product in products.items()}, "observed_but_not_isolated": ["multiple stored arrivals do not independently prove duplicate merge behavior", "the frozen six-case matrix has no otherwise-identical point-source control, so point-vs-line scaling is not isolated", "the directional companion and amplitude variation are structural evidence, not an omnidirectional numerical control"]}


def validate(
    results_root: Path,
    origin_executable: Path,
    f2cpp_executable: Path,
    rayreuse_executable: Path | None = None,
) -> dict[str, object]:
    definitions = discover_cases(STANDARD_CASES_ROOT / "cases")
    implementations = {"origin": origin_executable.resolve(), "f2cpp": f2cpp_executable.resolve()}
    if rayreuse_executable is not None:
        implementations["rayreuse"] = rayreuse_executable.resolve()
    products: dict[str, dict[str, ArrivalsProduct]] = {name: {} for name in implementations}
    provenance: dict[str, object] = {}
    comparison: dict[str, object] = {}
    for version, executable in implementations.items():
        version_cases = RAYREUSE_CASES if version == "rayreuse" else CASES
        version_info: dict[str, object] = {"executable": str(executable), "executable_sha256": sha256(executable), "executable_mtime_ns": executable.stat().st_mtime_ns}
        for case_id in version_cases:
            definition = definitions[case_id]
            manifest_path, manifest = _manifest(results_root, version, case_id)
            _require_executable_identity(manifest, executable, manifest_path)
            product_path, environment_path = _product_path(results_root, manifest_path, manifest)
            _require_fresh(product_path, environment_path, executable, manifest_path)
            product = _parse(product_path, definition.output_kind, definition.arrival_receiver_cell_count)
            products[version][case_id] = product
            version_info[case_id] = {"product_sha256": sha256(product_path), "environment_sha256": sha256(environment_path), "source_references": list(definition.source_references), "source_sha256": source_hashes(definition.source_references), "arrival_count": sum(cell.count for cell in product.cells)}
        provenance[version] = version_info
        _effects(products[version], full_matrix=version != "rayreuse")
    for case_id in CASES:
        comparison[case_id] = compare_arrival_products(products["origin"][case_id], products["f2cpp"][case_id], case_id)
    for version in implementations:
        comparison[f"{version}_ascii_binary"] = compare_arrival_products(products[version][CASES[0]], products[version][CASES[1]], f"{version} A/a encoding pair")
        comparison[f"{version}_ray_centered_ascii_binary"] = compare_arrival_products(
            products[version]["arrival_geometric_hat_ray_centered"],
            products[version]["arrival_geometric_hat_ray_centered_binary"],
            f"{version} Ag/ag encoding pair",
        )
    if "rayreuse" in implementations:
        for case_id in RAYREUSE_CASES:
            comparison[f"origin_vs_rayreuse_{case_id}"] = compare_arrival_products(
                products["origin"][case_id], products["rayreuse"][case_id],
                f"Origin/RayReuse {case_id}",
            )
            comparison[f"f2cpp_vs_rayreuse_{case_id}"] = compare_arrival_products(
                products["f2cpp"][case_id], products["rayreuse"][case_id],
                f"F2CPP/RayReuse {case_id}",
            )
    return {"schema": "bellhop.i8.arrivals.parity.v1", "status": "passed", "ulp_limit": ULP_LIMIT, "cases": list(CASES), "cases_by_version": {version: list(RAYREUSE_CASES if version == "rayreuse" else CASES) for version in implementations}, "provenance": provenance, "effects": {version: _effects(products[version], full_matrix=version != "rayreuse") for version in implementations}, "comparison": comparison}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--results-root", type=Path, required=True)
    parser.add_argument("--origin-executable", type=Path, required=True)
    parser.add_argument("--f2cpp-executable", type=Path, required=True)
    parser.add_argument("--rayreuse-executable", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    report = validate(args.results_root, args.origin_executable, args.f2cpp_executable, args.rayreuse_executable)
    report["command"] = ["validate_i8_arrivals.py", "--results-root", "<results-root>", "--origin-executable", "<origin-executable>", "--f2cpp-executable", "<f2cpp-executable>"]
    if args.rayreuse_executable is not None:
        report["command"].extend(["--rayreuse-executable", "<rayreuse-executable>"])
    report["command"].extend(["--output", "<report>"])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, sort_keys=True, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
