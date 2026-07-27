from __future__ import annotations

from pathlib import Path
import sys
import unittest


CODES_ROOT = Path(__file__).resolve().parents[1]
STANDARD_CASES_ROOT = CODES_ROOT.parent
sys.path.insert(0, str(CODES_ROOT))

from case_model import discover_cases


class CaseModelTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.cases = discover_cases(STANDARD_CASES_ROOT / "cases")

    def test_expected_case_families_are_present(self) -> None:
        self.assertEqual(
            set(self.cases),
            {
                "constant_speed_direct",
                "constant_speed_vacuum_rigid",
                "constant_speed_acoustic_bottom",
                "constant_speed_no_attenuation_5khz",
                "constant_speed_thorp",
                "munk_cerveny_cc",
            },
        )

    def test_every_profile_renders_all_origin_tokens(self) -> None:
        for definition in self.cases.values():
            for profile_name in definition.profiles:
                with self.subTest(
                    case=definition.case_id, profile=profile_name
                ):
                    frequencies = definition.frequencies(profile_name)
                    launch_count = definition.shared_launch_angle_count(
                        frequencies
                    )
                    self.assertGreaterEqual(launch_count, 1)
                    for frequency_hz in frequencies:
                        rendered = definition.render_origin_environment(
                            frequency_hz, launch_count
                        )
                        self.assertNotIn("@FREQUENCY_HZ@", rendered)
                        self.assertNotIn("@NALPHA@", rendered)

    def test_known_launch_counts_follow_fmax_policy(self) -> None:
        direct = self.cases["constant_speed_direct"]
        munk = self.cases["munk_cerveny_cc"]
        self.assertEqual(
            direct.shared_launch_angle_count(
                direct.frequencies("single")
            ),
            300,
        )
        self.assertEqual(
            munk.shared_launch_angle_count(munk.frequencies("single")),
            1000,
        )
        self.assertEqual(
            munk.shared_launch_angle_count(
                munk.frequencies("broadband_smoke")
            ),
            5000,
        )


if __name__ == "__main__":
    unittest.main()
