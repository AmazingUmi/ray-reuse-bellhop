#!/usr/bin/env python3
"""Ordered EOF-stream Origin/F2CPP parity validator for I8 eigenrays."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import sys

from case_model import discover_cases
from eigenray_io import EigenrayOutput, parse_eigenray
from validate_i6_ray_trace import parse_ray


CASES = (
    "eigenray_geometric_hat",
    "eigenray_geometric_hat_ray_centered",
    "eigenray_geometric_gaussian",
    "eigenray_zero",
)
RAYREUSE_CASES = (
    "eigenray_geometric_hat_ray_centered",
    "eigenray_geometric_gaussian",
    "eigenray_zero",
)
CONTROL = "ray_trace_vacuum_rigid"
ABS_TOL = 1.0e-7
REL_TOL = 1.0e-10
CODES_ROOT = Path(__file__).resolve().parent
STANDARD_CASES_ROOT = CODES_ROOT.parent
PROJECT_ROOT = STANDARD_CASES_ROOT.parents[1]


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_hashes(references: tuple[str, ...]) -> dict[str, str]:
    result: dict[str, str] = {}
    for relative in sorted({reference.split(" ", 1)[0] for reference in references}):
        path = PROJECT_ROOT / relative
        if not path.is_file():
            raise ValueError(f"referenced Origin source is absent: {path}")
        result[relative] = sha256(path)
    return result


def _manifest(root: Path, version: str, case_id: str) -> tuple[Path, dict[str, object]]:
    path = root / version / case_id / "single" / "run_manifest.json"
    if not path.is_file():
        raise ValueError(f"missing manifest: {path}")
    data = json.loads(path.read_text(encoding="utf-8"))
    if data.get("version") != version or data.get("case_id") != case_id:
        raise ValueError(f"{path}: manifest identity mismatch")
    if data.get("last_stage") != "test":
        raise ValueError(f"{path}: product is not a fresh validated test run")
    return path, data


def _output(manifest_path: Path, manifest: dict[str, object]) -> tuple[Path, Path]:
    runs = manifest.get("runs")
    if not isinstance(runs, list) or len(runs) != 1 or not isinstance(runs[0], dict):
        raise ValueError(f"{manifest_path}: expected one output run")
    run = runs[0]
    product_name, environment_name = run.get("ray_file"), run.get("environment_file")
    if not isinstance(product_name, str) or not isinstance(environment_name, str):
        raise ValueError(f"{manifest_path}: ray/environment record is absent")
    product, environment = manifest_path.parent / product_name, manifest_path.parent / environment_name
    if not product.is_file() or not environment.is_file():
        raise ValueError(f"{manifest_path}: product/environment is absent")
    if run.get("product_sha256") != sha256(product):
        raise ValueError(f"{manifest_path}: product hash mismatch")
    return product, environment


def _fresh(product: Path, environment: Path, executable: Path, manifest_path: Path) -> None:
    if not executable.is_file():
        raise ValueError(f"missing executable: {executable}")
    if product.stat().st_mtime_ns < max(environment.stat().st_mtime_ns, executable.stat().st_mtime_ns):
        raise ValueError(f"{manifest_path}: stale product predates executable or rendered ENV")


def _require_executable_identity(manifest: dict[str, object], executable: Path, manifest_path: Path) -> None:
    if Path(str(manifest.get("executable"))).resolve() != executable.resolve():
        raise ValueError(f"{manifest_path}: executable identity mismatch")


def _same_float(left: float, right: float) -> bool:
    return math.isclose(left, right, abs_tol=ABS_TOL, rel_tol=REL_TOL)


def compare_eigenrays(left: EigenrayOutput, right: EigenrayOutput, label: str) -> dict[str, object]:
    if left.header.source_counts != right.header.source_counts or left.header.launch_counts != right.header.launch_counts or left.header.plot_type != right.header.plot_type:
        raise ValueError(f"{label}: header dimensions/order differ")
    for name in ("frequency_hz", "top_depth_m", "bottom_depth_m"):
        if not _same_float(getattr(left.header, name), getattr(right.header, name)):
            raise ValueError(f"{label}: header {name} differs")
    if len(left.rays) != len(right.rays):
        raise ValueError(f"{label}: EOF block count differs")
    maximum_error = 0.0
    max_location: list[int] | None = None
    for block_index, (a, b) in enumerate(zip(left.rays, right.rays)):
        if not _same_float(a.launch_angle_deg, b.launch_angle_deg):
            raise ValueError(f"{label}: block {block_index} launch-angle order differs")
        if (a.point_count, a.top_bounces, a.bottom_bounces) != (b.point_count, b.top_bounces, b.bottom_bounces):
            raise ValueError(f"{label}: block {block_index} point/bounce structure differs")
        for point_index, (a_point, b_point) in enumerate(zip(a.points_m, b.points_m)):
            if len(a_point) != len(b_point):
                raise ValueError(f"{label}: block {block_index} coordinate dimension differs")
            for coordinate_index, (a_value, b_value) in enumerate(zip(a_point, b_point)):
                error = abs(a_value - b_value)
                limit = ABS_TOL + REL_TOL * max(abs(a_value), abs(b_value))
                if error > limit:
                    raise ValueError(f"{label}: block {block_index} point {point_index} coordinate {coordinate_index} differs by {error}")
                if error > maximum_error:
                    maximum_error, max_location = error, [block_index, point_index, coordinate_index]
    return {"blocks": len(left.rays), "maximum_coordinate_error_m": maximum_error, "maximum_coordinate_error_location": max_location}


def _effect_summary(products: dict[str, EigenrayOutput], control_point_counts: tuple[int, ...]) -> dict[str, object]:
    zero = products["eigenray_zero"]
    if zero.rays:
        raise ValueError("zero-hit eigenray case contains an EOF block")
    if set(products) == set(RAYREUSE_CASES):
        nonzero = products["eigenray_geometric_gaussian"].rays
        if not nonzero:
            raise ValueError("RayReuse eigenray matrix has no successful hit")
        if not any(ray.top_bounces or ray.bottom_bounces for ray in nonzero):
            raise ValueError("RayReuse eigenray stream did not observe reflected prefixes")
        return {
            "zero_blocks": 0,
            "nonzero_blocks": len(nonzero),
            "point_total": sum(ray.point_count for ray in nonzero),
            "top_bounce_total": sum(ray.top_bounces for ray in nonzero),
            "bottom_bounce_total": sum(ray.bottom_bounces for ray in nonzero),
            "prefix_point_minimum": min(ray.point_count for ray in nonzero),
            "prefix_point_maximum": max(ray.point_count for ray in nonzero),
            "scope": "rayreuse representative shared cases",
        }
    nonzero = [
        ray
        for key, product in products.items()
        if key != "eigenray_zero"
        for ray in product.rays
    ]
    if not nonzero:
        raise ValueError("eigenray matrix has no successful hit")
    if not any(len(product.rays) != product.header.launch_counts[0] for product in products.values()):
        raise ValueError("no eigenray case exposes EOF block count distinct from Nalpha")
    angles = [ray.launch_angle_deg for ray in nonzero]
    repeated = len(angles) - len(set(angles))
    if repeated <= 0:
        raise ValueError("eigenray matrix did not observe repeated launch-angle blocks")
    if not any(ray.top_bounces or ray.bottom_bounces for ray in nonzero):
        raise ValueError("eigenray matrix did not observe reflected prefixes")
    if not control_point_counts or not any(ray.point_count < max(control_point_counts) for ray in nonzero):
        raise ValueError("eigenray stream did not contain a prefix shorter than ordinary full-ray control")
    families = {key: len(product.rays) for key, product in products.items()}
    multi = products["eigenray_geometric_hat"]
    if math.prod(multi.header.source_counts) != 2:
        raise ValueError("geometric-hat eigenray case lost its two-source header")
    return {"zero_blocks": 0, "nonzero_blocks": len(nonzero), "repeated_launch_angle_blocks": repeated, "family_blocks": families, "point_total": sum(ray.point_count for ray in nonzero), "top_bounce_total": sum(ray.top_bounces for ray in nonzero), "bottom_bounce_total": sum(ray.bottom_bounces for ray in nonzero), "prefix_point_minimum": min(ray.point_count for ray in nonzero), "prefix_point_maximum": max(ray.point_count for ray in nonzero), "ordinary_control_point_maximum": max(control_point_counts), "shorter_than_full_ray": True, "multi_source_header_count": 2}


def validate(
    results_root: Path,
    origin_executable: Path,
    f2cpp_executable: Path,
    rayreuse_executable: Path | None = None,
) -> dict[str, object]:
    definitions = discover_cases(STANDARD_CASES_ROOT / "cases")
    implementations = {
        "origin": origin_executable.resolve(),
        "f2cpp": f2cpp_executable.resolve(),
    }
    if rayreuse_executable is not None:
        implementations["rayreuse"] = rayreuse_executable.resolve()
    products: dict[str, dict[str, EigenrayOutput]] = {name: {} for name in implementations}
    provenance: dict[str, object] = {}
    controls: dict[str, tuple[int, ...]] = {}
    for version, executable in implementations.items():
        version_cases = RAYREUSE_CASES if version == "rayreuse" else CASES
        info: dict[str, object] = {"executable": str(executable), "executable_sha256": sha256(executable), "executable_mtime_ns": executable.stat().st_mtime_ns}
        for case_id in version_cases:
            definition = definitions[case_id]
            manifest_path, manifest = _manifest(results_root, version, case_id)
            if manifest.get("output_kind") != "eigenray":
                raise ValueError(f"{manifest_path}: product provenance mismatch")
            _require_executable_identity(manifest, executable, manifest_path)
            product_path, environment_path = _output(manifest_path, manifest)
            _fresh(product_path, environment_path, executable, manifest_path)
            output = parse_eigenray(product_path)
            products[version][case_id] = output
            info[case_id] = {"product_sha256": sha256(product_path), "environment_sha256": sha256(environment_path), "source_references": list(definition.source_references), "source_sha256": source_hashes(definition.source_references), "blocks": len(output.rays)}
        if version != "rayreuse":
            control_manifest, control_data = _manifest(results_root, version, CONTROL)
            if control_data.get("output_kind") != "ray":
                raise ValueError(f"{control_manifest}: ordinary R control product provenance mismatch")
            _require_executable_identity(control_data, executable, control_manifest)
            control_path, control_env = _output(control_manifest, control_data)
            _fresh(control_path, control_env, executable, control_manifest)
            control = parse_ray(control_path)
            expected_block_count = math.prod(control.header.source_counts) * math.prod(control.header.launch_counts)
            if len(control.rays) != expected_block_count:
                raise ValueError("ordinary R control does not have header-derived fixed block count")
            controls[version] = tuple(len(ray.points_m) for ray in control.rays)
            info[CONTROL] = {"product_sha256": sha256(control_path), "fixed_blocks": len(control.rays)}
        provenance[version] = info
    comparisons = {case_id: compare_eigenrays(products["origin"][case_id], products["f2cpp"][case_id], case_id) for case_id in CASES}
    if "rayreuse" in implementations:
        for case_id in RAYREUSE_CASES:
            comparisons[f"origin_vs_rayreuse_{case_id}"] = compare_eigenrays(
                products["origin"][case_id], products["rayreuse"][case_id],
                f"Origin/RayReuse {case_id}",
            )
            comparisons[f"f2cpp_vs_rayreuse_{case_id}"] = compare_eigenrays(
                products["f2cpp"][case_id], products["rayreuse"][case_id],
                f"F2CPP/RayReuse {case_id}",
            )
    return {"schema": "bellhop.i8.eigenray.parity.v1", "status": "passed", "coordinate_tolerance": {"absolute_m": ABS_TOL, "relative": REL_TOL}, "cases": list(CASES), "cases_by_version": {version: list(RAYREUSE_CASES if version == "rayreuse" else CASES) for version in implementations}, "provenance": provenance, "effects": {version: _effect_summary(products[version], controls.get(version, ())) for version in products}, "comparison": comparisons}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--results-root", type=Path, required=True)
    parser.add_argument("--origin-executable", type=Path, required=True)
    parser.add_argument("--f2cpp-executable", type=Path, required=True)
    parser.add_argument("--rayreuse-executable", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    report = validate(args.results_root, args.origin_executable, args.f2cpp_executable, args.rayreuse_executable)
    report["command"] = ["validate_i8_eigenrays.py", "--results-root", "<results-root>", "--origin-executable", "<origin-executable>", "--f2cpp-executable", "<f2cpp-executable>"]
    if args.rayreuse_executable is not None:
        report["command"].extend(["--rayreuse-executable", "<rayreuse-executable>"])
    report["command"].extend(["--output", "<report>"])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, sort_keys=True, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
