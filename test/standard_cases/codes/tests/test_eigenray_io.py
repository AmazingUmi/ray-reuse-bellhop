from __future__ import annotations

from pathlib import Path
import tempfile
import unittest
import sys


CODES_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(CODES_ROOT))

from eigenray_io import parse_eigenray


HEADER = """'eigenray fixture'
250.0
1 1 1
5 1
0.0
100.0
'rz'
"""


class EigenrayReaderTests(unittest.TestCase):
    def write(self, root: Path, text: str) -> Path:
        path = root / "fixture.ray"
        path.write_text(text, encoding="ascii")
        return path

    def test_zero_blocks_are_valid(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            output = parse_eigenray(self.write(Path(directory), HEADER))
        self.assertEqual(output.rays, ())

    def test_eof_blocks_allow_repeated_angles_and_nalpha_mismatch(self) -> None:
        text = HEADER + """30.0
2 0 1
0.0 50.0
10.0 100.0
30.0
1 1 0
0.0 50.0
"""
        with tempfile.TemporaryDirectory() as directory:
            output = parse_eigenray(self.write(Path(directory), text))
        self.assertEqual(len(output.rays), 2)
        self.assertEqual(output.rays[0].launch_angle_deg, output.rays[1].launch_angle_deg)
        self.assertEqual(output.rays[0].bottom_bounces, 1)

    def test_truncated_block_is_rejected(self) -> None:
        text = HEADER + "30.0\n2 0 0\n0.0 50.0\n"
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(ValueError, "truncated"):
                parse_eigenray(self.write(Path(directory), text))

    def test_nonfinite_and_negative_counts_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(ValueError, "non-finite"):
                parse_eigenray(self.write(Path(directory), HEADER + "nan\n"))
            with self.assertRaisesRegex(ValueError, "non-negative"):
                parse_eigenray(self.write(Path(directory), HEADER + "30.0\n1 -1 0\n0.0 50.0\n"))


if __name__ == "__main__":
    unittest.main()
