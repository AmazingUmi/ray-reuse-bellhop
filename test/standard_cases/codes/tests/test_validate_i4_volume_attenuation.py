from __future__ import annotations

import json
from pathlib import Path
import struct
import tempfile
import unittest
import sys

import numpy as np

CODES_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(CODES_ROOT))

from validate_i4_volume_attenuation import (
    CASE_FREQUENCIES,
    CONTROL_CASE,
    EXPECTED_THORP_HASHES,
    sha256,
    validate,
)


def write_test_shd(
    path: Path, frequencies_hz: tuple[float, ...], scale: float = 1.0
) -> None:
    """Create a compact, deterministic Bellhop rectilinear SHD fixture."""
    record_bytes = 164
    ntheta = nsx = nsy = nsz = 1
    nrz, nrr = 2, 2
    records = [
        bytearray(record_bytes)
        for _ in range(10 + len(frequencies_hz) * nrz)
    ]

    def pack(record: int, fmt: str, *values: object) -> None:
        struct.pack_into(f"<{fmt}", records[record], 0, *values)

    pack(0, "i80s", record_bytes // 4, b"Synthetic rectilinear SHD fixture")
    pack(1, "10s", b"rectilin  ")
    pack(
        2,
        "7i2d",
        len(frequencies_hz),
        ntheta,
        nsx,
        nsy,
        nsz,
        nrz,
        nrr,
        frequencies_hz[0],
        0.0,
    )
    pack(3, f"{len(frequencies_hz)}d", *frequencies_hz)
    pack(4, "d", 0.0)
    pack(5, "d", 0.0)
    pack(6, "d", 0.0)
    pack(7, "f", 10.0)
    pack(8, "2f", 20.0, 30.0)
    pack(9, "2d", 1000.0, 2000.0)

    for frequency_index, _ in enumerate(frequencies_hz):
        for receiver_depth_index in range(nrz):
            values: list[float] = []
            for receiver_range_index in range(nrr):
                values.extend((scale, 0.0))
            record = 10 + frequency_index * nrz + receiver_depth_index
            pack(record, "4f", *values)

    path.write_bytes(b"".join(records))


class VolumeAttenuationValidatorTests(unittest.TestCase):
    def test_case_frequencies_structure(self) -> None:
        self.assertIn("constant_speed_thorp", CASE_FREQUENCIES)
        self.assertIn("volume_attenuation_francois_garrison", CASE_FREQUENCIES)
        self.assertIn("volume_attenuation_biological", CASE_FREQUENCIES)
        self.assertEqual(CONTROL_CASE, "constant_speed_no_attenuation_5khz")
        self.assertEqual(
            len(CASE_FREQUENCIES["constant_speed_thorp"]["broadband_regression"]), 16
        )

    def test_expected_thorp_hashes(self) -> None:
        self.assertEqual(len(EXPECTED_THORP_HASHES), 3)
        for profile in ("single", "broadband_smoke", "broadband_regression"):
            self.assertIn(profile, EXPECTED_THORP_HASHES)
            self.assertEqual(len(EXPECTED_THORP_HASHES[profile]), 64)

    def _setup_synthetic_tree(
        self, root: Path, mutate_thorp_hash: bool = False, make_noop: bool = False
    ) -> tuple[Path, Path, Path]:
        results_root = root / "results"
        bin_root = root / "bin"
        bin_root.mkdir(parents=True)
        origin_exe = bin_root / "bellhop_origin"
        f2cpp_exe = bin_root / "bellhop_f2cpp"
        rayreuse_exe = bin_root / "bellhop_rayreuse"
        origin_exe.write_text("origin_binary")
        f2cpp_exe.write_text("f2cpp_binary")
        rayreuse_exe.write_text("rayreuse_binary")

        all_cases = {**CASE_FREQUENCIES, CONTROL_CASE: {"single": (5000.0,)}}
        for version, exe in [("origin", origin_exe), ("f2cpp", f2cpp_exe), ("rayreuse", rayreuse_exe)]:
            for case_id, profiles in all_cases.items():
                for profile, frequencies in profiles.items():
                    prof_dir = results_root / version / case_id / profile
                    runs = []
                    if version == "rayreuse" and profile != "single":
                        b_dir = prof_dir / "broadband"
                        b_dir.mkdir(parents=True)
                        b_env = b_dir / f"{case_id}_{profile}_broadband.env"
                        b_shd = b_dir / f"{case_id}_{profile}_broadband.shd"
                        b_env.write_text("broadband_env")
                        scale = 1.0 if (case_id == CONTROL_CASE or (make_noop and case_id != CONTROL_CASE)) else 0.5
                        write_test_shd(b_shd, frequencies, scale)
                        if mutate_thorp_hash and case_id == "constant_speed_thorp":
                            b_shd.write_bytes(b_shd.read_bytes() + b"\x00")
                        runs = [
                            {
                                "frequency_hz": freq,
                                "status": "passed",
                                "environment_file": f"broadband/{case_id}_{profile}_broadband.env",
                                "shade_file": f"broadband/{case_id}_{profile}_broadband.shd",
                            }
                            for freq in frequencies
                        ]
                    else:
                        for idx, freq in enumerate(frequencies):
                            r_dir = prof_dir / (f"f{idx:03d}_{freq:g}Hz" if profile != "single" else f"f000_{freq:g}Hz")
                            r_dir.mkdir(parents=True)
                            fname = f"{case_id}_f{idx:03d}_{freq:g}Hz" if profile != "single" else f"{case_id}_f000_{freq:g}Hz"
                            env_f = r_dir / f"{fname}.env"
                            shd_f = r_dir / f"{fname}.shd"
                            env_f.write_text("shared_env_content")
                            scale = 1.0 if (case_id == CONTROL_CASE or (make_noop and case_id != CONTROL_CASE)) else 0.5
                            write_test_shd(shd_f, (freq,), scale)
                            runs.append({
                                "frequency_hz": freq,
                                "status": "passed",
                                "environment_file": f"{r_dir.name}/{fname}.env",
                                "shade_file": f"{r_dir.name}/{fname}.shd",
                            })
                    (prof_dir / "run_manifest.json").write_text(json.dumps({
                        "version": version,
                        "case_id": case_id,
                        "profile": profile,
                        "last_stage": "test",
                        "executable": str(exe),
                        "runs": runs,
                    }))

        # If not mutating thorp hash, dynamically patch EXPECTED_THORP_HASHES in test environment
        if not mutate_thorp_hash:
            for profile in ("single", "broadband_smoke", "broadband_regression"):
                shd = results_root / "rayreuse" / "constant_speed_thorp" / profile
                shd_file = (shd / "broadband" / f"constant_speed_thorp_{profile}_broadband.shd") if profile != "single" else (shd / "f000_5000Hz" / "constant_speed_thorp_f000_5000Hz.shd")
                EXPECTED_THORP_HASHES[profile] = sha256(shd_file)

        return results_root, origin_exe, f2cpp_exe, rayreuse_exe

    def test_three_party_synthetic_validation_counts_and_gates(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            results_root, origin_exe, f2cpp_exe, rayreuse_exe = self._setup_synthetic_tree(Path(temp_dir))
            res = validate(results_root, origin_exe, f2cpp_exe, rayreuse_exe)
            self.assertEqual(res["status"], "passed")
            self.assertEqual(res["total_pairwise_comparisons"], 75)
            self.assertEqual(res["gating_passed_comparisons"], 39)
            self.assertEqual(res["non_gating_comparisons"], 36)
            self.assertEqual(len(res["lossless_noop_guards"]), 9)
            self.assertTrue(res["thorp_baseline_hashes_verified"])

    def test_thorp_hash_mismatch_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            results_root, origin_exe, f2cpp_exe, rayreuse_exe = self._setup_synthetic_tree(
                Path(temp_dir)
            )
            EXPECTED_THORP_HASHES["broadband_smoke"] = "0" * 64
            with self.assertRaisesRegex(ValueError, "SHD hash"):
                validate(results_root, origin_exe, f2cpp_exe, rayreuse_exe)

    def test_noop_guard_failure_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            results_root, origin_exe, f2cpp_exe, rayreuse_exe = self._setup_synthetic_tree(
                Path(temp_dir), make_noop=True
            )
            with self.assertRaisesRegex(ValueError, "no-op"):
                validate(results_root, origin_exe, f2cpp_exe, rayreuse_exe)


if __name__ == "__main__":
    unittest.main()
