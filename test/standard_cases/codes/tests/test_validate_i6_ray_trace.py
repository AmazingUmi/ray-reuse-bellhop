from __future__ import annotations

from pathlib import Path
import sys
import tempfile
import unittest


CODES_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(CODES_ROOT))

from validate_i6_ray_trace import (
    compare_ray_outputs,
    parse_ray,
    semantic_sha256,
    validate_fixture_semantics,
)


def ray_text(*, second_angle: float = 30.0, second_range: float = 20.0) -> str:
    return f"""'semantic ray fixture'
2.5D+02
1 1 1
2 1
0.0
100.0
'rz'
-30.0
2 0 1
0.0 50.0
10.0 100.0
{second_angle}
2 1 0
0.0 50.0
{second_range} 0.0
"""


def two_source_fixture_text(*, swap_second_source: bool = False) -> str:
    lines = [
        "'I6 ray trace, two sources, vacuum surface and rigid bottom'",
        "250.0",
        "1 1 2",
        "5 1",
        "0.0",
        "100.0",
        "'rz'",
    ]
    angles = [-60.0, -30.0, 0.0, 30.0, 60.0]
    for source_index in range(2):
        source_angles = list(angles)
        if source_index == 1 and swap_second_source:
            source_angles[0], source_angles[1] = (
                source_angles[1],
                source_angles[0],
            )
        for launch_index, angle in enumerate(source_angles):
            lines.extend(
                (
                    str(angle),
                    (
                        "1 1 0"
                        if source_index == 0 and launch_index == 0
                        else "1 0 1"
                        if source_index == 1 and launch_index == 4
                        else "1 0 0"
                    ),
                    f"0.0 {25.0 + 50.0 * source_index}",
                )
            )
    return "\n".join(lines) + "\n"


class RayTraceValidatorTests(unittest.TestCase):
    def write_ray(self, root: Path, name: str, contents: str) -> Path:
        path = root / name
        path.write_text(contents, encoding="utf-8")
        return path

    def test_parses_fortran_exponents_and_ray_records(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            path = self.write_ray(
                Path(temporary_directory), "fixture.ray", ray_text()
            )
            output = parse_ray(path)

        self.assertEqual(output.header.frequency_hz, 250.0)
        self.assertEqual(output.header.source_counts, (1, 1, 1))
        self.assertEqual(len(output.rays), 2)
        self.assertEqual(output.rays[0].bottom_bounces, 1)
        self.assertEqual(output.rays[1].top_bounces, 1)

    def test_semantic_comparison_ignores_text_formatting(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            reference = parse_ray(
                self.write_ray(root, "reference.ray", ray_text())
            )
            candidate = parse_ray(
                self.write_ray(
                    root,
                    "candidate.ray",
                    ray_text().replace("2.5D+02", "250.000000000"),
                )
            )

            metrics = compare_ray_outputs(reference, candidate)

        self.assertTrue(metrics["passed"])
        self.assertEqual(metrics["ray_count"], 2)
        self.assertEqual(semantic_sha256(reference), semantic_sha256(candidate))

    def test_rejects_source_major_launch_order_mismatch(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            reference = parse_ray(
                self.write_ray(root, "reference.ray", ray_text())
            )
            candidate = parse_ray(
                self.write_ray(
                    root, "candidate.ray", ray_text(second_angle=29.0)
                )
            )

            with self.assertRaisesRegex(ValueError, "launch angle"):
                compare_ray_outputs(reference, candidate)

    def test_fixture_guard_enforces_source_major_angle_blocks(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            valid = parse_ray(
                self.write_ray(root, "valid.ray", two_source_fixture_text())
            )
            reordered = parse_ray(
                self.write_ray(
                    root,
                    "reordered.ray",
                    two_source_fixture_text(swap_second_source=True),
                )
            )

            validate_fixture_semantics(valid, "valid")
            with self.assertRaisesRegex(ValueError, "source 1 launch 0"):
                validate_fixture_semantics(reordered, "reordered")

    def test_rejects_point_count_or_coordinate_drift(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            reference = parse_ray(
                self.write_ray(root, "reference.ray", ray_text())
            )
            candidate = parse_ray(
                self.write_ray(
                    root, "candidate.ray", ray_text(second_range=20.01)
                )
            )

            with self.assertRaisesRegex(ValueError, "point 1 r"):
                compare_ray_outputs(reference, candidate)


if __name__ == "__main__":
    unittest.main()
