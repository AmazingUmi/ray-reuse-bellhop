from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path


CODES_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(CODES_DIR))

from microbenchmark_models import parse_stage_timings, summarize_samples


class MicrobenchmarkModelsTests(unittest.TestCase):
    def test_parses_fortran_profile_fields(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "case.prt"
            path.write_text(
                " Stage Trace seconds = 1.25E-02\n"
                " Stage Influence seconds = 2.5\n"
                " Stage Scale seconds = 3.0E-04\n"
                " Stage Output seconds = 4.0E-03\n",
                encoding="utf-8",
            )
            timings = parse_stage_timings(path, "origin")
        self.assertAlmostEqual(timings["trace_seconds"], 0.0125)
        self.assertAlmostEqual(timings["formula_core_seconds"], 2.5125)
        self.assertNotIn("project_seconds", timings)

    def test_parses_cpp_profile_fields_and_aggregates_project(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "case.prt"
            path.write_text(
                "Trace seconds = 1\n"
                "Project seconds = 2\n"
                "Influence seconds = 3\n"
                "Scale seconds = 4\n"
                "SHD seconds = 5\n",
                encoding="utf-8",
            )
            timings = parse_stage_timings(path, "f2cpp")
        self.assertEqual(timings["formula_core_seconds"], 6.0)
        self.assertEqual(timings["reported_stage_seconds"], 15.0)

    def test_rejects_missing_stage(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "case.prt"
            path.write_text("Trace seconds = 1\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "Project seconds"):
                parse_stage_timings(path, "rayreuse")

    def test_summarizes_each_field_by_median(self) -> None:
        summary = summarize_samples(
            [
                {"trace_seconds": 3.0, "formula_core_seconds": 5.0},
                {"trace_seconds": 1.0, "formula_core_seconds": 9.0},
                {"trace_seconds": 2.0, "formula_core_seconds": 7.0},
            ]
        )
        self.assertEqual(summary["sample_count"], 3)
        self.assertEqual(summary["median"]["trace_seconds"], 2.0)
        self.assertEqual(summary["median"]["formula_core_seconds"], 7.0)
        self.assertEqual(summary["minimum"]["trace_seconds"], 1.0)
        self.assertEqual(summary["maximum"]["trace_seconds"], 3.0)
        self.assertEqual(
            summary["median_absolute_deviation"]["trace_seconds"], 1.0
        )
        self.assertAlmostEqual(
            summary["coefficient_of_variation"]["trace_seconds"],
            0.408248290463863,
        )


if __name__ == "__main__":
    unittest.main()
