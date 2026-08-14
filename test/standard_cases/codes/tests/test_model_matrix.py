from __future__ import annotations

import json
from pathlib import Path
import sys
import tempfile
import unittest


CODES_ROOT = Path(__file__).resolve().parents[1]
PROJECT_ROOT = CODES_ROOT.parents[2]
PLOTREAD_TESTS_ROOT = PROJECT_ROOT / "test" / "PlotRead" / "tests"
sys.path.insert(0, str(CODES_ROOT))
sys.path.insert(0, str(PLOTREAD_TESTS_ROOT))

from model_matrix import compare_slice_sets, load_manifest_slices
from support import write_little_endian_rectilinear_file


class ModelMatrixTests(unittest.TestCase):
    def test_rejects_ray_output_manifest(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            manifest = Path(temporary_directory) / "run_manifest.json"
            manifest.write_text(
                json.dumps(
                    {
                        "schema_version": 1,
                        "output_kind": "ray",
                        "runs": [],
                    }
                ),
                encoding="utf-8",
            )
            with self.assertRaisesRegex(ValueError, "requires SHD"):
                load_manifest_slices(manifest)

    def test_loads_broadband_manifest_frequency_slices(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            shade = root / "broadband.shd"
            write_little_endian_rectilinear_file(shade, (50.0, 250.0))
            manifest = root / "run_manifest.json"
            manifest.write_text(
                json.dumps(
                    {
                        "schema_version": 1,
                        "execution_model": "single_broadband_invocation",
                        "runs": [
                            {
                                "frequency_index": 0,
                                "frequency_hz": 50.0,
                                "shade_file": "broadband.shd",
                                "status": "passed",
                            },
                            {
                                "frequency_index": 1,
                                "frequency_hz": 250.0,
                                "shade_file": "broadband.shd",
                                "status": "passed",
                            },
                        ],
                    }
                ),
                encoding="utf-8",
            )

            slices = load_manifest_slices(manifest)

        self.assertEqual(tuple(slices), (50.0, 250.0))
        self.assertEqual(slices[50.0].frequency_index, 0)
        self.assertEqual(slices[250.0].frequency_index, 1)

    def test_identical_slice_sets_pass_full_field_comparison(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            shade = root / "broadband.shd"
            write_little_endian_rectilinear_file(shade, (50.0, 250.0))
            manifest = root / "run_manifest.json"
            manifest.write_text(
                json.dumps(
                    {
                        "schema_version": 1,
                        "execution_model": "single_broadband_invocation",
                        "runs": [
                            {
                                "frequency_index": index,
                                "frequency_hz": frequency,
                                "shade_file": "broadband.shd",
                                "status": "passed",
                            }
                            for index, frequency in enumerate((50.0, 250.0))
                        ],
                    }
                ),
                encoding="utf-8",
            )
            slices = load_manifest_slices(manifest)

            results = compare_slice_sets(
                reference_label="origin",
                candidate_label="rayreuse",
                reference_slices=slices,
                candidate_slices=slices,
                tolerances_path=CODES_ROOT / "tolerances.toml",
            )

        self.assertEqual(len(results), 2)
        self.assertTrue(all(result["passed"] for result in results))
        self.assertTrue(all(result["gating"] for result in results))
        self.assertEqual(
            results[1]["metrics"]["max_pressure_absolute"], 0.0
        )

    def test_shared_fmax_can_limit_f2cpp_gating_frequency(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            shade = root / "broadband.shd"
            write_little_endian_rectilinear_file(shade, (50.0, 250.0))
            manifest = root / "run_manifest.json"
            manifest.write_text(
                json.dumps(
                    {
                        "schema_version": 1,
                        "execution_model": "single_broadband_invocation",
                        "runs": [
                            {
                                "frequency_index": index,
                                "frequency_hz": frequency,
                                "shade_file": "broadband.shd",
                                "status": "passed",
                            }
                            for index, frequency in enumerate((50.0, 250.0))
                        ],
                    }
                ),
                encoding="utf-8",
            )
            slices = load_manifest_slices(manifest)

            results = compare_slice_sets(
                reference_label="origin",
                candidate_label="f2cpp",
                reference_slices=slices,
                candidate_slices=slices,
                tolerances_path=CODES_ROOT / "tolerances.toml",
                gating_frequencies={250.0},
                non_gating_reason="current-frequency launch fan",
            )

        self.assertFalse(results[0]["gating"])
        self.assertEqual(
            results[0]["non_gating_reason"],
            "current-frequency launch fan",
        )
        self.assertTrue(results[1]["gating"])

    def test_rejects_frequency_set_mismatch(self) -> None:
        with self.assertRaisesRegex(ValueError, "frequency mismatch"):
            compare_slice_sets(
                reference_label="origin",
                candidate_label="rayreuse",
                reference_slices={},
                candidate_slices={50.0: object()},
                tolerances_path=CODES_ROOT / "tolerances.toml",
            )


if __name__ == "__main__":
    unittest.main()
