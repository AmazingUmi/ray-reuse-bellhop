from __future__ import annotations

from pathlib import Path
import sys
import unittest

import numpy as np


CODES_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(CODES_ROOT))

from validate_i7_gaussian_beams import (
    CASES,
    effect_metrics,
    normalize_family,
    parse_family_and_require_eof,
    validate_origin_source_contract,
)


class GaussianBeamValidatorTests(unittest.TestCase):
    def test_parser_requires_one_family_and_integrator_eof(self) -> None:
        valid = "'CB'\n300\n-60.0 60.0 /\n1.0 101.0 0.26\n"
        self.assertEqual(
            parse_family_and_require_eof(valid, "fixture"), "CB"
        )
        with self.assertRaisesRegex(ValueError, "exactly one"):
            parse_family_and_require_eof(
                valid.replace("'CB'", "'Cb'"), "fixture"
            )
        with self.assertRaisesRegex(ValueError, "end at the integrator"):
            parse_family_and_require_eof(
                valid + "'MS' 1.0 1.0\n3 5 'P'\n", "fixture"
            )
        with self.assertRaisesRegex(ValueError, "exactly one"):
            parse_family_and_require_eof(
                valid + "'CG'\n300\n-60.0 60.0 /\n"
                "1.0 101.0 0.26\n",
                "fixture",
            )

    def test_canonical_matrix_only_changes_family(self) -> None:
        cases_root = CODES_ROOT.parent / "cases"
        raw: set[str] = set()
        normalized: set[str] = set()
        for key, (case_id, family, _, _) in CASES.items():
            with self.subTest(key=key):
                contents = (
                    cases_root / case_id / "origin.env.in"
                ).read_text(encoding="utf-8")
                self.assertEqual(
                    parse_family_and_require_eof(contents, case_id), family
                )
                raw.add(contents)
                normalized.add(normalize_family(contents, case_id))
        self.assertEqual(len(raw), 3)
        self.assertEqual(len(normalized), 1)

    def test_origin_source_defines_gaussian_family_contract(self) -> None:
        contract = validate_origin_source_contract()
        self.assertTrue(contract["read_run_type_has_g_b_s"])
        self.assertTrue(contract["non_cerveny_tail_has_no_extra_reads"])
        self.assertTrue(contract["bellhop_dispatches_g_b_s_influences"])
        self.assertTrue(contract["gaussian_family_formulas_present"])
        self.assertTrue(
            contract[
                "ray_centered_geometric_gaussian_is_explicitly_unavailable"
            ]
        )

    def test_effect_metrics_capture_pairwise_separation(self) -> None:
        reference = np.asarray(
            (1.0e-2 + 1.0e-3j, 2.0e-3 + 1.0e-3j),
            dtype=np.complex64,
        )
        candidate = np.asarray(
            (5.0e-3 - 2.0e-3j, 4.0e-3 + 1.0e-3j),
            dtype=np.complex64,
        )
        metrics = effect_metrics(reference, candidate)
        self.assertGreater(metrics["max_pressure_absolute_difference"], 0.0)
        self.assertGreater(metrics["median_tl_difference_db"], 0.0)
        self.assertEqual(metrics["tl_mask_count"], 2)
        with self.assertRaisesRegex(ValueError, "shapes differ"):
            effect_metrics(reference, candidate[:1])
        with self.assertRaisesRegex(ValueError, "non-finite"):
            effect_metrics(
                reference,
                np.asarray((complex("nan"), 1.0 + 0.0j)),
            )


if __name__ == "__main__":
    unittest.main()
