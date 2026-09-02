from __future__ import annotations

import json
from pathlib import Path
import sys
import tempfile
import unittest


CODES_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(CODES_ROOT))

from benchmark_rayreuse import (
    BenchmarkConfiguration,
    add_speedups_vs_nonreuse,
    build_parser,
    expand_configurations,
    normalize_max_rss_kib,
    parse_frequency_csv,
    parse_prt_metrics,
    require_cross_configuration_hashes,
    require_identical_sample_hashes,
    rotated_indices,
    summarize_samples,
    validate_prt_metrics,
    write_json_atomic,
)


def fixture_prt(mode: str = "parallel") -> str:
    marker = {
        "nonreuse": "broadband non-reuse",
        "reuse": "broadband reuse",
        "fused": "broadband fused reuse",
        "parallel": "broadband parallel reuse",
    }[mode]
    wall_name = {
        "nonreuse": "non-reuse wall seconds",
        "reuse": "reuse wall seconds",
        "fused": "fused reuse wall seconds",
        "parallel": "parallel reuse wall seconds",
    }[mode]
    return "\n".join(
        (
            f"execution mode = {marker}",
            "Trace passes = 1",
            "ray count = 1000",
            "ray point count = 10000",
            "ray cache bytes = 4096",
            "requested worker count = 8",
            "active frequency limit = 6",
            "output queue capacity = 2",
            "peak queued results = 2",
            "estimated workspace bytes = 131072",
            "estimated peak memory bytes = 1048576",
            "memory budget bytes = 0",
            "Trace seconds = 1.25",
            "Project seconds = 2.5",
            "Influence seconds = 3.75",
            "Scale seconds = 0.125",
            f"{wall_name} = 7.625",
            "SHD seconds = 0.5",
            "Total solver and product seconds = 8.125",
            "Bellhop RayReuse completed successfully",
        )
    )


def sample(
    digest: str = "a" * 64,
    real_seconds: float = 10.0,
    max_rss_kib: int = 100,
) -> dict[str, object]:
    return {
        "real_seconds": real_seconds,
        "max_rss_kib": max_rss_kib,
        "shd_sha256": digest,
        "prt": {
            "trace_seconds": 1.0,
            "project_seconds": 2.0,
            "influence_seconds": 3.0,
            "scale_seconds": 0.1,
            "solver_wall_seconds": 6.1,
            "shd_seconds": 0.2,
            "total_solver_and_product_seconds": 6.3,
        },
    }


class PrtParsingTests(unittest.TestCase):
    def test_parses_required_phases_and_parallel_statistics(self) -> None:
        metrics = parse_prt_metrics(fixture_prt(), "parallel")

        self.assertEqual(metrics["trace_passes"], 1)
        self.assertEqual(metrics["trace_seconds"], 1.25)
        self.assertEqual(metrics["project_seconds"], 2.5)
        self.assertEqual(metrics["influence_seconds"], 3.75)
        self.assertEqual(metrics["scale_seconds"], 0.125)
        self.assertEqual(metrics["solver_wall_seconds"], 7.625)
        self.assertEqual(metrics["shd_seconds"], 0.5)
        self.assertEqual(metrics["total_solver_and_product_seconds"], 8.125)
        self.assertEqual(metrics["active_frequency_limit"], 6)
        self.assertEqual(metrics["estimated_peak_memory_bytes"], 1048576)
        self.assertTrue(metrics["completed_successfully"])

    def test_parses_and_validates_fused_mode(self) -> None:
        configuration = BenchmarkConfiguration(execution_mode="fused")
        metrics = parse_prt_metrics(fixture_prt("fused"), "fused")

        self.assertEqual(
            metrics["execution_mode_marker"], "broadband fused reuse"
        )
        self.assertEqual(metrics["solver_wall_field"], "fused reuse wall seconds")
        self.assertEqual(metrics["ray_count"], 1000)
        self.assertEqual(metrics["ray_cache_bytes"], 4096)

        validate_prt_metrics(metrics, configuration, 16)

        with self.assertRaisesRegex(ValueError, "does not match"):
            parse_prt_metrics(fixture_prt("reuse"), "fused")

    def test_parses_optional_influence_counters(self) -> None:
        counter_lines = "\n".join(
            (
                "Influence ray accumulations = 10000",
                "Influence active ray points = 3367946",
                "Influence window rejections = 1697322678",
                "Influence taper rejections = 894589970",
                "Influence nonzero image contributions = 406782232",
                "Influence geometry segment evaluations = 3304654",
                "Influence geometry range evaluations = 4972960",
                "Influence geometry depth evaluations = 999564960",
                "Influence geometry image geometry evaluations = 2998694880",
                "Influence frequency range kernel evaluations = 9945920",
                "Influence frequency image kernel evaluations = 5997389760",
                "Influence validation seconds = 0.000150723",
                "Influence precompute seconds = 0.133848942",
                "Influence hot loop seconds = 9.618053271",
            )
        )
        metrics = parse_prt_metrics(
            fixture_prt("reuse") + "\n" + counter_lines, "reuse"
        )

        self.assertEqual(
            metrics["influence_geometry_segment_evaluations"], 3304654
        )
        self.assertEqual(
            metrics["influence_geometry_depth_evaluations"], 999564960
        )
        self.assertEqual(
            metrics["influence_frequency_image_kernel_evaluations"],
            5997389760,
        )
        self.assertEqual(metrics["influence_window_rejections"], 1697322678)
        self.assertEqual(
            metrics["influence_nonzero_image_contributions"], 406782232
        )
        self.assertEqual(
            metrics["influence_hot_loop_seconds"], 9.618053271
        )
        self.assertEqual(
            metrics["influence_precompute_seconds"], 0.133848942
        )
        with self.assertRaisesRegex(ValueError, "is not an integer"):
            parse_prt_metrics(
                (
                    fixture_prt("reuse")
                    + "\n"
                    + counter_lines
                ).replace(
                    "Influence ray accumulations = 10000",
                    "Influence ray accumulations = many",
                ),
                "reuse",
            )

    def test_validates_parallel_protocol_invariants(self) -> None:
        configuration = BenchmarkConfiguration(
            execution_mode="parallel",
            parallel_workers=8,
            output_queue_capacity=2,
            memory_budget_mib=None,
        )
        metrics = parse_prt_metrics(fixture_prt(), "parallel")

        validate_prt_metrics(metrics, configuration, 10)

        invalid = dict(metrics)
        invalid["requested_worker_count"] = 4
        with self.assertRaisesRegex(ValueError, "worker count"):
            validate_prt_metrics(invalid, configuration, 10)

    def test_parses_and_validates_frequency_task_timings(self) -> None:
        task_lines = "\n".join(
            (
                "frequency task count = 2",
                "frequency task 0 frequency Hz = 50",
                "frequency task 0 Project seconds = 0.1",
                "frequency task 0 Influence seconds = 1.2",
                "frequency task 0 Scale seconds = 0.03",
                "frequency task 0 total seconds = 1.33",
                "frequency task 1 frequency Hz = 250",
                "frequency task 1 Project seconds = 0.2",
                "frequency task 1 Influence seconds = 2.4",
                "frequency task 1 Scale seconds = 0.04",
                "frequency task 1 total seconds = 2.64",
            )
        )
        metrics = parse_prt_metrics(
            fixture_prt().replace(
                "active frequency limit = 6",
                "active frequency limit = 2",
            )
            + "\n"
            + task_lines,
            "parallel",
        )
        configuration = BenchmarkConfiguration(
            execution_mode="parallel",
            parallel_workers=8,
            output_queue_capacity=2,
            memory_budget_mib=None,
        )

        validate_prt_metrics(
            metrics, configuration, 2, expect_frequency_tasks=True
        )
        self.assertEqual(len(metrics["frequency_tasks"]), 2)
        self.assertEqual(
            metrics["frequency_tasks"][1]["frequency_hz"], 250.0
        )
        self.assertEqual(
            metrics["frequency_tasks"][1]["total_seconds"], 2.64
        )

    def test_rejects_missing_completion_and_wrong_trace_count(self) -> None:
        configuration = BenchmarkConfiguration(
            execution_mode="parallel",
            parallel_workers=8,
            output_queue_capacity=2,
            memory_budget_mib=None,
        )
        missing_completion = parse_prt_metrics(
            fixture_prt().replace(
                "\nBellhop RayReuse completed successfully", ""
            ),
            "parallel",
        )
        with self.assertRaisesRegex(ValueError, "completion"):
            validate_prt_metrics(missing_completion, configuration, 10)

        wrong_trace_count = parse_prt_metrics(
            fixture_prt().replace("Trace passes = 1", "Trace passes = 2"),
            "parallel",
        )
        with self.assertRaisesRegex(ValueError, "trace passes"):
            validate_prt_metrics(wrong_trace_count, configuration, 10)

    def test_rejects_missing_duplicate_nonfinite_and_wrong_mode_fields(
        self,
    ) -> None:
        with self.assertRaisesRegex(ValueError, "missing PRT"):
            parse_prt_metrics(
                fixture_prt().replace("Trace seconds = 1.25\n", ""),
                "parallel",
            )
        with self.assertRaisesRegex(ValueError, "duplicate PRT"):
            parse_prt_metrics(
                fixture_prt() + "\nTrace seconds = 2.0\n", "parallel"
            )
        with self.assertRaisesRegex(ValueError, "finite"):
            parse_prt_metrics(
                fixture_prt().replace(
                    "Influence seconds = 3.75",
                    "Influence seconds = nan",
                ),
                "parallel",
            )
        with self.assertRaisesRegex(ValueError, "does not match"):
            parse_prt_metrics(fixture_prt("reuse"), "parallel")


class RssNormalizationTests(unittest.TestCase):
    def test_normalizes_darwin_bytes_and_linux_kib(self) -> None:
        self.assertEqual(normalize_max_rss_kib(10 * 1024, "Darwin"), 10)
        self.assertEqual(normalize_max_rss_kib(10, "Linux"), 10)
        self.assertEqual(normalize_max_rss_kib(1025, "darwin"), 2)

    def test_rejects_invalid_rss(self) -> None:
        for value in (-1, float("nan"), float("inf")):
            with self.subTest(value=value):
                with self.assertRaises(ValueError):
                    normalize_max_rss_kib(value, "Linux")

    def test_rejects_platform_with_unknown_rss_units(self) -> None:
        with self.assertRaisesRegex(ValueError, "unsupported platform"):
            normalize_max_rss_kib(10, "Windows")


class ConfigurationTests(unittest.TestCase):
    def test_expands_workers_only_for_parallel_mode(self) -> None:
        configurations = expand_configurations(
            ("nonreuse", "reuse", "fused", "parallel"),
            (4, 8),
            2,
            447,
        )

        self.assertEqual(
            [configuration.identifier for configuration in configurations],
            (
                [
                    "nonreuse",
                    "reuse",
                    "fused",
                    "parallel-w4-q2-m447MiB",
                    "parallel-w8-q2-m447MiB",
                ]
            ),
        )
        self.assertIsNone(configurations[0].parallel_workers)
        self.assertEqual(configurations[-1].parallel_workers, 8)
        self.assertEqual(configurations[-1].memory_budget_mib, 447)

    def test_rejects_invalid_configuration_inputs(self) -> None:
        with self.assertRaisesRegex(ValueError, "queue"):
            expand_configurations(("parallel",), (8,), 3, None)
        with self.assertRaisesRegex(ValueError, "worker"):
            expand_configurations(("parallel",), (), 2, None)
        with self.assertRaisesRegex(ValueError, "unknown"):
            expand_configurations(("invalid",), (8,), 2, None)


class FrequencyOverrideTests(unittest.TestCase):
    def test_accepts_strictly_increasing_positive_csv(self) -> None:
        self.assertEqual(
            parse_frequency_csv("50, 250.5,500"),
            (50.0, 250.5, 500.0),
        )

    def test_rejects_invalid_frequency_csv_values(self) -> None:
        for value in ("50", "50,50", "500,250", "0,250", "50,-250", "a,250"):
            with self.subTest(value=value):
                with self.assertRaises(SystemExit):
                    build_parser().parse_args(
                        ("--case", "constant_speed_direct",
                         "--frequencies-csv", value)
                    )


class SummaryAndHashTests(unittest.TestCase):
    def test_summary_contains_median_min_and_max(self) -> None:
        samples = [
            sample(real_seconds=12.0, max_rss_kib=300),
            sample(real_seconds=10.0, max_rss_kib=100),
            sample(real_seconds=11.0, max_rss_kib=200),
        ]

        summary = summarize_samples(samples)

        self.assertEqual(summary["sample_count"], 3)
        self.assertEqual(
            summary["real_seconds"],
            {"median": 11.0, "min": 10.0, "max": 12.0},
        )
        self.assertEqual(
            summary["max_rss_kib"],
            {"median": 200.0, "min": 100.0, "max": 300.0},
        )

    def test_rejects_hash_mismatch_within_configuration(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "not byte-identical"):
            require_identical_sample_hashes(
                [sample("a" * 64), sample("b" * 64)],
                "parallel-w8",
            )

    def test_rejects_hash_mismatch_across_configurations(self) -> None:
        results = [
            {
                "configuration": {"identifier": "reuse"},
                "representative_shd_sha256": "a" * 64,
            },
            {
                "configuration": {"identifier": "parallel-w8"},
                "representative_shd_sha256": "b" * 64,
            },
        ]
        with self.assertRaisesRegex(RuntimeError, "differ across"):
            require_cross_configuration_hashes(results)

    def test_adds_speedups_from_external_real_time_medians(self) -> None:
        results = [
            {
                "configuration": {"execution_mode": "nonreuse"},
                "summary": {"real_seconds": {"median": 12.0}},
            },
            {
                "configuration": {"execution_mode": "reuse"},
                "summary": {"real_seconds": {"median": 3.0}},
            },
        ]

        add_speedups_vs_nonreuse(results)

        self.assertEqual(results[0]["speedup_vs_nonreuse"], 1.0)
        self.assertEqual(results[1]["speedup_vs_nonreuse"], 4.0)

    def test_rotates_configuration_order_per_round(self) -> None:
        self.assertEqual(rotated_indices(3, 0), (0, 1, 2))
        self.assertEqual(rotated_indices(3, 1), (1, 2, 0))
        self.assertEqual(rotated_indices(3, 2), (2, 0, 1))
        self.assertEqual(rotated_indices(3, 3), (0, 1, 2))

    def test_atomic_json_write_replaces_complete_document(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "benchmark.json"
            output.write_text('{"old": true}\n', encoding="utf-8")

            write_json_atomic(output, {"schema_version": 1})

            self.assertEqual(
                json.loads(output.read_text(encoding="utf-8")),
                {"schema_version": 1},
            )
            self.assertEqual(list(output.parent.glob("*.tmp")), [])

    def test_atomic_json_write_rejects_nan_without_replacing_output(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "benchmark.json"
            output.write_text('{"old": true}\n', encoding="utf-8")

            with self.assertRaises(ValueError):
                write_json_atomic(output, {"invalid": float("nan")})

            self.assertEqual(
                json.loads(output.read_text(encoding="utf-8")),
                {"old": True},
            )


class CliValidationTests(unittest.TestCase):
    def test_parser_accepts_complete_configuration(self) -> None:
        args = build_parser().parse_args(
            [
                "--case",
                "constant_speed_direct",
                "--profile",
                "broadband_stress",
                "--frequencies-csv",
                "50,250",
                "--modes",
                "nonreuse,reuse,fused,parallel",
                "--repeats",
                "5",
                "--warmups",
                "2",
                "--parallel-workers",
                "4,8",
                "--queue",
                "1",
                "--memory-budget-mib",
                "447",
                "--profile-frequency-tasks",
                "--executable",
                "/tmp/bellhop_rayreuse",
                "--output",
                "/tmp/benchmark.json",
                "--machine-label",
                "benchmark-host",
                "--no-cross-mode-shd-check",
                "--allow-dirty",
            ]
        )

        self.assertEqual(args.cases, ["constant_speed_direct"])
        self.assertEqual(
            args.modes, ("nonreuse", "reuse", "fused", "parallel")
        )
        self.assertEqual(args.frequencies_csv, (50.0, 250.0))
        self.assertEqual(args.parallel_workers, (4, 8))
        self.assertEqual(args.repeats, 5)
        self.assertEqual(args.warmups, 2)
        self.assertEqual(args.output_queue_capacity, 1)
        self.assertEqual(args.memory_budget_mib, 447)
        self.assertTrue(args.profile_frequency_tasks)
        self.assertEqual(args.machine_label, "benchmark-host")
        self.assertTrue(args.no_cross_mode_shd_check)
        self.assertTrue(args.allow_dirty)

    def test_parser_rejects_invalid_numeric_and_mode_values(self) -> None:
        invalid_argument_sets = (
            ("--case", "constant_speed_direct", "--repeats", "0"),
            ("--case", "constant_speed_direct", "--warmups", "-1"),
            ("--case", "constant_speed_direct", "--queue", "3"),
            (
                "--case",
                "constant_speed_direct",
                "--modes",
                "reuse,unknown",
            ),
            (
                "--case",
                "constant_speed_direct",
                "--parallel-workers",
                "8,0",
            ),
        )
        for arguments in invalid_argument_sets:
            with self.subTest(arguments=arguments):
                with self.assertRaises(SystemExit):
                    build_parser().parse_args(arguments)

    def test_internal_sample_request_does_not_require_case(self) -> None:
        args = build_parser().parse_args(
            ["--internal-sample-request", "/tmp/request.json"]
        )

        self.assertIsNone(args.cases)
        self.assertEqual(
            args.internal_sample_request, Path("/tmp/request.json")
        )


if __name__ == "__main__":
    unittest.main()
