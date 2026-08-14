#!/usr/bin/env python3
"""Validate RunType fourth-position point/line source geometry."""

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
    "DEFAULT": "constant_speed_direct",
    "R": "source_geometry_point_explicit",
    "X": "source_geometry_line",
}
PROFILE = "single"
FREQUENCY_HZ = 50.0
LAUNCH_COUNT = 300
EXPECTED_DIMENSIONS = (1, 1, 1, 1, 1, 21, 51)
MINIMUM_PRESSURE_EFFECT = 1.0e-3
PRESSURE_MASK_FLOOR = 1.0e-7
MINIMUM_MEDIAN_TL_EFFECT_DB = 20.0
RUN_TYPE_LINE = re.compile(
    r"^\s*['\"]CC(?:\s+([RX]))?['\"]\s*(?:!.*)?$", re.MULTILINE
)
PRT_LABELS = {
    "DEFAULT": "Point source (cylindrical coordinates)",
    "R": "Point source (cylindrical coordinates)",
    "X": "Line source (Cartesian coordinates)",
}
PRT_OPPOSITE = {
    "DEFAULT": "Line source (Cartesian coordinates)",
    "R": "Line source (Cartesian coordinates)",
    "X": "Point source (cylindrical coordinates)",
}


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def aggregate_sha256(paths: list[Path]) -> str:
    digest = hashlib.sha256()
    for path in sorted(paths, key=lambda item: str(item)):
        digest.update(path.read_bytes())
    return digest.hexdigest()


def parse_source_geometry_token(contents: str, source: Path | str) -> str:
    matches = RUN_TYPE_LINE.findall(contents)
    if len(matches) != 1:
        raise ValueError(
            f"{source}: expected exactly one coherent CC RunType record "
            "with blank/R/X fourth position"
        )
    return matches[0] or "DEFAULT"


def normalize_source_geometry_token(
    contents: str, source: Path | str
) -> str:
    parse_source_geometry_token(contents, source)
    return RUN_TYPE_LINE.sub(
        lambda match: match.group(0).replace(
            match.group(0).split("!")[0].strip(), "'CC <SOURCE>'"
        ),
        contents,
    )


def source_geometry_effect_metrics(
    point_pressure: np.ndarray, line_pressure: np.ndarray
) -> dict[str, float]:
    if point_pressure.shape != line_pressure.shape:
        raise ValueError("point/line pressure shapes differ")
    if not np.all(np.isfinite(point_pressure)) or not np.all(
        np.isfinite(line_pressure)
    ):
        raise ValueError("point/line pressure contains non-finite values")
    point_magnitude = np.abs(point_pressure)
    line_magnitude = np.abs(line_pressure)
    mask = point_magnitude > PRESSURE_MASK_FLOOR
    if not np.any(mask):
        raise ValueError("point field has no values above the TL mask floor")
    tl_difference = np.abs(
        -20.0 * np.log10(np.maximum(line_magnitude[mask], 1.0e-30))
        + 20.0 * np.log10(point_magnitude[mask])
    )
    return {
        "max_pressure_absolute_difference": float(
            np.max(np.abs(line_pressure - point_pressure))
        ),
        "minimum_tl_difference_db": float(np.min(tl_difference)),
        "median_tl_difference_db": float(np.median(tl_difference)),
        "maximum_tl_difference_db": float(np.max(tl_difference)),
        "point_maximum_magnitude": float(np.max(point_magnitude)),
        "line_maximum_magnitude": float(np.max(line_magnitude)),
        "tl_mask_count": int(np.count_nonzero(mask)),
    }


def validate_origin_source_contract() -> dict[str, object]:
    read_environment = (
        PROJECT_ROOT / "Bellhop_origin" / "Bellhop" /
        "ReadEnvironmentBell.f90"
    )
    influence = (
        PROJECT_ROOT / "Bellhop_origin" / "Bellhop" / "influence.f90"
    )
    parser_source = read_environment.read_text(encoding="utf-8")
    run_type_start = parser_source.find("SUBROUTINE ReadRunType")
    run_type_end = parser_source.find(
        "END SUBROUTINE ReadRunType", run_type_start
    )
    if run_type_start < 0 or run_type_end < 0:
        raise ValueError("Origin ReadRunType routine is absent")
    run_type = parser_source[run_type_start:run_type_end]
    for anchor in (
        "SELECT CASE ( RunType( 4 : 4 ) )",
        "CASE ( 'R' )",
        "CASE ( 'X' )",
        "RunType( 4 : 4 ) = 'R'",
        "Point source (cylindrical coordinates)",
        "Line source (Cartesian coordinates)",
    ):
        if anchor not in run_type:
            raise ValueError(f"Origin ReadRunType anchor is absent: {anchor}")

    influence_source = influence.read_text(encoding="utf-8")
    cart_start = influence_source.find("SUBROUTINE InfluenceCervenyCart")
    cart_end = influence_source.find(
        "END SUBROUTINE InfluenceCervenyCart", cart_start
    )
    if cart_start < 0 or cart_end < 0:
        raise ValueError("Origin InfluenceCervenyCart routine is absent")
    cart = influence_source[cart_start:cart_end]
    for anchor in (
        "IF ( Beam%RunType( 4 : 4 ) == 'R' )",
        "Ratio1 = SQRT( ABS( COS( alpha ) ) )",
        "const = Ratio1 * SQRT( c * ABS( epsV( iS - 1 ) ) / q )",
    ):
        if anchor not in cart:
            raise ValueError(
                f"Origin Cartesian source-ratio anchor is absent: {anchor}"
            )

    scale_start = influence_source.find("SUBROUTINE ScalePressure")
    scale_end = influence_source.find(
        "END SUBROUTINE ScalePressure", scale_start
    )
    if scale_start < 0 or scale_end < 0:
        raise ValueError("Origin ScalePressure routine is absent")
    scale = influence_source[scale_start:scale_end]
    for anchor in (
        "IF ( RunType( 4 : 4 ) == 'X' )",
        "factor = -4.0 * SQRT( pi ) * const",
        "factor = const / SQRT( ABS( r( ir ) ) )",
        "U( :, ir ) = SNGL( factor ) * U( :, ir )",
    ):
        if anchor not in scale:
            raise ValueError(
                f"Origin source-spreading anchor is absent: {anchor}"
            )
    return {
        "blank_fourth_position_defaults_to_r": True,
        "r_selects_point_source": True,
        "x_selects_line_source": True,
        "cartesian_influence_applies_point_cosine_ratio": True,
        "scale_pressure_distinguishes_point_and_line": True,
        "read_environment_sha256": sha256(read_environment),
        "influence_sha256": sha256(influence),
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
    token: str,
    executable: Path,
    definitions: dict[str, object],
) -> dict[str, object]:
    case_id = CASES[token]
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
    if parse_source_geometry_token(
        environment_contents, paths["environment"]
    ) != token:
        raise ValueError(f"{manifest_path}: ENV source geometry mismatch")
    print_contents = paths["print"].read_text(errors="replace")
    if print_contents.count(PRT_LABELS[token]) != 1:
        raise ValueError(f"{manifest_path}: PRT source geometry mismatch")
    if PRT_OPPOSITE[token] in print_contents:
        raise ValueError(f"{manifest_path}: PRT has opposite source geometry")

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
            token: load_run(
                results_root, version, token, executable, definitions
            )
            for token in CASES
        }
        for version, executable in executables.items()
    }
    environment_hashes: dict[str, dict[str, str]] = {}
    normalized_hashes: dict[str, str] = {}
    for version in executables:
        environment_hashes[version] = {
            token: sha256(loaded[version][token]["environment"])
            for token in CASES
        }
        if len(set(environment_hashes[version].values())) != len(CASES):
            raise ValueError(f"{version}: source-geometry ENV files not distinct")
        normalized = {
            normalize_source_geometry_token(
                str(loaded[version][token]["environment_contents"]),
                loaded[version][token]["environment"],
            )
            for token in CASES
        }
        if len(normalized) != 1:
            raise ValueError(
                f"{version}: ENV files differ beyond RunType fourth position"
            )
        normalized_hashes[version] = hashlib.sha256(
            normalized.pop().encode("utf-8")
        ).hexdigest()
    if environment_hashes["origin"] != environment_hashes["f2cpp"]:
        raise ValueError("Origin and F2CPP rendered ENV inputs differ")
    if normalized_hashes["origin"] != normalized_hashes["f2cpp"]:
        raise ValueError("Origin and F2CPP normalized ENV identities differ")

    tolerance_path = STANDARD_CASES_ROOT / "codes" / "tolerances.toml"
    comparisons: dict[str, dict[str, object]] = {}
    for token, case_id in CASES.items():
        passed, metrics = compare_files(
            loaded["origin"][token]["shade"],
            loaded["f2cpp"][token]["shade"],
            0,
            0,
            tolerance_path,
        )
        if not passed:
            raise ValueError(f"{case_id} Origin/F2CPP mismatch: {metrics}")
        comparisons[token] = {"passed": True, **metrics}

    no_op_guards: dict[str, dict[str, object]] = {}
    effect_guards: dict[str, dict[str, object]] = {}
    for version in executables:
        implicit = loaded[version]["DEFAULT"]
        explicit = loaded[version]["R"]
        require_header_equal(
            implicit["field"], explicit["field"], f"{version}/DEFAULT-R"
        )
        if sha256(implicit["shade"]) != sha256(explicit["shade"]):
            raise ValueError(
                f"{version}: default and explicit point SHD bytes differ"
            )
        if not np.array_equal(
            implicit["field"].pressure, explicit["field"].pressure
        ):
            raise ValueError(
                f"{version}: default and explicit point arrays differ"
            )
        no_op_guards[version] = {
            "passed": True,
            "shd_sha256": sha256(implicit["shade"]),
            "semantic_arrays_exactly_equal": True,
            "shd_files_byte_identical": True,
        }

        line = loaded[version]["X"]
        require_header_equal(explicit["field"], line["field"], f"{version}/R-X")
        effect = source_geometry_effect_metrics(
            explicit["field"].pressure, line["field"].pressure
        )
        if effect["max_pressure_absolute_difference"] <= (
            MINIMUM_PRESSURE_EFFECT
        ):
            raise ValueError(f"{version}: point/line pressure effect too small")
        if effect["median_tl_difference_db"] <= MINIMUM_MEDIAN_TL_EFFECT_DB:
            raise ValueError(f"{version}: point/line TL effect too small")
        effect_guards[version] = {"passed": True, **effect}

    field_paths = {
        version: [loaded[version][token]["shade"] for token in CASES]
        for version in executables
    }
    return {
        "schema": "bellhop.f2cpp.i7_source_geometry_validation",
        "schema_version": 1,
        "status": "passed",
        "matrix": {
            "run_type_fourth_position": CASES,
            "frequency_hz": FREQUENCY_HZ,
            "launch_count": LAUNCH_COUNT,
            "minimum_pressure_effect": MINIMUM_PRESSURE_EFFECT,
            "pressure_mask_floor": PRESSURE_MASK_FLOOR,
            "minimum_median_tl_effect_db": MINIMUM_MEDIAN_TL_EFFECT_DB,
        },
        "origin_f2cpp_field_comparisons": comparisons,
        "default_explicit_point_no_op_guards": no_op_guards,
        "point_line_effect_guards": effect_guards,
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
            "origin_f2cpp_rendered_env_inputs_equal": True,
            "source_geometry_env_inputs_pairwise_distinct": True,
            "source_geometry_env_inputs_otherwise_identical": True,
            "env_and_prt_source_geometry_identity_bound": True,
            "nonzero_field_guard": True,
            "origin_f2cpp_comparison_count": len(comparisons),
            "no_op_guard_count": len(no_op_guards),
            "effect_guard_count": len(effect_guards),
        },
        "generation": {
            "case_commands": generation_commands(),
            "validator_command": (
                "python3 test/standard_cases/codes/"
                "validate_i7_source_geometry.py "
                "--results-root <results-root> "
                "--origin-executable Bellhop_origin/bin/bellhop "
                "--f2cpp-executable "
                "Bellhop_F2CPP/build/release/bellhop_f2cpp "
                "--output Bellhop_F2CPP/doc/validation/"
                "i7_source_geometry_report.json"
            ),
        },
        "sha256": {
            "rendered_source_geometry_env": environment_hashes["origin"],
            "normalized_source_geometry_env": normalized_hashes["origin"],
            "origin_prt": {
                token: sha256(loaded["origin"][token]["print"])
                for token in CASES
            },
            "f2cpp_prt": {
                token: sha256(loaded["f2cpp"][token]["print"])
                for token in CASES
            },
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
