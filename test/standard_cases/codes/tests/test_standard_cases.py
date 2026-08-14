from __future__ import annotations

from dataclasses import replace
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


CODES_ROOT = Path(__file__).resolve().parents[1]
STANDARD_CASES_ROOT = CODES_ROOT.parent
PROJECT_ROOT = STANDARD_CASES_ROOT.parents[1]
PLOTREAD_TESTS_ROOT = PROJECT_ROOT / "test" / "PlotRead" / "tests"
sys.path.insert(0, str(CODES_ROOT))
sys.path.insert(0, str(PLOTREAD_TESTS_ROOT))

from case_model import discover_cases
from standard_cases import (
    VersionAdapter,
    build_parser,
    default_adapters,
    format_frequency_csv,
    process_case,
    validate_print_output,
    validate_output,
    validate_broadband_output,
)
from support import write_little_endian_rectilinear_file


class StandardCasesAdapterTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        definitions = discover_cases(STANDARD_CASES_ROOT / "cases")
        cls.definition = definitions["constant_speed_direct"]

    def test_rayreuse_single_adapter_is_enabled_at_release_path(self) -> None:
        adapter = default_adapters(None)["rayreuse"]

        self.assertTrue(adapter.enabled)
        self.assertEqual(
            adapter.executable,
            PROJECT_ROOT
            / "Bellhop_RayReuse"
            / "build"
            / "release"
            / "bellhop_rayreuse",
        )

    def test_print_validation_uses_declared_noncoherent_mode(self) -> None:
        definitions = discover_cases(STANDARD_CASES_ROOT / "cases")
        for case_id in ("incoherent_direct", "semicoherent_direct"):
            with self.subTest(case=case_id):
                definition = definitions[case_id]
                contents = "\n".join(
                    (
                        "Cartesian beams",
                        "Rectilinear receiver grid",
                        *definition.prt_markers,
                    )
                )
                with tempfile.TemporaryDirectory() as temporary_directory:
                    print_path = Path(temporary_directory) / "case.prt"
                    print_path.write_text(contents, encoding="utf-8")
                    validate_print_output(definition, print_path)

    def test_print_validation_uses_declared_ray_centered_family(self) -> None:
        definition = discover_cases(
            STANDARD_CASES_ROOT / "cases"
        )["ray_centered_component_pressure"]
        contents = "\n".join(
            (
                "Coherent TL calculation",
                "Rectilinear receiver grid",
                *definition.prt_markers,
            )
        )
        with tempfile.TemporaryDirectory() as temporary_directory:
            print_path = Path(temporary_directory) / "case.prt"
            print_path.write_text(contents, encoding="utf-8")
            validate_print_output(definition, print_path)

    def test_print_validation_uses_declared_non_cerveny_family(self) -> None:
        definitions = discover_cases(STANDARD_CASES_ROOT / "cases")
        for case_id in (
            "geometric_hat_cartesian",
            "geometric_hat_ray_centered",
            "geometric_hat_cartesian_safe_control",
            "geometric_gaussian_cartesian",
            "simple_gaussian_cartesian",
        ):
            with self.subTest(case=case_id):
                definition = definitions[case_id]
                contents = "\n".join(
                    (
                        "Coherent TL calculation",
                        "Rectilinear receiver grid",
                        *definition.prt_markers,
                    )
                )
                with tempfile.TemporaryDirectory() as temporary_directory:
                    print_path = Path(temporary_directory) / "case.prt"
                    print_path.write_text(contents, encoding="utf-8")
                    validate_print_output(definition, print_path)

    def test_runner_execution_mode_defaults_and_accepts_both_modes(self) -> None:
        parser = build_parser()
        default_args = parser.parse_args(
            [
                "generate",
                "--version",
                "rayreuse",
                "--profile",
                "broadband_smoke",
            ]
        )
        self.assertEqual(
            default_args.rayreuse_execution_mode,
            "nonreuse",
        )

        for execution_mode in ("nonreuse", "reuse", "parallel"):
            with self.subTest(execution_mode=execution_mode):
                args = parser.parse_args(
                    [
                        "generate",
                        "--version",
                        "rayreuse",
                        "--profile",
                        "broadband_smoke",
                        "--rayreuse-execution-mode",
                        execution_mode,
                    ]
                )
                self.assertEqual(
                    args.rayreuse_execution_mode,
                    execution_mode,
                )

    def test_rayreuse_broadband_cli_passes_explicit_execution_modes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            working_directory = Path(temporary_directory)
            executable = working_directory / "bellhop_rayreuse"
            executable.touch()
            adapter = VersionAdapter(
                name="rayreuse",
                executable=executable,
                enabled=True,
            )

            for execution_mode in ("nonreuse", "reuse", "parallel"):
                with self.subTest(execution_mode=execution_mode):
                    with patch("standard_cases.subprocess.run") as run:
                        adapter.run_broadband(
                            working_directory,
                            "direct_broadband",
                            (50.0, 250.0),
                            execution_mode,
                        )

                    run.assert_called_once_with(
                        [
                            str(executable),
                            "direct_broadband",
                            "--frequencies-hz",
                            "50,250",
                            "--execution-mode",
                            execution_mode,
                        ],
                        cwd=working_directory,
                        check=True,
                    )

    def test_frequency_csv_requires_strictly_increasing_values(self) -> None:
        self.assertEqual(
            format_frequency_csv((50.0, 50.5, 250.0)),
            "50,50.5,250",
        )
        with self.assertRaisesRegex(ValueError, "strictly increasing"):
            format_frequency_csv((50.0, 50.0))
        with self.assertRaisesRegex(ValueError, "strictly increasing"):
            format_frequency_csv((250.0, 50.0))

    def test_rayreuse_broadband_manifest_records_execution_modes(self) -> None:
        adapter = default_adapters(None)["rayreuse"]
        frequencies = self.definition.frequencies("broadband_smoke")
        launch_count = self.definition.shared_launch_angle_count(frequencies)

        for execution_mode in ("nonreuse", "reuse", "parallel"):
            with self.subTest(execution_mode=execution_mode):
                with tempfile.TemporaryDirectory() as temporary_directory:
                    results_root = Path(temporary_directory)
                    manifest_path = process_case(
                        self.definition,
                        "broadband_smoke",
                        adapter,
                        "generate",
                        results_root,
                        execution_mode,
                    )

                    profile_root = (
                        results_root
                        / "rayreuse"
                        / self.definition.case_id
                        / "broadband_smoke"
                    )
                    run_root = profile_root / "broadband"
                    environment_files = list(run_root.glob("*.env"))
                    self.assertEqual(len(environment_files), 1)
                    self.assertEqual(
                        environment_files[0].read_text(encoding="utf-8"),
                        self.definition.render_origin_environment(
                            frequencies[0], launch_count
                        ),
                    )
                    self.assertFalse(
                        any(profile_root.glob("f[0-9][0-9][0-9]_*"))
                    )

                    manifest = json.loads(
                        manifest_path.read_text(encoding="utf-8")
                    )
                    self.assertEqual(
                        manifest["execution_model"],
                        "single_broadband_invocation",
                    )
                    self.assertEqual(
                        manifest["execution_mode"],
                        execution_mode,
                    )
                    self.assertEqual(
                        manifest["broadband_run"][
                            "execution_mode_argument"
                        ],
                        execution_mode,
                    )
                    self.assertEqual(
                        manifest["broadband_run"][
                            "expected_solver_invocations"
                        ],
                        1,
                    )
                    self.assertEqual(
                        manifest["broadband_run"]["frequencies_argument"],
                        "50,250",
                    )
                    self.assertEqual(len(manifest["runs"]), 2)
                    self.assertEqual(
                        len(
                            {
                                record["environment_file"]
                                for record in manifest["runs"]
                            }
                        ),
                        1,
                    )
                    self.assertEqual(
                        len(
                            {
                                record["file_root"]
                                for record in manifest["runs"]
                            }
                        ),
                        1,
                    )

    def test_rayreuse_single_generate_keeps_per_frequency_layout(self) -> None:
        adapter = default_adapters(None)["rayreuse"]

        with tempfile.TemporaryDirectory() as temporary_directory:
            results_root = Path(temporary_directory)
            manifest_path = process_case(
                self.definition,
                "single",
                adapter,
                "generate",
                results_root,
            )

            profile_root = (
                results_root
                / "rayreuse"
                / self.definition.case_id
                / "single"
            )
            self.assertTrue(
                (
                    profile_root
                    / "f000_50Hz"
                    / "constant_speed_direct_f000_50Hz.env"
                ).is_file()
            )
            self.assertFalse((profile_root / "broadband").exists())
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            self.assertNotIn("execution_mode", manifest)
            self.assertNotIn("broadband_run", manifest)

    def test_ray_case_generate_records_ray_output_contract(self) -> None:
        definition = discover_cases(
            STANDARD_CASES_ROOT / "cases"
        )["ray_trace_vacuum_rigid"]
        adapter = default_adapters(None)["origin"]

        with tempfile.TemporaryDirectory() as temporary_directory:
            results_root = Path(temporary_directory)
            manifest_path = process_case(
                definition,
                "single",
                adapter,
                "generate",
                results_root,
            )
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            run = manifest["runs"][0]
            environment = manifest_path.parent / run["environment_file"]
            rendered = environment.read_text(encoding="utf-8")

        self.assertEqual(manifest["output_kind"], "ray")
        self.assertIn("product:ray", manifest["coverage_tags"])
        self.assertIn("ray", manifest["test_sets"])
        self.assertEqual(manifest["shared_launch_angle_count"], 5)
        self.assertIsNone(run["shade_file"])
        self.assertIsNone(run["ray_file"])
        self.assertIn("\n'R'\n5\n-60.0  60.0 /\n", rendered)
        self.assertNotIn("'MS'", rendered)

    def test_shd_validation_rejects_a_stale_ray_product(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            output_root = Path(temporary_directory)
            print_path = output_root / "fixture.prt"
            shade_path = output_root / "fixture.shd"
            ray_path = output_root / "fixture.ray"
            print_path.write_text(
                "\n".join(
                    (
                        "Coherent TL calculation",
                        "Cartesian beams",
                        "Rectilinear receiver grid",
                        *self.definition.prt_markers,
                    )
                ),
                encoding="utf-8",
            )
            write_little_endian_rectilinear_file(shade_path, (50.0,))
            ray_path.write_text("stale", encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "retained ray output"):
                validate_output(
                    replace(
                        self.definition,
                        expected_dimensions=(1, 1, 1, 1, 1, 2, 3),
                    ),
                    50.0,
                    print_path,
                    shade_path,
                )

    def test_failed_run_removes_the_old_manifest_and_outputs(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            results_root = Path(temporary_directory)
            executable = results_root / "solver"
            executable.touch()
            adapter = VersionAdapter("f2cpp", executable, True)
            manifest_path = process_case(
                self.definition, "single", adapter, "generate", results_root
            )
            run_root = next(manifest_path.parent.glob("f000_*"))
            file_root = next(run_root.glob("*.env")).stem
            for suffix in (".prt", ".shd", ".ray", ".shd.tmp", ".ray.tmp"):
                (run_root / f"{file_root}{suffix}").write_text(
                    "stale", encoding="utf-8"
                )
            with patch(
                "standard_cases.subprocess.run",
                side_effect=subprocess.CalledProcessError(1, ["solver"]),
            ):
                with self.assertRaises(subprocess.CalledProcessError):
                    process_case(
                        self.definition,
                        "single",
                        adapter,
                        "run",
                        results_root,
                    )
            self.assertFalse(manifest_path.exists())
            self.assertFalse(any(run_root.glob("*.prt")))
            self.assertFalse(any(run_root.glob("*.shd")))
            self.assertFalse(any(run_root.glob("*.ray")))
            self.assertFalse(any(run_root.glob("*.tmp")))

    def test_failed_broadband_run_removes_old_manifest_and_outputs(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            results_root = Path(temporary_directory)
            executable = results_root / "solver"
            executable.touch()
            adapter = VersionAdapter("rayreuse", executable, True)
            manifest_path = process_case(
                self.definition,
                "broadband_smoke",
                adapter,
                "generate",
                results_root,
            )
            run_root = manifest_path.parent / "broadband"
            file_root = next(run_root.glob("*.env")).stem
            for suffix in (".prt", ".shd", ".ray", ".shd.tmp", ".ray.tmp"):
                (run_root / f"{file_root}{suffix}").write_text(
                    "stale", encoding="utf-8"
                )
            with patch(
                "standard_cases.subprocess.run",
                side_effect=subprocess.CalledProcessError(1, ["solver"]),
            ):
                with self.assertRaises(subprocess.CalledProcessError):
                    process_case(
                        self.definition,
                        "broadband_smoke",
                        adapter,
                        "run",
                        results_root,
                    )
            self.assertFalse(manifest_path.exists())
            self.assertFalse(any(run_root.glob("*.prt")))
            self.assertFalse(any(run_root.glob("*.shd")))
            self.assertFalse(any(run_root.glob("*.ray")))
            self.assertFalse(any(run_root.glob("*.tmp")))

    def test_broadband_validation_checks_both_execution_modes(self) -> None:
        frequencies = (50.0, 250.0)
        definition = replace(
            self.definition,
            expected_dimensions=(1, 1, 1, 1, 1, 2, 3),
        )
        for execution_mode, mode_marker, trace_passes in (
            ("nonreuse", "execution mode = broadband non-reuse", 2),
            ("reuse", "execution mode = broadband reuse", 1),
            (
                "parallel",
                "execution mode = broadband parallel reuse",
                1,
            ),
        ):
            with self.subTest(execution_mode=execution_mode):
                with tempfile.TemporaryDirectory() as temporary_directory:
                    output_root = Path(temporary_directory)
                    print_path = output_root / "fixture.prt"
                    shade_path = output_root / "fixture.shd"
                    print_path.write_text(
                        "\n".join(
                            (
                                "Coherent TL calculation",
                                "Cartesian beams",
                                "Rectilinear receiver grid",
                                *definition.prt_markers,
                                mode_marker,
                                f"Trace passes = {trace_passes}",
                            )
                        ),
                        encoding="utf-8",
                    )
                    write_little_endian_rectilinear_file(
                        shade_path, frequencies
                    )

                    validate_broadband_output(
                        definition,
                        frequencies,
                        execution_mode,
                        print_path,
                        shade_path,
                    )

    def test_broadband_validation_rejects_wrong_mode_statistics(self) -> None:
        frequencies = (50.0, 250.0)
        definition = replace(
            self.definition,
            expected_dimensions=(1, 1, 1, 1, 1, 2, 3),
        )
        with tempfile.TemporaryDirectory() as temporary_directory:
            output_root = Path(temporary_directory)
            print_path = output_root / "fixture.prt"
            shade_path = output_root / "fixture.shd"
            common_lines = (
                "Coherent TL calculation",
                "Cartesian beams",
                "Rectilinear receiver grid",
                *definition.prt_markers,
            )
            write_little_endian_rectilinear_file(shade_path, frequencies)

            for incorrect_lines, missing_marker in (
                (
                    (
                        "execution mode = broadband reuse",
                        "Trace passes = 2",
                    ),
                    "execution mode = broadband non-reuse",
                ),
                (
                    (
                        "execution mode = broadband non-reuse",
                        "Trace passes = 20",
                    ),
                    "Trace passes = 2",
                ),
            ):
                with self.subTest(missing_marker=missing_marker):
                    print_path.write_text(
                        "\n".join((*common_lines, *incorrect_lines)),
                        encoding="utf-8",
                    )
                    with self.assertRaisesRegex(
                        RuntimeError,
                        "PRT marker missing",
                    ):
                        validate_broadband_output(
                            definition,
                            frequencies,
                            "nonreuse",
                            print_path,
                            shade_path,
                        )

    def test_broadband_validation_rejects_wrong_frequency_axis(self) -> None:
        definition = replace(
            self.definition,
            expected_dimensions=(1, 1, 1, 1, 1, 2, 3),
        )
        with tempfile.TemporaryDirectory() as temporary_directory:
            output_root = Path(temporary_directory)
            print_path = output_root / "fixture.prt"
            shade_path = output_root / "fixture.shd"
            print_path.write_text(
                "\n".join(
                    (
                        "Coherent TL calculation",
                        "Cartesian beams",
                        "Rectilinear receiver grid",
                        *definition.prt_markers,
                        "execution mode = broadband non-reuse",
                        "Trace passes = 2",
                    )
                ),
                encoding="utf-8",
            )
            write_little_endian_rectilinear_file(
                shade_path, (50.0, 251.0)
            )

            with self.assertRaisesRegex(RuntimeError, "frequency axis"):
                validate_broadband_output(
                    definition,
                    (50.0, 250.0),
                    "nonreuse",
                    print_path,
                    shade_path,
                )


if __name__ == "__main__":
    unittest.main()
