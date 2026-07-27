from __future__ import annotations

import csv
import json
from pathlib import Path
import sys
import tempfile
import unittest


CODES_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(CODES_ROOT))

from validate_ray_oracle import validate_oracle


POINT_COLUMNS = (
    "point_index",
    "point_kind",
    "step_valid",
    "incoming_step_index",
    "r_m",
    "z_m",
    "t_r_s_per_m",
    "t_z_s_per_m",
    "p1",
    "p2",
    "q1",
    "q2",
    "c_m_per_s",
    "tau_real_s",
    "tau_imag_s",
    "amplitude",
    "phase_rad",
    "num_top_bounces",
    "num_bottom_bounces",
    "h_m",
    "hw0_m",
    "hw1_m",
)


def point_row(
    index: int,
    kind: str,
    *,
    step_valid: int,
    incoming_step_index: int,
    depth: float,
    slowness_depth: float,
    bottom_bounces: int,
) -> dict[str, object]:
    return {
        "point_index": index,
        "point_kind": kind,
        "step_valid": step_valid,
        "incoming_step_index": incoming_step_index,
        "r_m": 10.0 if index > 1 else 0.0,
        "z_m": depth,
        "t_r_s_per_m": 0.0,
        "t_z_s_per_m": slowness_depth,
        "p1": 1.0,
        "p2": 0.0,
        "q1": 100.0,
        "q2": 1.0,
        "c_m_per_s": 1500.0,
        "tau_real_s": 0.1 if index > 1 else 0.0,
        "tau_imag_s": 0.0,
        "amplitude": 1.0,
        "phase_rad": 0.0,
        "num_top_bounces": 0,
        "num_bottom_bounces": bottom_bounces,
        "h_m": 10.0 if step_valid else 0.0,
        "hw0_m": 9.99 if step_valid else 0.0,
        "hw1_m": 0.01 if step_valid else 0.0,
    }


def valid_event() -> dict[str, object]:
    slowness = 1.0 / 1500.0
    return {
        "event_index": 1,
        "pre_point_index": 2,
        "post_point_index": 3,
        "boundary": "seabed",
        "boundary_condition": "R",
        "boundary_segment_index": 1,
        "pre_r_m": 10.0,
        "pre_z_m": 100.0,
        "post_r_m": 10.0,
        "post_z_m": 100.0,
        "tangent_r": 1.0,
        "tangent_z": 0.0,
        "normal_r": 0.0,
        "normal_z": 1.0,
        "incident_t_r_s_per_m": 0.0,
        "incident_t_z_s_per_m": slowness,
        "reflected_t_r_s_per_m": 0.0,
        "reflected_t_z_s_per_m": -slowness,
        "incident_p1": 1.0,
        "incident_p2": 0.0,
        "incident_q1": 100.0,
        "incident_q2": 1.0,
        "reflected_p1": 1.0,
        "reflected_p2": 0.0,
        "reflected_q1": 100.0,
        "reflected_q2": 1.0,
        "incident_sound_speed_m_per_s": 1500.0,
        "reflected_sound_speed_m_per_s": 1500.0,
        "incident_tau_real_s": 0.1,
        "incident_tau_imag_s": 0.0,
        "reflected_tau_real_s": 0.1,
        "reflected_tau_imag_s": 0.0,
        "tangent_slowness_s_per_m": 0.0,
        "normal_slowness_s_per_m": slowness,
        "boundary_curvature_per_m": 0.0,
        "reflection_coefficient_real": 1.0,
        "reflection_coefficient_imag": 0.0,
        "reflection_magnitude": 1.0,
        "reflection_phase_rad": 0.0,
        "incident_amplitude": 1.0,
        "incident_phase_rad": 0.0,
        "reflected_amplitude": 1.0,
        "reflected_phase_rad": 0.0,
        "coefficient_suppressed": 0,
        "beam_shift_applied": 0,
    }


class RayOracleValidatorTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.directory = Path(self.temporary_directory.name)
        self.event = valid_event()
        self.write_fixture(schema_version=2)

    def tearDown(self) -> None:
        self.temporary_directory.cleanup()

    def write_fixture(self, schema_version: int) -> None:
        points = [
            point_row(
                1,
                "source",
                step_valid=0,
                incoming_step_index=0,
                depth=50.0,
                slowness_depth=0.0,
                bottom_bounces=0,
            ),
            point_row(
                2,
                "integrated",
                step_valid=1,
                incoming_step_index=1,
                depth=100.0,
                slowness_depth=1.0 / 1500.0,
                bottom_bounces=0,
            ),
            point_row(
                3,
                "bottom_reflection",
                step_valid=0,
                incoming_step_index=0,
                depth=100.0,
                slowness_depth=-1.0 / 1500.0,
                bottom_bounces=1,
            ),
        ]
        with (self.directory / "ray_points.csv").open(
            "w", newline="", encoding="utf-8"
        ) as stream:
            writer = csv.DictWriter(stream, fieldnames=POINT_COLUMNS)
            writer.writeheader()
            writer.writerows(points)
        with (self.directory / "reflection_events.csv").open(
            "w", newline="", encoding="utf-8"
        ) as stream:
            writer = csv.DictWriter(
                stream, fieldnames=tuple(self.event)
            )
            writer.writeheader()
            writer.writerow(self.event)

        manifest: dict[str, object] = {
            "schema": "bellhop.fortran.ray_step_oracle",
            "schema_version": schema_version,
            "status": "complete",
            "points_file": "ray_points.csv",
            "point_count": 3,
            "integrated_step_count": 1,
            "reported_nsteps": 3,
            "termination_reason": "spatial_box_range",
        }
        if schema_version >= 2:
            manifest.update(
                {
                    "frequency_hz": 250.0,
                    "reflection_events_file":
                        "reflection_events.csv",
                    "reflection_event_count": 1,
                    "boundary_curvature_mode": "standard",
                    "beam_shift_enabled": False,
                }
            )
        (self.directory / "manifest.json").write_text(
            json.dumps(manifest), encoding="utf-8"
        )

    def test_valid_v2_reflection_event(self) -> None:
        summary = validate_oracle(self.directory)
        self.assertEqual(summary["schema_version"], 2)
        self.assertEqual(summary["reflection_event_count"], 1)

    def test_v1_backward_compatibility(self) -> None:
        self.write_fixture(schema_version=1)
        summary = validate_oracle(self.directory)
        self.assertEqual(summary["schema_version"], 1)

    def test_rejects_wrong_rigid_coefficient(self) -> None:
        self.event["reflection_coefficient_real"] = -1.0
        self.write_fixture(schema_version=2)
        with self.assertRaises(ValueError):
            validate_oracle(self.directory)

    def test_rejects_invalid_event_point_mapping(self) -> None:
        self.event["post_point_index"] = 2
        self.write_fixture(schema_version=2)
        with self.assertRaisesRegex(ValueError, "pre/post point indices"):
            validate_oracle(self.directory)

    def test_rejects_non_mirror_slowness(self) -> None:
        self.event["reflected_t_z_s_per_m"] = 1.0 / 1500.0
        self.write_fixture(schema_version=2)
        with self.assertRaises(ValueError):
            validate_oracle(self.directory)


if __name__ == "__main__":
    unittest.main()
