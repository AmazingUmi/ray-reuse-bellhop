from __future__ import annotations

from pathlib import Path
import sys
import tempfile
import unittest


CODES_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(CODES_ROOT))

from validate_i8_arrival_accumulator import (
    PROBE_HEADER,
    REQUIRED_SCENARIOS,
    check_expected_provenance,
    compare,
    parse_probe,
)


def complete_probe() -> str:
    lines = [PROBE_HEADER]
    for scenario in sorted(REQUIRED_SCENARIOS):
        lines.append(f"SCENARIO {scenario} 0")
    return "\n".join(lines) + "\n"


class ArrivalAccumulatorValidatorTests(unittest.TestCase):
    def test_zero_arrival_complete_probe_compares(self) -> None:
        parsed = parse_probe(complete_probe(), "fixture")
        result = compare(parsed, parsed)
        self.assertEqual(result["scenario_count"], 15)
        self.assertEqual(result["arrival_count"], 0)

    def test_rejects_missing_duplicate_and_error_scenarios(self) -> None:
        with self.assertRaisesRegex(ValueError, "missing scenarios"):
            parse_probe(PROBE_HEADER + "\nSCENARIO 1 0\n", "fixture")
        with self.assertRaisesRegex(ValueError, "duplicate scenario"):
            parse_probe(
                complete_probe() + "SCENARIO 1 0\n", "fixture"
            )
        with self.assertRaisesRegex(ValueError, "production API error"):
            parse_probe(PROBE_HEADER + "\nERROR 10 rejected\n", "fixture")
        with self.assertRaisesRegex(ValueError, "unknown record"):
            parse_probe(complete_probe() + "UNKNOWN 1\n", "fixture")
        with self.assertRaisesRegex(ValueError, "non-sequential"):
            parse_probe(
                complete_probe().replace(
                    "SCENARIO 1 0",
                    "SCENARIO 1 1\nARRIVAL 1 2 0 0 0 0 0 0 0 0",
                ),
                "fixture",
            )

    def test_rejects_corrupt_count_nonfinite_and_mismatch(self) -> None:
        corrupt = complete_probe().replace(
            "SCENARIO 1 0", "SCENARIO 1 1"
        )
        with self.assertRaisesRegex(ValueError, "count mismatch"):
            parse_probe(corrupt, "fixture")
        with self.assertRaisesRegex(ValueError, "non-finite"):
            compare(
                parse_probe(
                    complete_probe().replace(
                        "SCENARIO 1 0",
                        "SCENARIO 1 1\nARRIVAL 1 1 2139095040 0 0 0 0 0 0 0",
                    ),
                    "origin",
                ),
                parse_probe(
                    complete_probe().replace(
                        "SCENARIO 1 0",
                        "SCENARIO 1 1\nARRIVAL 1 1 2139095040 0 0 0 0 0 0 0",
                    ),
                    "f2cpp",
                ),
            )
        left = parse_probe(
            complete_probe().replace(
                "SCENARIO 1 0",
                "SCENARIO 1 1\nARRIVAL 1 1 1065353216 0 0 0 0 0 0 0",
            ),
            "origin",
        )
        right = parse_probe(
            complete_probe().replace(
                "SCENARIO 1 0",
                "SCENARIO 1 1\nARRIVAL 1 1 1065353218 0 0 0 0 0 0 0",
            ),
            "f2cpp",
        )
        with self.assertRaisesRegex(ValueError, "exceeds 1 ULP"):
            compare(left, right)

    def test_rejects_provenance_mismatch(self) -> None:
        report = {"provenance_sha256": {"origin_probe": "abc"}}
        with tempfile.TemporaryDirectory() as temporary:
            expected = Path(temporary) / "expected.json"
            expected.write_text('{"origin_probe": "different"}\n')
            with self.assertRaisesRegex(ValueError, "provenance mismatch"):
                check_expected_provenance(report, expected)


if __name__ == "__main__":
    unittest.main()
