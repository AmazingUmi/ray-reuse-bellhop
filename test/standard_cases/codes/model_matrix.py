"""Run and compare the local Fortran, F2CPP and RayReuse model matrix."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import platform
import subprocess
import sys
import tempfile
from typing import Sequence


CODES_ROOT = Path(__file__).resolve().parent
STANDARD_CASES_ROOT = CODES_ROOT.parent
PROJECT_ROOT = STANDARD_CASES_ROOT.parents[1]
sys.path.insert(0, str(CODES_ROOT))

from case_model import CaseDefinition, discover_cases
from compare_fields import compare_files, decoded_complex64_payload
from reference_snapshots import sha256_file, validate_candidate
from standard_cases import (
    RAYREUSE_EXECUTION_MODES,
    VersionAdapter,
    default_adapters,
    process_case,
    select_cases,
)


SCHEMA = "bellhop.standard_case.model_matrix"
SCHEMA_VERSION = 1
SUPPORTED_PROFILES = ("single", "broadband_smoke")

# FP-2D-R1: the established 0.005 dB Munk-spline oracle remains unchanged
# everywhere except the reviewed Origin-to-C++ 250 Hz comparison.  Keep this
# policy in the matrix routing layer so it cannot widen a whole case, frequency,
# or C++-to-C++ comparison accidentally.
MUNK_SPLINE_ORIGIN_CPP_250_HZ = 250.0
MUNK_SPLINE_ORIGIN_CPP_250_HZ_TL_DB = 0.0065


@dataclass(frozen=True)
class OutputSlice:
    frequency_hz: float
    frequency_index: int
    shade_path: Path


def scoped_tl_absolute_db(
    *,
    case_id: str | None,
    reference_label: str,
    candidate_label: str,
    frequency_hz: float,
) -> float | None:
    cpp_candidate = candidate_label == "f2cpp" or candidate_label.startswith(
        "rayreuse-"
    )
    if (
        case_id == "munk_spline"
        and reference_label == "origin"
        and cpp_candidate
        and frequency_hz == MUNK_SPLINE_ORIGIN_CPP_250_HZ
    ):
        return MUNK_SPLINE_ORIGIN_CPP_250_HZ_TL_DB
    return None


def compare_decoded_payloads(
    *,
    reference_label: str,
    candidate_label: str,
    frequency_hz: float,
    reference: OutputSlice,
    candidate: OutputSlice,
) -> dict[str, object]:
    reference_payload = decoded_complex64_payload(
        reference.shade_path, reference.frequency_index
    )
    candidate_payload = decoded_complex64_payload(
        candidate.shade_path, candidate.frequency_index
    )
    return {
        "reference": reference_label,
        "candidate": candidate_label,
        "frequency_hz": frequency_hz,
        "encoding": "little-endian-complex64-c-order",
        "reference_bytes": len(reference_payload),
        "candidate_bytes": len(candidate_payload),
        "reference_sha256": sha256_bytes(reference_payload),
        "candidate_sha256": sha256_bytes(candidate_payload),
        "passed": reference_payload == candidate_payload,
    }


def sha256_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def load_manifest_slices(manifest_path: Path) -> dict[float, OutputSlice]:
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("schema_version") != 1:
        raise ValueError(f"{manifest_path}: unsupported manifest schema")
    if manifest.get("output_kind", "shd") != "shd":
        raise ValueError(f"{manifest_path}: model matrix requires SHD output")
    broadband = (
        manifest.get("execution_model") == "single_broadband_invocation"
    )
    records = manifest.get("runs")
    if not isinstance(records, list) or not records:
        raise ValueError(f"{manifest_path}: manifest has no runs")

    slices: dict[float, OutputSlice] = {}
    for record in records:
        if not isinstance(record, dict) or record.get("status") != "passed":
            raise ValueError(f"{manifest_path}: every run must be passed")
        shade_name = record.get("shade_file")
        if not isinstance(shade_name, str) or not shade_name:
            raise ValueError(f"{manifest_path}: run lacks shade_file")
        frequency_hz = float(record["frequency_hz"])
        if frequency_hz in slices:
            raise ValueError(
                f"{manifest_path}: duplicate frequency {frequency_hz}"
            )
        shade_path = manifest_path.parent / shade_name
        if not shade_path.is_file():
            raise FileNotFoundError(f"candidate SHD does not exist: {shade_path}")
        slices[frequency_hz] = OutputSlice(
            frequency_hz=frequency_hz,
            frequency_index=(int(record["frequency_index"]) if broadband else 0),
            shade_path=shade_path,
        )
    return slices


def resolve_tolerances_path(
    definition: CaseDefinition, explicit_path: Path | None
) -> Path:
    """Resolve an explicit matrix override or the case's established policy."""
    if explicit_path is not None:
        return explicit_path.resolve()
    case_path = definition.directory / "tolerances.toml"
    if case_path.is_file():
        return case_path.resolve()
    return (CODES_ROOT / "tolerances.toml").resolve()


def compare_slice_sets(
    *,
    reference_label: str,
    candidate_label: str,
    reference_slices: dict[float, OutputSlice],
    candidate_slices: dict[float, OutputSlice],
    tolerances_path: Path,
    case_id: str | None = None,
    gating_frequencies: set[float] | None = None,
    non_gating_reason: str | None = None,
) -> list[dict[str, object]]:
    if set(reference_slices) != set(candidate_slices):
        raise ValueError(
            f"frequency mismatch for {reference_label} vs {candidate_label}"
        )
    results: list[dict[str, object]] = []
    for frequency_hz in sorted(reference_slices):
        reference = reference_slices[frequency_hz]
        candidate = candidate_slices[frequency_hz]
        tl_absolute_db = scoped_tl_absolute_db(
            case_id=case_id,
            reference_label=reference_label,
            candidate_label=candidate_label,
            frequency_hz=frequency_hz,
        )
        passed, metrics = compare_files(
            reference.shade_path,
            candidate.shade_path,
            reference.frequency_index,
            candidate.frequency_index,
            tolerances_path,
            transmission_loss_absolute_db=tl_absolute_db,
        )
        results.append(
            {
                "reference": reference_label,
                "candidate": candidate_label,
                "frequency_hz": frequency_hz,
                "metrics": metrics,
                "tolerance_policy": (
                    "munk_spline_origin_cpp_250hz"
                    if tl_absolute_db is not None
                    else "default"
                ),
                "gating": (
                    gating_frequencies is None
                    or frequency_hz in gating_frequencies
                ),
                "non_gating_reason": (
                    None
                    if gating_frequencies is None
                    or frequency_hz in gating_frequencies
                    else non_gating_reason
                ),
                "passed": passed,
            }
        )
    return results


def run_case_profile(
    *,
    definition: CaseDefinition,
    profile_name: str,
    adapters: dict[str, VersionAdapter],
    modes: tuple[str, ...],
    work_root: Path,
    tolerances_path: Path,
    reference_root: Path,
) -> dict[str, object]:
    if profile_name not in SUPPORTED_PROFILES:
        raise ValueError(f"unsupported matrix profile {profile_name!r}")
    if profile_name not in definition.profiles:
        raise ValueError(
            f"{definition.case_id}: profile {profile_name!r} is not defined"
        )

    manifests: dict[str, Path] = {}
    for version in ("origin", "f2cpp"):
        manifests[version] = process_case(
            definition,
            profile_name,
            adapters[version],
            "test",
            work_root / "solver-results",
        )

    rayreuse_labels: list[str] = []
    if profile_name == "single":
        label = "rayreuse-single"
        rayreuse_labels.append(label)
        manifests[label] = process_case(
            definition,
            profile_name,
            adapters["rayreuse"],
            "test",
            work_root / label,
        )
    else:
        for mode in modes:
            label = f"rayreuse-{mode}"
            rayreuse_labels.append(label)
            manifests[label] = process_case(
                definition,
                profile_name,
                adapters["rayreuse"],
                "test",
                work_root / label,
                mode,
            )

    slices = {
        label: load_manifest_slices(path)
        for label, path in manifests.items()
    }
    comparisons: list[dict[str, object]] = []
    f2cpp_gating_frequencies = None
    f2cpp_non_gating_reason = None
    if profile_name != "single":
        f2cpp_gating_frequencies = {
            max(definition.frequencies(profile_name))
        }
        f2cpp_non_gating_reason = (
            "F2CPP D-02 replans from the current single frequency; "
            "only fmax matches the shared-fmax launch fan"
        )
    for candidate_label in ("f2cpp", *rayreuse_labels):
        comparisons.extend(
            compare_slice_sets(
                reference_label="origin",
                candidate_label=candidate_label,
                reference_slices=slices["origin"],
                candidate_slices=slices[candidate_label],
                tolerances_path=tolerances_path,
                case_id=definition.case_id,
                gating_frequencies=(
                    f2cpp_gating_frequencies
                    if candidate_label == "f2cpp"
                    else None
                ),
                non_gating_reason=(
                    f2cpp_non_gating_reason
                    if candidate_label == "f2cpp"
                    else None
                ),
            )
        )
    for candidate_label in rayreuse_labels:
        comparisons.extend(
            compare_slice_sets(
                reference_label="f2cpp",
                candidate_label=candidate_label,
                reference_slices=slices["f2cpp"],
                candidate_slices=slices[candidate_label],
                tolerances_path=tolerances_path,
                case_id=definition.case_id,
                gating_frequencies=f2cpp_gating_frequencies,
                non_gating_reason=f2cpp_non_gating_reason,
            )
        )

    payload_exact_results: list[dict[str, object]] = []
    if (
        definition.case_id == "munk_spline"
        and MUNK_SPLINE_ORIGIN_CPP_250_HZ in slices["f2cpp"]
    ):
        frequency_hz = MUNK_SPLINE_ORIGIN_CPP_250_HZ
        for candidate_label in rayreuse_labels:
            payload_exact_results.append(
                compare_decoded_payloads(
                    reference_label="f2cpp",
                    candidate_label=candidate_label,
                    frequency_hz=frequency_hz,
                    reference=slices["f2cpp"][frequency_hz],
                    candidate=slices[candidate_label][frequency_hz],
                )
            )

    compact_results: list[dict[str, object]] = []
    if profile_name == "single":
        compact_reference = (
            reference_root / f"{definition.case_id}.json"
        )
        for label in ("origin", "f2cpp", *rayreuse_labels):
            output = next(iter(slices[label].values()))
            passed, report = validate_candidate(
                compact_reference,
                output.shade_path,
                output.frequency_index,
                tolerances_path,
            )
            compact_results.append(
                {
                    "candidate": label,
                    "passed": passed,
                    "compared": report["compared"],
                    "maxima": report["maxima"],
                    "failures": report["failures"],
                }
            )

    cross_mode: dict[str, object] | None = None
    if len(rayreuse_labels) > 1:
        hashes = {
            label: sha256_file(next(iter(slices[label].values())).shade_path)
            for label in rayreuse_labels
        }
        cross_mode = {
            "sha256": hashes,
            "identical": len(set(hashes.values())) == 1,
        }

    passed = all(
        bool(result["passed"])
        for result in comparisons
        if bool(result["gating"])
    )
    passed = passed and all(
        bool(result["passed"]) for result in compact_results
    )
    if cross_mode is not None:
        passed = passed and bool(cross_mode["identical"])
    passed = passed and all(
        bool(result["passed"]) for result in payload_exact_results
    )
    return {
        "case_id": definition.case_id,
        "profile": profile_name,
        "tolerances_path": str(tolerances_path),
        "frequencies_hz": list(definition.frequencies(profile_name)),
        "comparisons": comparisons,
        "compact_reference_comparisons": compact_results,
        "rayreuse_cross_mode": cross_mode,
        "f2cpp_rayreuse_payload_exact": payload_exact_results,
        "f2cpp_broadband_scope": (
            None
            if profile_name == "single"
            else {
                "gating_frequencies_hz": sorted(
                    f2cpp_gating_frequencies or set()
                ),
                "reason": f2cpp_non_gating_reason,
            }
        ),
        "passed": passed,
    }


def git_identity() -> dict[str, object]:
    revision = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=PROJECT_ROOT,
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()
    status = subprocess.run(
        ["git", "status", "--porcelain"],
        cwd=PROJECT_ROOT,
        check=True,
        capture_output=True,
        text=True,
    ).stdout
    return {"revision": revision, "dirty": bool(status)}


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Run the local three-model standard-case comparison matrix."
    )
    parser.add_argument("--case", action="append", dest="cases")
    parser.add_argument(
        "--profiles", default="single,broadband_smoke"
    )
    parser.add_argument(
        # Pinned explicitly: fused is out of model-matrix scope because the
        # matrix runs every case kind, while fused is only defined for
        # CC coherent TL broadband runs.
        "--modes",
        default="nonreuse,reuse,parallel",
    )
    parser.add_argument(
        "--work-root",
        type=Path,
        default=STANDARD_CASES_ROOT / "results" / "model_matrix",
    )
    parser.add_argument(
        "--report",
        type=Path,
        default=STANDARD_CASES_ROOT / "results" / "model_matrix.json",
    )
    parser.add_argument(
        "--tolerances",
        type=Path,
        help=(
            "override tolerances for every selected case; by default each "
            "case-local tolerances.toml is used when present, otherwise the "
            "shared codes/tolerances.toml"
        ),
    )
    parser.add_argument(
        "--reference-root",
        type=Path,
        default=(
            STANDARD_CASES_ROOT
            / "results"
            / "reference"
            / "origin"
            / "single"
        ),
    )
    parser.add_argument("--origin-executable", type=Path)
    parser.add_argument("--f2cpp-executable", type=Path)
    parser.add_argument("--rayreuse-executable", type=Path)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        profiles = tuple(
            value.strip()
            for value in args.profiles.split(",")
            if value.strip()
        )
        if not profiles or any(
            profile_name not in SUPPORTED_PROFILES
            for profile_name in profiles
        ):
            raise ValueError(
                "profiles must be single and/or broadband_smoke"
            )
        modes = tuple(
            value.strip() for value in args.modes.split(",") if value.strip()
        )
        if not modes or len(set(modes)) != len(modes) or any(
            mode not in RAYREUSE_EXECUTION_MODES for mode in modes
        ):
            raise ValueError(
                "modes must be unique values from "
                + ",".join(RAYREUSE_EXECUTION_MODES)
            )

        definitions = discover_cases(STANDARD_CASES_ROOT / "cases")
        selected, explicit = select_cases(definitions, args.cases)
        fully_supported = [
            definition
            for definition in selected
            if definition.output_kind == "shd" and all(
                version in definition.supported_versions
                for version in ("origin", "f2cpp", "rayreuse")
            )
        ]
        if explicit and len(fully_supported) != len(selected):
            unsupported = sorted(
                definition.case_id
                for definition in selected
                if definition not in fully_supported
            )
            raise ValueError(
                "model matrix requires SHD output and "
                "origin/f2cpp/rayreuse support: "
                + ", ".join(unsupported)
            )
        selected = fully_supported
        adapters = default_adapters(None)
        overrides = {
            "origin": args.origin_executable,
            "f2cpp": args.f2cpp_executable,
            "rayreuse": args.rayreuse_executable,
        }
        for name, override in overrides.items():
            if override is not None:
                adapters[name] = VersionAdapter(
                    name=name,
                    executable=override.resolve(),
                    enabled=True,
                )
            adapters[name].require_available()

        work_parent = args.work_root.resolve()
        work_parent.mkdir(parents=True, exist_ok=True)
        invocation_root = Path(
            tempfile.mkdtemp(prefix="run-", dir=work_parent)
        )

        results = [
            run_case_profile(
                definition=definition,
                profile_name=profile_name,
                adapters=adapters,
                modes=modes,
                work_root=invocation_root,
                tolerances_path=resolve_tolerances_path(
                    definition, args.tolerances
                ),
                reference_root=args.reference_root.resolve(),
            )
            for definition in selected
            for profile_name in profiles
            if profile_name in definition.profiles
        ]
        report = {
            "schema": SCHEMA,
            "schema_version": SCHEMA_VERSION,
            "git": git_identity(),
            "platform": platform.platform(),
            "executables": {
                name: {
                    "path": str(adapter.executable),
                    "sha256": sha256_file(adapter.executable),
                }
                for name, adapter in adapters.items()
            },
            "profiles": list(profiles),
            "rayreuse_modes": list(modes),
            "run_root": str(invocation_root),
            "results": results,
            "passed": bool(results)
            and all(bool(result["passed"]) for result in results),
        }
        report_path = args.report.resolve()
        report_path.parent.mkdir(parents=True, exist_ok=True)
        report_path.write_text(
            json.dumps(
                report,
                ensure_ascii=False,
                indent=2,
                sort_keys=True,
                allow_nan=False,
            )
            + "\n",
            encoding="utf-8",
        )
        print(f"Wrote model matrix report: {report_path}")
        print(
            f"MODEL MATRIX {'PASSED' if report['passed'] else 'FAILED'}: "
            f"{len(results)} case/profile result(s)"
        )
        return 0 if report["passed"] else 1
    except (
        FileNotFoundError,
        IndexError,
        KeyError,
        OSError,
        subprocess.CalledProcessError,
        TypeError,
        ValueError,
    ) as error:
        print(f"model_matrix.py: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
