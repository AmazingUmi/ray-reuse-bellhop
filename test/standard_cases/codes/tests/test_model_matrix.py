from __future__ import annotations

import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest


CODES_ROOT = Path(__file__).resolve().parents[1]
PROJECT_ROOT = CODES_ROOT.parents[2]
PLOTREAD_TESTS_ROOT = PROJECT_ROOT / "test" / "PlotRead" / "tests"
sys.path.insert(0, str(CODES_ROOT))
sys.path.insert(0, str(PLOTREAD_TESTS_ROOT))

from model_matrix import (
    OutputSlice,
    build_parser,
    compare_decoded_payloads,
    compare_slice_sets,
    load_manifest_slices,
    resolve_tolerances_path,
    scoped_tl_absolute_db,
)
from case_model import load_case
from support import write_little_endian_rectilinear_file


class ModelMatrixTests(unittest.TestCase):
    @staticmethod
    def scale_first_pressure(
        shade_path: Path, frequency_index: int, factor: float
    ) -> None:
        record_bytes = 164
        receiver_depth_count = 2
        record_index = 10 + frequency_index * receiver_depth_count
        offset = record_index * record_bytes
        contents = bytearray(shade_path.read_bytes())
        real, imaginary = struct.unpack_from("<2f", contents, offset)
        struct.pack_into(
            "<2f", contents, offset, real * factor, imaginary * factor
        )
        shade_path.write_bytes(contents)

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

    def test_default_cli_routes_case_local_then_shared_tolerances(self) -> None:
        args = build_parser().parse_args(
            ["--case", "munk_spline", "--profiles", "broadband_smoke"]
        )
        self.assertIsNone(args.tolerances)

        cases_root = CODES_ROOT.parent / "cases"
        spline = load_case(cases_root / "munk_spline")
        constant = load_case(cases_root / "constant_speed_direct")
        self.assertEqual(
            resolve_tolerances_path(spline, args.tolerances),
            (cases_root / "munk_spline" / "tolerances.toml").resolve(),
        )
        self.assertEqual(
            resolve_tolerances_path(constant, args.tolerances),
            (CODES_ROOT / "tolerances.toml").resolve(),
        )

    def test_explicit_cli_tolerances_override_every_case(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            override = Path(temporary_directory) / "override.toml"
            args = build_parser().parse_args(
                ["--tolerances", str(override)]
            )
            cases_root = CODES_ROOT.parent / "cases"
            for case_id in ("munk_spline", "constant_speed_direct"):
                definition = load_case(cases_root / case_id)
                self.assertEqual(
                    resolve_tolerances_path(definition, args.tolerances),
                    override.resolve(),
                )

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

    def test_munk_spline_250hz_tl_policy_is_pair_and_frequency_scoped(
        self,
    ) -> None:
        self.assertEqual(
            scoped_tl_absolute_db(
                case_id="munk_spline",
                reference_label="origin",
                candidate_label="f2cpp",
                frequency_hz=250.0,
            ),
            0.0065,
        )
        self.assertEqual(
            scoped_tl_absolute_db(
                case_id="munk_spline",
                reference_label="origin",
                candidate_label="rayreuse-parallel",
                frequency_hz=250.0,
            ),
            0.0065,
        )
        for case_id, reference, candidate, frequency in (
            ("munk_spline", "origin", "f2cpp", 50.0),
            ("munk_spline", "f2cpp", "rayreuse-parallel", 250.0),
            ("munk_pchip", "origin", "f2cpp", 250.0),
        ):
            self.assertIsNone(
                scoped_tl_absolute_db(
                    case_id=case_id,
                    reference_label=reference,
                    candidate_label=candidate,
                    frequency_hz=frequency,
                )
            )

    def test_munk_spline_250hz_override_does_not_widen_50hz(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            reference_path = root / "reference.shd"
            candidate_path = root / "candidate.shd"
            write_little_endian_rectilinear_file(
                reference_path, (50.0, 250.0)
            )
            candidate_path.write_bytes(reference_path.read_bytes())
            amplitude_factor = 10.0 ** (-0.006 / 20.0)
            self.scale_first_pressure(candidate_path, 0, amplitude_factor)
            self.scale_first_pressure(candidate_path, 1, amplitude_factor)
            reference_slices = {
                frequency: OutputSlice(frequency, index, reference_path)
                for index, frequency in enumerate((50.0, 250.0))
            }
            candidate_slices = {
                frequency: OutputSlice(frequency, index, candidate_path)
                for index, frequency in enumerate((50.0, 250.0))
            }

            results = compare_slice_sets(
                case_id="munk_spline",
                reference_label="origin",
                candidate_label="f2cpp",
                reference_slices=reference_slices,
                candidate_slices=candidate_slices,
                tolerances_path=(
                    CODES_ROOT.parent / "cases" / "munk_spline" / "tolerances.toml"
                ),
            )

        self.assertFalse(results[0]["passed"])
        self.assertEqual(results[0]["tolerance_policy"], "default")
        self.assertEqual(
            results[0]["metrics"]["transmission_loss_absolute_db"],
            0.005,
        )
        self.assertTrue(results[1]["passed"])
        self.assertEqual(
            results[1]["tolerance_policy"],
            "munk_spline_origin_cpp_250hz",
        )
        self.assertEqual(
            results[1]["metrics"]["transmission_loss_absolute_db"],
            0.0065,
        )

    def test_decoded_complex64_payload_gate_ignores_container_bytes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            reference_path = root / "reference.shd"
            candidate_path = root / "candidate.shd"
            write_little_endian_rectilinear_file(reference_path, (250.0,))
            candidate = bytearray(reference_path.read_bytes())
            candidate[8] ^= 1
            candidate_path.write_bytes(candidate)
            reference = OutputSlice(250.0, 0, reference_path)
            candidate_slice = OutputSlice(250.0, 0, candidate_path)

            identical = compare_decoded_payloads(
                reference_label="f2cpp",
                candidate_label="rayreuse-reuse",
                frequency_hz=250.0,
                reference=reference,
                candidate=candidate_slice,
            )
            self.scale_first_pressure(candidate_path, 0, 0.5)
            different = compare_decoded_payloads(
                reference_label="f2cpp",
                candidate_label="rayreuse-reuse",
                frequency_hz=250.0,
                reference=reference,
                candidate=candidate_slice,
            )

        self.assertTrue(identical["passed"])
        self.assertEqual(
            identical["reference_sha256"], identical["candidate_sha256"]
        )
        self.assertFalse(different["passed"])
        self.assertNotEqual(
            different["reference_sha256"], different["candidate_sha256"]
        )

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
