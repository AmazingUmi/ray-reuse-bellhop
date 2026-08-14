from __future__ import annotations

from pathlib import Path
import sys
import unittest

import numpy as np


CODES_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(CODES_ROOT))

from validate_i7_geometric_hat import (
    CASES,
    effect_metrics,
    normalize_family,
    parse_family_and_require_eof,
    validate_origin_source_contract,
)


class GeometricHatValidatorTests(unittest.TestCase):
    def test_parser_requires_one_family_and_integrator_eof(self) -> None:
        valid = "'CG'\n497\n-40.0 40.0 /\n500.0 121.0 2.1\n"
        self.assertEqual(
            parse_family_and_require_eof(valid, "fixture"), "CG"
        )
        with self.assertRaisesRegex(ValueError, "exactly one"):
            parse_family_and_require_eof(
                valid.replace("'CG'", "'CC'"), "fixture"
            )
        with self.assertRaisesRegex(ValueError, "end at the integrator"):
            parse_family_and_require_eof(
                valid + "'MS' 1.0 1.0\n3 5 'P'\n", "fixture"
            )
        with self.assertRaisesRegex(ValueError, "exactly one"):
            parse_family_and_require_eof(
                valid + "'Cg'\n497\n-40.0 40.0 /\n"
                "500.0 121.0 2.1\n",
                "fixture",
            )

    def test_canonical_matrix_only_changes_coordinate_family(self) -> None:
        cases_root = CODES_ROOT.parent / "cases"
        i3_root = cases_root / "i3_piecewise_boundaries"
        raw: set[str] = set()
        normalized: set[str] = set()
        for key, (case_id, family, _) in CASES.items():
            with self.subTest(key=key):
                case_root = cases_root / case_id
                contents = (case_root / "origin.env.in").read_text(
                    encoding="utf-8"
                )
                self.assertEqual(
                    parse_family_and_require_eof(contents, case_id), family
                )
                raw.add(contents)
                normalized.add(normalize_family(contents, case_id))
                for companion in ("origin.ati", "origin.bty"):
                    self.assertEqual(
                        (case_root / companion).read_bytes(),
                        (i3_root / companion).read_bytes(),
                    )
        self.assertEqual(len(raw), 2)
        self.assertEqual(len(normalized), 1)

    def test_origin_source_defines_geometric_hat_contract(self) -> None:
        contract = validate_origin_source_contract()
        self.assertTrue(
            contract["read_run_type_has_g_and_cartesian_default"]
        )
        self.assertTrue(contract["non_cerveny_tail_has_no_extra_reads"])
        self.assertTrue(
            contract[
                "bellhop_dispatches_cartesian_and_ray_centered_geometric_hat"
            ]
        )
        self.assertTrue(contract["geometric_hat_formulas_present"])

    def test_effect_metrics_capture_support_separation(self) -> None:
        cartesian = np.asarray(
            (1.0e-2 + 0.0j, 2.0e-3 + 1.0e-3j), dtype=np.complex64
        )
        ray_centered = np.asarray(
            (0.0 + 0.0j, 2.0e-3 + 1.0e-3j), dtype=np.complex64
        )
        metrics = effect_metrics(cartesian, ray_centered)
        self.assertGreater(metrics["max_pressure_absolute_difference"], 0.0)
        self.assertEqual(metrics["support_mismatch_count"], 1)
        self.assertEqual(metrics["cartesian_only_support_count"], 1)
        self.assertEqual(metrics["common_support_count"], 1)
        with self.assertRaisesRegex(ValueError, "shapes differ"):
            effect_metrics(cartesian, ray_centered[:1])
        with self.assertRaisesRegex(ValueError, "non-finite"):
            effect_metrics(
                cartesian,
                np.asarray((complex("nan"), 1.0 + 0.0j)),
            )


if __name__ == "__main__":
    unittest.main()
