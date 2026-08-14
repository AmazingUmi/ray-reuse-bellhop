from __future__ import annotations

from pathlib import Path
import sys
import unittest

import numpy as np


CODES_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(CODES_ROOT))

from validate_i7_source_geometry import (
    CASES,
    normalize_source_geometry_token,
    parse_source_geometry_token,
    source_geometry_effect_metrics,
    validate_origin_source_contract,
)


class SourceGeometryValidatorTests(unittest.TestCase):
    def test_run_type_parser_requires_one_blank_r_or_x_record(self) -> None:
        self.assertEqual(parse_source_geometry_token("'CC'\n", "fixture"), "DEFAULT")
        self.assertEqual(parse_source_geometry_token("'CC R'\n", "fixture"), "R")
        self.assertEqual(parse_source_geometry_token("'CC X'\n", "fixture"), "X")
        with self.assertRaisesRegex(ValueError, "exactly one"):
            parse_source_geometry_token("'CC'\n'CC X'\n", "fixture")
        with self.assertRaisesRegex(ValueError, "exactly one"):
            parse_source_geometry_token("'CC Z'\n", "fixture")

    def test_canonical_fixture_matrix_only_changes_fourth_position(self) -> None:
        cases_root = CODES_ROOT.parent / "cases"
        raw: set[str] = set()
        normalized: set[str] = set()
        for token, case_id in CASES.items():
            contents = (
                cases_root / case_id / "origin.env.in"
            ).read_text(encoding="utf-8")
            self.assertEqual(
                parse_source_geometry_token(contents, case_id), token
            )
            raw.add(contents)
            normalized.add(normalize_source_geometry_token(contents, case_id))
        self.assertEqual(len(raw), 3)
        self.assertEqual(len(normalized), 1)

    def test_origin_source_defines_point_line_contract(self) -> None:
        contract = validate_origin_source_contract()
        self.assertTrue(contract["blank_fourth_position_defaults_to_r"])
        self.assertTrue(contract["r_selects_point_source"])
        self.assertTrue(contract["x_selects_line_source"])
        self.assertTrue(
            contract["cartesian_influence_applies_point_cosine_ratio"]
        )
        self.assertTrue(
            contract["scale_pressure_distinguishes_point_and_line"]
        )

    def test_effect_metrics_capture_pressure_and_tl_separation(self) -> None:
        point = np.asarray((1.0e-3, 2.0e-3), dtype=np.complex64)
        line = np.asarray((1.0e-1, 2.0e-1), dtype=np.complex64)
        metrics = source_geometry_effect_metrics(point, line)
        self.assertGreater(metrics["max_pressure_absolute_difference"], 0.1)
        self.assertAlmostEqual(metrics["median_tl_difference_db"], 40.0, places=5)
        self.assertEqual(metrics["tl_mask_count"], 2)
        with self.assertRaisesRegex(ValueError, "shapes differ"):
            source_geometry_effect_metrics(point, line[:1])


if __name__ == "__main__":
    unittest.main()
