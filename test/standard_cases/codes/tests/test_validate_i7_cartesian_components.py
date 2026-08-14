from __future__ import annotations

from pathlib import Path
import sys
import unittest


CODES_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(CODES_ROOT))

from validate_i7_cartesian_components import (
    CASES,
    normalize_component,
    parse_component,
    validate_legacy_source_contract,
)


class CartesianComponentValidatorTests(unittest.TestCase):
    def test_component_parser_requires_one_explicit_record(self) -> None:
        self.assertEqual(parse_component("1 5 'V'\n", "fixture"), "V")
        with self.assertRaisesRegex(ValueError, "exactly one"):
            parse_component("1 5 'P'\n1 5 'H'\n", "fixture")
        with self.assertRaisesRegex(ValueError, "exactly one"):
            parse_component("1 5 'X'\n", "fixture")

    def test_canonical_fixtures_are_identical_after_normalization(self) -> None:
        cases_root = CODES_ROOT.parent / "cases"
        normalized: set[str] = set()
        raw: set[str] = set()
        for component, case_id in CASES.items():
            contents = (
                cases_root / case_id / "origin.env.in"
            ).read_text(encoding="utf-8")
            self.assertEqual(parse_component(contents, case_id), component)
            raw.add(contents)
            normalized.add(normalize_component(contents, case_id))
        self.assertEqual(len(raw), 3)
        self.assertEqual(len(normalized), 1)

    def test_origin_source_still_defines_legacy_cartesian_contract(self) -> None:
        contract = validate_legacy_source_contract()
        self.assertTrue(contract["component_record_is_parsed"])
        self.assertTrue(contract["cartesian_influence_ignores_component"])
        self.assertTrue(contract["ray_centered_influence_has_v_h_branches"])


if __name__ == "__main__":
    unittest.main()
