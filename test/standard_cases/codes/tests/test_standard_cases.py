from __future__ import annotations

from dataclasses import replace
import json
from pathlib import Path
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
