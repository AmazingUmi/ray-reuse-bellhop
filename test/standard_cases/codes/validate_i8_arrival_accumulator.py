#!/usr/bin/env python3
"""Compare the real ArrMod::AddArr probe with the F2CPP accumulator probe."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
from typing import NamedTuple


PROBE_HEADER = "I8_ARRIVAL_ACCUMULATOR_PROBE_V1"
REQUIRED_SCENARIOS = frozenset(range(1, 16))
SCENARIOS = {
    1: ("append", "append one arrival", 1),
    2: ("last_duplicate_merge", "merge only the last matching record", 1),
    3: ("non_last_similar", "append when only a non-last record is similar", 3),
    4: ("delay_below", "merge below the delay threshold", 1),
    5: ("delay_equal", "append at the exact delay threshold", 2),
    6: ("delay_above", "append above the delay threshold", 2),
    7: ("phase_below", "merge below the phase threshold", 1),
    8: ("phase_equal", "append at the exact phase threshold", 2),
    9: ("phase_above", "append above the phase threshold", 2),
    10: ("axial_cusp", "retain record when weighted amplitude cancels", 1),
    11: ("first_minimum_tie", "replace the first equal weakest record", 2),
    12: ("stronger_replacement", "replace weakest record when full", 2),
    13: ("equal_discard", "discard equal weakest candidate when full", 2),
    14: ("weaker_discard", "discard weaker candidate when full", 2),
    15: ("zero_arrivals", "retain an empty cell", 0),
}


class StoredArrival(NamedTuple):
    amplitude_bits: int
    phase_bits: int
    delay_real_bits: int
    delay_imag_bits: int
    source_angle_bits: int
    receiver_angle_bits: int
    top_bounces: int
    bottom_bounces: int


def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def parse_probe(text: str, source: str) -> dict[int, list[StoredArrival]]:
    lines = text.splitlines()
    if not lines or lines[0] != PROBE_HEADER:
        raise ValueError(f"{source}: missing or invalid probe header")
    scenarios: dict[int, list[StoredArrival]] = {}
    expected_count: dict[int, int] = {}
    for line_number, line in enumerate(lines[1:], start=2):
        fields = line.split()
        if not fields:
            raise ValueError(f"{source}:{line_number}: blank line is invalid")
        if fields[0] == "ERROR":
            if len(fields) < 3:
                raise ValueError(f"{source}:{line_number}: malformed error")
            raise ValueError(
                f"{source}: scenario {fields[1]} production API error: "
                f"{' '.join(fields[2:])}"
            )
        if fields[0] == "SCENARIO":
            if len(fields) != 3:
                raise ValueError(f"{source}:{line_number}: malformed scenario")
            scenario, count = (int(item) for item in fields[1:])
            if scenario in scenarios:
                raise ValueError(f"{source}: duplicate scenario {scenario}")
            if scenario not in REQUIRED_SCENARIOS or count < 0:
                raise ValueError(f"{source}: impossible scenario/count")
            scenarios[scenario] = []
            expected_count[scenario] = count
        elif fields[0] == "ARRIVAL":
            if len(fields) != 11:
                raise ValueError(f"{source}:{line_number}: malformed arrival")
            scenario = int(fields[1])
            ordinal = int(fields[2])
            if scenario not in scenarios:
                raise ValueError(f"{source}: arrival before scenario")
            if ordinal != len(scenarios[scenario]) + 1:
                raise ValueError(f"{source}: non-sequential arrival ordinal")
            values = tuple(int(item) for item in fields[3:])
            if len(values) != 8:
                raise AssertionError("parser width checked above")
            scenarios[scenario].append(StoredArrival(*values))
        else:
            raise ValueError(f"{source}:{line_number}: unknown record {fields[0]}")
    if set(scenarios) != REQUIRED_SCENARIOS:
        missing = sorted(REQUIRED_SCENARIOS - set(scenarios))
        raise ValueError(f"{source}: missing scenarios {missing}")
    for scenario, arrivals in scenarios.items():
        if len(arrivals) != expected_count[scenario]:
            raise ValueError(f"{source}: scenario {scenario} count mismatch")
    return scenarios


def _unsigned32(value: int) -> int:
    if not -(1 << 31) <= value < (1 << 31):
        raise ValueError(f"int32 bit pattern out of range: {value}")
    return value & 0xFFFFFFFF


def _ulp_distance(left: int, right: int) -> int:
    # All probe values must be finite IEEE float32 values.  Re-map signed bits
    # monotonically so adjacent floats differ by one.
    left_u, right_u = _unsigned32(left), _unsigned32(right)
    if left_u & 0x80000000:
        left_u = 0x80000000 - left_u
    if right_u & 0x80000000:
        right_u = 0x80000000 - right_u
    return abs(left_u - right_u)


def _check_finite_float32(bits: int, label: str) -> None:
    unsigned = _unsigned32(bits)
    if (unsigned & 0x7F800000) == 0x7F800000:
        raise ValueError(f"{label}: non-finite float32 field")


def _float32(bits: int) -> float:
    return struct.unpack("<f", struct.pack("<I", _unsigned32(bits)))[0]


def verify_scenario_effects(scenarios: dict[int, list[StoredArrival]]) -> list[dict[str, object]]:
    """Check fixed, observable ArrMod effects without reimplementing AddArr."""
    for scenario, (_, _, expected_count) in SCENARIOS.items():
        if len(scenarios[scenario]) != expected_count:
            raise ValueError(
                f"scenario {scenario}: expected {expected_count} stored arrivals"
            )

    merged = scenarios[2][0]
    if _float32(merged.amplitude_bits) != 3.0 or merged.top_bounces != 1 or merged.bottom_bounces != 2:
        raise ValueError("scenario 2: weighted merge/preserved metadata effect absent")
    if scenarios[3][-1].top_bounces != 5:
        raise ValueError("scenario 3: last-only grouping effect absent")
    if [len(scenarios[item]) for item in (4, 5, 6)] != [1, 2, 2]:
        raise ValueError("delay threshold strict-boundary effects absent")
    if [len(scenarios[item]) for item in (7, 8, 9)] != [1, 2, 2]:
        raise ValueError("phase threshold strict-boundary effects absent")
    cusp = scenarios[10][0]
    if _float32(cusp.amplitude_bits) != 1.0 or cusp.top_bounces != 1:
        raise ValueError("scenario 10: axial-cusp unchanged effect absent")
    if scenarios[11][0].top_bounces != 5 or scenarios[11][1].top_bounces != 3:
        raise ValueError("scenario 11: first-minimum replacement effect absent")
    if scenarios[12][0].top_bounces != 5:
        raise ValueError("scenario 12: stronger replacement effect absent")
    for scenario in (13, 14):
        if [arrival.top_bounces for arrival in scenarios[scenario]] != [1, 3]:
            raise ValueError(f"scenario {scenario}: capacity discard effect absent")

    return [
        {
            "id": scenario,
            "name": name,
            "expected_branch": branch,
            "stored_arrival_count": len(scenarios[scenario]),
        }
        for scenario, (name, branch, _) in sorted(SCENARIOS.items())
    ]


def compare(origin: dict[int, list[StoredArrival]], f2cpp: dict[int, list[StoredArrival]]) -> dict[str, int]:
    compared_arrivals = 0
    exact_float_fields = 0
    for scenario in sorted(REQUIRED_SCENARIOS):
        expected, actual = origin[scenario], f2cpp[scenario]
        if len(expected) != len(actual):
            raise ValueError(
                f"scenario {scenario}: count differs "
                f"({len(expected)} != {len(actual)})"
            )
        for ordinal, (left, right) in enumerate(zip(expected, actual), start=1):
            for name, lbits, rbits in zip(
                ("amplitude", "phase", "delay_real", "delay_imag", "source_angle", "receiver_angle"),
                left[:6], right[:6],
            ):
                _check_finite_float32(lbits, f"origin scenario {scenario} {name}")
                _check_finite_float32(rbits, f"f2cpp scenario {scenario} {name}")
                distance = _ulp_distance(lbits, rbits)
                if distance > 1:
                    raise ValueError(
                        f"scenario {scenario} arrival {ordinal} {name}: "
                        f"float32 distance {distance} exceeds 1 ULP"
                    )
                if distance == 0:
                    exact_float_fields += 1
            if left[6:] != right[6:]:
                raise ValueError(
                    f"scenario {scenario} arrival {ordinal}: bounce counts differ"
                )
            compared_arrivals += 1
    return {
        "scenario_count": len(REQUIRED_SCENARIOS),
        "arrival_count": compared_arrivals,
        "exact_float_field_count": exact_float_fields,
    }


def run_probe(path: Path) -> str:
    completed = subprocess.run(
        [str(path)], check=False, text=True, capture_output=True
    )
    if completed.returncode != 0:
        raise ValueError(
            f"{path}: probe exited {completed.returncode}: {completed.stderr.strip()}"
        )
    return completed.stdout


def validate(origin_probe: Path, f2cpp_probe: Path) -> dict[str, object]:
    origin_text, f2cpp_text = run_probe(origin_probe), run_probe(f2cpp_probe)
    origin = parse_probe(origin_text, str(origin_probe))
    f2cpp = parse_probe(f2cpp_text, str(f2cpp_probe))
    metrics = compare(
        origin, f2cpp,
    )
    scenario_results = verify_scenario_effects(origin)
    # The two complete streams are compared above; independently require the
    # same observable branch outcomes from the production API probe.
    verify_scenario_effects(f2cpp)
    project_root = Path(__file__).resolve().parents[3]
    sources = {
        "origin_probe": origin_probe,
        "origin_probe_source": project_root / "Bellhop_origin/Bellhop/ArrivalAccumulatorProbe.f90",
        "origin_arrmod": project_root / "Bellhop_origin/Bellhop/ArrMod.f90",
        "origin_arrmod_object": project_root / "Bellhop_origin/build/obj/bellhop_ArrMod.o",
        "origin_executable": project_root / "Bellhop_origin/bin/bellhop",
        "f2cpp_probe": f2cpp_probe,
        "f2cpp_probe_source": project_root / "Bellhop_F2CPP/tests/tools/arrival_accumulator_probe.cpp",
        "f2cpp_accumulator": project_root / "Bellhop_F2CPP/src/field/arrival_workspace.cpp",
    }
    return {
        "validator": "i8_arrival_accumulator",
        "status": "passed",
        "metrics": metrics,
        "scenarios": scenario_results,
        "provenance_sha256": {
            name: _sha256(path) for name, path in sources.items()
        },
    }


def check_expected_provenance(
    report: dict[str, object], expected_path: Path
) -> None:
    expected = json.loads(expected_path.read_text(encoding="utf-8"))
    actual = report["provenance_sha256"]
    if not isinstance(expected, dict) or expected != actual:
        raise ValueError("provenance mismatch")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--origin-probe", type=Path, required=True)
    parser.add_argument("--f2cpp-probe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--expect-provenance", type=Path)
    args = parser.parse_args(argv)
    report = validate(args.origin_probe, args.f2cpp_probe)
    if args.expect_provenance is not None:
        check_expected_provenance(report, args.expect_provenance)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(report, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
