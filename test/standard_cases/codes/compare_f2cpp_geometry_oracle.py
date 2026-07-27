#!/usr/bin/env python3
"""Compare the F2CPP geometry probe with a Fortran oracle directory."""

from __future__ import annotations

import argparse
import csv
import json
import math
import subprocess
import tempfile
from pathlib import Path


FIELD_RULES = {
    "r_m": ("r_m", 1e-8, 1e-10),
    "z_m": ("z_m", 1e-8, 1e-10),
    "t_r_s_per_m": ("t_r_s_per_m", 1e-13, 1e-10),
    "t_z_s_per_m": ("t_z_s_per_m", 1e-13, 1e-10),
    "p1": ("p1", 1e-12, 1e-9),
    "p2": ("p2", 1e-12, 1e-9),
    "q1": ("q1", 1e-12, 1e-9),
    "q2": ("q2", 1e-12, 1e-9),
    "c_m_per_s": ("c_m_per_s", 1e-9, 1e-11),
    "tau_real_s": ("tau_real_s", 1e-12, 1e-10),
    "h_m": ("h_m", 1e-8, 1e-10),
    "hw0_m": ("hw0_m", 1e-8, 1e-10),
    "hw1_m": ("hw1_m", 1e-8, 1e-10),
    "mid_r_m": ("mid_r_m", 1e-8, 1e-10),
    "mid_z_m": ("mid_z_m", 1e-8, 1e-10),
}


def load_csv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as stream:
        return list(csv.DictReader(stream))


def compare(
    oracle_dir: Path, probe: Path, probe_configuration: str
) -> dict[str, object]:
    manifest = json.loads(
        (oracle_dir / "manifest.json").read_text(encoding="utf-8")
    )
    oracle_rows = load_csv(oracle_dir / manifest["points_file"])
    with tempfile.TemporaryDirectory(prefix="f2cpp-geometry-oracle-") as temp:
        f2cpp_csv = Path(temp) / "ray_points.csv"
        probe_command = [
            str(probe),
            str(f2cpp_csv),
            repr(float(manifest["launch_angle_rad"])),
        ]
        if probe_configuration == "vacuum-rigid":
            probe_command.extend(["100.0", "50.0", "101.0", "100000"])
        elif probe_configuration == "munk":
            probe_command.append("munk")
        completed = subprocess.run(
            probe_command,
            check=True,
            capture_output=True,
            text=True,
        )
        f2cpp_rows = load_csv(f2cpp_csv)

    summary_fields = completed.stdout.strip().split(",")
    if len(summary_fields) != 3:
        raise ValueError(f"unexpected F2CPP probe summary: {completed.stdout!r}")
    point_count, step_count = map(int, summary_fields[:2])
    termination = summary_fields[2]
    expected_step_count = int(manifest["integrated_step_count"])
    if point_count != len(oracle_rows) or len(f2cpp_rows) != len(oracle_rows):
        raise ValueError("F2CPP/Fortran point-count mismatch")
    if step_count != expected_step_count:
        raise ValueError("F2CPP/Fortran integrated-step mismatch")
    if termination != "ExitedDomain":
        raise ValueError(f"unexpected F2CPP termination: {termination!r}")
    if manifest["termination_reason"] != "spatial_box_range":
        raise ValueError("Fortran oracle did not terminate at the range box")

    worst = {
        "scaled_error": 0.0,
        "absolute_error": 0.0,
        "field": "",
        "point_index": 0,
    }
    for index, (actual_row, expected_row) in enumerate(
        zip(f2cpp_rows, oracle_rows), start=1
    ):
        if int(actual_row["point_index"]) != index:
            raise ValueError("F2CPP point indices are not contiguous")
        for discrete_field in (
            "point_kind",
            "step_valid",
            "incoming_step_index",
            "num_top_bounces",
            "num_bottom_bounces",
        ):
            if actual_row[discrete_field] != expected_row[discrete_field]:
                raise ValueError(
                    f"point {index} field {discrete_field}: "
                    f"{actual_row[discrete_field]!r} != "
                    f"{expected_row[discrete_field]!r}"
                )
        for actual_name, (
            expected_name,
            absolute_tolerance,
            relative_tolerance,
        ) in FIELD_RULES.items():
            actual = float(actual_row[actual_name])
            expected = float(expected_row[expected_name])
            if not math.isfinite(actual):
                raise ValueError(
                    f"point {index} field {actual_name} is non-finite"
                )
            absolute_error = abs(actual - expected)
            tolerance = (
                absolute_tolerance
                + relative_tolerance * abs(expected)
            )
            scaled_error = absolute_error / tolerance
            if scaled_error > worst["scaled_error"]:
                worst = {
                    "scaled_error": scaled_error,
                    "absolute_error": absolute_error,
                    "field": actual_name,
                    "point_index": index,
                }
            if absolute_error > tolerance:
                raise ValueError(
                    f"point {index} field {actual_name}: "
                    f"error {absolute_error:.17e} exceeds "
                    f"tolerance {tolerance:.17e}"
                )

    return {
        "point_count": point_count,
        "integrated_step_count": step_count,
        "termination": termination,
        "worst_comparison": worst,
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("oracle_dir", type=Path)
    parser.add_argument("f2cpp_probe", type=Path)
    parser.add_argument(
        "--probe-configuration",
        choices=("direct", "vacuum-rigid", "munk"),
        default="direct",
    )
    args = parser.parse_args()
    print(
        json.dumps(
            compare(
                args.oracle_dir,
                args.f2cpp_probe,
                args.probe_configuration,
            ),
            indent=2,
            sort_keys=True,
        )
    )


if __name__ == "__main__":
    main()
