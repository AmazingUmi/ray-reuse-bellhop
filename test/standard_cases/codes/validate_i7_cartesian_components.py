#!/usr/bin/env python3
"""Validate the Origin-compatible Cartesian Cerveny P/V/H contract."""

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
    "P": "cartesian_component_pressure",
    "V": "cartesian_component_vertical",
    "H": "cartesian_component_horizontal",
}
PROFILE = "single"
FREQUENCY_HZ = 1000.0
EXPECTED_SOURCE_REFERENCES = (
    "Bellhop_origin/Bellhop/ReadEnvironmentBell.f90 component input",
    "Bellhop_origin/Bellhop/influence.f90 "
    "InfluenceCervenyCart legacy component contract",
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


def parse_component(contents: str, source: Path | str) -> str:
    components = COMPONENT_LINE.findall(contents)
    if len(components) != 1:
        raise ValueError(
            f"{source}: expected exactly one P/V/H component record"
        )
    return components[0]


def normalize_component(contents: str, source: Path | str) -> str:
    parse_component(contents, source)
    return COMPONENT_LINE.sub(
        lambda match: match.group(0).replace(
            f"'{match.group(1)}'", "'<COMPONENT>'"
        ).replace(
            f'"{match.group(1)}"', '"<COMPONENT>"'
        ),
        contents,
    )


def validate_legacy_source_contract() -> dict[str, str | bool]:
    read_environment = (
        PROJECT_ROOT / "Bellhop_origin" / "Bellhop" /
        "ReadEnvironmentBell.f90"
    )
    influence = (
        PROJECT_ROOT / "Bellhop_origin" / "Bellhop" / "influence.f90"
    )
    parser_source = read_environment.read_text(encoding="utf-8")
    influence_source = influence.read_text(encoding="utf-8")
    parser_anchor = (
        "READ(  ENVFile, * ) Beam%Nimage, Beam%iBeamWindow, "
        "Beam%Component"
    )
    if parser_anchor not in parser_source:
        raise ValueError("Origin no longer parses the Cerveny component record")

    cartesian_start = influence_source.find(
        "SUBROUTINE InfluenceCervenyCart"
    )
    cartesian_end = influence_source.find(
        "END SUBROUTINE InfluenceCervenyCart", cartesian_start
    )
    if cartesian_start < 0 or cartesian_end < 0:
        raise ValueError("Origin Cartesian Cerveny routine is absent")
    cartesian_source = influence_source[cartesian_start:cartesian_end]
    if "Beam%Component" in cartesian_source:
        raise ValueError(
            "Origin Cartesian Cerveny now applies component semantics; "
            "the legacy compatibility oracle must be redesigned"
        )

    ray_centered_start = influence_source.find(
        "SUBROUTINE InfluenceCervenyRayCen"
    )
    ray_centered_end = influence_source.find(
        "END SUBROUTINE InfluenceCervenyRayCen", ray_centered_start
    )
    if ray_centered_start < 0 or ray_centered_end < 0:
        raise ValueError("Origin ray-centered Cerveny routine is absent")
    ray_centered_source = influence_source[
        ray_centered_start:ray_centered_end
    ]
    for anchor in (
        "SELECT CASE ( Beam%Component )",
        "CASE ( 'V' )",
        "CASE ( 'H' )",
    ):
        if anchor not in ray_centered_source:
            raise ValueError(
                "Origin ray-centered component contrast anchor is absent"
            )

    return {
        "component_record_is_parsed": True,
        "cartesian_influence_ignores_component": True,
        "ray_centered_influence_has_v_h_branches": True,
        "read_environment_sha256": sha256(read_environment),
        "influence_sha256": sha256(influence),
    }


def require_current(
    artifact: Path, executable: Path, manifest_path: Path
) -> None:
    if artifact.stat().st_mtime_ns < executable.stat().st_mtime_ns:
        raise ValueError(
            f"{manifest_path}: {artifact.name} predates executable"
        )


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
    if any(getattr(left, name) != getattr(right, name)
           for name in scalar_fields):
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
    component: str,
    executable: Path,
) -> dict[str, object]:
    case_id = CASES[component]
    definitions = discover_cases(STANDARD_CASES_ROOT / "cases")
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
        EXPECTED_SOURCE_REFERENCES
    ):
        raise ValueError(f"{manifest_path}: source references mismatch")
    if tuple(float(value) for value in manifest.get("frequencies_hz", ())) != (
        FREQUENCY_HZ,
    ):
        raise ValueError(f"{manifest_path}: frequency grid mismatch")
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

    launch_count = definition.shared_launch_angle_count((FREQUENCY_HZ,))
    expected_environment = definition.render_origin_environment(
        FREQUENCY_HZ, launch_count
    )
    environment_contents = paths["environment"].read_text(encoding="utf-8")
    if environment_contents != expected_environment:
        raise ValueError(f"{manifest_path}: staged ENV differs from fixture")
    if parse_component(environment_contents, paths["environment"]) != component:
        raise ValueError(f"{manifest_path}: ENV component mismatch")

    print_contents = paths["print"].read_text(errors="replace")
    prt_components = PRT_COMPONENT_LINE.findall(print_contents)
    if prt_components != [component]:
        raise ValueError(f"{manifest_path}: PRT component mismatch")

    field = ShdReader(paths["shade"]).read()
    if field.header.dimensions != (1, 1, 1, 1, 1, 7, 21):
        raise ValueError(f"{manifest_path}: unexpected SHD dimensions")
    maximum_magnitude = float(np.max(np.abs(field.pressure)))
    if not np.isfinite(maximum_magnitude) or maximum_magnitude <= 0.0:
        raise ValueError(f"{manifest_path}: component field is zero/non-finite")
    return {
        "manifest": manifest_path,
        "environment": paths["environment"],
        "print": paths["print"],
        "shade": paths["shade"],
        "environment_contents": environment_contents,
        "field": field,
        "maximum_magnitude": maximum_magnitude,
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
    source_contract = validate_legacy_source_contract()
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

    loaded: dict[str, dict[str, dict[str, object]]] = {
        version: {
            component: load_run(
                results_root, version, component, executables[version]
            )
            for component in CASES
        }
        for version in executables
    }

    environment_hashes: dict[str, dict[str, str]] = {}
    normalized_hashes: dict[str, str] = {}
    equivalence: dict[str, dict[str, object]] = {}
    for version in executables:
        environment_hashes[version] = {
            component: sha256(loaded[version][component]["environment"])
            for component in CASES
        }
        if len(set(environment_hashes[version].values())) != len(CASES):
            raise ValueError(f"{version}: component ENV files are not distinct")
        normalized = {
            normalize_component(
                str(loaded[version][component]["environment_contents"]),
                loaded[version][component]["environment"],
            )
            for component in CASES
        }
        if len(normalized) != 1:
            raise ValueError(
                f"{version}: component ENV files differ beyond component token"
            )
        normalized_hashes[version] = hashlib.sha256(
            normalized.pop().encode("utf-8")
        ).hexdigest()

        pressure = loaded[version]["P"]["field"]
        shade_hashes = {
            component: sha256(loaded[version][component]["shade"])
            for component in CASES
        }
        if len(set(shade_hashes.values())) != 1:
            raise ValueError(f"{version}: legacy P/V/H SHD bytes differ")
        for component in ("V", "H"):
            candidate = loaded[version][component]["field"]
            require_header_equal(
                pressure, candidate, f"{version}/P={component}"
            )
            if not np.array_equal(pressure.pressure, candidate.pressure):
                raise ValueError(
                    f"{version}: legacy P/{component} field arrays differ"
                )
        equivalence[version] = {
            "passed": True,
            "p_v_h_shd_sha256": next(iter(shade_hashes.values())),
            "maximum_pressure_magnitude": loaded[version]["P"][
                "maximum_magnitude"
            ],
            "semantic_arrays_exactly_equal": True,
            "shd_files_byte_identical": True,
        }

    if environment_hashes["origin"] != environment_hashes["f2cpp"]:
        raise ValueError("Origin and F2CPP rendered component ENV files differ")
    if normalized_hashes["origin"] != normalized_hashes["f2cpp"]:
        raise ValueError("Origin and F2CPP normalized ENV identities differ")

    comparisons: dict[str, dict[str, float | bool]] = {}
    tolerance_path = STANDARD_CASES_ROOT / "codes" / "tolerances.toml"
    for component, case_id in CASES.items():
        passed, metrics = compare_files(
            loaded["origin"][component]["shade"],
            loaded["f2cpp"][component]["shade"],
            0,
            0,
            tolerance_path,
        )
        if not passed:
            raise ValueError(f"{case_id} Origin/F2CPP mismatch: {metrics}")
        comparisons[component] = {"passed": True, **metrics}

    field_paths = {
        version: [loaded[version][component]["shade"] for component in CASES]
        for version in executables
    }
    return {
        "schema": "bellhop.f2cpp.i7_cartesian_component_legacy_validation",
        "schema_version": 1,
        "status": "passed",
        "contract": {
            "scope": "2-D coherent Cartesian Cerveny",
            "behavior": "Origin legacy component selector is parsed but ignored",
            "components": CASES,
            "frequency_hz": FREQUENCY_HZ,
            "shd_component_metadata_present": False,
            "component_identity_bound_to_env_and_prt": True,
        },
        "origin_f2cpp_field_comparisons": comparisons,
        "legacy_component_equivalence_guards": equivalence,
        "origin_source_contract": source_contract,
        "provenance_guards": {
            "distinct_executable_paths": True,
            "distinct_executable_hashes": True,
            "results_not_older_than_executables": True,
            "manifest_executable_identity_bound": True,
            "manifest_source_references_bound": True,
            "staged_env_matches_canonical_fixture": True,
            "origin_f2cpp_rendered_env_inputs_equal": True,
            "component_env_inputs_pairwise_distinct": True,
            "component_env_inputs_otherwise_identical": True,
            "env_component_identity_bound": True,
            "prt_component_identity_bound": True,
            "nonzero_field_guard": True,
            "origin_f2cpp_comparison_count": len(comparisons),
        },
        "generation": {
            "case_commands": generation_commands(),
            "validator_command": (
                "python3 test/standard_cases/codes/"
                "validate_i7_cartesian_components.py "
                "--results-root <results-root> "
                "--origin-executable Bellhop_origin/bin/bellhop "
                "--f2cpp-executable "
                "Bellhop_F2CPP/build/release/bellhop_f2cpp "
                "--output Bellhop_F2CPP/doc/reports/validation/"
                "i7_cartesian_components_report.json"
            ),
        },
        "sha256": {
            "origin_executable": executable_hashes["origin"],
            "f2cpp_executable": executable_hashes["f2cpp"],
            "rendered_component_env": environment_hashes["origin"],
            "normalized_component_env": normalized_hashes["origin"],
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
