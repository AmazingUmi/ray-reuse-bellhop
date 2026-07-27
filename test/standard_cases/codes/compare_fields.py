from __future__ import annotations

import argparse
from pathlib import Path
import sys
import tomllib


STANDARD_CASES_ROOT = Path(__file__).resolve().parents[1]
PROJECT_ROOT = STANDARD_CASES_ROOT.parents[1]
sys.path.insert(0, str(PROJECT_ROOT / "test" / "PlotRead"))

import numpy as np

from bellhop_io_py.shd import ShdReader


def compare_files(
    reference_path: Path,
    candidate_path: Path,
    reference_frequency_index: int,
    candidate_frequency_index: int,
    tolerances_path: Path,
) -> tuple[bool, dict[str, float]]:
    with tolerances_path.open("rb") as stream:
        tolerances = tomllib.load(stream)

    reference_reader = ShdReader(reference_path)
    candidate_reader = ShdReader(candidate_path)
    reference = reference_reader.read(
        frequency_index=reference_frequency_index
    )
    candidate = candidate_reader.read(
        frequency_index=candidate_frequency_index
    )

    coordinate_pairs = {
        "bearings": (
            reference.header.bearings_deg,
            candidate.header.bearings_deg,
        ),
        "source depths": (
            reference.header.source_depths_m,
            candidate.header.source_depths_m,
        ),
        "receiver depths": (
            reference.header.receiver_depths_m,
            candidate.header.receiver_depths_m,
        ),
        "receiver ranges": (
            reference.header.receiver_ranges_m,
            candidate.header.receiver_ranges_m,
        ),
    }
    for label, (reference_axis, candidate_axis) in coordinate_pairs.items():
        if not np.array_equal(reference_axis, candidate_axis):
            raise SystemExit(f"{label} mismatch")
    if not np.isclose(reference.frequency_hz, candidate.frequency_hz):
        raise SystemExit(
            f"frequency mismatch: {reference.frequency_hz} != "
            f"{candidate.frequency_hz}"
        )

    if reference.pressure.shape != candidate.pressure.shape:
        raise SystemExit(
            f"shape mismatch: {reference.pressure.shape} != "
            f"{candidate.pressure.shape}"
        )

    difference = np.abs(candidate.pressure - reference.pressure)
    reference_magnitude = np.abs(reference.pressure)
    pressure_rules = tolerances["pressure"]
    relative = difference / np.maximum(
        reference_magnitude, float(pressure_rules["relative_floor"])
    )
    max_absolute = float(np.max(difference))
    max_relative = float(np.max(relative))
    pressure_passed = bool(
        np.all(
            difference
            <= float(pressure_rules["absolute"])
            + float(pressure_rules["relative"]) * reference_magnitude
        )
    )

    tl_rules = tolerances["transmission_loss"]
    mask = reference_magnitude > float(tl_rules["pressure_floor"])
    if np.any(mask):
        reference_tl = -20.0 * np.log10(reference_magnitude[mask])
        candidate_tl = -20.0 * np.log10(
            np.maximum(
                np.abs(candidate.pressure[mask]),
                float(tl_rules["pressure_floor"]),
            )
        )
        max_tl_db = float(np.max(np.abs(candidate_tl - reference_tl)))
    else:
        max_tl_db = 0.0

    passed = pressure_passed and max_tl_db <= float(
        tl_rules["absolute_db"]
    )
    return passed, {
        "max_pressure_absolute": max_absolute,
        "max_pressure_relative": max_relative,
        "max_tl_difference_db": max_tl_db,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Compare two Bellhop SHD frequency slices."
    )
    parser.add_argument("reference", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--reference-frequency-index", type=int, default=0)
    parser.add_argument("--candidate-frequency-index", type=int, default=0)
    parser.add_argument(
        "--tolerances",
        type=Path,
        default=STANDARD_CASES_ROOT / "codes" / "tolerances.toml",
    )
    args = parser.parse_args(argv)
    passed, metrics = compare_files(
        args.reference,
        args.candidate,
        args.reference_frequency_index,
        args.candidate_frequency_index,
        args.tolerances,
    )
    for key, value in metrics.items():
        print(f"{key}={value:.9g}")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
