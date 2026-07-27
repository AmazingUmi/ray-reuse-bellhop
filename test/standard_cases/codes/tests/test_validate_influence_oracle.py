from __future__ import annotations

import cmath
import csv
import json
import math
from pathlib import Path
import struct
import sys
import tempfile
import unittest


CODES_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(CODES_ROOT))

from validate_influence_oracle import CSV_COLUMNS, hermite_taper, validate_oracle


def float32(value: float) -> float:
    return struct.unpack("=f", struct.pack("=f", value))[0]


def complex_columns(prefix: str, value: complex) -> dict[str, float]:
    return {f"{prefix}_real": value.real, f"{prefix}_imag": value.imag}


def make_fixture() -> tuple[
    dict[str, object],
    list[dict[str, object]],
    list[dict[str, object]],
]:
    frequency = 50.0
    launch_angle = 0.0
    epsilon = 100.0j
    receiver_range = 15.0
    receiver_depth = 50.0
    surface_depth = 0.0
    seabed_depth = 100.0
    radius_max = 100.0
    beam_window = 25
    weight = 0.5

    points: list[dict[str, object]] = [
        {
            "point_index": 1, "r_m": 0.0, "z_m": 50.0,
            "t_r_s_per_m": 1.0 / 1500.0, "t_z_s_per_m": 0.0,
            "p1": 1.0, "p2": 0.0, "q1": 0.0, "q2": 1.0,
            "c_m_per_s": 1500.0, "tau_real_s": 0.0,
            "tau_imag_s": 0.0, "amplitude": 1.0, "phase_rad": 0.0,
        },
        {
            "point_index": 2, "r_m": 10.0, "z_m": 50.0,
            "t_r_s_per_m": 1.0 / 1500.0, "t_z_s_per_m": 0.0,
            "p1": 1.0, "p2": 0.0, "q1": 10.0, "q2": 1.0,
            "c_m_per_s": 1500.0, "tau_real_s": 0.01,
            "tau_imag_s": 0.0, "amplitude": 1.0, "phase_rad": 0.125,
        },
        {
            "point_index": 3, "r_m": 20.0, "z_m": 50.0,
            "t_r_s_per_m": 1.0 / 1500.0, "t_z_s_per_m": 0.0,
            "p1": 1.0, "p2": 0.0, "q1": 20.0, "q2": 1.0,
            "c_m_per_s": 1500.0, "tau_real_s": 0.02,
            "tau_imag_s": 0.0, "amplitude": 0.8, "phase_rad": 0.25,
        },
    ]

    q_left = 10.0 + epsilon
    q_right = 20.0 + epsilon
    q = q_left + weight * (q_right - q_left)
    gamma_left = 0.5 / q_left
    gamma_right = 0.5 / q_right
    gamma = gamma_left + weight * (gamma_right - gamma_left)
    tau = 0.015 + 0.0j
    ratio1 = 1.0
    const_before = ratio1 * cmath.sqrt(1500.0 * abs(epsilon) / q)
    const = const_before
    omega = 2.0 * math.pi * frequency

    rows: list[dict[str, object]] = []
    contributions: list[complex] = []
    image_data = (
        ("true", 1.0, receiver_depth - 50.0),
        ("surface", -1.0, -receiver_depth + 2.0 * surface_depth - 50.0),
        ("bottom", 1.0, -receiver_depth + 2.0 * seabed_depth - 50.0),
    )
    for image_index, (kind, polarity, delta) in enumerate(image_data, start=1):
        metric = -omega * gamma.imag * delta * delta
        window_pass = int(metric < beam_window)
        taper = hermite_taper(delta, radius_max)
        contribution = (
            polarity
            * 0.8
            * taper
            * cmath.exp(-1j * (omega * (tau + gamma * delta * delta) - 0.25))
            if window_pass
            else 0.0j
        )
        contributions.append(contribution)
        row: dict[str, object] = {
            "image_index": image_index,
            "image_kind": kind,
            "left_point_index": 2,
            "right_point_index": 3,
            "interpolation_weight": weight,
            "interp_r_m": receiver_range,
            "interp_z_m": 50.0,
            "interp_t_r_s_per_m": 1.0 / 1500.0,
            "interp_t_z_s_per_m": 0.0,
            "interp_c_m_per_s": 1500.0,
            "tau_real_s": tau.real,
            "tau_imag_s": tau.imag,
            "kmah_left": 1,
            "kmah_interpolated": 1,
            "right_amplitude": 0.8,
            "right_phase_rad": 0.25,
            "delta_z_m": delta,
            "polarity": polarity,
            "window_metric": metric,
            "window_pass": window_pass,
            "hermite_taper": taper,
        }
        row.update(complex_columns("q_left", q_left))
        row.update(complex_columns("q_right", q_right))
        row.update(complex_columns("q", q))
        row.update(complex_columns("gamma_left", gamma_left))
        row.update(complex_columns("gamma_right", gamma_right))
        row.update(complex_columns("gamma", gamma))
        row.update(complex_columns("const_before_kmah", const_before))
        row.update(complex_columns("const", const))
        row.update(complex_columns("image_contribution", contribution))
        rows.append(row)

    image_sum = sum(contributions, 0.0j)
    ray_contribution = const * image_sum
    quantized = complex(
        float32(ray_contribution.real),
        float32(ray_contribution.imag),
    )
    for row in rows:
        row.update(complex_columns("image_sum", image_sum))
        row.update(complex_columns("ray_contribution", ray_contribution))
        row.update(complex_columns("complex64_increment", quantized))
        assert set(row) == set(CSV_COLUMNS)

    manifest: dict[str, object] = {
        "schema": "bellhop.fortran.cartesian_cerveny_influence_sample",
        "schema_version": 1,
        "status": "complete",
        "file_root": "synthetic",
        "source_index": 1,
        "launch_angle_index": 2,
        "receiver_range_index": 4,
        "receiver_depth_index": 3,
        "launch_angle_rad": launch_angle,
        "receiver_range_m": receiver_range,
        "receiver_depth_m": receiver_depth,
        "sea_surface_depth_m": surface_depth,
        "seabed_depth_m": seabed_depth,
        "frequency_hz": frequency,
        "source_sound_speed_m_per_s": 1500.0,
        "dalpha_rad": 0.01,
        "rloop_km": 10.0,
        "epsilon_multiplier": 1.0,
        "epsilon_real": epsilon.real,
        "epsilon_imag": epsilon.imag,
        "ratio1": ratio1,
        "radius_max_m": radius_max,
        "beam_window_squared": beam_window,
        "image_count": 3,
        "run_type": "CC R",
        "beam_type": "CMS",
        "beam_width_mode": "minimum",
        "contribution_stage":
            "complex128_before_complex64_accumulation_and_scale_pressure",
        "images_file": "influence_images.csv",
        "ray_points_file": "ray_points.csv",
        "selected_range_evaluation_count": 1,
        "evaluation_count": 1,
        "image_row_count": 3,
        "index_base": 1,
        "limitations": [],
    }
    return manifest, rows, points


class InfluenceOracleValidatorTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.directory = Path(self.temporary_directory.name)
        self.manifest, self.rows, self.points = make_fixture()
        self.write_fixture()

    def tearDown(self) -> None:
        self.temporary_directory.cleanup()

    def write_fixture(self, *, include_ray_points: bool = True) -> None:
        (self.directory / "influence_manifest.json").write_text(
            json.dumps(self.manifest), encoding="utf-8"
        )
        with (self.directory / "influence_images.csv").open(
            "w", newline="", encoding="utf-8"
        ) as stream:
            writer = csv.DictWriter(stream, fieldnames=CSV_COLUMNS)
            writer.writeheader()
            writer.writerows(self.rows)
        points_path = self.directory / "ray_points.csv"
        if include_ray_points:
            with points_path.open("w", newline="", encoding="utf-8") as stream:
                writer = csv.DictWriter(stream, fieldnames=tuple(self.points[0]))
                writer.writeheader()
                writer.writerows(self.points)
        elif points_path.exists():
            points_path.unlink()

    def rewrite(self, *, include_ray_points: bool = True) -> None:
        self.write_fixture(include_ray_points=include_ray_points)

    def test_valid_sample_with_ray_cross_check(self) -> None:
        summary = validate_oracle(self.directory)
        self.assertEqual(summary["schema_version"], 1)
        self.assertEqual(summary["image_count"], 3)
        self.assertEqual(summary["window_pass_count"], 1)
        self.assertTrue(summary["ray_points_cross_checked"])

    def test_valid_sample_without_ray_points(self) -> None:
        self.rewrite(include_ray_points=False)
        self.assertFalse(validate_oracle(self.directory)["ray_points_cross_checked"])

    def test_rejects_non_finite_field(self) -> None:
        self.rows[0]["gamma_real"] = math.inf
        self.rewrite()
        with self.assertRaisesRegex(ValueError, "non-finite"):
            validate_oracle(self.directory)

    def test_rejects_image_count_mismatch(self) -> None:
        self.manifest["image_count"] = 2
        self.rewrite()
        with self.assertRaisesRegex(ValueError, "count mismatch"):
            validate_oracle(self.directory)

    def test_rejects_image_order(self) -> None:
        self.rows[1]["image_kind"] = "bottom"
        self.rewrite()
        with self.assertRaisesRegex(ValueError, "true/surface/bottom"):
            validate_oracle(self.directory)

    def test_rejects_wrong_window_decision(self) -> None:
        self.rows[1]["window_pass"] = 1
        self.rewrite()
        with self.assertRaisesRegex(ValueError, "window decision"):
            validate_oracle(self.directory)

    def test_rejects_wrong_hermite_taper(self) -> None:
        self.rows[0]["hermite_taper"] = 0.5
        self.rewrite()
        with self.assertRaisesRegex(ValueError, "Hermite"):
            validate_oracle(self.directory)

    def test_rejects_wrong_image_contribution(self) -> None:
        self.rows[0]["image_contribution_real"] += 0.25
        self.rewrite()
        with self.assertRaisesRegex(ValueError, "contribution"):
            validate_oracle(self.directory)

    def test_rejects_wrong_ordered_image_sum(self) -> None:
        for row in self.rows:
            row["image_sum_imag"] += 0.5
        self.rewrite()
        with self.assertRaisesRegex(ValueError, "ordered image sum"):
            validate_oracle(self.directory)

    def test_rejects_wrong_const_times_sum(self) -> None:
        for row in self.rows:
            row["ray_contribution_real"] += 1.0
        self.rewrite()
        with self.assertRaisesRegex(ValueError, "ray contribution"):
            validate_oracle(self.directory)

    def test_rejects_wrong_complex64_increment(self) -> None:
        for row in self.rows:
            row["complex64_increment_real"] += 1.0
        self.rewrite()
        with self.assertRaisesRegex(ValueError, "complex64"):
            validate_oracle(self.directory)

    def test_rejects_bad_interpolation_against_ray_points(self) -> None:
        for row in self.rows:
            row["interpolation_weight"] = 0.25
        self.rewrite()
        with self.assertRaisesRegex(
            ValueError, "q interpolation|interpolation weight"
        ):
            validate_oracle(self.directory)

    def test_rejects_bad_kmah_against_ray_history(self) -> None:
        for row in self.rows:
            row["kmah_left"] = -1
        self.rewrite()
        with self.assertRaisesRegex(ValueError, "kmah_left"):
            validate_oracle(self.directory)


if __name__ == "__main__":
    unittest.main()
