from __future__ import annotations

from pathlib import Path
import sys
import unittest

import numpy as np


CODES_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(CODES_ROOT))

from validate_i7_ray_centered_components import (
    CASES,
    effect_metrics,
    normalize_family_and_component,
    parse_family_and_component,
    validate_origin_source_contract,
)


class RayCenteredComponentValidatorTests(unittest.TestCase):
    def test_parser_requires_one_family_and_component(self) -> None:
        self.assertEqual(
            parse_family_and_component("'CR'\n1 5 'V'\n", "fixture"),
            ("CR", "V"),
        )
        with self.assertRaisesRegex(ValueError, "exactly one"):
            parse_family_and_component("'CR'\n'CC'\n1 5 'P'\n", "fixture")
        with self.assertRaisesRegex(ValueError, "exactly one"):
            parse_family_and_component("'CG'\n1 5 'P'\n", "fixture")
        with self.assertRaisesRegex(ValueError, "exactly one"):
            parse_family_and_component("'CR'\n1 5 'X'\n", "fixture")

    def test_canonical_matrix_only_changes_family_and_component(self) -> None:
        cases_root = CODES_ROOT.parent / "cases"
        raw: set[str] = set()
        normalized: set[str] = set()
        for key, (case_id, family, component) in CASES.items():
            with self.subTest(key=key):
                contents = (
                    cases_root / case_id / "origin.env.in"
                ).read_text(encoding="utf-8")
                self.assertEqual(
                    parse_family_and_component(contents, case_id),
                    (family, component),
                )
                raw.add(contents)
                normalized.add(
                    normalize_family_and_component(contents, case_id)
                )
        self.assertEqual(len(raw), 4)
        self.assertEqual(len(normalized), 1)

    def test_origin_source_defines_ray_centered_component_contract(self) -> None:
        contract = validate_origin_source_contract()
        self.assertTrue(
            contract["read_run_type_has_cartesian_and_ray_centered"]
        )
        self.assertTrue(contract["bellhop_dispatches_c_and_r_influence"])
        self.assertTrue(contract["ray_centered_has_p_v_h_formulas"])

    def test_effect_metrics_capture_family_and_component_separation(self) -> None:
        reference = np.asarray(
            (1.0e-3 + 2.0e-3j, 2.0e-3 - 1.0e-3j), dtype=np.complex64
        )
        candidate = np.asarray(
            (3.0e-3 - 1.0e-3j, -1.0e-3 + 4.0e-3j), dtype=np.complex64
        )
        metrics = effect_metrics(reference, candidate)
        self.assertGreater(metrics["max_pressure_absolute_difference"], 0.0)
        self.assertGreater(metrics["median_tl_difference_db"], 0.0)
        self.assertEqual(metrics["tl_mask_count"], 2)
        with self.assertRaisesRegex(ValueError, "shapes differ"):
            effect_metrics(reference, candidate[:1])


if __name__ == "__main__":
    unittest.main()
