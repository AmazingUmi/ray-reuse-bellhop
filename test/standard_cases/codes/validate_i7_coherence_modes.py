#!/usr/bin/env python3
"""Validate coherent, incoherent, and semi-coherent 2-D fields."""

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
    "C": "constant_speed_direct",
    "I": "incoherent_direct",
    "S": "semicoherent_direct",
}
PROFILE = "single"
FREQUENCY_HZ = 50.0
LAUNCH_COUNT = 300
EXPECTED_DIMENSIONS = (1, 1, 1, 1, 1, 21, 51)
PRESSURE_MASK_FLOOR = 1.0e-7
MINIMUM_C_I_PRESSURE_EFFECT = 1.0e-3
MINIMUM_C_I_MEDIAN_TL_DB = 10.0
MINIMUM_I_S_PRESSURE_EFFECT = 5.0e-7
MINIMUM_I_S_MEDIAN_TL_DB = 2.0e-2
MINIMUM_COHERENT_IMAGINARY_MAGNITUDE = 1.0e-7
RUN_MODE_LINE = re.compile(
    r"^\s*['\"]([CIS])C['\"]\s*(?:!.*)?$", re.MULTILINE
)
PRT_LABELS = {
    "C": "Coherent TL calculation",
    "I": "Incoherent TL calculation",
    "S": "Semi-coherent TL calculation",
}


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def aggregate_sha256(paths: list[Path]) -> str:
    digest = hashlib.sha256()
    for path in sorted(paths, key=lambda item: str(item)):
        digest.update(path.read_bytes())
    return digest.hexdigest()


def parse_coherence_mode(contents: str, source: Path | str) -> str:
    modes = RUN_MODE_LINE.findall(contents)
    if len(modes) != 1:
        raise ValueError(
            f"{source}: expected exactly one CC/IC/SC RunType record"
        )
    return modes[0]


def normalize_coherence_mode(contents: str, source: Path | str) -> str:
    parse_coherence_mode(contents, source)
    return RUN_MODE_LINE.sub(
        lambda match: match.group(0).replace(
            f"'{match.group(1)}C'", "'<MODE>C'"
        ).replace(
            f'"{match.group(1)}C"', '"<MODE>C"'
        ),
        contents,
    )


def mode_effect_metrics(
    left_pressure: np.ndarray, right_pressure: np.ndarray
) -> dict[str, float | int]:
    if left_pressure.shape != right_pressure.shape:
        raise ValueError("mode pressure shapes differ")
    if not np.all(np.isfinite(left_pressure)) or not np.all(
        np.isfinite(right_pressure)
    ):
        raise ValueError("mode pressure contains non-finite values")
    left_magnitude = np.abs(left_pressure)
    right_magnitude = np.abs(right_pressure)
    mask = (
        (left_magnitude > PRESSURE_MASK_FLOOR)
        & (right_magnitude > PRESSURE_MASK_FLOOR)
    )
    if not np.any(mask):
        raise ValueError("mode fields have no common values above TL floor")
    tl_difference = np.abs(
        -20.0 * np.log10(left_magnitude[mask])
        + 20.0 * np.log10(right_magnitude[mask])
    )
    return {
        "max_pressure_absolute_difference": float(
            np.max(np.abs(left_pressure - right_pressure))
        ),
        "minimum_tl_difference_db": float(np.min(tl_difference)),
        "median_tl_difference_db": float(np.median(tl_difference)),
        "maximum_tl_difference_db": float(np.max(tl_difference)),
        "tl_mask_count": int(np.count_nonzero(mask)),
    }


def field_mode_semantics(
    mode: str, pressure: np.ndarray
) -> dict[str, float | int | bool]:
    if not np.all(np.isfinite(pressure)):
        raise ValueError(f"{mode}: field contains non-finite values")
    magnitude = np.abs(pressure)
    nonzero_count = int(np.count_nonzero(magnitude))
    if nonzero_count < 2:
        raise ValueError(f"{mode}: field is empty or sparse")
    maximum_imaginary = float(np.max(np.abs(pressure.imag)))
    if mode == "C":
        if maximum_imaginary <= MINIMUM_COHERENT_IMAGINARY_MAGNITUDE:
            raise ValueError("C: coherent field lacks a complex phase")
    else:
        if maximum_imaginary != 0.0:
            raise ValueError(f"{mode}: intensity field is not exactly real")
        if np.any(pressure.real > 0.0) or not np.any(pressure.real < 0.0):
            raise ValueError(
                f"{mode}: final intensity-derived pressure is not non-positive"
            )
    return {
        "passed": True,
        "nonzero_count": nonzero_count,
        "maximum_magnitude": float(np.max(magnitude)),
        "maximum_imaginary_magnitude": maximum_imaginary,
        "minimum_real": float(np.min(pressure.real)),
        "maximum_real": float(np.max(pressure.real)),
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
        "CASE ( 'I' )",
        "Incoherent TL calculation",
        "CASE ( 'S' )",
        "Semi-coherent TL calculation",
        "CASE ( 'C' )",
        "Coherent TL calculation",
    ):
        if anchor not in run_type:
            raise ValueError(f"Origin C/I/S parser anchor is absent: {anchor}")

    core = text["bellhop_core"]
    for anchor in (
        "IF ( Beam%RunType( 1 : 1 ) == 'S' )",
        "Amp0 = Amp0 * SQRT( 2.0 ) * ABS( SIN( omega / c",
        "xs( 2 ) * SIN( Angles%alpha( ialpha ) ) ) )",
    ):
        if anchor not in core:
            raise ValueError(f"Origin Lloyd mirror anchor is absent: {anchor}")

    influence_source = text["influence"]
    cart_start = influence_source.find("SUBROUTINE InfluenceCervenyCart")
    cart_end = influence_source.find(
        "END SUBROUTINE InfluenceCervenyCart", cart_start
    )
    if cart_start < 0 or cart_end < 0:
        raise ValueError("Origin InfluenceCervenyCart routine is absent")
    cart = influence_source[cart_start:cart_end]
    for anchor in (
        "CASE ( 'C' )        ! coherent",
        "CASE ( 'I', 'S' )   ! incoherent or semi-coherent",
        "contri = ABS( const * contri ) ** 2",
        "U( iz, ir ) = U( iz, ir ) + QuantizedContribution",
    ):
        if anchor not in cart:
            raise ValueError(f"Origin C/I/S influence anchor absent: {anchor}")

    scale_start = influence_source.find("SUBROUTINE ScalePressure")
    scale_end = influence_source.find(
        "END SUBROUTINE ScalePressure", scale_start
    )
    if scale_start < 0 or scale_end < 0:
        raise ValueError("Origin ScalePressure routine is absent")
    scale = influence_source[scale_start:scale_end]
    if "IF ( RunType( 1 : 1 ) /= 'C' ) U = SQRT( REAL( U ) )" not in scale:
        raise ValueError("Origin intensity-to-pressure conversion is absent")

    return {
        "read_run_type_has_c_i_s": True,
        "semi_coherent_applies_lloyd_mirror": True,
        "cerveny_cart_accumulates_i_s_intensity": True,
        "noncoherent_scale_uses_sqrt_real": True,
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
    mode: str,
    executable: Path,
    definitions: dict[str, object],
) -> dict[str, object]:
    case_id = CASES[mode]
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
    if parse_coherence_mode(environment_contents, paths["environment"]) != mode:
        raise ValueError(f"{manifest_path}: ENV coherence mode mismatch")
    print_contents = paths["print"].read_text(errors="replace")
    if print_contents.count(PRT_LABELS[mode]) != 1:
        raise ValueError(f"{manifest_path}: PRT coherence mode mismatch")
    for other_mode, marker in PRT_LABELS.items():
        if other_mode != mode and marker in print_contents:
            raise ValueError(f"{manifest_path}: PRT has another mode marker")
    if print_contents.count("Point source (cylindrical coordinates)") != 1:
        raise ValueError(f"{manifest_path}: PRT point-source identity mismatch")
    if "Line source (Cartesian coordinates)" in print_contents:
        raise ValueError(f"{manifest_path}: PRT unexpectedly selects line source")

    field = ShdReader(paths["shade"]).read()
    if field.header.dimensions != EXPECTED_DIMENSIONS:
        raise ValueError(f"{manifest_path}: unexpected SHD dimensions")
    semantics = field_mode_semantics(mode, field.pressure)
    return {
        "manifest": manifest_path,
        **paths,
        "environment_contents": environment_contents,
        "field": field,
        "semantics": semantics,
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
            mode: load_run(results_root, version, mode, executable, definitions)
            for mode in CASES
        }
        for version, executable in executables.items()
    }
    environment_hashes: dict[str, dict[str, str]] = {}
    normalized_hashes: dict[str, str] = {}
    for version in executables:
        environment_hashes[version] = {
            mode: sha256(loaded[version][mode]["environment"])
            for mode in CASES
        }
        if len(set(environment_hashes[version].values())) != len(CASES):
            raise ValueError(f"{version}: C/I/S ENV files are not distinct")
        normalized = {
            normalize_coherence_mode(
                str(loaded[version][mode]["environment_contents"]),
                loaded[version][mode]["environment"],
            )
            for mode in CASES
        }
        if len(normalized) != 1:
            raise ValueError(f"{version}: ENV files differ beyond C/I/S mode")
        normalized_hashes[version] = hashlib.sha256(
            normalized.pop().encode("utf-8")
        ).hexdigest()
    if environment_hashes["origin"] != environment_hashes["f2cpp"]:
        raise ValueError("Origin and F2CPP rendered ENV inputs differ")
    if normalized_hashes["origin"] != normalized_hashes["f2cpp"]:
        raise ValueError("Origin and F2CPP normalized ENV identities differ")

    tolerance_path = STANDARD_CASES_ROOT / "codes" / "tolerances.toml"
    comparisons: dict[str, dict[str, object]] = {}
    for mode, case_id in CASES.items():
        passed, metrics = compare_files(
            loaded["origin"][mode]["shade"],
            loaded["f2cpp"][mode]["shade"],
            0,
            0,
            tolerance_path,
        )
        if not passed:
            raise ValueError(f"{case_id} Origin/F2CPP mismatch: {metrics}")
        comparisons[mode] = {"passed": True, **metrics}

    semantics = {
        f"{version}/{mode}": loaded[version][mode]["semantics"]
        for version in executables
        for mode in CASES
    }
    effects: dict[str, dict[str, object]] = {}
    for version in executables:
        for left_mode, right_mode, min_pressure, min_tl in (
            (
                "C", "I", MINIMUM_C_I_PRESSURE_EFFECT,
                MINIMUM_C_I_MEDIAN_TL_DB,
            ),
            (
                "I", "S", MINIMUM_I_S_PRESSURE_EFFECT,
                MINIMUM_I_S_MEDIAN_TL_DB,
            ),
        ):
            left = loaded[version][left_mode]["field"]
            right = loaded[version][right_mode]["field"]
            require_header_equal(left, right, f"{version}/{left_mode}-{right_mode}")
            metrics = mode_effect_metrics(left.pressure, right.pressure)
            if metrics["max_pressure_absolute_difference"] <= min_pressure:
                raise ValueError(
                    f"{version}/{left_mode}-{right_mode}: pressure effect absent"
                )
            if metrics["median_tl_difference_db"] <= min_tl:
                raise ValueError(
                    f"{version}/{left_mode}-{right_mode}: TL effect absent"
                )
            effects[f"{version}/{left_mode}-{right_mode}"] = {
                "passed": True,
                **metrics,
            }

    field_paths = {
        version: [loaded[version][mode]["shade"] for mode in CASES]
        for version in executables
    }
    return {
        "schema": "bellhop.f2cpp.i7_coherence_modes_validation",
        "schema_version": 1,
        "status": "passed",
        "matrix": {
            "modes": CASES,
            "frequency_hz": FREQUENCY_HZ,
            "launch_count": LAUNCH_COUNT,
            "source_geometry": "point",
            "c_i_minimum_pressure_effect": MINIMUM_C_I_PRESSURE_EFFECT,
            "c_i_minimum_median_tl_db": MINIMUM_C_I_MEDIAN_TL_DB,
            "i_s_minimum_pressure_effect": MINIMUM_I_S_PRESSURE_EFFECT,
            "i_s_minimum_median_tl_db": MINIMUM_I_S_MEDIAN_TL_DB,
        },
        "origin_f2cpp_field_comparisons": comparisons,
        "final_shd_mode_semantics": semantics,
        "independent_mode_effect_guards": effects,
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
            "coherence_mode_env_inputs_pairwise_distinct": True,
            "coherence_mode_env_inputs_otherwise_identical": True,
            "env_and_prt_mode_identity_bound": True,
            "final_shd_semantics_bound": True,
            "origin_f2cpp_comparison_count": len(comparisons),
            "final_shd_semantics_count": len(semantics),
            "effect_guard_count": len(effects),
        },
        "generation": {
            "case_commands": generation_commands(),
            "validator_command": (
                "python3 test/standard_cases/codes/"
                "validate_i7_coherence_modes.py "
                "--results-root <results-root> "
                "--origin-executable Bellhop_origin/bin/bellhop "
                "--f2cpp-executable "
                "Bellhop_F2CPP/build/release/bellhop_f2cpp "
                "--output Bellhop_F2CPP/doc/validation/"
                "i7_coherence_modes_report.json"
            ),
        },
        "sha256": {
            "rendered_coherence_mode_env": environment_hashes["origin"],
            "normalized_coherence_mode_env": normalized_hashes["origin"],
            "origin_field_aggregate": aggregate_sha256(field_paths["origin"]),
            "f2cpp_field_aggregate": aggregate_sha256(field_paths["f2cpp"]),
            "origin_prt": {
                mode: sha256(loaded["origin"][mode]["print"])
                for mode in CASES
            },
            "f2cpp_prt": {
                mode: sha256(loaded["f2cpp"][mode]["print"])
                for mode in CASES
            },
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
