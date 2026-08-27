from __future__ import annotations

from pathlib import Path
import sys
import tempfile
import unittest


CODES_ROOT = Path(__file__).resolve().parents[1]
STANDARD_CASES_ROOT = CODES_ROOT.parent
sys.path.insert(0, str(CODES_ROOT))

from case_model import discover_cases
from coverage_manifest import load_coverage_manifest


class CoverageManifestTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.cases = discover_cases(STANDARD_CASES_ROOT / "cases")
        cls.coverage = load_coverage_manifest(
            STANDARD_CASES_ROOT / "coverage.toml", cls.cases
        )

    def test_every_case_has_complete_coverage_and_a_test_set(self) -> None:
        self.assertEqual(set(self.coverage.cases), set(self.cases))
        for case_id, coverage in self.coverage.cases.items():
            with self.subTest(case=case_id):
                self.assertTrue(coverage.tags)
                self.assertTrue(coverage.test_sets)

    def test_function_sets_select_existing_cases_in_case_order(self) -> None:
        selected = self.coverage.case_ids_for_sets(
            ("arrival",), self.cases
        )
        self.assertEqual(len(selected), 7)
        self.assertTrue(
            all(case_id.startswith("arrival_") for case_id in selected)
        )
        self.assertEqual(
            set(self.coverage.case_ids_for_sets(("eigenray",), self.cases)),
            {
                "eigenray_geometric_hat",
                "eigenray_geometric_hat_ray_centered",
                "eigenray_geometric_gaussian",
                "eigenray_zero",
            },
        )

    def test_manifest_rejects_missing_case_coverage(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            path = Path(temporary_directory) / "coverage.toml"
            path.write_text(
                "schema_version = 1\n"
                "[sets.core]\n"
                "description = 'core'\n"
                "ctest_regex = 'core'\n"
                "[cases]\n",
                encoding="utf-8",
            )
            with self.assertRaisesRegex(ValueError, "missing="):
                load_coverage_manifest(path, ("required_case",))


if __name__ == "__main__":
    unittest.main()
