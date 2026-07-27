#!/usr/bin/env python3
"""Validate one Cartesian-Cerveny ray/receiver influence sample."""

from __future__ import annotations

import argparse
import cmath
import csv
import json
import math
import struct
from pathlib import Path


SCHEMA_NAME = "bellhop.fortran.cartesian_cerveny_influence_sample"
SCHEMA_VERSION = 1
DEFAULT_IMAGES_FILE = "influence_images.csv"
DEFAULT_RAY_POINTS_FILE = "ray_points.csv"

ABSOLUTE_TOLERANCE = 1e-12
RELATIVE_TOLERANCE = 1e-10
CONTRIBUTION_RELATIVE_TOLERANCE = 1e-9

CSV_COLUMNS = (
    "image_index", "image_kind", "left_point_index", "right_point_index",
    "interpolation_weight", "interp_r_m", "interp_z_m",
    "interp_t_r_s_per_m", "interp_t_z_s_per_m", "interp_c_m_per_s",
    "tau_real_s", "tau_imag_s", "q_left_real", "q_left_imag",
    "q_right_real", "q_right_imag", "q_real", "q_imag",
    "gamma_left_real", "gamma_left_imag", "gamma_right_real",
    "gamma_right_imag", "gamma_real", "gamma_imag", "kmah_left",
    "kmah_interpolated", "const_before_kmah_real",
    "const_before_kmah_imag", "const_real", "const_imag",
    "right_amplitude", "right_phase_rad", "delta_z_m", "polarity",
    "window_metric", "window_pass", "hermite_taper",
    "image_contribution_real", "image_contribution_imag", "image_sum_real",
    "image_sum_imag", "ray_contribution_real", "ray_contribution_imag",
    "complex64_increment_real", "complex64_increment_imag",
)

INTEGER_COLUMNS = {
    "image_index", "left_point_index", "right_point_index", "kmah_left",
    "kmah_interpolated", "window_pass",
}
IMAGE_COLUMNS = {
    "image_index", "image_kind", "delta_z_m", "polarity", "window_metric",
    "window_pass", "hermite_taper", "image_contribution_real",
    "image_contribution_imag",
}
IMAGE_KINDS = {
    1: ("true", 1.0),
    2: ("surface", -1.0),
    3: ("bottom", 1.0),
}


def require_close(
    actual: float,
    expected: float,
    message: str,
    *,
    relative_tolerance: float = RELATIVE_TOLERANCE,
) -> None:
    if not math.isclose(
        actual,
        expected,
        abs_tol=ABSOLUTE_TOLERANCE,
        rel_tol=relative_tolerance,
    ):
        raise ValueError(f"{message}: {actual!r} != {expected!r}")


def require_complex_close(
    actual: complex,
    expected: complex,
    message: str,
    *,
    relative_tolerance: float = RELATIVE_TOLERANCE,
) -> None:
    require_close(
        actual.real,
        expected.real,
        f"{message} real",
        relative_tolerance=relative_tolerance,
    )
    require_close(
        actual.imag,
        expected.imag,
        f"{message} imag",
        relative_tolerance=relative_tolerance,
    )


def require_manifest_float(
    manifest: dict[str, object],
    name: str,
    *,
    positive: bool = False,
    nonnegative: bool = False,
) -> float:
    try:
        value = float(manifest[name])
    except (KeyError, TypeError, ValueError) as error:
        raise ValueError(f"influence manifest requires numeric {name}") from error
    if not math.isfinite(value):
        raise ValueError(f"influence manifest {name} must be finite")
    if positive and value <= 0.0:
        raise ValueError(f"influence manifest {name} must be positive")
    if nonnegative and value < 0.0:
        raise ValueError(f"influence manifest {name} must be non-negative")
    return value


def require_manifest_integer(
    manifest: dict[str, object],
    name: str,
    *,
    positive: bool = False,
    nonnegative: bool = False,
) -> int:
    value = manifest.get(name)
    if isinstance(value, bool):
        raise ValueError(f"influence manifest {name} must be an integer")
    try:
        numeric = float(value)
        integer = int(numeric)
    except (TypeError, ValueError) as error:
        raise ValueError(f"influence manifest requires integer {name}") from error
    if not math.isfinite(numeric) or numeric != integer:
        raise ValueError(f"influence manifest {name} must be an integer")
    if positive and integer < 1:
        raise ValueError(f"influence manifest {name} must be positive")
    if nonnegative and integer < 0:
        raise ValueError(f"influence manifest {name} must be non-negative")
    return integer


def complex_field(row: dict[str, object], prefix: str) -> complex:
    return complex(float(row[f"{prefix}_real"]), float(row[f"{prefix}_imag"]))


def hermite_taper(value: float, radius_max: float) -> float:
    absolute_value = abs(value)
    if absolute_value <= radius_max:
        return 1.0
    if absolute_value >= 2.0 * radius_max:
        return 0.0
    unit = (absolute_value - radius_max) / radius_max
    return (1.0 + 2.0 * unit) * (1.0 - unit) ** 2


def to_float32(value: float) -> float:
    try:
        return struct.unpack("=f", struct.pack("=f", value))[0]
    except (OverflowError, struct.error) as error:
        raise ValueError("ray contribution cannot be represented as complex64") from error


def load_image_rows(path: Path) -> list[dict[str, object]]:
    with path.open(newline="", encoding="utf-8") as stream:
        reader = csv.DictReader(stream)
        raw_rows = list(reader)
        fieldnames = reader.fieldnames or []
    missing = set(CSV_COLUMNS).difference(fieldnames)
    if missing:
        raise ValueError(
            "influence image CSV is missing columns: "
            + ", ".join(sorted(missing))
        )

    rows: list[dict[str, object]] = []
    for row_number, raw in enumerate(raw_rows, start=1):
        kind = raw["image_kind"]
        if not kind:
            raise ValueError(f"influence image row {row_number}: empty image_kind")
        row: dict[str, object] = {"image_kind": kind}
        for name in CSV_COLUMNS:
            if name == "image_kind":
                continue
            try:
                number = float(raw[name])
            except (TypeError, ValueError) as error:
                raise ValueError(
                    f"influence image row {row_number}: invalid numeric field {name}"
                ) from error
            if not math.isfinite(number):
                raise ValueError(
                    f"influence image row {row_number}: non-finite field {name}"
                )
            if name in INTEGER_COLUMNS:
                integer = int(number)
                if number != integer:
                    raise ValueError(
                        f"influence image row {row_number}: {name} is not an integer"
                    )
                row[name] = integer
            else:
                row[name] = number
        rows.append(row)
    return rows


def validate_common_rows(rows: list[dict[str, object]]) -> None:
    first = rows[0]
    for row_number, row in enumerate(rows[1:], start=2):
        for name in set(CSV_COLUMNS).difference(IMAGE_COLUMNS):
            require_close(
                float(row[name]),
                float(first[name]),
                f"influence image row {row_number} common field {name}",
            )


def load_ray_points(path: Path) -> list[dict[str, str]]:
    required = {
        "point_index", "r_m", "z_m", "t_r_s_per_m", "t_z_s_per_m",
        "p1", "p2", "q1", "q2", "c_m_per_s", "tau_real_s",
        "tau_imag_s", "amplitude", "phase_rad",
    }
    with path.open(newline="", encoding="utf-8") as stream:
        reader = csv.DictReader(stream)
        rows = list(reader)
        fieldnames = reader.fieldnames or []
    if not rows or not required.issubset(fieldnames):
        raise ValueError("ray point CSV lacks fields required by influence oracle")
    for expected_index, row in enumerate(rows, start=1):
        try:
            values = {name: float(row[name]) for name in required}
        except (TypeError, ValueError) as error:
            raise ValueError("ray point CSV contains invalid numeric data") from error
        if not all(math.isfinite(value) for value in values.values()):
            raise ValueError("ray point CSV contains non-finite data")
        if values["point_index"] != expected_index:
            raise ValueError("ray point indices are not contiguous")
    return rows


def interpolate(left: float, right: float, weight: float) -> float:
    return left + weight * (right - left)


def branch_cut(first: complex, second: complex, beam_type: str, kmah: int) -> int:
    if len(beam_type) >= 2 and beam_type[1] == "W":
        crossed = (
            first.real < 0.0 <= second.real
            or first.real > 0.0 >= second.real
        )
    else:
        crossed = (
            second.real < 0.0
            and (
                first.imag < 0.0 <= second.imag
                or first.imag > 0.0 >= second.imag
            )
        )
    return -kmah if crossed else kmah


def validate_ray_cross_check(
    points: list[dict[str, str]],
    sample: dict[str, object],
    epsilon: complex,
    receiver_range: float,
    beam_type: str,
) -> None:
    left_index = int(sample["left_point_index"])
    right_index = int(sample["right_point_index"])
    if left_index < 1 or right_index != left_index + 1 or right_index > len(points):
        raise ValueError("influence sample has invalid ray point bracket")
    left = points[left_index - 1]
    right = points[right_index - 1]
    left_range = float(left["r_m"])
    right_range = float(right["r_m"])
    if right_range <= left_range:
        raise ValueError("influence sample ray bracket is duplicate or backward")
    expected_weight = (receiver_range - left_range) / (right_range - left_range)
    weight = float(sample["interpolation_weight"])
    require_close(weight, expected_weight, "influence interpolation weight")

    for sample_name, point_name in {
        "interp_r_m": "r_m",
        "interp_z_m": "z_m",
        "interp_t_r_s_per_m": "t_r_s_per_m",
        "interp_t_z_s_per_m": "t_z_s_per_m",
        "interp_c_m_per_s": "c_m_per_s",
        "tau_real_s": "tau_real_s",
        "tau_imag_s": "tau_imag_s",
    }.items():
        expected = interpolate(
            float(left[point_name]), float(right[point_name]), weight
        )
        require_close(
            float(sample[sample_name]),
            expected,
            f"influence interpolated field {sample_name}",
        )
    require_close(
        float(sample["right_amplitude"]),
        float(right["amplitude"]),
        "influence right-point amplitude",
    )
    require_close(
        float(sample["right_phase_rad"]),
        float(right["phase_rad"]),
        "influence right-point phase",
    )

    q_values = [
        float(point["q1"]) + epsilon * float(point["q2"])
        for point in points
    ]
    q_left = q_values[left_index - 1]
    q_right = q_values[right_index - 1]
    q = q_left + weight * (q_right - q_left)
    require_complex_close(complex_field(sample, "q_left"), q_left, "influence q_left")
    require_complex_close(complex_field(sample, "q_right"), q_right, "influence q_right")
    require_complex_close(complex_field(sample, "q"), q, "influence q")

    kmah_values = [1]
    for first, second in zip(q_values, q_values[1:]):
        kmah_values.append(branch_cut(first, second, beam_type, kmah_values[-1]))
    expected_left = kmah_values[left_index - 1]
    expected_interpolated = branch_cut(q_left, q, beam_type, expected_left)
    if int(sample["kmah_left"]) != expected_left:
        raise ValueError("influence kmah_left does not match ray history")
    if int(sample["kmah_interpolated"]) != expected_interpolated:
        raise ValueError("influence interpolated KMAH does not match BranchCut")


def validate_oracle(directory: Path) -> dict[str, object]:
    manifest = json.loads(
        (directory / "influence_manifest.json").read_text(encoding="utf-8")
    )
    if manifest.get("schema") != SCHEMA_NAME:
        raise ValueError(f"unexpected influence schema: {manifest.get('schema')!r}")
    if manifest.get("schema_version") != SCHEMA_VERSION:
        raise ValueError(
            f"unsupported influence schema version: {manifest.get('schema_version')!r}"
        )
    if manifest.get("status") != "complete":
        raise ValueError(f"influence oracle is not complete: {manifest.get('status')!r}")
    if require_manifest_integer(manifest, "index_base") != 1:
        raise ValueError("influence manifest index_base must be 1")

    for name in (
        "source_index", "launch_angle_index", "receiver_range_index",
        "receiver_depth_index",
    ):
        require_manifest_integer(manifest, name, positive=True)
    frequency = require_manifest_float(manifest, "frequency_hz", positive=True)
    require_manifest_float(manifest, "source_sound_speed_m_per_s", positive=True)
    require_manifest_float(manifest, "dalpha_rad", positive=True)
    require_manifest_float(manifest, "rloop_km", positive=True)
    require_manifest_float(manifest, "epsilon_multiplier", positive=True)
    launch_angle = require_manifest_float(manifest, "launch_angle_rad")
    receiver_range = require_manifest_float(manifest, "receiver_range_m")
    receiver_depth = require_manifest_float(manifest, "receiver_depth_m")
    surface_depth = require_manifest_float(manifest, "sea_surface_depth_m")
    seabed_depth = require_manifest_float(manifest, "seabed_depth_m")
    if surface_depth >= seabed_depth:
        raise ValueError("influence manifest has invalid sea boundaries")
    epsilon = complex(
        require_manifest_float(manifest, "epsilon_real"),
        require_manifest_float(manifest, "epsilon_imag"),
    )
    if epsilon == 0.0:
        raise ValueError("influence epsilon must be non-zero")
    ratio1 = require_manifest_float(manifest, "ratio1", nonnegative=True)
    radius_max = require_manifest_float(manifest, "radius_max_m", positive=True)
    beam_window = require_manifest_integer(
        manifest, "beam_window_squared", nonnegative=True
    )
    image_count = require_manifest_integer(manifest, "image_count", positive=True)
    if image_count > 3:
        raise ValueError("influence oracle supports at most three images")
    evaluation_count = require_manifest_integer(
        manifest, "evaluation_count", positive=True
    )
    selected_evaluation_count = require_manifest_integer(
        manifest, "selected_range_evaluation_count", positive=True
    )
    if evaluation_count != selected_evaluation_count or evaluation_count < 1:
        raise ValueError("influence evaluation counts are inconsistent")
    row_count = require_manifest_integer(manifest, "image_row_count", positive=True)
    run_type = str(manifest.get("run_type", ""))
    beam_type = str(manifest.get("beam_type", ""))
    if not run_type.startswith("CC") or len(run_type) < 4:
        raise ValueError("influence oracle requires coherent Cartesian run_type")
    if not beam_type.startswith("CMS"):
        raise ValueError("influence oracle requires CMS beam_type")
    if manifest.get("beam_width_mode") != "minimum":
        raise ValueError("influence manifest has invalid beam_width_mode")
    if manifest.get("contribution_stage") != (
        "complex128_before_complex64_accumulation_and_scale_pressure"
    ):
        raise ValueError("influence manifest has invalid contribution_stage")
    expected_ratio1 = (
        1.0
        if run_type[3] == "X"
        else math.sqrt(abs(math.cos(launch_angle)))
    )
    require_close(ratio1, expected_ratio1, "influence manifest ratio1")

    rows = load_image_rows(
        directory / str(manifest.get("images_file", DEFAULT_IMAGES_FILE))
    )
    if len(rows) != image_count or len(rows) != row_count:
        raise ValueError("influence image CSV/manifest count mismatch")
    validate_common_rows(rows)
    sample = rows[0]
    if int(sample["left_point_index"]) < 1 or (
        int(sample["right_point_index"]) != int(sample["left_point_index"]) + 1
    ):
        raise ValueError("influence sample has invalid point indices")
    weight = float(sample["interpolation_weight"])
    if not 0.0 <= weight <= 1.0:
        raise ValueError("influence interpolation weight is outside [0, 1]")
    require_close(float(sample["interp_r_m"]), receiver_range, "influence receiver range")
    if float(sample["gamma_imag"]) > 0.0:
        raise ValueError("complete influence sample has unbounded gamma")

    q_left = complex_field(sample, "q_left")
    q_right = complex_field(sample, "q_right")
    q = complex_field(sample, "q")
    require_complex_close(q, q_left + weight * (q_right - q_left), "influence q interpolation")
    if q == 0.0:
        raise ValueError("influence interpolated q must be non-zero")
    gamma_left = complex_field(sample, "gamma_left")
    gamma_right = complex_field(sample, "gamma_right")
    gamma = complex_field(sample, "gamma")
    require_complex_close(
        gamma,
        gamma_left + weight * (gamma_right - gamma_left),
        "influence gamma interpolation",
    )
    expected_const_before = ratio1 * cmath.sqrt(
        float(sample["interp_c_m_per_s"]) * abs(epsilon) / q
    )
    const_before = complex_field(sample, "const_before_kmah")
    require_complex_close(
        const_before, expected_const_before, "influence const before KMAH"
    )
    kmah_left = int(sample["kmah_left"])
    kmah = int(sample["kmah_interpolated"])
    if kmah_left not in {-1, 1} or kmah not in {-1, 1}:
        raise ValueError("influence KMAH values must be -1 or 1")
    const = complex_field(sample, "const")
    require_complex_close(
        const, -const_before if kmah < 0 else const_before, "influence const"
    )

    omega = 2.0 * math.pi * frequency
    tau = complex(float(sample["tau_real_s"]), float(sample["tau_imag_s"]))
    ray_t_z = float(sample["interp_t_z_s_per_m"])
    ray_amplitude = float(sample["right_amplitude"])
    if ray_amplitude < 0.0:
        raise ValueError("influence right-point amplitude is negative")
    ray_phase = float(sample["right_phase_rad"])
    expected_deltas = (
        receiver_depth - float(sample["interp_z_m"]),
        -receiver_depth + 2.0 * surface_depth - float(sample["interp_z_m"]),
        -receiver_depth + 2.0 * seabed_depth - float(sample["interp_z_m"]),
    )

    contributions: list[complex] = []
    pass_count = 0
    for expected_index, row in enumerate(rows, start=1):
        if int(row["image_index"]) != expected_index:
            raise ValueError("influence image indices are not contiguous")
        expected_kind, expected_polarity = IMAGE_KINDS[expected_index]
        if row["image_kind"] != expected_kind:
            raise ValueError("influence images are not in true/surface/bottom order")
        require_close(float(row["polarity"]), expected_polarity, "influence image polarity")
        delta = float(row["delta_z_m"])
        require_close(delta, expected_deltas[expected_index - 1], "influence image delta")
        metric = -omega * gamma.imag * delta * delta
        require_close(float(row["window_metric"]), metric, "influence window metric")
        expected_pass = int(metric < beam_window)
        if int(row["window_pass"]) != expected_pass:
            raise ValueError("influence image window decision is incorrect")
        pass_count += expected_pass
        taper = hermite_taper(delta, radius_max)
        require_close(float(row["hermite_taper"]), taper, "influence Hermite taper")
        if expected_pass:
            delay = tau + ray_t_z * delta + gamma * delta * delta
            expected_contribution = (
                expected_polarity
                * ray_amplitude
                * taper
                * cmath.exp(-1j * (omega * delay - ray_phase))
            )
        else:
            expected_contribution = 0.0j
        contribution = complex_field(row, "image_contribution")
        require_complex_close(
            contribution,
            expected_contribution,
            f"influence image {expected_index} contribution",
            relative_tolerance=CONTRIBUTION_RELATIVE_TOLERANCE,
        )
        contributions.append(contribution)

    expected_sum = sum(contributions, 0.0j)
    image_sum = complex_field(sample, "image_sum")
    require_complex_close(
        image_sum,
        expected_sum,
        "influence ordered image sum",
        relative_tolerance=CONTRIBUTION_RELATIVE_TOLERANCE,
    )
    ray_contribution = complex_field(sample, "ray_contribution")
    require_complex_close(
        ray_contribution,
        const * image_sum,
        "influence ray contribution",
        relative_tolerance=CONTRIBUTION_RELATIVE_TOLERANCE,
    )
    expected_increment = complex(
        to_float32(ray_contribution.real),
        to_float32(ray_contribution.imag),
    )
    if complex_field(sample, "complex64_increment") != expected_increment:
        raise ValueError(
            "influence complex64 increment does not match ray contribution"
        )

    ray_points_path = directory / str(
        manifest.get("ray_points_file", DEFAULT_RAY_POINTS_FILE)
    )
    ray_cross_checked = ray_points_path.exists()
    if ray_cross_checked:
        validate_ray_cross_check(
            load_ray_points(ray_points_path),
            sample,
            epsilon,
            receiver_range,
            beam_type,
        )

    return {
        "schema_version": SCHEMA_VERSION,
        "image_count": image_count,
        "window_pass_count": pass_count,
        "left_point_index": int(sample["left_point_index"]),
        "right_point_index": int(sample["right_point_index"]),
        "ray_contribution_magnitude": abs(ray_contribution),
        "ray_points_cross_checked": ray_cross_checked,
        "all_numeric_fields_finite": True,
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "directory",
        type=Path,
        help="directory containing influence_manifest.json and influence_images.csv",
    )
    args = parser.parse_args()
    print(json.dumps(validate_oracle(args.directory), indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
