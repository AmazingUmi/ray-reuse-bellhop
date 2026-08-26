#!/usr/bin/env python3
"""Validate ray-centered Cerveny P/V/H against a Cartesian P control."""

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
    "CC/P": ("cartesian_component_pressure", "CC", "P"),
    "CR/P": ("ray_centered_component_pressure", "CR", "P"),
    "CR/V": ("ray_centered_component_vertical", "CR", "V"),
    "CR/H": ("ray_centered_component_horizontal", "CR", "H"),
}
PROFILE = "single"
FREQUENCY_HZ = 1000.0
LAUNCH_COUNT = 300
EXPECTED_DIMENSIONS = (1, 1, 1, 1, 1, 7, 21)
PRESSURE_MASK_FLOOR = 1.0e-7
MINIMUM_FAMILY_OR_COMPONENT_EFFECT = 1.0e-3
FAMILY_LINE = re.compile(
    r"^\s*['\"](C[CR])['\"]\s*(?:!.*)?$", re.MULTILINE
)
COMPONENT_LINE = re.compile(
    r"^\s*\d+\s+\d+\s+['\"]([PVH])['\"]\s*(?:!.*)?$",
    re.MULTILINE,
)
PRT_COMPONENT_LINE = re.compile(
    r"^\s*Component\s*=\s*([PVH])\s*$", re.MULTILINE
)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def aggregate_sha256(paths: list[Path]) -> str:
    digest = hashlib.sha256()
    for path in sorted(paths, key=lambda item: str(item)):
        digest.update(path.read_bytes())
    return digest.hexdigest()


def parse_family_and_component(
    contents: str, source: Path | str
) -> tuple[str, str]:
    families = FAMILY_LINE.findall(contents)
    components = COMPONENT_LINE.findall(contents)
    if len(families) != 1 or len(components) != 1:
        raise ValueError(
            f"{source}: expected exactly one CC/CR family and P/V/H component"
        )
    return families[0], components[0]


def normalize_family_and_component(
    contents: str, source: Path | str
) -> str:
    parse_family_and_component(contents, source)
    normalized = FAMILY_LINE.sub(
        lambda match: match.group(0).replace(
            f"'{match.group(1)}'", "'<FAMILY>'"
        ).replace(
            f'"{match.group(1)}"', '"<FAMILY>"'
        ),
        contents,
    )
    return COMPONENT_LINE.sub(
        lambda match: match.group(0).replace(
            f"'{match.group(1)}'", "'<COMPONENT>'"
        ).replace(
            f'"{match.group(1)}"', '"<COMPONENT>"'
        ),
        normalized,
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
    mask = (
        (reference_magnitude > PRESSURE_MASK_FLOOR)
        & (candidate_magnitude > PRESSURE_MASK_FLOOR)
    )
    if not np.any(mask):
        raise ValueError("fields have no common values above TL floor")
    tl_difference = np.abs(
        -20.0 * np.log10(reference_magnitude[mask])
        + 20.0 * np.log10(candidate_magnitude[mask])
    )
    return {
        "max_pressure_absolute_difference": float(
            np.max(np.abs(reference_pressure - candidate_pressure))
        ),
        "minimum_tl_difference_db": float(np.min(tl_difference)),
        "median_tl_difference_db": float(np.median(tl_difference)),
        "maximum_tl_difference_db": float(np.max(tl_difference)),
        "tl_mask_count": int(np.count_nonzero(mask)),
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
        "CASE ( 'C' )",
        "Cartesian beams",
        "CASE ( 'R' )",
        "Ray centered beams",
    ):
        if anchor not in run_type:
            raise ValueError(f"Origin beam-family parser anchor absent: {anchor}")

    core = text["bellhop_core"]
    dispatch_start = core.find(
        "SELECT CASE ( Beam%Type( 1 : 1 ) )"
    )
    dispatch_end = core.find("END SELECT", dispatch_start)
    if dispatch_start < 0 or dispatch_end < 0:
        raise ValueError("Origin influence dispatch is absent")
    dispatch = core[dispatch_start:dispatch_end]
    for anchor in (
        "CASE ( 'R' )",
        "CALL InfluenceCervenyRayCen",
        "CASE ( 'C' )",
        "CALL InfluenceCervenyCart",
    ):
        if anchor not in dispatch:
            raise ValueError(f"Origin influence dispatch anchor absent: {anchor}")

    influence_source = text["influence"]
    ray_start = influence_source.find("SUBROUTINE InfluenceCervenyRayCen")
    ray_end = influence_source.find(
        "END SUBROUTINE InfluenceCervenyRayCen", ray_start
    )
    if ray_start < 0 or ray_end < 0:
        raise ValueError("Origin InfluenceCervenyRayCen routine is absent")
    ray_centered = influence_source[ray_start:ray_end]
    for anchor in (
        "CASE ( 'P' )   ! pressure",
        "CASE ( 'V' )   ! vertical component",
        "P_n    = -i * omega * gamma * n * contri",
        "P_s    = -i * omega / c         * contri",
        "contri = c * DOT_PRODUCT( [ P_n, P_s ], ray2D( iS )%t )",
        "CASE ( 'H' )   ! horizontal component",
        "contri = c * ( -P_n * ray2D( iS )%t( 2 ) + P_s * ray2D( iS )%t( 1 ) )",
    ):
        if anchor not in ray_centered:
            raise ValueError(f"Origin ray-centered formula anchor absent: {anchor}")
    return {
        "read_run_type_has_cartesian_and_ray_centered": True,
        "bellhop_dispatches_c_and_r_influence": True,
        "ray_centered_has_p_v_h_formulas": True,
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
    case_id, family, component = CASES[key]
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
    if parse_family_and_component(
        environment_contents, paths["environment"]
    ) != (family, component):
        raise ValueError(f"{manifest_path}: ENV family/component mismatch")
    print_contents = paths["print"].read_text(errors="replace")
    family_marker = (
        "Ray centered beams" if family == "CR" else "Cartesian beams"
    )
    opposite_marker = (
        "Cartesian beams" if family == "CR" else "Ray centered beams"
    )
    if print_contents.count(family_marker) != 1:
        raise ValueError(f"{manifest_path}: PRT beam family mismatch")
    if opposite_marker in print_contents:
        raise ValueError(f"{manifest_path}: PRT has opposite beam family")
    if PRT_COMPONENT_LINE.findall(print_contents) != [component]:
        raise ValueError(f"{manifest_path}: PRT component mismatch")

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
    }
    if include_rayreuse:
        executables["rayreuse"] = (
            "Bellhop_RayReuse/build/release/bellhop_rayreuse"
        )
    return [
        "python3 test/standard_cases/codes/standard_cases.py test "
        f"--version {version} --case {case_id} --profile single "
        f"--executable {executables[version]} "
        "--results-root <results-root>"
        for version in executables
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
        raise ValueError("implementation executable paths must differ")
    if not all(path.is_file() for path in executables.values()):
        raise ValueError("an expected executable does not exist")
    executable_hashes = {
        version: sha256(path) for version, path in executables.items()
    }
    if len(set(executable_hashes.values())) != len(executable_hashes):
        raise ValueError("implementation executable hashes must differ")

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
            raise ValueError(f"{version}: family/component ENV files not distinct")
        normalized = {
            normalize_family_and_component(
                str(loaded[version][key]["environment_contents"]),
                loaded[version][key]["environment"],
            )
            for key in CASES
        }
        if len(normalized) != 1:
            raise ValueError(
                f"{version}: ENV files differ beyond family/component"
            )
        normalized_hashes[version] = hashlib.sha256(
            normalized.pop().encode("utf-8")
        ).hexdigest()
    if any(
        hashes != environment_hashes["origin"]
        for hashes in environment_hashes.values()
    ):
        raise ValueError("rendered ENV inputs differ across implementations")
    if any(
        digest != normalized_hashes["origin"]
        for digest in normalized_hashes.values()
    ):
        raise ValueError("normalized ENV identities differ")

    tolerance_path = STANDARD_CASES_ROOT / "codes" / "tolerances.toml"
    comparisons: dict[str, dict[str, object]] = {}
    comparison_pairs = [("origin", "f2cpp")]
    if "rayreuse" in executables:
        comparison_pairs.extend(
            [("origin", "rayreuse"), ("f2cpp", "rayreuse")]
        )
    for left_version, right_version in comparison_pairs:
        for key, (case_id, _, _) in CASES.items():
            passed, metrics = compare_files(
                loaded[left_version][key]["shade"],
                loaded[right_version][key]["shade"],
                0,
                0,
                tolerance_path,
            )
            label = f"{left_version}_vs_{right_version}/{key}"
            if not passed:
                raise ValueError(f"{case_id} {label} mismatch: {metrics}")
            comparisons[label] = {"passed": True, **metrics}

    effect_pairs = (
        ("CC/P", "CR/P"),
        ("CR/P", "CR/V"),
        ("CR/P", "CR/H"),
        ("CR/V", "CR/H"),
    )
    effects: dict[str, dict[str, object]] = {}
    for version in executables:
        for left_key, right_key in effect_pairs:
            left = loaded[version][left_key]["field"]
            right = loaded[version][right_key]["field"]
            require_header_equal(left, right, f"{version}/{left_key}-{right_key}")
            metrics = effect_metrics(left.pressure, right.pressure)
            if metrics["max_pressure_absolute_difference"] <= (
                MINIMUM_FAMILY_OR_COMPONENT_EFFECT
            ):
                raise ValueError(
                    f"{version}/{left_key}-{right_key}: effect is absent"
                )
            effects[f"{version}/{left_key}-{right_key}"] = {
                "passed": True,
                **metrics,
            }

    field_paths = {
        version: [loaded[version][key]["shade"] for key in CASES]
        for version in executables
    }
    return {
        "schema": "bellhop.fp1h.ray_centered_components_validation",
        "schema_version": 2,
        "status": "passed",
        "matrix": {
            "cases": {
                key: {
                    "case_id": value[0],
                    "family": value[1],
                    "component": value[2],
                }
                for key, value in CASES.items()
            },
            "frequency_hz": FREQUENCY_HZ,
            "launch_count": LAUNCH_COUNT,
            "minimum_family_or_component_effect": (
                MINIMUM_FAMILY_OR_COMPONENT_EFFECT
            ),
        },
        "field_comparisons": comparisons,
        "independent_family_component_effect_guards": effects,
        "field_summaries": {
            f"{version}/{key}": {
                "nonzero_count": loaded[version][key]["nonzero_count"],
                "maximum_magnitude": loaded[version][key]["maximum_magnitude"],
            }
            for version in executables
            for key in CASES
        },
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
            "rendered_env_inputs_equal_across_implementations": True,
            "family_component_env_inputs_pairwise_distinct": True,
            "family_component_env_inputs_otherwise_identical": True,
            "env_and_prt_family_component_identity_bound": True,
            "field_comparison_count": len(comparisons),
            "effect_guard_count": len(effects),
        },
        "generation": {
            "case_commands": generation_commands(
                include_rayreuse="rayreuse" in executables
            ),
            "validator_command": (
                "python3 test/standard_cases/codes/"
                "validate_i7_ray_centered_components.py "
                "--results-root <results-root> "
                "--origin-executable Bellhop_origin/bin/bellhop "
                "--f2cpp-executable "
                "Bellhop_F2CPP/build/release/bellhop_f2cpp "
                + (
                    "--rayreuse-executable "
                    "Bellhop_RayReuse/build/release/bellhop_rayreuse "
                    if "rayreuse" in executables
                    else ""
                )
                + "--output Bellhop_F2CPP/doc/reports/validation/"
                "i7_ray_centered_components_report.json"
            ),
        },
        "sha256": {
            "rendered_family_component_env": environment_hashes["origin"],
            "normalized_family_component_env": normalized_hashes["origin"],
            "origin_field_aggregate": aggregate_sha256(field_paths["origin"]),
            "f2cpp_field_aggregate": aggregate_sha256(field_paths["f2cpp"]),
            **(
                {
                    "rayreuse_field_aggregate": aggregate_sha256(
                        field_paths["rayreuse"]
                    )
                }
                if "rayreuse" in field_paths
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
                if "rayreuse" in loaded
                else {}
            ),
        },
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--results-root", type=Path, required=True)
    parser.add_argument("--origin-executable", type=Path, required=True)
    parser.add_argument("--f2cpp-executable", type=Path, required=True)
    parser.add_argument("--rayreuse-executable", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = validate(
        args.results_root,
        args.origin_executable,
        args.f2cpp_executable,
        args.rayreuse_executable,
    )
    rendered = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.output is not None:
        args.output.write_text(rendered, encoding="utf-8")
    print(rendered, end="")


if __name__ == "__main__":
    main()
