from __future__ import annotations

from pathlib import Path
import json
import stat
import tempfile
import unittest

import sys

CODES_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(CODES_ROOT))

from validate_i8_output_safety import product_info, validate


class OutputSafetyValidatorTests(unittest.TestCase):
    def _fixture_dir(self, root: Path) -> Path:
        fixtures = root / "fixtures"
        fixtures.mkdir()
        for name, product in (
            ("cli_coherent.env", "shd"),
            ("cli_ray.env", "ray"),
            ("cli_arrival_ascii.env", "arr"),
            ("cli_eigenray_gaussian.env", "ray"),
            ("cli_arrival_binary.env", "arr"),
            ("cli_eigenray_zero.env", "ray"),
            ("cli_solver_failure.env", "solve_failure"),
        ):
            (fixtures / name).write_text(product + "\n", encoding="ascii")
        (fixtures / "cli_solver_failure.ssp").write_text("solver companion\n", encoding="ascii")
        return fixtures

    def _executable(self, root: Path) -> Path:
        executable = root / "fake_bellhop.py"
        executable.write_text(
            "#!/usr/bin/env python3\n"
            "import pathlib, sys\n"
            "root = pathlib.Path(sys.argv[1])\n"
            "env = root.with_suffix('.env').read_text()\n"
            "if env.startswith('invalid'):\n"
            "  root.with_suffix('.prt').write_text('FATAL ERROR\\n')\n"
            "  raise SystemExit(1)\n"
            "product = env.strip()\n"
            "if product == 'solve_failure':\n"
            "  root.with_suffix('.prt').write_text('FATAL ERROR: solve failure\\n')\n"
            "  print('solve failure', file=sys.stderr)\n"
            "  raise SystemExit(1)\n"
            "target = root.with_suffix('.' + product)\n"
            "if target.exists() and target.is_dir():\n"
            "  root.with_suffix('.prt').write_text('FATAL ERROR\\n')\n"
            "  raise SystemExit(1)\n"
            "for suffix in ('shd', 'ray', 'arr'):\n"
            "  path = root.with_suffix('.' + suffix)\n"
            "  temp = root.with_suffix('.' + suffix + '.tmp')\n"
            "  if temp.exists(): temp.unlink()\n"
            "  if suffix != product and path.exists(): path.unlink()\n"
            "target.write_bytes(('product-' + product).encode())\n"
            "root.with_suffix('.prt').write_text('ok\\n')\n",
            encoding="utf-8",
        )
        executable.chmod(executable.stat().st_mode | stat.S_IXUSR)
        return executable

    def test_product_info_is_typed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.assertEqual(product_info(root / "missing")["type"], "missing")
            (root / "file").write_bytes(b"x")
            self.assertEqual(product_info(root / "file")["size_bytes"], 1)
            (root / "directory").mkdir()
            self.assertEqual(product_info(root / "directory")["type"], "directory")

    def test_full_matrix_is_deterministic_and_keeps_temp_paths_out(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            report_a = validate(self._executable(root), self._fixture_dir(root))
            report_b = validate(root / "fake_bellhop.py", root / "fixtures")
            encoded_a = json.dumps(report_a, indent=2, sort_keys=True) + "\n"
            encoded_b = json.dumps(report_b, indent=2, sort_keys=True) + "\n"
            self.assertEqual(encoded_a, encoded_b)
            self.assertEqual(report_a["status"], "passed")
            self.assertEqual([step["name"] for step in report_a["steps"][:6]], ["coherent_1", "ray", "arrivals_ascii", "arrivals_binary", "eigenray", "coherent_2"])
            self.assertIn("solve_failure_preserves_shd", [step["name"] for step in report_a["steps"]])
            self.assertNotIn("bellhop_f2cpp_i8_output_safety_", encoded_a)
            self.assertTrue(all(step["status"] == "passed" for step in report_a["steps"]))


if __name__ == "__main__":
    unittest.main()
