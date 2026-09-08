from __future__ import annotations

from pathlib import Path
import sys
import tempfile
import unittest


DEMO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(DEMO_ROOT / "codes"))

from reliability import output_paths, parse_versions
from speed import (
    parse_routes,
    parse_timings,
    parse_workers,
    route_arguments,
    route_directory,
)


class ShowcaseTests(unittest.TestCase):
    def test_parse_versions_preserves_requested_order(self) -> None:
        self.assertEqual(
            parse_versions("broadband,origin"), ("broadband", "origin")
        )
        with self.assertRaisesRegex(ValueError, "duplicates"):
            parse_versions("origin,origin")
        with self.assertRaisesRegex(ValueError, "unknown versions"):
            parse_versions("origin,unknown")

    def test_each_version_gets_an_isolated_direct_run_root(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            output = output_paths(root, "f2cpp", "munk")

        self.assertEqual(output.root, root / "f2cpp" / "munk")
        self.assertEqual(output.environment.suffix, ".env")
        self.assertEqual(output.print_log.suffix, ".prt")
        self.assertEqual(output.shade.suffix, ".shd")

    def test_execution_routes_keep_nonreuse_first_as_reference(self) -> None:
        self.assertEqual(
            parse_routes("nonreuse,serial"), ("nonreuse", "serial")
        )
        with self.assertRaisesRegex(ValueError, "unknown routes"):
            parse_routes("nonreuse,unknown")
        with self.assertRaisesRegex(ValueError, "duplicates"):
            parse_routes("nonreuse,serial,serial")
        with self.assertRaisesRegex(ValueError, "first route"):
            parse_routes("serial,nonreuse")

    def test_execution_route_arguments_match_the_cli_contract(self) -> None:
        self.assertEqual(
            route_arguments("nonreuse", 2), ["--execution-mode", "nonreuse"]
        )
        self.assertEqual(
            route_arguments("serial", 2),
            ["--execution-mode", "reuse", "--reuse-mode", "serial"],
        )
        self.assertEqual(
            route_arguments("frequency", 3),
            [
                "--execution-mode",
                "reuse",
                "--reuse-mode",
                "frequency",
                "--reuse-workers",
                "3",
            ],
        )
        self.assertEqual(
            route_arguments("range", 2)[-2:],
            ["--reuse-workers", "2"],
        )

    def test_parallel_route_directories_encode_the_worker_count(self) -> None:
        self.assertEqual(route_directory("nonreuse", 2), "nonreuse")
        self.assertEqual(route_directory("serial", 2), "reuse_serial")
        self.assertEqual(route_directory("frequency", 4), "reuse_frequency_w4")
        self.assertEqual(route_directory("range", 2), "reuse_range_w2")
        with self.assertRaisesRegex(ValueError, "positive"):
            parse_workers(0)

    def test_prt_timing_lines_are_parsed_by_label(self) -> None:
        timings = parse_timings(
            "\n".join(
                [
                    "Trace seconds = 0.30167245799999998",
                    "Influence seconds = 17.837544677999986",
                    "parallel reuse wall seconds = 1.0",
                    "Total solver and product seconds = 18.281487250000001",
                    "phase criterion angles = 5000",
                    "",
                ]
            )
        )
        self.assertAlmostEqual(timings["Trace"], 0.30167245799999998)
        self.assertAlmostEqual(timings["Influence"], 17.837544677999986)
        self.assertAlmostEqual(timings["Total solver and product"], 18.28148725)
        self.assertNotIn("phase criterion angles", timings)


if __name__ == "__main__":
    unittest.main()
