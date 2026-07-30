from __future__ import annotations

from pathlib import Path
import sys
import tempfile
import unittest


sys.path.insert(0, str(Path(__file__).resolve().parent))

from evaluate_amortized_performance import Timing, evaluate, parse_prt


class AmortizedPerformanceTest(unittest.TestCase):
    def test_parse_and_evaluate_trace_once_model(self) -> None:
        contents = """\
ray count = 10
ray point count = 100
ray cache bytes = 12000
Trace seconds = 2.0
Project seconds = 1.0
Influence seconds = 3.0
Scale seconds = 0.1
SHD seconds = 0.1
"""
        with tempfile.TemporaryDirectory() as directory:
            path = (
                Path(directory)
                / "example"
                / "single"
                / "f000_50Hz"
                / "example.prt"
            )
            path.parent.mkdir(parents=True)
            path.write_text(contents, encoding="utf-8")
            timing = parse_prt(path)

        result = evaluate(timing, 16)
        self.assertEqual(timing.case_id, "example")
        self.assertAlmostEqual(timing.frequency_seconds, 4.2)
        self.assertAlmostEqual(result.repeated_seconds, 99.2)
        self.assertAlmostEqual(result.reused_seconds, 69.2)
        self.assertAlmostEqual(result.savings_percent, 30.241935483870968)

    def test_rejects_one_frequency(self) -> None:
        with self.assertRaisesRegex(ValueError, "at least two"):
            evaluate(
                Timing(
                    case_id="example",
                    trace_seconds=1.0,
                    project_seconds=1.0,
                    influence_seconds=1.0,
                    scale_seconds=1.0,
                    shd_seconds=1.0,
                    ray_count=1,
                    ray_point_count=2,
                    ray_cache_bytes=3,
                ),
                1,
            )

    def test_rejects_missing_fields(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "broken.prt"
            path.write_text("Trace seconds = 1.0\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "missing PRT fields"):
                parse_prt(path)

if __name__ == "__main__":
    unittest.main()
