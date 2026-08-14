from __future__ import annotations

import math
from pathlib import Path
import sys
import unittest


CODES_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(CODES_ROOT))

from validate_i7_beam_options import (
    CASES,
    expected_epsilon_anchors,
    normalize_beam_option,
    parse_beam_option,
    parse_prt_epsilon,
    validate_origin_source_contract,
)


class BeamOptionValidatorTests(unittest.TestCase):
    def test_option_parser_requires_one_valid_record(self) -> None:
        self.assertEqual(parse_beam_option("'FS' 1.0 1.0\n", "fixture"), "FS")
        with self.assertRaisesRegex(ValueError, "exactly one"):
            parse_beam_option("'FS' 1 1\n'MZ' 1 1\n", "fixture")
        with self.assertRaisesRegex(ValueError, "exactly one"):
            parse_beam_option("'FX' 1 1\n", "fixture")
        with self.assertRaisesRegex(ValueError, "exactly one"):
            parse_beam_option("no beam record\n", "fixture")

    def test_canonical_fixture_matrix_only_changes_option(self) -> None:
        cases_root = CODES_ROOT.parent / "cases"
        raw: set[str] = set()
        normalized: set[str] = set()
        companions: dict[str, set[bytes]] = {"ati": set(), "bty": set()}
        for option, case_id in CASES.items():
            contents = (
                cases_root / case_id / "origin.env.in"
            ).read_text(encoding="utf-8")
            self.assertEqual(parse_beam_option(contents, case_id), option)
            raw.add(contents)
            normalized.add(normalize_beam_option(contents, case_id))
            for suffix in companions:
                companions[suffix].add(
                    (cases_root / case_id / f"origin.{suffix}").read_bytes()
                )
        self.assertEqual(len(raw), 5)
        self.assertEqual(len(normalized), 1)
        self.assertEqual(len(companions["ati"]), 1)
        self.assertEqual(len(companions["bty"]), 1)

    def test_origin_source_defines_width_and_curvature_contract(self) -> None:
        contract = validate_origin_source_contract()
        self.assertTrue(contract["beam_type_2_3_is_parsed"])
        self.assertTrue(contract["pick_epsilon_has_f_m_w"])
        self.assertTrue(contract["wkb_formula_preserves_cos_alpha_squared"])
        self.assertTrue(contract["wkb_branch_cut_uses_real_q"])
        self.assertTrue(contract["curvature_d_z_apply_to_total_rn"])

    def test_prt_epsilon_parser_matches_independent_anchors(self) -> None:
        parsed = parse_prt_epsilon(
            " HalfWidth = 6.9098829894267098D+01\n"
            " epsilonOpt = ( 0.0D+00, 1.5000000000000002D+06 )\n",
            "synthetic.prt",
        )
        expected = expected_epsilon_anchors()["MS"]
        self.assertTrue(
            math.isclose(
                parsed["half_width"], expected["half_width"], rel_tol=1e-14
            )
        )
        for observed, anchor in zip(parsed["epsilon"], expected["epsilon"]):
            self.assertTrue(math.isclose(observed, anchor, rel_tol=1e-14))


if __name__ == "__main__":
    unittest.main()
