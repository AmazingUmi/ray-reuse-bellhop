#!/usr/bin/env python3
"""Validate I7 Cerveny width and reflection-curvature beam options."""

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
    "FS": "cerveny_width_space_filling",
    "MS": "i3_curvilinear_oracle",
    "WS": "cerveny_width_wkb",
    "MD": "cerveny_curvature_double",
    "MZ": "cerveny_curvature_zero",
}
PROFILE = "single"
FREQUENCY_HZ = 100.0
LAUNCH_COUNT = 459
EXPECTED_DIMENSIONS = (1, 1, 1, 1, 1, 2, 3)
MINIMUM_OPTION_EFFECT = 1.0e-5
WIDTH_PAIRS = (("FS", "MS"), ("MS", "WS"), ("FS", "WS"))
CURVATURE_PAIRS = (("MD", "MS"), ("MS", "MZ"), ("MD", "MZ"))
OPTION_LINE = re.compile(
    r"^\s*['\"]([FMW][DSZ])['\"]\s+\S+\s+\S+\s*(?:!.*)?$",
    re.MULTILINE,
)
FORTRAN_NUMBER = r"[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[DEde][+-]?\d+)?"
HALF_WIDTH_LINE = re.compile(
    rf"^\s*HalfWidth\s*=\s*({FORTRAN_NUMBER})\s*$", re.MULTILINE
)
EPSILON_LINE = re.compile(
    rf"^\s*epsilonOpt\s*=\s*\(\s*({FORTRAN_NUMBER})\s*,\s*"
    rf"({FORTRAN_NUMBER})\s*\)\s*$",
    re.MULTILINE,
)
WIDTH_LABELS = {
    "FS": "Space filling beams",
    "MS": "Minimum width beams",
    "WS": "WKB beams",
    "MD": "Minimum width beams",
    "MZ": "Minimum width beams",
}
CURVATURE_LABELS = {
    "FS": "Standard curvature condition",
    "MS": "Standard curvature condition",
    "WS": "Standard curvature condition",
    "MD": "Curvature doubling invoked",
    "MZ": "Curvature zeroing invoked",
}


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def aggregate_sha256(paths: list[Path]) -> str:
    digest = hashlib.sha256()
    for path in sorted(paths, key=lambda item: str(item)):
        digest.update(path.read_bytes())
    return digest.hexdigest()


def parse_beam_option(contents: str, source: Path | str) -> str:
    options = OPTION_LINE.findall(contents)
    if len(options) != 1:
        raise ValueError(
            f"{source}: expected exactly one F/M/W plus D/S/Z option record"
        )
    return options[0]


def normalize_beam_option(contents: str, source: Path | str) -> str:
    parse_beam_option(contents, source)
    return OPTION_LINE.sub(
        lambda match: match.group(0).replace(
            f"'{match.group(1)}'", "'<OPTION>'"
        ).replace(
            f'"{match.group(1)}"', '"<OPTION>"'
        ),
        contents,
    )


def fortran_float(value: str) -> float:
    return float(value.replace("D", "E").replace("d", "e"))


def parse_prt_epsilon(contents: str, source: Path | str) -> dict[str, object]:
    half_widths = HALF_WIDTH_LINE.findall(contents)
    epsilons = EPSILON_LINE.findall(contents)
    if len(half_widths) != 1 or len(epsilons) != 1:
        raise ValueError(
            f"{source}: expected exactly one HalfWidth and epsilonOpt record"
        )
    return {
        "half_width": fortran_float(half_widths[0]),
        "epsilon": (
            fortran_float(epsilons[0][0]),
            fortran_float(epsilons[0][1]),
        ),
    }


def expected_epsilon_anchors() -> dict[str, dict[str, object]]:
    omega = 2.0 * math.pi * FREQUENCY_HZ
    sound_speed = 1500.0
    delta_alpha = math.radians(80.0 / (LAUNCH_COUNT - 1))
    space_half_width = 2.0 / ((omega / sound_speed) * delta_alpha)
    space_epsilon_imag = 0.5 * omega * space_half_width ** 2
    minimum_half_width = math.sqrt(
        2.0 * sound_speed * 1000.0 * 1.0 / omega
    )
    minimum_epsilon_imag = 0.5 * omega * minimum_half_width ** 2
    return {
        "FS": {
            "half_width": space_half_width,
            "epsilon": (0.0, space_epsilon_imag),
        },
        "MS": {
            "half_width": minimum_half_width,
            "epsilon": (0.0, minimum_epsilon_imag),
        },
        "WS": {
            "half_width": sys.float_info.max,
            "epsilon": (1.0e10, 0.0),
        },
    }


def validate_origin_source_contract() -> dict[str, object]:
    sources = {
        "read_environment": (
            PROJECT_ROOT / "Bellhop_origin" / "Bellhop" /
            "ReadEnvironmentBell.f90"
        ),
        "pick_epsilon": (
            PROJECT_ROOT / "Bellhop_origin" / "Bellhop" / "bellhop.f90"
        ),
        "branch_cut": (
            PROJECT_ROOT / "Bellhop_origin" / "Bellhop" / "influence.f90"
        ),
        "reflection": (
            PROJECT_ROOT / "Bellhop_origin" / "Bellhop" / "ReflectMod.f90"
        ),
    }
    text = {
        name: path.read_text(encoding="utf-8")
        for name, path in sources.items()
    }
    parser = text["read_environment"]
    if not re.search(
        r"READ\s*\(\s*ENVFile\s*,\s*\*\s*\)\s*"
        r"Beam%Type\(\s*2\s*:\s*3\s*\)",
        parser,
        re.IGNORECASE,
    ):
        raise ValueError("Origin no longer parses Beam%Type(2:3)")

    pick_source = text["pick_epsilon"]
    pick_start = pick_source.find("FUNCTION PickEpsilon")
    pick_end = pick_source.find("END FUNCTION PickEpsilon", pick_start)
    if pick_start < 0 or pick_end < 0:
        raise ValueError("Origin PickEpsilon routine is absent")
    pick = pick_source[pick_start:pick_end]
    for anchor in (
        "CASE ( 'F' )",
        "CASE ( 'M' )",
        "CASE ( 'W' )",
        "COS( alpha ** 2 )",
        "epsilonOpt = 1.0D10",
    ):
        if anchor not in pick:
            raise ValueError(f"Origin PickEpsilon anchor is absent: {anchor}")

    branch_source = text["branch_cut"]
    branch_start = branch_source.find("SUBROUTINE BranchCut")
    branch_end = branch_source.find("END SUBROUTINE BranchCut", branch_start)
    if branch_start < 0 or branch_end < 0:
        raise ValueError("Origin BranchCut routine is absent")
    branch = branch_source[branch_start:branch_end]
    wkb_start = branch.find("CASE ( 'W' )")
    default_start = branch.find("CASE DEFAULT", wkb_start)
    if wkb_start < 0 or default_start < 0:
        raise ValueError("Origin WKB BranchCut branch is absent")
    wkb = branch[wkb_start:default_start]
    if "q1 = REAL( q1C )" not in wkb or "q2 = REAL( q2C )" not in wkb:
        raise ValueError("Origin WKB BranchCut no longer uses real(q)")

    reflection_source = text["reflection"]
    reflection_start = reflection_source.find(
        "SUBROUTINE CurvatureCorrection2"
    )
    reflection_end = reflection_source.find(
        "END SUBROUTINE CurvatureCorrection2", reflection_start
    )
    if reflection_start < 0 or reflection_end < 0:
        raise ValueError("Origin CurvatureCorrection2 routine is absent")
    reflection = reflection_source[reflection_start:reflection_end]
    total_anchor = "RN = RN + RM * ( 2 * cnjump - RM * csjump ) / c ** 2"
    double_anchor = "RN = 2.0 * RN"
    zero_anchor = "RN = 0.0"
    indices = tuple(
        reflection.find(anchor)
        for anchor in (total_anchor, double_anchor, zero_anchor)
    )
    if min(indices) < 0 or not (indices[0] < indices[1] < indices[2]):
        raise ValueError(
            "Origin D/Z correction is not applied after total RN construction"
        )

    return {
        "beam_type_2_3_is_parsed": True,
        "pick_epsilon_has_f_m_w": True,
        "wkb_formula_preserves_cos_alpha_squared": True,
        "wkb_branch_cut_uses_real_q": True,
        "curvature_d_z_apply_to_total_rn": True,
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
    option: str,
    executable: Path,
    definitions: dict[str, object],
) -> dict[str, object]:
    case_id = CASES[option]
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
    paths["ati"] = paths["environment"].with_suffix(".ati")
    paths["bty"] = paths["environment"].with_suffix(".bty")
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
    if parse_beam_option(environment_contents, paths["environment"]) != option:
        raise ValueError(f"{manifest_path}: ENV beam option mismatch")
    for suffix in ("ati", "bty"):
        canonical = definition.directory / f"origin.{suffix}"
        if paths[suffix].read_bytes() != canonical.read_bytes():
            raise ValueError(f"{manifest_path}: staged {suffix} mismatch")

    print_contents = paths["print"].read_text(errors="replace")
    if print_contents.count(WIDTH_LABELS[option]) != 1:
        raise ValueError(f"{manifest_path}: PRT width identity mismatch")
    if print_contents.count(CURVATURE_LABELS[option]) != 1:
        raise ValueError(f"{manifest_path}: PRT curvature identity mismatch")
    epsilon = parse_prt_epsilon(print_contents, paths["print"])

    field = ShdReader(paths["shade"]).read()
    if field.header.dimensions != EXPECTED_DIMENSIONS:
        raise ValueError(f"{manifest_path}: unexpected SHD dimensions")
    magnitudes = np.abs(field.pressure)
    if not np.all(np.isfinite(magnitudes)) or np.count_nonzero(magnitudes) < 2:
        raise ValueError(f"{manifest_path}: field is zero/sparse/non-finite")
    return {
        "manifest": manifest_path,
        **paths,
        "environment_contents": environment_contents,
        "epsilon": epsilon,
        "field": field,
        "maximum_magnitude": float(np.max(magnitudes)),
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
        for case_id in CASES.values()
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
            option: load_run(
                results_root, version, option, executable, definitions
            )
            for option in CASES
        }
        for version, executable in executables.items()
    }

    environment_hashes: dict[str, dict[str, str]] = {}
    normalized_hashes: dict[str, str] = {}
    for version in executables:
        environment_hashes[version] = {
            option: sha256(loaded[version][option]["environment"])
            for option in CASES
        }
        if len(set(environment_hashes[version].values())) != len(CASES):
            raise ValueError(f"{version}: beam-option ENV files are not distinct")
        normalized = {
            normalize_beam_option(
                str(loaded[version][option]["environment_contents"]),
                loaded[version][option]["environment"],
            )
            for option in CASES
        }
        if len(normalized) != 1:
            raise ValueError(
                f"{version}: ENV files differ beyond the beam-option token"
            )
        normalized_hashes[version] = hashlib.sha256(
            normalized.pop().encode("utf-8")
        ).hexdigest()
    if environment_hashes["origin"] != environment_hashes["f2cpp"]:
        raise ValueError("Origin and F2CPP rendered ENV inputs differ")
    if normalized_hashes["origin"] != normalized_hashes["f2cpp"]:
        raise ValueError("Origin and F2CPP normalized ENV identities differ")

    anchors = expected_epsilon_anchors()
    epsilon_checks: dict[str, dict[str, object]] = {}
    for version in executables:
        for option in ("FS", "MS", "WS"):
            observed = loaded[version][option]["epsilon"]
            expected = anchors[option]
            if not math.isclose(
                observed["half_width"], expected["half_width"],
                rel_tol=2.0e-12,
            ):
                raise ValueError(f"{version}/{option}: HalfWidth mismatch")
            if any(
                not math.isclose(left, right, rel_tol=2.0e-12, abs_tol=1e-12)
                for left, right in zip(observed["epsilon"], expected["epsilon"])
            ):
                raise ValueError(f"{version}/{option}: epsilonOpt mismatch")
            epsilon_checks[f"{version}/{option}"] = {
                "passed": True,
                "half_width": observed["half_width"],
                "epsilon": list(observed["epsilon"]),
            }

    tolerance_path = STANDARD_CASES_ROOT / "codes" / "tolerances.toml"
    comparisons: dict[str, dict[str, object]] = {}
    for option, case_id in CASES.items():
        passed, metrics = compare_files(
            loaded["origin"][option]["shade"],
            loaded["f2cpp"][option]["shade"],
            0,
            0,
            tolerance_path,
        )
        if not passed:
            raise ValueError(f"{case_id} Origin/F2CPP mismatch: {metrics}")
        comparisons[option] = {"passed": True, **metrics}

    effect_guards: dict[str, dict[str, object]] = {}
    for version in executables:
        control = loaded[version]["MS"]["field"]
        for left, right in (*WIDTH_PAIRS, *CURVATURE_PAIRS):
            left_field = loaded[version][left]["field"]
            right_field = loaded[version][right]["field"]
            require_header_equal(left_field, right_field, f"{version}/{left}-{right}")
            difference = float(
                np.max(np.abs(left_field.pressure - right_field.pressure))
            )
            if not np.isfinite(difference) or difference <= MINIMUM_OPTION_EFFECT:
                raise ValueError(
                    f"{version}/{left}-{right}: option effect is absent "
                    f"({difference})"
                )
            effect_guards[f"{version}/{left}-{right}"] = {
                "passed": True,
                "max_pressure_absolute_difference": difference,
            }
        require_header_equal(
            control, loaded[version]["MD"]["field"], f"{version}/control-MD"
        )

    field_paths = {
        version: [loaded[version][option]["shade"] for option in CASES]
        for version in executables
    }
    companion_hashes = {
        option: {
            suffix: sha256(
                definitions[case_id].directory / f"origin.{suffix}"
            )
            for suffix in ("ati", "bty")
        }
        for option, case_id in CASES.items()
    }
    return {
        "schema": "bellhop.f2cpp.i7_beam_options_validation",
        "schema_version": 1,
        "status": "passed",
        "matrix": {
            "options": CASES,
            "width_pairs": [list(pair) for pair in WIDTH_PAIRS],
            "curvature_pairs": [list(pair) for pair in CURVATURE_PAIRS],
            "frequency_hz": FREQUENCY_HZ,
            "launch_count": LAUNCH_COUNT,
            "minimum_effect": MINIMUM_OPTION_EFFECT,
        },
        "origin_f2cpp_field_comparisons": comparisons,
        "independent_option_effect_guards": effect_guards,
        "epsilon_anchor_guards": epsilon_checks,
        "origin_source_contract": source_contract,
        "executables": {
            version: {
                "path": str(path),
                "sha256": executable_hashes[version],
                "mtime_ns": path.stat().st_mtime_ns,
            }
            for version, path in executables.items()
        },
        "provenance_guards": {
            "distinct_executable_paths": True,
            "distinct_executable_hashes": True,
            "results_not_older_than_executables": True,
            "manifest_executable_identity_bound": True,
            "manifest_source_references_bound": True,
            "staged_env_matches_canonical_fixture": True,
            "staged_companions_match_canonical_fixture": True,
            "origin_f2cpp_rendered_env_inputs_equal": True,
            "beam_option_env_inputs_pairwise_distinct": True,
            "beam_option_env_inputs_otherwise_identical": True,
            "env_and_prt_option_identity_bound": True,
            "nonzero_field_guard": True,
            "origin_f2cpp_comparison_count": len(comparisons),
            "option_effect_guard_count": len(effect_guards),
        },
        "generation": {
            "case_commands": generation_commands(),
            "validator_command": (
                "python3 test/standard_cases/codes/"
                "validate_i7_beam_options.py "
                "--results-root <results-root> "
                "--origin-executable Bellhop_origin/bin/bellhop "
                "--f2cpp-executable "
                "Bellhop_F2CPP/build/release/bellhop_f2cpp "
                "--output Bellhop_F2CPP/doc/reports/validation/"
                "i7_beam_options_report.json"
            ),
        },
        "sha256": {
            "rendered_beam_option_env": environment_hashes["origin"],
            "normalized_beam_option_env": normalized_hashes["origin"],
            "canonical_companions": companion_hashes,
            "origin_field_aggregate": aggregate_sha256(field_paths["origin"]),
            "f2cpp_field_aggregate": aggregate_sha256(field_paths["f2cpp"]),
        },
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--results-root", type=Path, required=True)
    parser.add_argument("--origin-executable", type=Path, required=True)
    parser.add_argument("--f2cpp-executable", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = validate(
        args.results_root,
        args.origin_executable,
        args.f2cpp_executable,
    )
    rendered = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.output is not None:
        args.output.write_text(rendered, encoding="utf-8")
    print(rendered, end="")


if __name__ == "__main__":
    main()
