#!/usr/bin/env python3
"""Validate Cartesian and ray-centered geometric-hat standard cases."""

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
        "geometric_hat_cartesian",
        "CG",
        "Geometric hat beams in Cartesian coordinates",
    ),
    "g": (
        "geometric_hat_ray_centered",
        "Cg",
        "Geometric hat beams in ray-centered coordinates",
    ),
}
PROFILE = "single"
FREQUENCY_HZ = 100.0
LAUNCH_COUNT = 497
EXPECTED_DIMENSIONS = (1, 1, 1, 1, 1, 2, 3)
PRESSURE_MASK_FLOOR = 1.0e-7
MINIMUM_COORDINATE_EFFECT = 1.0e-3
RUN_TYPE_LINE = re.compile(
    r"^\s*['\"](C[Gg])['\"]\s*(?:!.*)?$", re.MULTILINE
)
LAUNCH_COUNT_LINE = re.compile(r"^(?:@NALPHA@|\d+)$")
ANGLE_LINE = re.compile(r"^-40(?:\.0)?\s+40(?:\.0)?\s*/$")
INTEGRATOR_LINE = re.compile(
    r"^500(?:\.0)?\s+121(?:\.0)?\s+2\.1$"
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
            f"{source}: expected exactly one CG/Cg geometric-hat run type"
        )
    lines = meaningful_lines(contents)
    run_indices = [
        index
        for index, line in enumerate(lines)
        if RUN_TYPE_LINE.fullmatch(line)
    ]
    if len(run_indices) != 1:
        raise ValueError(f"{source}: geometric-hat run record is ambiguous")
    tail = lines[run_indices[0] + 1 :]
    if (
        len(tail) != 3
        or LAUNCH_COUNT_LINE.fullmatch(tail[0]) is None
        or ANGLE_LINE.fullmatch(tail[1]) is None
        or INTEGRATOR_LINE.fullmatch(tail[2]) is None
    ):
        raise ValueError(
            f"{source}: geometric-hat ENV must end at the integrator record"
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
    cartesian_pressure: np.ndarray, ray_centered_pressure: np.ndarray
) -> dict[str, float | int]:
    if cartesian_pressure.shape != ray_centered_pressure.shape:
        raise ValueError("field shapes differ")
    if not np.all(np.isfinite(cartesian_pressure)) or not np.all(
        np.isfinite(ray_centered_pressure)
    ):
        raise ValueError("field contains non-finite values")
    cartesian_magnitude = np.abs(cartesian_pressure)
    ray_centered_magnitude = np.abs(ray_centered_pressure)
    cartesian_support = cartesian_magnitude > PRESSURE_MASK_FLOOR
    ray_centered_support = ray_centered_magnitude > PRESSURE_MASK_FLOOR
    common = cartesian_support & ray_centered_support
    if not np.any(common):
        raise ValueError("fields have no common values above TL floor")
    tl_difference = np.abs(
        -20.0 * np.log10(cartesian_magnitude[common])
        + 20.0 * np.log10(ray_centered_magnitude[common])
    )
    return {
        "max_pressure_absolute_difference": float(
            np.max(np.abs(cartesian_pressure - ray_centered_pressure))
        ),
        "minimum_tl_difference_db": float(np.min(tl_difference)),
        "median_tl_difference_db": float(np.median(tl_difference)),
        "maximum_tl_difference_db": float(np.max(tl_difference)),
        "common_support_count": int(np.count_nonzero(common)),
        "cartesian_only_support_count": int(
            np.count_nonzero(cartesian_support & ~ray_centered_support)
        ),
        "ray_centered_only_support_count": int(
            np.count_nonzero(ray_centered_support & ~cartesian_support)
        ),
        "support_mismatch_count": int(
            np.count_nonzero(cartesian_support ^ ray_centered_support)
        ),
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
        "CASE ( 'g' )",
        "Geometric hat beams in ray-centered coordinates",
        "Geometric hat beams in Cartesian coordinates",
        "RunType( 2 : 2 ) = 'G'",
    ):
        if anchor not in run_type:
            raise ValueError(
                f"Origin geometric-hat parser anchor absent: {anchor}"
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
    geometric_start = beam_config.find(
        "CASE ( 'G', 'g' , '^', 'B', 'b', 'S' )"
    )
    cerveny_start = beam_config.find("CASE ( 'R', 'C' )")
    if geometric_start < 0 or cerveny_start <= geometric_start:
        raise ValueError("Origin geometric-hat/Cerveny option split is absent")
    geometric_branch = beam_config[geometric_start:cerveny_start]
    cerveny_branch = beam_config[cerveny_start:]
    if "READ(" in geometric_branch or "READ (" in geometric_branch:
        raise ValueError("Origin geometric-hat branch unexpectedly reads a tail")
    for anchor in (
        "READ(  ENVFile, * ) Beam%Type( 2 : 3 )",
        "READ(  ENVFile, * ) Beam%Nimage",
    ):
        if anchor not in cerveny_branch:
            raise ValueError(
                f"Origin Cerveny tail contract absent: {anchor}"
            )

    core = text["bellhop_core"]
    dispatch_start = core.find("SELECT CASE ( Beam%Type( 1 : 1 ) )")
    dispatch_end = core.find("END SELECT", dispatch_start)
    if dispatch_start < 0 or dispatch_end < 0:
        raise ValueError("Origin influence dispatch is absent")
    dispatch = core[dispatch_start:dispatch_end]
    for anchor in (
        "CASE ( 'g' )",
        "CALL InfluenceGeoHatRayCen",
        "CASE DEFAULT",
        "CALL InfluenceGeoHatCart",
    ):
        if anchor not in dispatch:
            raise ValueError(
                f"Origin geometric-hat dispatch anchor absent: {anchor}"
            )

    influence = text["influence"]
    for routine in (
        "InfluenceGeoHatRayCen",
        "InfluenceGeoHatCart",
    ):
        start = influence.find(f"SUBROUTINE {routine}")
        end = influence.find(f"END SUBROUTINE {routine}", start)
        if start < 0 or end < 0:
            raise ValueError(f"Origin {routine} routine is absent")
    for anchor in (
        "W        = ( L - n ) / L",
        "W        = ( RadiusMax - n ) / RadiusMax",
        "CALL ApplyContribution( U( iz, ir ) )",
    ):
        if anchor not in influence:
            raise ValueError(
                f"Origin geometric-hat formula anchor absent: {anchor}"
            )
    return {
        "read_run_type_has_g_and_cartesian_default": True,
        "non_cerveny_tail_has_no_extra_reads": True,
        "bellhop_dispatches_cartesian_and_ray_centered_geometric_hat": True,
        "geometric_hat_formulas_present": True,
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
    case_id, family, family_marker = CASES[key]
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
    environment = profile_root / run["environment_file"]
    paths = {
        "environment": environment,
        "altimetry": environment.with_suffix(".ati"),
        "bathymetry": environment.with_suffix(".bty"),
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
    environment_contents = environment.read_text(encoding="utf-8")
    if environment_contents != expected_environment:
        raise ValueError(f"{manifest_path}: staged ENV differs from fixture")
    if parse_family_and_require_eof(environment_contents, environment) != family:
        raise ValueError(f"{manifest_path}: ENV family mismatch")
    for label, suffix in (("altimetry", "ati"), ("bathymetry", "bty")):
        canonical = definition.directory / f"origin.{suffix}"
        if paths[label].read_bytes() != canonical.read_bytes():
            raise ValueError(f"{manifest_path}: staged {suffix} differs")

    print_contents = paths["print"].read_text(errors="replace")
    opposite_marker = CASES["g" if key == "G" else "G"][2]
    if print_contents.count(family_marker) != 1:
        raise ValueError(f"{manifest_path}: PRT family marker mismatch")
    if opposite_marker in print_contents:
        raise ValueError(f"{manifest_path}: PRT has opposite family marker")
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


def generation_commands(include_rayreuse: bool) -> list[str]:
    executables = {
        "origin": "Bellhop_origin/bin/bellhop",
        "f2cpp": "Bellhop_F2CPP/build/release/bellhop_f2cpp",
        "rayreuse": "Bellhop_RayReuse/build/release/bellhop_rayreuse",
    }
    versions = ("origin", "f2cpp", "rayreuse") if include_rayreuse else (
        "origin",
        "f2cpp",
    )
    return [
        "python3 test/standard_cases/codes/standard_cases.py test "
        f"--version {version} --case {case_id} --profile single "
        f"--executable {executables[version]} "
        "--results-root <results-root>"
        for version in versions
        for case_id, _, _ in CASES.values()
    ]


def validate(
    results_root: Path,
    origin_executable: Path,
    f2cpp_executable: Path,
    rayreuse_executable: Path | None = None,
) -> dict[str, object]:
    source_contract = validate_origin_source_contract()
    definitions = discover_cases(STANDARD_CASES_ROOT / "cases")
    executables = {
        "origin": origin_executable.resolve(),
        "f2cpp": f2cpp_executable.resolve(),
    }
    if rayreuse_executable is not None:
        executables["rayreuse"] = rayreuse_executable.resolve()
    if len(set(executables.values())) != len(executables):
        raise ValueError("solver executable paths must differ")
    if not all(path.is_file() for path in executables.values()):
        raise ValueError("an expected executable does not exist")
    executable_hashes = {
        version: sha256(path) for version, path in executables.items()
    }
    if len(set(executable_hashes.values())) != len(executable_hashes):
        raise ValueError("solver executable hashes must differ")

    loaded = {
        version: {
            key: load_run(results_root, version, key, executable, definitions)
            for key in CASES
        }
        for version, executable in executables.items()
    }
    environment_hashes: dict[str, dict[str, str]] = {}
    normalized_hashes: dict[str, str] = {}
    companion_hashes: dict[str, dict[str, str]] = {}
    for version in executables:
        environment_hashes[version] = {
            key: sha256(loaded[version][key]["environment"])
            for key in CASES
        }
        if len(set(environment_hashes[version].values())) != len(CASES):
            raise ValueError(f"{version}: G/g ENV files are not distinct")
        normalized = {
            normalize_family(
                str(loaded[version][key]["environment_contents"]),
                loaded[version][key]["environment"],
            )
            for key in CASES
        }
        if len(normalized) != 1:
            raise ValueError(f"{version}: ENV files differ beyond G/g")
        normalized_hashes[version] = hashlib.sha256(
            normalized.pop().encode("utf-8")
        ).hexdigest()
        companion_hashes[version] = {
            suffix: sha256(loaded[version]["G"][label])
            for suffix, label in (("ati", "altimetry"), ("bty", "bathymetry"))
        }
        for label in ("altimetry", "bathymetry"):
            if loaded[version]["G"][label].read_bytes() != (
                loaded[version]["g"][label].read_bytes()
            ):
                raise ValueError(f"{version}: G/g companion files differ")
        require_header_equal(
            loaded[version]["G"]["field"],
            loaded[version]["g"]["field"],
            f"{version} G/g",
        )
    for version in executables:
        if environment_hashes["origin"] != environment_hashes[version]:
            raise ValueError(f"Origin and {version} rendered ENV inputs differ")
        if normalized_hashes["origin"] != normalized_hashes[version]:
            raise ValueError(
                f"Origin and {version} normalized ENV identities differ"
            )
        if companion_hashes["origin"] != companion_hashes[version]:
            raise ValueError(f"Origin and {version} companion inputs differ")

    tolerance_path = STANDARD_CASES_ROOT / "codes" / "tolerances.toml"
    comparisons: dict[str, dict[str, object]] = {}
    for key, (case_id, _, _) in CASES.items():
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

    rayreuse_comparisons: dict[str, dict[str, dict[str, object]]] = {}
    if "rayreuse" in executables:
        for reference in ("origin", "f2cpp"):
            pair = f"{reference}_rayreuse"
            rayreuse_comparisons[pair] = {}
            for key, (case_id, _, _) in CASES.items():
                passed, metrics = compare_files(
                    loaded[reference][key]["shade"],
                    loaded["rayreuse"][key]["shade"],
                    0,
                    0,
                    tolerance_path,
                )
                if not passed:
                    raise ValueError(
                        f"{case_id} {reference}/RayReuse mismatch: {metrics}"
                    )
                rayreuse_comparisons[pair][key] = {
                    "passed": True,
                    **metrics,
                }

    effects: dict[str, dict[str, object]] = {}
    for version in executables:
        metrics = effect_metrics(
            loaded[version]["G"]["field"].pressure,
            loaded[version]["g"]["field"].pressure,
        )
        passed = (
            metrics["max_pressure_absolute_difference"]
            > MINIMUM_COORDINATE_EFFECT
            and metrics["support_mismatch_count"] > 0
        )
        if not passed:
            raise ValueError(
                f"{version}: geometric-hat G/g effect is empty: {metrics}"
            )
        effects[version] = {"passed": True, **metrics}

    executable_records = {
        version: {
            "path": str(path),
            "mtime_ns": path.stat().st_mtime_ns,
            "sha256": executable_hashes[version],
        }
        for version, path in executables.items()
    }
    field_summaries = {
        f"{version}/{key}": {
            "nonzero_count": loaded[version][key]["nonzero_count"],
            "maximum_magnitude": loaded[version][key]["maximum_magnitude"],
        }
        for version in executables
        for key in CASES
    }
    return {
        "schema": (
            "bellhop.feature_parity.fp1i_ray_centered_geometric_hat_validation"
            if "rayreuse" in executables
            else "bellhop.f2cpp.i7_geometric_hat_validation"
        ),
        "schema_version": 2 if "rayreuse" in executables else 1,
        "status": "passed",
        "matrix": {
            "frequency_hz": FREQUENCY_HZ,
            "launch_count": LAUNCH_COUNT,
            "minimum_coordinate_effect": MINIMUM_COORDINATE_EFFECT,
            "cases": {
                key: {
                    "case_id": case_id,
                    "run_type": family,
                    "prt_family_marker": marker,
                }
                for key, (case_id, family, marker) in CASES.items()
            },
        },
        "executables": executable_records,
        "origin_source_contract": source_contract,
        "origin_f2cpp_field_comparisons": comparisons,
        "rayreuse_field_comparisons": rayreuse_comparisons,
        "independent_coordinate_effect_guards": effects,
        "field_summaries": field_summaries,
        "provenance_guards": {
            "distinct_executable_paths": True,
            "distinct_executable_hashes": True,
            "manifest_executable_identity_bound": True,
            "manifest_source_references_bound": True,
            "results_not_older_than_executables": True,
            "staged_env_and_sidecars_match_canonical_fixtures": True,
            "origin_f2cpp_rendered_inputs_equal": True,
            "g_and_lower_g_inputs_otherwise_identical": True,
            "integrator_record_is_environment_eof": True,
            "env_and_prt_family_identity_bound": True,
            "origin_f2cpp_comparison_count": len(comparisons),
            "rayreuse_comparison_count": sum(
                len(pair) for pair in rayreuse_comparisons.values()
            ),
            "coordinate_effect_guard_count": len(effects),
        },
        "sha256": {
            "rendered_environment": environment_hashes["origin"],
            "normalized_environment": normalized_hashes["origin"],
            "companions": companion_hashes["origin"],
            "origin_field_aggregate": aggregate_sha256(
                [loaded["origin"][key]["shade"] for key in CASES]
            ),
            "f2cpp_field_aggregate": aggregate_sha256(
                [loaded["f2cpp"][key]["shade"] for key in CASES]
            ),
            **(
                {
                    "rayreuse_field_aggregate": aggregate_sha256(
                        [loaded["rayreuse"][key]["shade"] for key in CASES]
                    )
                }
                if "rayreuse" in executables
                else {}
            ),
            "origin_prt": {
                key: sha256(loaded["origin"][key]["print"])
                for key in CASES
            },
            "f2cpp_prt": {
                key: sha256(loaded["f2cpp"][key]["print"])
                for key in CASES
            },
            **(
                {
                    "rayreuse_prt": {
                        key: sha256(loaded["rayreuse"][key]["print"])
                        for key in CASES
                    }
                }
                if "rayreuse" in executables
                else {}
            ),
        },
        "generation": {
            "case_commands": generation_commands("rayreuse" in executables),
            "validator_command": (
                "python3 test/standard_cases/codes/"
                "validate_i7_geometric_hat.py "
                "--results-root <results-root> "
                "--origin-executable Bellhop_origin/bin/bellhop "
                "--f2cpp-executable "
                "Bellhop_F2CPP/build/release/bellhop_f2cpp "
                "--rayreuse-executable "
                "Bellhop_RayReuse/build/release/bellhop_rayreuse "
                "--output Bellhop_RayReuse/doc/reports/validation/"
                "fp1i_ray_centered_geometric_hat_report.json"
            ),
        },
    }


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--results-root", type=Path, required=True)
    parser.add_argument("--origin-executable", type=Path, required=True)
    parser.add_argument("--f2cpp-executable", type=Path, required=True)
    parser.add_argument("--rayreuse-executable", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    return parser


def main() -> int:
    args = build_parser().parse_args()
    report = validate(
        args.results_root.resolve(),
        args.origin_executable.resolve(),
        args.f2cpp_executable.resolve(),
        args.rayreuse_executable.resolve()
        if args.rayreuse_executable is not None
        else None,
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(f"I7 geometric-hat validation: PASSED ({args.output})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
