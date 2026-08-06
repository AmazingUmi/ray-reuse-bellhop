from __future__ import annotations

from pathlib import Path
import sys
import tempfile
import unittest


DEMO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(DEMO_ROOT / "codes"))

from reliability import output_paths, parse_versions
from rayreuse_multifrequency import parse_indexes


class ShowcaseTests(unittest.TestCase):
    def test_parse_versions_preserves_requested_order(self) -> None:
        self.assertEqual(
            parse_versions("rayreuse,origin"), ("rayreuse", "origin")
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

    def test_multifrequency_plot_indexes_are_explicit_and_bounded(self) -> None:
        self.assertEqual(parse_indexes("0,2,4", 5), (0, 2, 4))
        with self.assertRaisesRegex(ValueError, "duplicates"):
            parse_indexes("0,2,2", 5)
        with self.assertRaisesRegex(ValueError, "out of range"):
            parse_indexes("0,5", 5)


if __name__ == "__main__":
    unittest.main()
