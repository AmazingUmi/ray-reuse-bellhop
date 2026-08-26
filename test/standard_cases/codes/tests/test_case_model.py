from __future__ import annotations

from pathlib import Path
import shutil
import sys
import tempfile
import unittest


CODES_ROOT = Path(__file__).resolve().parents[1]
STANDARD_CASES_ROOT = CODES_ROOT.parent
sys.path.insert(0, str(CODES_ROOT))

from case_model import discover_cases, load_case


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
                "munk_n2",
                "munk_pchip",
                "munk_spline",
                "i3_piecewise_boundaries",
                "i3_curvilinear_oracle",
                "i3_long_format_materials",
                "elastic_ll_top_bottom",
                "attenuation_unit_n",
                "attenuation_unit_f",
                "attenuation_unit_m",
                "attenuation_unit_w",
                "attenuation_unit_q",
                "attenuation_unit_l",
                "volume_attenuation_francois_garrison",
                "volume_attenuation_biological",
                "elastic_halfspace_flat",
                "elastic_halfspace_fluid_control",
                "grain_size_flat",
                "grain_size_equivalent_acoustic_control",
                "tabulated_reflection_bottom",
                "tabulated_reflection_rigid_control",
                "top_tabulated_bottom_vacuum",
                "q_range_dependent_cross_gradient",
                "q_range_independent_control",
                "multi_source_depths",
                "irregular_receiver_pairs",
                "source_beam_pattern_directional",
                "source_beam_pattern_omni_control",
                "ray_trace_vacuum_rigid",
                "ray_trace_directional_tabulated",
                "cartesian_component_pressure",
                "cartesian_component_vertical",
                "cartesian_component_horizontal",
                "cerveny_width_space_filling",
                "cerveny_width_wkb",
                "cerveny_curvature_double",
                "cerveny_curvature_zero",
                "cerveny_curvature_double_flat_gradient",
                "cerveny_curvature_zero_flat_gradient",
                "cerveny_width_space_filling_flat_gradient",
                "cerveny_width_wkb_flat_gradient",
                "source_geometry_point_explicit",
                "source_geometry_line",
                "incoherent_direct",
                "semicoherent_direct",
                "ray_centered_component_pressure",
                "ray_centered_component_vertical",
                "ray_centered_component_horizontal",
                "geometric_hat_cartesian",
                "geometric_hat_ray_centered",
                "geometric_hat_cartesian_safe_control",
                "geometric_hat_incoherent",
                "geometric_hat_semicoherent",
                "geometric_hat_directional",
                "geometric_gaussian_cartesian",
                "geometric_gaussian_incoherent",
                "geometric_gaussian_semicoherent",
                "geometric_gaussian_directional",
                "simple_gaussian_cartesian",
                "simple_gaussian_directional",
                "arrival_geometric_hat_ascii",
                "arrival_geometric_hat_binary",
                "arrival_geometric_hat_ray_centered",
                "arrival_geometric_gaussian_irregular",
                "arrival_line_directional_multisource",
                "arrival_zero",
                "eigenray_geometric_hat",
                "eigenray_geometric_hat_ray_centered",
                "eigenray_geometric_gaussian",
                "eigenray_zero",
            },
        )

    def test_i4_case_scopes_match_implemented_versions(self) -> None:
        for suffix in "nfmwql":
            with self.subTest(unit=suffix):
                self.assertEqual(
                    self.cases[f"attenuation_unit_{suffix}"].supported_versions,
                    ("origin", "f2cpp"),
                )
        for case_id in (
            "volume_attenuation_francois_garrison",
            "volume_attenuation_biological",
            "elastic_halfspace_flat",
            "elastic_halfspace_fluid_control",
            "q_range_dependent_cross_gradient",
            "q_range_independent_control",
            "multi_source_depths",
            "irregular_receiver_pairs",
            "ray_trace_vacuum_rigid",
            "cerveny_width_space_filling",
            "cerveny_width_wkb",
            "cerveny_curvature_double",
            "cerveny_curvature_zero",
            "source_geometry_point_explicit",
            "source_geometry_line",
            "geometric_hat_ray_centered",
        ):
            with self.subTest(case=case_id):
                self.assertEqual(
                    self.cases[case_id].supported_versions,
                    ("origin", "f2cpp"),
                )
        for case_id in (
            "source_beam_pattern_directional",
            "source_beam_pattern_omni_control",
            "cartesian_component_pressure",
            "cartesian_component_vertical",
            "cartesian_component_horizontal",
            "incoherent_direct",
            "semicoherent_direct",
            "geometric_hat_cartesian",
            "geometric_hat_cartesian_safe_control",
            "geometric_hat_incoherent",
            "geometric_hat_semicoherent",
            "geometric_hat_directional",
            "geometric_gaussian_cartesian",
            "geometric_gaussian_incoherent",
            "geometric_gaussian_semicoherent",
            "geometric_gaussian_directional",
            "simple_gaussian_cartesian",
            "simple_gaussian_directional",
            "grain_size_flat",
            "grain_size_equivalent_acoustic_control",
            "tabulated_reflection_bottom",
            "tabulated_reflection_rigid_control",
            "cerveny_curvature_double_flat_gradient",
            "cerveny_curvature_zero_flat_gradient",
            "cerveny_width_space_filling_flat_gradient",
            "cerveny_width_wkb_flat_gradient",
            "ray_centered_component_pressure",
            "ray_centered_component_vertical",
            "ray_centered_component_horizontal",
        ):
            with self.subTest(case=case_id):
                self.assertEqual(
                    self.cases[case_id].supported_versions,
                    ("origin", "f2cpp", "rayreuse"),
                )

    def test_i7_geometric_hat_fixtures_only_change_coordinate_family(self) -> None:
        expected = {
            "geometric_hat_cartesian": "CG",
            "geometric_hat_ray_centered": "Cg",
        }
        normalized: set[str] = set()
        rendered_inputs: set[str] = set()
        for case_id, run_type in expected.items():
            with self.subTest(case=case_id):
                definition = self.cases[case_id]
                frequencies = definition.frequencies("single")
                self.assertEqual(frequencies, (100.0,))
                launch_count = definition.shared_launch_angle_count(
                    frequencies
                )
                self.assertEqual(launch_count, 497)
                rendered = definition.render_origin_environment(
                    frequencies[0], launch_count
                )
                self.assertTrue(rendered.endswith("500.0  121.0  2.1\n"))
                self.assertIn(f"'{run_type}'", rendered)
                self.assertNotIn("'MS'", rendered)
                rendered_inputs.add(rendered)
                normalized.add(
                    rendered.replace(f"'{run_type}'", "'<FAMILY>'")
                )
                for companion in ("origin.ati", "origin.bty"):
                    self.assertEqual(
                        (definition.directory / companion).read_bytes(),
                        (
                            STANDARD_CASES_ROOT
                            / "cases"
                            / "i3_piecewise_boundaries"
                            / companion
                        ).read_bytes(),
                    )
        self.assertEqual(len(rendered_inputs), 2)
        self.assertEqual(len(normalized), 1)

    def test_simple_gaussian_directional_fixture(self) -> None:
        definition = self.cases["simple_gaussian_directional"]
        frequencies = definition.frequencies("single")
        launch_count = definition.shared_launch_angle_count(frequencies)
        rendered = definition.render_origin_environment(
            frequencies[0], launch_count
        )
        self.assertIn("'CS*'", rendered)
        self.assertEqual(
            tuple(path.name for path in definition.companion_files),
            ("origin.sbp",),
        )
        self.assertEqual(
            definition.supported_versions,
            ("origin", "f2cpp", "rayreuse"),
        )

    def test_i7_ray_centered_fixtures_only_change_family_and_component(self) -> None:
        expected = {
            "cartesian_component_pressure": ("CC", "P"),
            "ray_centered_component_pressure": ("CR", "P"),
            "ray_centered_component_vertical": ("CR", "V"),
            "ray_centered_component_horizontal": ("CR", "H"),
        }
        normalized: set[str] = set()
        rendered_inputs: set[str] = set()
        for case_id, (family, component) in expected.items():
            with self.subTest(case=case_id):
                definition = self.cases[case_id]
                frequencies = definition.frequencies("single")
                self.assertEqual(frequencies, (1000.0,))
                launch_count = definition.shared_launch_angle_count(frequencies)
                self.assertEqual(launch_count, 300)
                rendered = definition.render_origin_environment(
                    frequencies[0], launch_count
                )
                self.assertIn(f"'{family}'", rendered)
                self.assertIn(f"1  5  '{component}'", rendered)
                rendered_inputs.add(rendered)
                normalized.add(
                    rendered.replace(f"'{family}'", "'<FAMILY>'").replace(
                        f"1  5  '{component}'", "1  5  '<COMPONENT>'"
                    )
                )
        self.assertEqual(len(rendered_inputs), 4)
        self.assertEqual(len(normalized), 1)

    def test_i7_cartesian_gaussian_fixtures_only_change_family(self) -> None:
        expected = {
            "geometric_hat_cartesian_safe_control": "CG",
            "geometric_gaussian_cartesian": "CB",
            "simple_gaussian_cartesian": "CS",
        }
        normalized: set[str] = set()
        rendered_inputs: set[str] = set()
        for case_id, run_type in expected.items():
            with self.subTest(case=case_id):
                definition = self.cases[case_id]
                frequencies = definition.frequencies("single")
                self.assertEqual(frequencies, (1000.0,))
                launch_count = definition.shared_launch_angle_count(
                    frequencies
                )
                self.assertEqual(launch_count, 300)
                rendered = definition.render_origin_environment(
                    frequencies[0], launch_count
                )
                self.assertTrue(rendered.endswith("1.0  101.0  0.26\n"))
                self.assertIn(f"'{run_type}'", rendered)
                self.assertNotIn("'MS'", rendered)
                rendered_inputs.add(rendered)
                normalized.add(
                    rendered.replace(f"'{run_type}'", "'<FAMILY>'")
                )
        self.assertEqual(len(rendered_inputs), 3)
        self.assertEqual(len(normalized), 1)

    def test_i7_cartesian_geometric_hat_fixtures_only_change_mode(self) -> None:
        expected = {
            "geometric_hat_cartesian_safe_control": "CG",
            "geometric_hat_incoherent": "IG",
            "geometric_hat_semicoherent": "SG",
        }
        normalized: set[str] = set()
        for case_id, run_type in expected.items():
            with self.subTest(case=case_id):
                definition = self.cases[case_id]
                frequencies = definition.frequencies("single")
                self.assertEqual(frequencies, (1000.0,))
                rendered = definition.render_origin_environment(
                    frequencies[0],
                    definition.shared_launch_angle_count(frequencies),
                )
                self.assertIn(f"'{run_type}'", rendered)
                self.assertNotIn("'MS'", rendered)
                normalized.add(
                    rendered.replace(f"'{run_type}'", "'<MODE>G'")
                )
        self.assertEqual(len(normalized), 1)

    def test_i7_cartesian_geometric_gaussian_fixtures_only_change_mode(
        self,
    ) -> None:
        expected = {
            "geometric_gaussian_cartesian": "CB",
            "geometric_gaussian_incoherent": "IB",
            "geometric_gaussian_semicoherent": "SB",
        }
        normalized: set[str] = set()
        for case_id, run_type in expected.items():
            with self.subTest(case=case_id):
                definition = self.cases[case_id]
                frequencies = definition.frequencies("single")
                self.assertEqual(frequencies, (1000.0,))
                rendered = definition.render_origin_environment(
                    frequencies[0],
                    definition.shared_launch_angle_count(frequencies),
                )
                self.assertIn(f"'{run_type}'", rendered)
                self.assertNotIn("'MS'", rendered)
                normalized.add(
                    rendered.replace(f"'{run_type}'", "'<MODE>B'")
                )
        self.assertEqual(len(normalized), 1)

    def test_i7_c_i_s_fixtures_only_change_run_mode(self) -> None:
        expected = {
            "constant_speed_direct": "'CC'",
            "incoherent_direct": "'IC'",
            "semicoherent_direct": "'SC'",
        }
        normalized: set[str] = set()
        rendered_inputs: set[str] = set()
        for case_id, run_type in expected.items():
            with self.subTest(case=case_id):
                definition = self.cases[case_id]
                frequencies = definition.frequencies("single")
                self.assertEqual(frequencies, (50.0,))
                launch_count = definition.shared_launch_angle_count(frequencies)
                self.assertEqual(launch_count, 300)
                rendered = definition.render_origin_environment(
                    frequencies[0], launch_count
                )
                self.assertIn(run_type, rendered)
                rendered_inputs.add(rendered)
                normalized.add(rendered.replace(run_type, "'<MODE>C'"))
        self.assertEqual(len(rendered_inputs), 3)
        self.assertEqual(len(normalized), 1)

    def test_i7_source_geometry_fixtures_only_change_run_type_fourth(self) -> None:
        expected = {
            "constant_speed_direct": "'CC'",
            "source_geometry_point_explicit": "'CC R'",
            "source_geometry_line": "'CC X'",
        }
        normalized: set[str] = set()
        rendered_inputs: set[str] = set()
        for case_id, run_type in expected.items():
            with self.subTest(case=case_id):
                definition = self.cases[case_id]
                frequencies = definition.frequencies("single")
                self.assertEqual(frequencies, (50.0,))
                launch_count = definition.shared_launch_angle_count(frequencies)
                self.assertEqual(launch_count, 300)
                rendered = definition.render_origin_environment(
                    frequencies[0], launch_count
                )
                self.assertIn(run_type, rendered)
                rendered_inputs.add(rendered)
                normalized.add(rendered.replace(run_type, "'<SOURCE>'"))
        self.assertEqual(len(rendered_inputs), 3)
        self.assertEqual(len(normalized), 1)

    def test_i7_cerveny_beam_option_fixtures_only_change_option(self) -> None:
        expected = {
            "cerveny_width_space_filling": "FS",
            "i3_curvilinear_oracle": "MS",
            "cerveny_width_wkb": "WS",
            "cerveny_curvature_double": "MD",
            "cerveny_curvature_zero": "MZ",
        }
        normalized: set[str] = set()
        rendered_inputs: set[str] = set()
        control = self.cases["i3_curvilinear_oracle"]
        for case_id, option in expected.items():
            with self.subTest(case=case_id):
                definition = self.cases[case_id]
                frequencies = definition.frequencies("single")
                self.assertEqual(frequencies, (100.0,))
                launch_count = definition.shared_launch_angle_count(frequencies)
                self.assertEqual(launch_count, 459)
                rendered = definition.render_origin_environment(
                    frequencies[0], launch_count
                )
                self.assertIn(f"'{option}' 1.0  1.0", rendered)
                rendered_inputs.add(rendered)
                normalized.add(
                    rendered.replace(
                        f"'{option}' 1.0  1.0", "'<OPTION>' 1.0  1.0"
                    )
                )
                for companion in ("origin.ati", "origin.bty"):
                    self.assertEqual(
                        (definition.directory / companion).read_bytes(),
                        (control.directory / companion).read_bytes(),
                    )
        self.assertEqual(len(rendered_inputs), 5)
        self.assertEqual(len(normalized), 1)

    def test_fp1f_flat_gradient_curvature_fixtures_only_change_option(
        self,
    ) -> None:
        expected = {
            "cerveny_curvature_double_flat_gradient": "MD",
            "cerveny_curvature_zero_flat_gradient": "MZ",
        }
        normalized: set[str] = set()
        rendered_inputs: set[str] = set()
        for case_id, option in expected.items():
            with self.subTest(case=case_id):
                definition = self.cases[case_id]
                frequencies = definition.frequencies("single")
                self.assertEqual(frequencies, (100.0,))
                self.assertEqual(
                    definition.supported_versions,
                    ("origin", "f2cpp", "rayreuse"),
                )
                rendered = definition.render_origin_environment(
                    frequencies[0],
                    definition.shared_launch_angle_count(frequencies),
                )
                self.assertIn(f"'{option}' 1.0  0.20", rendered)
                rendered_inputs.add(rendered)
                normalized.add(
                    rendered.replace(
                        f"'{option}' 1.0  0.20",
                        "'<CURVATURE>' 1.0  0.20",
                    ).replace(
                        "doubled curvature", "<curvature>"
                    ).replace(
                        "zero curvature", "<curvature>"
                    )
                )
        self.assertEqual(len(rendered_inputs), 2)
        self.assertEqual(len(normalized), 1)

    def test_fp1g_flat_gradient_width_fixtures_only_change_option(self) -> None:
        expected = {
            "cerveny_width_space_filling_flat_gradient": "FS",
            "cerveny_width_wkb_flat_gradient": "WS",
        }
        normalized: set[str] = set()
        rendered_inputs: set[str] = set()
        for case_id, option in expected.items():
            with self.subTest(case=case_id):
                definition = self.cases[case_id]
                frequencies = definition.frequencies("single")
                self.assertEqual(frequencies, (100.0,))
                self.assertEqual(
                    definition.supported_versions,
                    ("origin", "f2cpp", "rayreuse"),
                )
                rendered = definition.render_origin_environment(
                    frequencies[0],
                    definition.shared_launch_angle_count(frequencies),
                )
                self.assertIn(f"'{option}' 1.0  0.20", rendered)
                rendered_inputs.add(rendered)
                normalized.add(
                    rendered.replace(
                        f"'{option}' 1.0  0.20",
                        "'<WIDTH>' 1.0  0.20",
                    )
                )
        self.assertEqual(len(rendered_inputs), 2)
        self.assertEqual(len(normalized), 1)

    def test_i7_cartesian_component_fixtures_only_change_component(self) -> None:
        expected = {
            "cartesian_component_pressure": "P",
            "cartesian_component_vertical": "V",
            "cartesian_component_horizontal": "H",
        }
        normalized: set[str] = set()
        rendered_inputs: set[str] = set()
        for case_id, component in expected.items():
            with self.subTest(case=case_id):
                definition = self.cases[case_id]
                frequencies = definition.frequencies("single")
                self.assertEqual(frequencies, (1000.0,))
                rendered = definition.render_origin_environment(
                    frequencies[0],
                    definition.shared_launch_angle_count(frequencies),
                )
                self.assertIn(f"1  5  '{component}'", rendered)
                self.assertEqual(
                    rendered.count("1  5  'P'")
                    + rendered.count("1  5  'V'")
                    + rendered.count("1  5  'H'"),
                    1,
                )
                rendered_inputs.add(rendered)
                normalized.add(
                    rendered.replace(f"1  5  '{component}'", "1  5  '<C>'")
                )
        self.assertEqual(len(rendered_inputs), 3)
        self.assertEqual(len(normalized), 1)

    def test_ray_trace_case_declares_ray_output(self) -> None:
        ray_case = self.cases["ray_trace_vacuum_rigid"]
        self.assertEqual(ray_case.output_kind, "ray")
        self.assertIsNone(ray_case.expected_dimensions)
        self.assertEqual(ray_case.supported_versions, ("origin", "f2cpp"))
        self.assertEqual(
            ray_case.shared_launch_angle_count(ray_case.frequencies("single")),
            5,
        )

    def test_rayreuse_product_cases_are_in_shared_manifest(self) -> None:
        expected = {
            "ray_trace_directional_tabulated": "ray",
            "arrival_geometric_hat_ascii": "arrivals_ascii",
            "arrival_geometric_hat_binary": "arrivals_binary",
            "arrival_zero": "arrivals_ascii",
            "eigenray_geometric_gaussian": "eigenray",
            "eigenray_zero": "eigenray",
        }
        for case_id, output_kind in expected.items():
            with self.subTest(case=case_id):
                definition = self.cases[case_id]
                self.assertEqual(definition.output_kind, output_kind)
                self.assertIn("rayreuse", definition.supported_versions)
                self.assertEqual(definition.frequencies("single"), (1000.0,))

    def test_shd_output_remains_the_default(self) -> None:
        direct = self.cases["constant_speed_direct"]
        self.assertEqual(direct.output_kind, "shd")
        self.assertIsNotNone(direct.expected_dimensions)

    def test_shd_dimensions_require_seven_positive_axes(self) -> None:
        source = self.cases["constant_speed_direct"].directory
        for replacement in (
            "shd_dimensions = [1, 1, 1]",
            "shd_dimensions = [1, 1, 1, 1, 1, 0, 51]",
        ):
            with self.subTest(replacement=replacement):
                with tempfile.TemporaryDirectory() as temporary_directory:
                    target = Path(temporary_directory) / "case"
                    shutil.copytree(source, target)
                    manifest = target / "case.toml"
                    contents = manifest.read_text(encoding="utf-8")
                    contents = contents.replace(
                        "shd_dimensions = [1, 1, 1, 1, 1, 21, 51]",
                        replacement,
                    )
                    manifest.write_text(contents, encoding="utf-8")
                    with self.assertRaisesRegex(
                        ValueError, "seven positive integers"
                    ):
                        load_case(target)

    def test_pchip_case_is_scoped_to_implemented_versions(self) -> None:
        self.assertEqual(
            self.cases["munk_pchip"].supported_versions,
            ("origin", "f2cpp"),
        )

    def test_n2_case_is_scoped_to_implemented_versions(self) -> None:
        self.assertEqual(
            self.cases["munk_n2"].supported_versions,
            ("origin", "f2cpp"),
        )

    def test_spline_case_is_scoped_to_implemented_versions(self) -> None:
        self.assertEqual(
            self.cases["munk_spline"].supported_versions,
            ("origin", "f2cpp"),
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
