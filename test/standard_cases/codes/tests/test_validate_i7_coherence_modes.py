from __future__ import annotations

from pathlib import Path
import sys
import unittest

import numpy as np


CODES_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(CODES_ROOT))

from validate_i7_coherence_modes import (
    CASES,
    field_mode_semantics,
    mode_effect_metrics,
    normalize_coherence_mode,
    parse_coherence_mode,
    validate_origin_source_contract,
)


class CoherenceModeValidatorTests(unittest.TestCase):
    def test_mode_parser_requires_one_cc_ic_or_sc_record(self) -> None:
        for mode in "CIS":
            with self.subTest(mode=mode):
                self.assertEqual(
                    parse_coherence_mode(f"'{mode}C'\n", "fixture"), mode
                )
        with self.assertRaisesRegex(ValueError, "exactly one"):
            parse_coherence_mode("'CC'\n'IC'\n", "fixture")
        with self.assertRaisesRegex(ValueError, "exactly one"):
            parse_coherence_mode("'XC'\n", "fixture")

    def test_canonical_fixture_matrix_only_changes_first_position(self) -> None:
        cases_root = CODES_ROOT.parent / "cases"
        raw: set[str] = set()
        normalized: set[str] = set()
        for mode, case_id in CASES.items():
            contents = (
                cases_root / case_id / "origin.env.in"
            ).read_text(encoding="utf-8")
            self.assertEqual(parse_coherence_mode(contents, case_id), mode)
            raw.add(contents)
            normalized.add(normalize_coherence_mode(contents, case_id))
        self.assertEqual(len(raw), 3)
        self.assertEqual(len(normalized), 1)

    def test_origin_source_defines_c_i_s_and_lloyd_contract(self) -> None:
        contract = validate_origin_source_contract()
        self.assertTrue(contract["read_run_type_has_c_i_s"])
        self.assertTrue(contract["semi_coherent_applies_lloyd_mirror"])
        self.assertTrue(contract["cerveny_cart_accumulates_i_s_intensity"])
        self.assertTrue(contract["noncoherent_scale_uses_sqrt_real"])

    def test_field_semantics_and_effect_metrics_are_independent(self) -> None:
        coherent = np.asarray(
            (1.0e-3 + 2.0e-3j, -2.0e-3 + 1.0e-3j),
            dtype=np.complex64,
        )
        incoherent = np.asarray((-1.0e-4, -2.0e-4), dtype=np.complex64)
        semicoherent = np.asarray((-1.1e-4, -2.2e-4), dtype=np.complex64)
        self.assertTrue(field_mode_semantics("C", coherent)["passed"])
        self.assertTrue(field_mode_semantics("I", incoherent)["passed"])
        self.assertTrue(field_mode_semantics("S", semicoherent)["passed"])
        metrics = mode_effect_metrics(incoherent, semicoherent)
        self.assertGreater(metrics["max_pressure_absolute_difference"], 0.0)
        self.assertGreater(metrics["median_tl_difference_db"], 0.0)
        with self.assertRaisesRegex(ValueError, "exactly real"):
            field_mode_semantics("I", incoherent + 1.0e-8j)


if __name__ == "__main__":
    unittest.main()
