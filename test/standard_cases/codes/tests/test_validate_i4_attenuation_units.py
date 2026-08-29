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

from validate_i4_attenuation_units import (
    PROFILES,
    UNIT_SUFFIXES,
    sha256,
    validate,
)


def write_test_shd(
    path: Path, frequencies_hz: tuple[float, ...], pressure_by_freq: dict[float, float] | float = 1.0
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

    for frequency_index, freq in enumerate(frequencies_hz):
        for receiver_depth_index in range(nrz):
            values: list[float] = []
            val = pressure_by_freq[freq] if isinstance(pressure_by_freq, dict) else pressure_by_freq
            for receiver_range_index in range(nrr):
                values.extend((val, 0.0))
            record = 10 + frequency_index * nrz + receiver_depth_index
            pack(record, "4f", *values)

    path.write_bytes(b"".join(records))


class AttenuationUnitsValidatorTests(unittest.TestCase):
    def test_units_and_profiles_structure(self) -> None:
        self.assertEqual(len(UNIT_SUFFIXES), 6)
        self.assertIn("single", PROFILES)
        self.assertIn("broadband_smoke", PROFILES)
        self.assertEqual(PROFILES["single"], (5000.0,))
        self.assertEqual(PROFILES["broadband_smoke"], (4000.0, 5000.0))

    def test_sha256_computes_deterministic_digest(self) -> None:
        case_toml = CODES_ROOT.parent / "cases" / "attenuation_unit_n" / "case.toml"
        digest = sha256(case_toml)
        self.assertEqual(len(digest), 64)
        self.assertEqual(digest, sha256(case_toml))

    def test_synthetic_validation_counts_and_gates(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            results_root = root / "results"
            bin_root = root / "bin"
            bin_root.mkdir(parents=True)
            origin_exe = bin_root / "bellhop_origin"
            f2cpp_exe = bin_root / "bellhop_f2cpp"
            rayreuse_exe = bin_root / "bellhop_rayreuse"
            origin_exe.write_text("origin_binary")
            f2cpp_exe.write_text("f2cpp_binary")
            rayreuse_exe.write_text("rayreuse_binary")

            # Create synthetic products
            for version, exe in [("origin", origin_exe), ("f2cpp", f2cpp_exe), ("rayreuse", rayreuse_exe)]:
                for suffix in UNIT_SUFFIXES:
                    case_id = f"attenuation_unit_{suffix}"
                    # Single profile
                    single_dir = results_root / version / case_id / "single"
                    run_dir = single_dir / "f000_5000Hz"
                    run_dir.mkdir(parents=True)
                    env_file = run_dir / f"{case_id}_f000_5000Hz.env"
                    shd_file = run_dir / f"{case_id}_f000_5000Hz.shd"
                    env_file.write_text("shared_env_content_single")
                    write_test_shd(shd_file, (5000.0,), 1.0)
                    manifest = {
                        "version": version,
                        "case_id": case_id,
                        "profile": "single",
                        "last_stage": "test",
                        "executable": str(exe),
                        "runs": [{
                            "frequency_hz": 5000.0,
                            "status": "passed",
                            "environment_file": f"f000_5000Hz/{case_id}_f000_5000Hz.env",
                            "shade_file": f"f000_5000Hz/{case_id}_f000_5000Hz.shd",
                        }]
                    }
                    (single_dir / "run_manifest.json").write_text(json.dumps(manifest))

                    # Broadband smoke profile
                    smoke_dir = results_root / version / case_id / "broadband_smoke"
                    p_4k = 0.8 if suffix in ("f", "w", "q", "l") else 1.0
                    if version == "rayreuse":
                        b_run_dir = smoke_dir / "broadband"
                        b_run_dir.mkdir(parents=True)
                        b_env = b_run_dir / f"{case_id}_broadband_smoke_broadband.env"
                        b_shd = b_run_dir / f"{case_id}_broadband_smoke_broadband.shd"
                        b_env.write_text("broadband_env")
                        write_test_shd(b_shd, (4000.0, 5000.0), {4000.0: p_4k, 5000.0: 1.0})
                        manifest = {
                            "version": version,
                            "case_id": case_id,
                            "profile": "broadband_smoke",
                            "last_stage": "test",
                            "executable": str(exe),
                            "runs": [
                                {
                                    "frequency_hz": 4000.0,
                                    "status": "passed",
                                    "environment_file": f"broadband/{case_id}_broadband_smoke_broadband.env",
                                    "shade_file": f"broadband/{case_id}_broadband_smoke_broadband.shd",
                                },
                                {
                                    "frequency_hz": 5000.0,
                                    "status": "passed",
                                    "environment_file": f"broadband/{case_id}_broadband_smoke_broadband.env",
                                    "shade_file": f"broadband/{case_id}_broadband_smoke_broadband.shd",
                                }
                            ]
                        }
                        (smoke_dir / "run_manifest.json").write_text(json.dumps(manifest))
                    else:
                        r0 = smoke_dir / "f000_4000Hz"
                        r1 = smoke_dir / "f001_5000Hz"
                        r0.mkdir(parents=True)
                        r1.mkdir(parents=True)
                        (r0 / f"{case_id}_f000_4000Hz.env").write_text("smoke_4k_env")
                        (r1 / f"{case_id}_f001_5000Hz.env").write_text("smoke_5k_env")
                        write_test_shd(r0 / f"{case_id}_f000_4000Hz.shd", (4000.0,), p_4k)
                        write_test_shd(r1 / f"{case_id}_f001_5000Hz.shd", (5000.0,), 1.0)
                        manifest = {
                            "version": version,
                            "case_id": case_id,
                            "profile": "broadband_smoke",
                            "last_stage": "test",
                            "executable": str(exe),
                            "runs": [
                                {
                                    "frequency_hz": 4000.0,
                                    "status": "passed",
                                    "environment_file": f"f000_4000Hz/{case_id}_f000_4000Hz.env",
                                    "shade_file": f"f000_4000Hz/{case_id}_f000_4000Hz.shd",
                                },
                                {
                                    "frequency_hz": 5000.0,
                                    "status": "passed",
                                    "environment_file": f"f001_5000Hz/{case_id}_f001_5000Hz.env",
                                    "shade_file": f"f001_5000Hz/{case_id}_f001_5000Hz.shd",
                                }
                            ]
                        }
                        (smoke_dir / "run_manifest.json").write_text(json.dumps(manifest))

            res = validate(results_root, origin_exe, f2cpp_exe, rayreuse_exe)
            self.assertEqual(res["status"], "passed")
            self.assertEqual(res["total_pairwise_comparisons"], 54)
            self.assertEqual(res["gating_passed_comparisons"], 42)
            self.assertEqual(res["non_gating_comparisons"], 12)

    def test_mismatched_env_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            results_root = root / "results"
            bin_root = root / "bin"
            bin_root.mkdir(parents=True)
            origin_exe = bin_root / "bellhop_origin"
            f2cpp_exe = bin_root / "bellhop_f2cpp"
            rayreuse_exe = bin_root / "bellhop_rayreuse"
            origin_exe.write_text("origin_binary")
            f2cpp_exe.write_text("f2cpp_binary")
            rayreuse_exe.write_text("rayreuse_binary")

            for version, exe in [("origin", origin_exe), ("f2cpp", f2cpp_exe), ("rayreuse", rayreuse_exe)]:
                for suffix in UNIT_SUFFIXES:
                    case_id = f"attenuation_unit_{suffix}"
                    single_dir = results_root / version / case_id / "single"
                    run_dir = single_dir / "f000_5000Hz"
                    run_dir.mkdir(parents=True)
                    env_file = run_dir / f"{case_id}_f000_5000Hz.env"
                    shd_file = run_dir / f"{case_id}_f000_5000Hz.shd"
                    # Introduce mismatch in f2cpp env
                    env_file.write_text("corrupt_env" if version == "f2cpp" and suffix == "n" else "shared_env_content_single")
                    write_test_shd(shd_file, (5000.0,), 1.0)
                    (single_dir / "run_manifest.json").write_text(json.dumps({
                        "version": version, "case_id": case_id, "profile": "single", "last_stage": "test",
                        "executable": str(exe),
                        "runs": [{"frequency_hz": 5000.0, "status": "passed",
                                 "environment_file": f"f000_5000Hz/{case_id}_f000_5000Hz.env",
                                 "shade_file": f"f000_5000Hz/{case_id}_f000_5000Hz.shd"}]
                    }))
                    # Minimal smoke setup
                    smoke_dir = results_root / version / case_id / "broadband_smoke"
                    if version == "rayreuse":
                        b_run_dir = smoke_dir / "broadband"
                        b_run_dir.mkdir(parents=True)
                        (b_run_dir / f"{case_id}_broadband_smoke_broadband.env").write_text("broadband_env")
                        write_test_shd(b_run_dir / f"{case_id}_broadband_smoke_broadband.shd", (4000.0, 5000.0), 1.0)
                        (smoke_dir / "run_manifest.json").write_text(json.dumps({
                            "version": version, "case_id": case_id, "profile": "broadband_smoke", "last_stage": "test",
                            "executable": str(exe),
                            "runs": [{"frequency_hz": 4000.0, "status": "passed", "environment_file": f"broadband/{case_id}_broadband_smoke_broadband.env", "shade_file": f"broadband/{case_id}_broadband_smoke_broadband.shd"},
                                     {"frequency_hz": 5000.0, "status": "passed", "environment_file": f"broadband/{case_id}_broadband_smoke_broadband.env", "shade_file": f"broadband/{case_id}_broadband_smoke_broadband.shd"}]
                        }))
                    else:
                        r0 = smoke_dir / "f000_4000Hz"
                        r1 = smoke_dir / "f001_5000Hz"
                        r0.mkdir(parents=True)
                        r1.mkdir(parents=True)
                        (r0 / f"{case_id}_f000_4000Hz.env").write_text("smoke_4k_env")
                        (r1 / f"{case_id}_f001_5000Hz.env").write_text("smoke_5k_env")
                        write_test_shd(r0 / f"{case_id}_f000_4000Hz.shd", (4000.0,), 1.0)
                        write_test_shd(r1 / f"{case_id}_f001_5000Hz.shd", (5000.0,), 1.0)
                        (smoke_dir / "run_manifest.json").write_text(json.dumps({
                            "version": version, "case_id": case_id, "profile": "broadband_smoke", "last_stage": "test",
                            "executable": str(exe),
                            "runs": [{"frequency_hz": 4000.0, "status": "passed", "environment_file": f"f000_4000Hz/{case_id}_f000_4000Hz.env", "shade_file": f"f000_4000Hz/{case_id}_f000_4000Hz.shd"},
                                     {"frequency_hz": 5000.0, "status": "passed", "environment_file": f"f001_5000Hz/{case_id}_f001_5000Hz.env", "shade_file": f"f001_5000Hz/{case_id}_f001_5000Hz.shd"}]
                        }))

            with self.assertRaisesRegex(ValueError, "rendered env bytes differ"):
                validate(results_root, origin_exe, f2cpp_exe, rayreuse_exe)


if __name__ == "__main__":
    unittest.main()
