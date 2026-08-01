from __future__ import annotations

from pathlib import Path
import json
import math
import sys
import tempfile
import unittest


CODES_ROOT = Path(__file__).resolve().parents[1]
PROJECT_ROOT = CODES_ROOT.parents[2]
PLOTREAD_TESTS_ROOT = PROJECT_ROOT / "test" / "PlotRead" / "tests"
sys.path.insert(0, str(CODES_ROOT))
sys.path.insert(0, str(PLOTREAD_TESTS_ROOT))

from reference_snapshots import (
    compare_pressure_values,
    create_from_manifest,
    create_snapshot,
    load_reference,
    load_tolerances,
    spaced_indices,
    validate_candidate,
)
from support import write_little_endian_rectilinear_file


class ReferenceSnapshotTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.tolerances = load_tolerances(CODES_ROOT / "tolerances.toml")

    def test_spaced_indices_include_endpoints_without_duplicates(self) -> None:
        self.assertEqual(spaced_indices(1, 5), (0,))
        self.assertEqual(spaced_indices(11, 1), (0,))
        self.assertEqual(spaced_indices(3, 5), (0, 1, 2))
        self.assertEqual(spaced_indices(11, 5), (0, 2, 5, 8, 10))

    def test_snapshot_records_coordinates_values_and_provenance(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            environment = root / "fixture.env"
            shade = root / "fixture.shd"
            executable = root / "bellhop"
            environment.write_text("fixture environment\n", encoding="utf-8")
            executable.write_bytes(b"fixture executable")
            write_little_endian_rectilinear_file(shade, (50.0, 250.0))

            snapshot = create_snapshot(
                case_id="fixture",
                profile="single",
                oracle_version="origin",
                source_revision="abc1234",
                environment_path=environment,
                shade_path=shade,
                executable_path=executable,
                frequency_index=1,
            )

        self.assertEqual(snapshot["schema_version"], 1)
        self.assertEqual(snapshot["frequency_hz"], 250.0)
        self.assertEqual(snapshot["shd"]["dimensions"], [2, 1, 1, 1, 1, 2, 3])
        self.assertEqual(len(snapshot["samples"]), 6)
        last_sample = snapshot["samples"][-1]
        self.assertEqual(last_sample["id"], "b000_sz000_rz001_rr002")
        self.assertEqual(
            last_sample["selection"], ["grid", "max_magnitude"]
        )
        self.assertEqual(
            last_sample["coordinates"]["receiver_range_m"], 3000.0
        )
        self.assertAlmostEqual(last_sample["pressure"]["real"], 0.223)
        self.assertAlmostEqual(last_sample["pressure"]["imag"], -0.1115)
        self.assertEqual(
            snapshot["artifacts"]["executable"]["name"], "bellhop"
        )

    def test_passed_single_manifest_resolves_relative_artifacts(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            run_directory = root / "f000_50Hz"
            run_directory.mkdir()
            environment = run_directory / "fixture.env"
            shade = run_directory / "fixture.shd"
            executable = root / "bellhop"
            environment.write_text("fixture environment\n", encoding="utf-8")
            executable.write_bytes(b"fixture executable")
            write_little_endian_rectilinear_file(shade, (50.0,))
            manifest = root / "run_manifest.json"
            manifest.write_text(
                json.dumps(
                    {
                        "schema_version": 1,
                        "version": "origin",
                        "executable": str(executable),
                        "case_id": "fixture",
                        "profile": "single",
                        "runs": [
                            {
                                "status": "passed",
                                "environment_file": "f000_50Hz/fixture.env",
                                "shade_file": "f000_50Hz/fixture.shd",
                            }
                        ],
                    }
                ),
                encoding="utf-8",
            )

            snapshot = create_from_manifest(manifest, "abc1234")

        self.assertEqual(snapshot["case_id"], "fixture")
        self.assertEqual(snapshot["source_revision"], "abc1234")

    def test_all_committed_references_are_internally_consistent(self) -> None:
        reference_root = (
            PROJECT_ROOT
            / "test"
            / "standard_cases"
            / "results"
            / "reference"
            / "origin"
            / "single"
        )
        references = sorted(
            path
            for path in reference_root.glob("*.json")
            if not path.name.startswith("._")
        )
        self.assertEqual(len(references), 6)
        self.assertEqual(
            {load_reference(path)["case_id"] for path in references},
            {
                "constant_speed_acoustic_bottom",
                "constant_speed_direct",
                "constant_speed_no_attenuation_5khz",
                "constant_speed_thorp",
                "constant_speed_vacuum_rigid",
                "munk_cerveny_cc",
            },
        )

    def test_candidate_identical_to_reference_passes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            environment = root / "fixture.env"
            shade = root / "fixture.shd"
            executable = root / "bellhop"
            reference_path = root / "reference.json"
            environment.touch()
            executable.touch()
            write_little_endian_rectilinear_file(shade, (50.0,))
            reference_path.write_text(
                json.dumps(
                    create_snapshot(
                        case_id="fixture",
                        profile="single",
                        oracle_version="origin",
                        source_revision="abc1234",
                        environment_path=environment,
                        shade_path=shade,
                        executable_path=executable,
                    )
                ),
                encoding="utf-8",
            )

            passed, report = validate_candidate(
                reference_path,
                shade,
                0,
                CODES_ROOT / "tolerances.toml",
            )

        self.assertTrue(passed)
        self.assertTrue(report["passed"])
        self.assertEqual(report["failures"], [])
        self.assertEqual(report["maxima"]["pressure_absolute"], 0.0)

    def test_pressure_and_phase_failures_are_reported(self) -> None:
        metrics = compare_pressure_values(
            1.0 + 0.0j,
            complex(math.cos(0.002), math.sin(0.002)),
            self.tolerances,
        )
        self.assertTrue(metrics["phase_compared"])
        self.assertFalse(metrics["phase_passed"])
        self.assertFalse(metrics["pressure_passed"])
        self.assertFalse(metrics["passed"])

    def test_phase_is_skipped_below_floor(self) -> None:
        metrics = compare_pressure_values(
            1.0e-5 + 0.0j,
            0.0 + 1.0e-5j,
            self.tolerances,
        )
        self.assertFalse(metrics["phase_compared"])
        self.assertIsNone(metrics["phase_difference_rad"])

    def test_snapshot_rejects_empty_identity(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            environment = root / "fixture.env"
            shade = root / "fixture.shd"
            executable = root / "bellhop"
            environment.touch()
            executable.touch()
            write_little_endian_rectilinear_file(shade, (50.0,))
            with self.assertRaisesRegex(ValueError, "identity"):
                create_snapshot(
                    case_id="",
                    profile="single",
                    oracle_version="origin",
                    source_revision="abc1234",
                    environment_path=environment,
                    shade_path=shade,
                    executable_path=executable,
                )


if __name__ == "__main__":
    unittest.main()
