from __future__ import annotations

import csv
import json
import sys
import tempfile
import unittest
from pathlib import Path


CODES_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(CODES_DIR))

from compare_f2cpp_geometry_oracle import validate_probe_manifest


COLUMNS = [
    "point_index",
    "point_kind",
    "step_valid",
    "incoming_step_index",
    "r_m",
]


class IntermediateStateMatrixTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.directory = Path(self.temporary.name)
        self.csv_path = self.directory / "ray_points.csv"
        with self.csv_path.open("w", newline="", encoding="utf-8") as stream:
            writer = csv.writer(stream)
            writer.writerow(COLUMNS)
            writer.writerow([1, "source", 0, 0, 0.0])
        self.manifest_path = Path(str(self.csv_path) + ".manifest.json")
        self.manifest = {
            "schema": "bellhop.cpp.ray_path_probe",
            "schema_version": 1,
            "contract_version": 1,
            "producer": "f2cpp",
            "status": "complete",
            "points_file": self.csv_path.name,
            "point_count": 1,
            "integrated_step_count": 0,
            "reflection_event_count": 0,
            "termination": "ExitedDomain",
            "index_base": 1,
            "numeric_precision": "binary64",
            "units": "SI",
            "columns": COLUMNS,
        }
        self._write_manifest()

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def _write_manifest(self) -> None:
        self.manifest_path.write_text(
            json.dumps(self.manifest), encoding="utf-8"
        )

    def validate(self) -> dict[str, object]:
        return validate_probe_manifest(
            self.manifest_path,
            expected_producer="f2cpp",
            csv_path=self.csv_path,
            point_count=1,
            step_count=0,
            termination="ExitedDomain",
        )

    def test_accepts_frozen_schema(self) -> None:
        self.assertEqual(self.validate()["contract_version"], 1)

    def test_rejects_wrong_producer(self) -> None:
        self.manifest["producer"] = "rayreuse"
        self._write_manifest()
        with self.assertRaisesRegex(ValueError, "producer"):
            self.validate()

    def test_rejects_column_drift(self) -> None:
        self.manifest["columns"] = list(reversed(COLUMNS))
        self._write_manifest()
        with self.assertRaisesRegex(ValueError, "columns"):
            self.validate()


if __name__ == "__main__":
    unittest.main()
