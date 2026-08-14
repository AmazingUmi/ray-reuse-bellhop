from __future__ import annotations

from pathlib import Path
import tempfile
import time
import unittest
import sys

CODES_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(CODES_ROOT))

from eigenray_io import EigenrayBlock, EigenrayHeader, EigenrayOutput
from validate_i8_eigenrays import _fresh, _manifest, _require_executable_identity, compare_eigenrays


def output(blocks: tuple[EigenrayBlock, ...]) -> EigenrayOutput:
    return EigenrayOutput(EigenrayHeader("test", 1000.0, (1, 1, 1), (5, 1), 0.0, 100.0, "rz"), blocks)


def block(angle: float = 10.0, points: tuple[tuple[float, float], ...] = ((0.0, 50.0), (10.0, 51.0))) -> EigenrayBlock:
    return EigenrayBlock(angle, 0, 1, points)


class EigenrayParityTests(unittest.TestCase):
    def test_order_prefix_and_coordinate_gates(self) -> None:
        reference = output((block(10.0), block(20.0)))
        self.assertEqual(compare_eigenrays(reference, reference, "same")["blocks"], 2)
        with self.assertRaisesRegex(ValueError, "launch-angle"):
            compare_eigenrays(reference, output((block(20.0), block(10.0))), "reordered")
        with self.assertRaisesRegex(ValueError, "block count"):
            compare_eigenrays(reference, output((block(10.0),)), "missing")
        with self.assertRaisesRegex(ValueError, "block count"):
            compare_eigenrays(output((block(10.0),)), reference, "extra")
        with self.assertRaisesRegex(ValueError, "point/bounce"):
            compare_eigenrays(output((block(),)), output((block(points=((0.0, 50.0),)),)), "prefix")
        with self.assertRaisesRegex(ValueError, "coordinate"):
            compare_eigenrays(output((block(),)), output((block(points=((0.0, 50.0), (10.01, 51.0))),)), "coordinate")
        with self.assertRaisesRegex(ValueError, "point/bounce"):
            compare_eigenrays(output((block(),)), output((EigenrayBlock(10.0, 1, 0, block().points_m),)), "bounce")

    def test_stale_and_identity_rejections(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            executable, environment, ray = root / "bellhop", root / "case.env", root / "case.ray"
            for path in (executable, environment, ray):
                path.write_text("x", encoding="ascii")
            executable.touch()
            with self.assertRaisesRegex(ValueError, "stale"):
                _fresh(ray, environment, executable, root / "manifest")
            manifest = root / "f2cpp" / "expected" / "single" / "run_manifest.json"
            manifest.parent.mkdir(parents=True)
            manifest.write_text('{"version":"f2cpp","case_id":"wrong","output_kind":"eigenray","last_stage":"test"}', encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "identity"):
                _manifest(root, "f2cpp", "expected")
            with self.assertRaisesRegex(ValueError, "executable identity"):
                _require_executable_identity({"executable": str(root / "other")}, executable, manifest)


if __name__ == "__main__":
    unittest.main()
