#!/usr/bin/env python3
"""Validate geometric-hat, geometric-Gaussian, and simple-Gaussian fields."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import sys

import numpy as np

from case_model import discover_cases
from compare_fields import STANDARD_CASES_ROOT, compare_files


PLOTREAD_ROOT = STANDARD_CASES_ROOT.parent / "PlotRead"
PROJECT_ROOT = STANDARD_CASES_ROOT.parents[1]
sys.path.insert(0, str(PLOTREAD_ROOT))

from bellhop_io_py.shd import PressureField, ShdReader


CASES = {
    "G": (
        "geometric_hat_cartesian_safe_control",
        "CG",
        "Geometric hat beams in Cartesian coordinates",
        "Geometric hat beams",
    ),
    "B": (
        "geometric_gaussian_cartesian",
        "CB",
        "Geometric gaussian beams in Cartesian coordinates",
        "Geometric Gaussian beams",
    ),
    "S": (
        "simple_gaussian_cartesian",
        "CS",
        "Simple gaussian beams",
        "Simple Gaussian beams",
    ),
}
PROFILE = "single"
FREQUENCY_HZ = 1000.0
LAUNCH_COUNT = 300
EXPECTED_DIMENSIONS = (1, 1, 1, 1, 1, 7, 21)
PRESSURE_MASK_FLOOR = 1.0e-7
MINIMUM_FAMILY_EFFECT = 1.0e-3
RUN_TYPE_LINE = re.compile(
    r"^\s*['\"](C[GBS])['\"]\s*(?:!.*)?$", re.MULTILINE
)
LAUNCH_COUNT_LINE = re.compile(r"^(?:@NALPHA@|\d+)$")
ANGLE_LINE = re.compile(r"^-60(?:\.0)?\s+60(?:\.0)?\s*/$")
INTEGRATOR_LINE = re.compile(
    r"^1(?:\.0)?\s+101(?:\.0)?\s+0\.26$"
)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def aggregate_sha256(paths: list[Path]) -> str:
    digest = hashlib.sha256()
    for path in sorted(paths, key=lambda item: str(item)):
        digest.update(path.read_bytes())
    return digest.hexdigest()


def meaningful_lines(contents: str) -> list[str]:
    return [
        line.split("!", 1)[0].strip()
        for line in contents.splitlines()
        if line.split("!", 1)[0].strip()
    ]


def parse_family_and_require_eof(
    contents: str, source: Path | str
) -> str:
    families = RUN_TYPE_LINE.findall(contents)
    if len(families) != 1:
        raise ValueError(
            f"{source}: expected exactly one CG/CB/CS beam-family run type"
        )
    lines = meaningful_lines(contents)
    run_indices = [
        index
        for index, line in enumerate(lines)
        if RUN_TYPE_LINE.fullmatch(line)
    ]
    if len(run_indices) != 1:
        raise ValueError(f"{source}: Gaussian-family run record is ambiguous")
    tail = lines[run_indices[0] + 1 :]
    if (
        len(tail) != 3
        or LAUNCH_COUNT_LINE.fullmatch(tail[0]) is None
        or ANGLE_LINE.fullmatch(tail[1]) is None
        or INTEGRATOR_LINE.fullmatch(tail[2]) is None
    ):
        raise ValueError(
            f"{source}: non-Cerveny ENV must end at the integrator record"
        )
    return families[0]


def normalize_family(contents: str, source: Path | str) -> str:
    family = parse_family_and_require_eof(contents, source)
    return RUN_TYPE_LINE.sub(
        lambda match: match.group(0).replace(
            f"'{family}'", "'<FAMILY>'"
        ).replace(f'"{family}"', '"<FAMILY>"'),
        contents,
    )


def effect_metrics(
    reference_pressure: np.ndarray, candidate_pressure: np.ndarray
) -> dict[str, float | int]:
    if reference_pressure.shape != candidate_pressure.shape:
        raise ValueError("field shapes differ")
    if not np.all(np.isfinite(reference_pressure)) or not np.all(
        np.isfinite(candidate_pressure)
    ):
        raise ValueError("field contains non-finite values")
    reference_magnitude = np.abs(reference_pressure)
    candidate_magnitude = np.abs(candidate_pressure)
    common = (
        (reference_magnitude > PRESSURE_MASK_FLOOR)
        & (candidate_magnitude > PRESSURE_MASK_FLOOR)
    )
    if not np.any(common):
        raise ValueError("fields have no common values above TL floor")
    tl_difference = np.abs(
        -20.0 * np.log10(reference_magnitude[common])
        + 20.0 * np.log10(candidate_magnitude[common])
    )
    return {
        "max_pressure_absolute_difference": float(
            np.max(np.abs(reference_pressure - candidate_pressure))
        ),
        "minimum_tl_difference_db": float(np.min(tl_difference)),
        "median_tl_difference_db": float(np.median(tl_difference)),
        "maximum_tl_difference_db": float(np.max(tl_difference)),
        "tl_mask_count": int(np.count_nonzero(common)),
    }


def validate_origin_source_contract() -> dict[str, object]:
    sources = {
        "read_environment": (
            PROJECT_ROOT / "Bellhop_origin" / "Bellhop" /
            "ReadEnvironmentBell.f90"
        ),
        "bellhop_core": (
            PROJECT_ROOT / "Bellhop_origin" / "Bellhop" / "bellhop.f90"
        ),
        "influence": (
            PROJECT_ROOT / "Bellhop_origin" / "Bellhop" / "influence.f90"
        ),
    }
    text = {
        name: path.read_text(encoding="utf-8")
        for name, path in sources.items()
    }
    parser = text["read_environment"]
    run_start = parser.find("SUBROUTINE ReadRunType")
    run_end = parser.find("END SUBROUTINE ReadRunType", run_start)
    if run_start < 0 or run_end < 0:
        raise ValueError("Origin ReadRunType routine is absent")
    run_type = parser[run_start:run_end]
    for anchor in (
        "CASE ( 'S' )",
        "Simple gaussian beams",
        "CASE ( 'B' )",
        "Geometric gaussian beams in Cartesian coordinates",
        "RunType( 2 : 2 ) = 'G'",
        "Geometric hat beams in Cartesian coordinates",
    ):
        if anchor not in run_type:
            raise ValueError(
                f"Origin Gaussian-family parser anchor absent: {anchor}"
            )

    config_anchor = "Beam%Type( 1 : 1 ) = Beam%RunType( 2 : 2 )"
    config_start = parser.find(config_anchor)
    select_start = parser.find(
        "SELECT CASE ( Beam%Type( 1 : 1 ) )", config_start
    )
    config_end = parser.find("END IF", select_start)
    if config_start < 0 or select_start < 0 or config_end < 0:
        raise ValueError("Origin beam-option parser is absent")
    beam_config = parser[select_start:config_end]
    non_cerveny_start = beam_config.find(
        "CASE ( 'G', 'g' , '^', 'B', 'b', 'S' )"
    )
    cerveny_start = beam_config.find("CASE ( 'R', 'C' )")
    if non_cerveny_start < 0 or cerveny_start <= non_cerveny_start:
        raise ValueError("Origin non-Cerveny/Cerveny option split is absent")
    non_cerveny = beam_config[non_cerveny_start:cerveny_start]
    if "READ(" in non_cerveny or "READ (" in non_cerveny:
        raise ValueError("Origin non-Cerveny branch unexpectedly reads a tail")

    core = text["bellhop_core"]
    dispatch_start = core.find("SELECT CASE ( Beam%Type( 1 : 1 ) )")
    dispatch_end = core.find("END SELECT", dispatch_start)
    if dispatch_start < 0 or dispatch_end < 0:
        raise ValueError("Origin influence dispatch is absent")
    dispatch = core[dispatch_start:dispatch_end]
    for anchor in (
        "CASE ( 'S' )",
        "CALL InfluenceSGB",
        "CASE ( 'B' )",
        "CALL InfluenceGeoGaussianCart",
        "CASE DEFAULT",
        "CALL InfluenceGeoHatCart",
    ):
        if anchor not in dispatch:
            raise ValueError(
                f"Origin Gaussian-family dispatch anchor absent: {anchor}"
            )
    for anchor in (
        "TAG        = 'Geometric hat beams'",
        "TAG        = 'Geometric Gaussian beams'",
        "TAG        = 'Simple Gaussian beams'",
        "Geo Gaussian beams in ray-centered coords. not implemented",
    ):
        if anchor not in core:
            raise ValueError(
                f"Origin Gaussian-family epsilon anchor absent: {anchor}"
            )

    influence = text["influence"]
    for routine in (
        "InfluenceGeoHatCart",
        "InfluenceGeoGaussianCart",
        "InfluenceSGB",
    ):
        start = influence.find(f"SUBROUTINE {routine}")
        end = influence.find(f"END SUBROUTINE {routine}", start)
        if start < 0 or end < 0:
            raise ValueError(f"Origin {routine} routine is absent")
    for anchor in (
        "W        = ( RadiusMax - n ) / RadiusMax",
        "W        = SQRT( sigma / sigma1 ) * EXP",
        "A      = -4.0 * LOG( BETA ) / Dalpha**2",
    ):
        if anchor not in influence:
            raise ValueError(
                f"Origin Gaussian-family formula anchor absent: {anchor}"
            )
    return {
        "read_run_type_has_g_b_s": True,
        "non_cerveny_tail_has_no_extra_reads": True,
        "bellhop_dispatches_g_b_s_influences": True,
        "gaussian_family_formulas_present": True,
        "ray_centered_geometric_gaussian_is_explicitly_unavailable": True,
        "source_sha256": {
            name: sha256(path) for name, path in sources.items()
        },
    }


def require_current(artifact: Path, executable: Path, manifest: Path) -> None:
    if artifact.stat().st_mtime_ns < executable.stat().st_mtime_ns:
        raise ValueError(f"{manifest}: {artifact.name} predates executable")


def require_header_equal(
    reference: PressureField, candidate: PressureField, label: str
) -> None:
    left = reference.header
    right = candidate.header
    scalar_fields = (
        "title",
        "plot_type",
        "nominal_frequency_hz",
        "attenuation",
        "record_bytes",
        "byte_order",
    )
    if any(
        getattr(left, name) != getattr(right, name)
        for name in scalar_fields
    ):
        raise ValueError(f"{label}: SHD scalar header mismatch")
    axes = (
        "frequencies_hz",
        "bearings_deg",
        "source_x_m",
        "source_y_m",
        "source_depths_m",
        "receiver_depths_m",
        "receiver_ranges_m",
    )
    if any(
        not np.array_equal(getattr(left, name), getattr(right, name))
        for name in axes
    ):
        raise ValueError(f"{label}: SHD coordinate header mismatch")


def load_run(
    results_root: Path,
    version: str,
    key: str,
    executable: Path,
    definitions: dict[str, object],
) -> dict[str, object]:
    case_id, family, family_marker, epsilon_marker = CASES[key]
    definition = definitions[case_id]
    profile_root = results_root / version / case_id / PROFILE
    manifest_path = profile_root / "run_manifest.json"
    if not manifest_path.is_file():
        raise ValueError(f"{manifest_path}: run manifest is absent")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if (
        manifest.get("version") != version
        or manifest.get("case_id") != case_id
        or manifest.get("profile") != PROFILE
        or manifest.get("last_stage") != "test"
        or manifest.get("output_kind") != "shd"
    ):
        raise ValueError(f"{manifest_path}: run identity mismatch")
    if Path(manifest["executable"]).resolve() != executable:
        raise ValueError(f"{manifest_path}: executable identity mismatch")
    if tuple(manifest.get("source_references", ())) != (
        definition.source_references
    ):
        raise ValueError(f"{manifest_path}: source references mismatch")
    if tuple(float(value) for value in manifest.get("frequencies_hz", ())) != (
        FREQUENCY_HZ,
    ):
        raise ValueError(f"{manifest_path}: frequency grid mismatch")
    if manifest.get("shared_launch_angle_count") != LAUNCH_COUNT:
        raise ValueError(f"{manifest_path}: launch count mismatch")
    runs = manifest.get("runs")
    if not isinstance(runs, list) or len(runs) != 1:
        raise ValueError(f"{manifest_path}: expected one run")
    run = runs[0]
    if (
        run.get("frequency_index") != 0
        or float(run.get("frequency_hz", math.nan)) != FREQUENCY_HZ
        or run.get("status") != "passed"
    ):
        raise ValueError(f"{manifest_path}: run order/status mismatch")
    paths = {
        "environment": profile_root / run["environment_file"],
        "print": profile_root / run["print_file"],
        "shade": profile_root / run["shade_file"],
    }
    require_current(manifest_path, executable, manifest_path)
    for artifact in paths.values():
        if not artifact.is_file() or artifact.stat().st_size == 0:
            raise ValueError(f"{manifest_path}: missing {artifact}")
        require_current(artifact, executable, manifest_path)

    expected_environment = definition.render_origin_environment(
        FREQUENCY_HZ, LAUNCH_COUNT
    )
    environment_contents = paths["environment"].read_text(encoding="utf-8")
    if environment_contents != expected_environment:
        raise ValueError(f"{manifest_path}: staged ENV differs from fixture")
    if parse_family_and_require_eof(
        environment_contents, paths["environment"]
    ) != family:
        raise ValueError(f"{manifest_path}: ENV family mismatch")

    print_contents = paths["print"].read_text(errors="replace")
    print_lines = [line.strip() for line in print_contents.splitlines()]
    if print_lines.count(family_marker) != 1:
        raise ValueError(f"{manifest_path}: PRT family marker mismatch")
    if print_lines.count(epsilon_marker) != 1:
        raise ValueError(f"{manifest_path}: PRT epsilon tag mismatch")
    for other_key, (_, _, other_family, _) in CASES.items():
        if other_key != key and other_family in print_contents:
            raise ValueError(f"{manifest_path}: PRT has another family marker")
    for forbidden in (
        "Cartesian beams",
        "Ray centered beams",
        "Type of beam =",
        "Component                 =",
        "Number of images",
        "FATAL ERROR",
    ):
        if forbidden in print_contents:
            raise ValueError(f"{manifest_path}: forbidden PRT marker {forbidden}")

    field = ShdReader(paths["shade"]).read()
    if field.header.dimensions != EXPECTED_DIMENSIONS:
        raise ValueError(f"{manifest_path}: unexpected SHD dimensions")
    magnitude = np.abs(field.pressure)
    if not np.all(np.isfinite(magnitude)) or np.count_nonzero(magnitude) < 2:
        raise ValueError(f"{manifest_path}: field is empty/sparse/non-finite")
    return {
        "manifest": manifest_path,
        **paths,
        "environment_contents": environment_contents,
        "field": field,
        "nonzero_count": int(np.count_nonzero(magnitude)),
        "maximum_magnitude": float(np.max(magnitude)),
    }


def generation_commands() -> list[str]:
    executables = {
        "origin": "Bellhop_origin/bin/bellhop",
        "f2cpp": "Bellhop_F2CPP/build/release/bellhop_f2cpp",
    }
    return [
        "python3 test/standard_cases/codes/standard_cases.py test "
        f"--version {version} --case {case_id} --profile single "
        f"--executable {executables[version]} "
        "--results-root <results-root>"
        for version in ("origin", "f2cpp")
        for case_id, _, _, _ in CASES.values()
    ]


def validate(
    results_root: Path,
    origin_executable: Path,
    f2cpp_executable: Path,
) -> dict[str, object]:
    source_contract = validate_origin_source_contract()
    definitions = discover_cases(STANDARD_CASES_ROOT / "cases")
    executables = {
        "origin": origin_executable.resolve(),
        "f2cpp": f2cpp_executable.resolve(),
    }
    if executables["origin"] == executables["f2cpp"]:
        raise ValueError("Origin and F2CPP executable paths must differ")
    if not all(path.is_file() for path in executables.values()):
        raise ValueError("an expected executable does not exist")
    executable_hashes = {
        version: sha256(path) for version, path in executables.items()
    }
    if executable_hashes["origin"] == executable_hashes["f2cpp"]:
        raise ValueError("Origin and F2CPP executable hashes must differ")

    loaded = {
        version: {
            key: load_run(results_root, version, key, executable, definitions)
            for key in CASES
        }
        for version, executable in executables.items()
    }
    environment_hashes: dict[str, dict[str, str]] = {}
    normalized_hashes: dict[str, str] = {}
    for version in executables:
        environment_hashes[version] = {
            key: sha256(loaded[version][key]["environment"])
            for key in CASES
        }
        if len(set(environment_hashes[version].values())) != len(CASES):
            raise ValueError(f"{version}: G/B/S ENV files are not distinct")
        normalized = {
            normalize_family(
                str(loaded[version][key]["environment_contents"]),
                loaded[version][key]["environment"],
            )
            for key in CASES
        }
        if len(normalized) != 1:
            raise ValueError(f"{version}: ENV files differ beyond G/B/S")
        normalized_hashes[version] = hashlib.sha256(
            normalized.pop().encode("utf-8")
        ).hexdigest()
        for reference, candidate in (("G", "B"), ("G", "S"), ("B", "S")):
            require_header_equal(
                loaded[version][reference]["field"],
                loaded[version][candidate]["field"],
                f"{version} {reference}/{candidate}",
            )
    if environment_hashes["origin"] != environment_hashes["f2cpp"]:
        raise ValueError("Origin and F2CPP rendered ENV inputs differ")
    if normalized_hashes["origin"] != normalized_hashes["f2cpp"]:
        raise ValueError("Origin and F2CPP normalized ENV identities differ")

    tolerance_path = STANDARD_CASES_ROOT / "codes" / "tolerances.toml"
    comparisons: dict[str, dict[str, object]] = {}
    for key, (case_id, _, _, _) in CASES.items():
        passed, metrics = compare_files(
            loaded["origin"][key]["shade"],
            loaded["f2cpp"][key]["shade"],
            0,
            0,
            tolerance_path,
        )
        if not passed:
            raise ValueError(f"{case_id} Origin/F2CPP mismatch: {metrics}")
        comparisons[key] = {"passed": True, **metrics}

    effects: dict[str, dict[str, object]] = {}
    for version in executables:
        for reference, candidate in (("G", "B"), ("G", "S"), ("B", "S")):
            metrics = effect_metrics(
                loaded[version][reference]["field"].pressure,
                loaded[version][candidate]["field"].pressure,
            )
            if (
                metrics["max_pressure_absolute_difference"]
                <= MINIMUM_FAMILY_EFFECT
            ):
                raise ValueError(
                    f"{version}: {reference}/{candidate} effect is empty: "
                    f"{metrics}"
                )
            effects[f"{version}/{reference}-{candidate}"] = {
                "passed": True,
                **metrics,
            }

    executable_records = {
        version: {
            "path": str(path),
            "mtime_ns": path.stat().st_mtime_ns,
            "sha256": executable_hashes[version],
        }
        for version, path in executables.items()
    }
    return {
        "schema": "bellhop.f2cpp.i7_gaussian_beams_validation",
        "schema_version": 1,
        "status": "passed",
        "matrix": {
            "frequency_hz": FREQUENCY_HZ,
            "launch_count": LAUNCH_COUNT,
            "minimum_family_effect": MINIMUM_FAMILY_EFFECT,
            "cases": {
                key: {
                    "case_id": case_id,
                    "run_type": family,
                    "prt_family_marker": marker,
                }
                for key, (case_id, family, marker, _) in CASES.items()
            },
        },
        "executables": executable_records,
        "origin_source_contract": source_contract,
        "origin_f2cpp_field_comparisons": comparisons,
        "independent_family_effect_guards": effects,
        "field_summaries": {
            f"{version}/{key}": {
                "nonzero_count": loaded[version][key]["nonzero_count"],
                "maximum_magnitude": loaded[version][key]["maximum_magnitude"],
            }
            for version in executables
            for key in CASES
        },
        "provenance_guards": {
            "distinct_executable_paths": True,
            "distinct_executable_hashes": True,
            "manifest_executable_identity_bound": True,
            "manifest_source_references_bound": True,
            "results_not_older_than_executables": True,
            "staged_env_matches_canonical_fixture": True,
            "origin_f2cpp_rendered_inputs_equal": True,
            "g_b_s_inputs_otherwise_identical": True,
            "integrator_record_is_environment_eof": True,
            "env_and_prt_family_identity_bound": True,
            "origin_f2cpp_comparison_count": len(comparisons),
            "family_effect_guard_count": len(effects),
        },
        "sha256": {
            "rendered_environment": environment_hashes["origin"],
            "normalized_environment": normalized_hashes["origin"],
            "origin_field_aggregate": aggregate_sha256(
                [loaded["origin"][key]["shade"] for key in CASES]
            ),
            "f2cpp_field_aggregate": aggregate_sha256(
                [loaded["f2cpp"][key]["shade"] for key in CASES]
            ),
            "origin_prt": {
                key: sha256(loaded["origin"][key]["print"])
                for key in CASES
            },
            "f2cpp_prt": {
                key: sha256(loaded["f2cpp"][key]["print"])
                for key in CASES
            },
        },
        "generation": {
            "case_commands": generation_commands(),
            "validator_command": (
                "python3 test/standard_cases/codes/"
                "validate_i7_gaussian_beams.py "
                "--results-root <results-root> "
                "--origin-executable Bellhop_origin/bin/bellhop "
                "--f2cpp-executable "
                "Bellhop_F2CPP/build/release/bellhop_f2cpp "
                "--output Bellhop_F2CPP/doc/validation/"
                "i7_gaussian_beams_report.json"
            ),
        },
    }


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--results-root", type=Path, required=True)
    parser.add_argument("--origin-executable", type=Path, required=True)
    parser.add_argument("--f2cpp-executable", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    return parser


def main() -> int:
    args = build_parser().parse_args()
    report = validate(
        args.results_root.resolve(),
        args.origin_executable.resolve(),
        args.f2cpp_executable.resolve(),
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(f"I7 Gaussian-beam validation: PASSED ({args.output})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
