from __future__ import annotations

import argparse
from dataclasses import asdict, dataclass
import json
from pathlib import Path
import shutil
import subprocess
import sys
from typing import Sequence


CODES_ROOT = Path(__file__).resolve().parent
STANDARD_CASES_ROOT = CODES_ROOT.parent
PROJECT_ROOT = STANDARD_CASES_ROOT.parents[1]
PLOTREAD_ROOT = PROJECT_ROOT / "test" / "PlotRead"
sys.path.insert(0, str(PLOTREAD_ROOT))
sys.path.insert(0, str(CODES_ROOT))

import numpy as np

from bellhop_io_py.shd import ShdReader
from case_model import CaseDefinition, discover_cases
from compare_fields import compare_files


VERSIONS = ("origin", "f2cpp", "rayreuse")
STAGES = ("generate", "run", "validate", "test")


@dataclass(frozen=True)
class VersionAdapter:
    name: str
    executable: Path
    enabled: bool
    unavailable_reason: str = ""

    def require_available(self) -> None:
        if not self.enabled:
            raise RuntimeError(
                f"{self.name} is not enabled: {self.unavailable_reason}"
            )
        if not self.executable.is_file():
            raise FileNotFoundError(
                f"{self.name} executable not found: {self.executable}"
            )

    def run_single(self, working_directory: Path, file_root: str) -> None:
        self.require_available()
        subprocess.run(
            [str(self.executable), file_root],
            cwd=working_directory,
            check=True,
        )


@dataclass(frozen=True)
class RunRecord:
    frequency_index: int
    frequency_hz: float
    file_root: str
    environment_file: str
    print_file: str | None
    shade_file: str | None
    status: str


def default_adapters(executable_override: Path | None) -> dict[str, VersionAdapter]:
    origin_executable = (
        executable_override.resolve()
        if executable_override is not None
        else PROJECT_ROOT / "Bellhop_origin" / "bin" / "bellhop"
    )
    return {
        "origin": VersionAdapter(
            name="origin",
            executable=origin_executable,
            enabled=True,
        ),
        "f2cpp": VersionAdapter(
            name="f2cpp",
            executable=(
                PROJECT_ROOT
                / "Bellhop_F2CPP"
                / "build"
                / "bellhop_f2cpp"
            ),
            enabled=False,
            unavailable_reason=(
                "solver executable and CLI contract are not implemented"
            ),
        ),
        "rayreuse": VersionAdapter(
            name="rayreuse",
            executable=(
                PROJECT_ROOT
                / "Bellhop_RayReuse"
                / "build"
                / "bellhop_rayreuse"
            ),
            enabled=False,
            unavailable_reason=(
                "multi-frequency input and CLI contract are not implemented"
            ),
        ),
    }


def frequency_label(index: int, frequency_hz: float) -> str:
    value = format(frequency_hz, ".12g").replace(".", "p")
    return f"f{index:03d}_{value}Hz"


def validate_output(
    definition: CaseDefinition,
    frequency_hz: float,
    print_path: Path,
    shade_path: Path,
) -> None:
    if not print_path.is_file() or print_path.stat().st_size == 0:
        raise RuntimeError(f"missing print output: {print_path}")
    if not shade_path.is_file() or shade_path.stat().st_size == 0:
        raise RuntimeError(f"missing shade output: {shade_path}")

    print_contents = print_path.read_text(errors="replace")
    common_markers = (
        "Coherent TL calculation",
        "Cartesian beams",
        "Rectilinear receiver grid",
    )
    for marker in common_markers + definition.prt_markers:
        if marker not in print_contents:
            raise RuntimeError(
                f"{definition.case_id}: PRT marker missing: {marker!r}"
            )
    for marker in definition.prt_forbidden_markers:
        if marker in print_contents:
            raise RuntimeError(
                f"{definition.case_id}: forbidden PRT marker present: {marker!r}"
            )
    if "FATAL ERROR" in print_contents:
        raise RuntimeError(f"{definition.case_id}: solver reported FATAL ERROR")

    reader = ShdReader(shade_path)
    if reader.header.dimensions != definition.expected_dimensions:
        raise RuntimeError(
            f"{definition.case_id}: SHD dimensions "
            f"{reader.header.dimensions} != {definition.expected_dimensions}"
        )
    if not np.isclose(reader.header.frequencies_hz[0], frequency_hz):
        raise RuntimeError(
            f"{definition.case_id}: SHD frequency "
            f"{reader.header.frequencies_hz[0]} != {frequency_hz}"
        )
    pressure = reader.read().pressure
    if not np.isfinite(pressure).all():
        raise RuntimeError(f"{definition.case_id}: non-finite pressure")
    if not np.any(pressure):
        raise RuntimeError(f"{definition.case_id}: pressure is entirely zero")


def process_case(
    definition: CaseDefinition,
    profile_name: str,
    adapter: VersionAdapter,
    stage: str,
    results_root: Path,
) -> Path:
    if adapter.name != "origin":
        adapter.require_available()
        raise NotImplementedError(
            f"{adapter.name} input adapter is not implemented"
        )

    frequencies = definition.frequencies(profile_name)
    launch_angle_counts = definition.launch_angle_counts(frequencies)
    launch_angle_count = launch_angle_counts["final"]
    case_result_root = (
        results_root / adapter.name / definition.case_id / profile_name
    )
    case_result_root.mkdir(parents=True, exist_ok=True)

    records: list[RunRecord] = []
    for index, frequency_hz in enumerate(frequencies):
        label = frequency_label(index, frequency_hz)
        run_directory = case_result_root / label
        file_root = f"{definition.case_id}_{label}"
        environment_path = run_directory / f"{file_root}.env"
        print_path = run_directory / f"{file_root}.prt"
        shade_path = run_directory / f"{file_root}.shd"

        if stage in ("generate", "test"):
            if run_directory.exists():
                shutil.rmtree(run_directory)
            run_directory.mkdir(parents=True)
            environment_path.write_text(
                definition.render_origin_environment(
                    frequency_hz, launch_angle_count
                ),
                encoding="utf-8",
            )
            status = "generated"
        else:
            if not environment_path.is_file():
                raise RuntimeError(
                    f"{environment_path} does not exist; run generate first"
                )
            status = "existing"

        if stage in ("run", "test"):
            adapter.run_single(run_directory, file_root)
            status = "completed"

        if stage in ("validate", "test"):
            validate_output(
                definition, frequency_hz, print_path, shade_path
            )
            status = "passed"

        records.append(
            RunRecord(
                frequency_index=index,
                frequency_hz=frequency_hz,
                file_root=file_root,
                environment_file=str(
                    environment_path.relative_to(case_result_root)
                ),
                print_file=(
                    str(print_path.relative_to(case_result_root))
                    if print_path.exists()
                    else None
                ),
                shade_file=(
                    str(shade_path.relative_to(case_result_root))
                    if shade_path.exists()
                    else None
                ),
                status=status,
            )
        )
        print(
            f"{adapter.name}/{definition.case_id}/{profile_name}/"
            f"{label}/{stage}: {status.upper()}"
        )

    manifest = {
        "schema_version": 1,
        "version": adapter.name,
        "executable": str(adapter.executable),
        "case_id": definition.case_id,
        "profile": profile_name,
        "last_stage": stage,
        "description": definition.description,
        "source_references": definition.source_references,
        "frequencies_hz": frequencies,
        "design_frequency_hz": max(frequencies),
        "shared_launch_angle_count": launch_angle_count,
        "launch_angle_counts": launch_angle_counts,
        "launch_angle_range_deg": [
            definition.minimum_angle_deg,
            definition.maximum_angle_deg,
        ],
        "runs": [asdict(record) for record in records],
    }
    manifest_path = case_result_root / "run_manifest.json"
    manifest_path.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    return manifest_path


def select_cases(
    definitions: dict[str, CaseDefinition],
    requested: list[str] | None,
) -> tuple[list[CaseDefinition], bool]:
    names = requested or ["all"]
    explicit = "all" not in names
    if not explicit:
        return list(definitions.values()), False
    unknown = sorted(set(names) - set(definitions))
    if unknown:
        raise ValueError(f"unknown cases: {', '.join(unknown)}")
    return [definitions[name] for name in names], True


def run_selection(
    *,
    stage: str,
    version: str,
    profile: str,
    requested_cases: list[str] | None,
    executable: Path | None,
    results_root: Path,
) -> int:
    definitions = discover_cases(STANDARD_CASES_ROOT / "cases")
    selected, explicit = select_cases(definitions, requested_cases)
    adapter = default_adapters(executable)[version]
    if stage in ("run", "test"):
        adapter.require_available()

    completed = 0
    for definition in selected:
        if profile not in definition.profiles:
            if explicit:
                raise ValueError(
                    f"{definition.case_id}: profile {profile!r} is not defined"
                )
            print(
                f"{version}/{definition.case_id}/{profile}: "
                "SKIP (profile not defined)"
            )
            continue
        process_case(
            definition,
            profile,
            adapter,
            stage,
            results_root,
        )
        completed += 1
    if completed == 0:
        raise RuntimeError("no cases were selected")
    return completed


def add_selection_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--version", choices=VERSIONS, default="origin")
    parser.add_argument(
        "--case",
        action="append",
        dest="cases",
        help="case id; repeat for multiple cases; default: all",
    )
    parser.add_argument("--profile", default="single")
    parser.add_argument("--executable", type=Path)
    parser.add_argument(
        "--results-root",
        type=Path,
        default=STANDARD_CASES_ROOT / "results",
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Run standard cases by version, case, frequency profile and stage."
        )
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    for stage in STAGES:
        stage_parser = subparsers.add_parser(
            stage,
            help={
                "generate": "only materialize solver inputs",
                "run": "only execute previously generated inputs",
                "validate": "only validate existing outputs",
                "test": "generate, run and validate",
            }[stage],
        )
        add_selection_arguments(stage_parser)

    batch_parser = subparsers.add_parser(
        "batch", help="run complete tests for all available versions"
    )
    batch_parser.add_argument(
        "--versions",
        default="available",
        help="comma-separated versions or 'available'",
    )
    batch_parser.add_argument(
        "--profiles",
        default="single,broadband_smoke",
        help="comma-separated profiles",
    )
    batch_parser.add_argument("--executable", type=Path)
    batch_parser.add_argument(
        "--results-root",
        type=Path,
        default=STANDARD_CASES_ROOT / "results",
    )

    subparsers.add_parser("list", help="list versions, cases and profiles")

    compare_parser = subparsers.add_parser(
        "compare", help="compare two SHD frequency slices"
    )
    compare_parser.add_argument("reference", type=Path)
    compare_parser.add_argument("candidate", type=Path)
    compare_parser.add_argument(
        "--reference-frequency-index", type=int, default=0
    )
    compare_parser.add_argument(
        "--candidate-frequency-index", type=int, default=0
    )
    compare_parser.add_argument(
        "--tolerances",
        type=Path,
        default=CODES_ROOT / "tolerances.toml",
    )
    return parser


def list_configuration() -> int:
    definitions = discover_cases(STANDARD_CASES_ROOT / "cases")
    adapters = default_adapters(None)
    print("Versions:")
    for name in VERSIONS:
        adapter = adapters[name]
        if not adapter.enabled:
            state = f"reserved ({adapter.unavailable_reason})"
        elif adapter.executable.is_file():
            state = f"available ({adapter.executable})"
        else:
            state = f"missing executable ({adapter.executable})"
        print(f"  {name}: {state}")
    print("Cases:")
    for definition in definitions.values():
        profiles = ", ".join(definition.profiles)
        print(f"  {definition.case_id}: {profiles}")
    return 0


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        if args.command in STAGES:
            run_selection(
                stage=args.command,
                version=args.version,
                profile=args.profile,
                requested_cases=args.cases,
                executable=args.executable,
                results_root=args.results_root.resolve(),
            )
            return 0

        if args.command == "batch":
            adapters = default_adapters(args.executable)
            if args.versions == "available":
                versions = [
                    name
                    for name, adapter in adapters.items()
                    if adapter.enabled and adapter.executable.is_file()
                ]
            else:
                versions = [
                    value.strip()
                    for value in args.versions.split(",")
                    if value.strip()
                ]
                unknown = sorted(set(versions) - set(VERSIONS))
                if unknown:
                    raise ValueError(
                        f"unknown versions: {', '.join(unknown)}"
                    )
            if not versions:
                raise RuntimeError("no executable-backed versions are available")
            profiles = [
                value.strip()
                for value in args.profiles.split(",")
                if value.strip()
            ]
            completed = 0
            for version in versions:
                for profile in profiles:
                    completed += run_selection(
                        stage="test",
                        version=version,
                        profile=profile,
                        requested_cases=None,
                        executable=args.executable,
                        results_root=args.results_root.resolve(),
                    )
            print(
                f"BATCH PASSED: {completed} version/case/profile combinations"
            )
            return 0

        if args.command == "list":
            return list_configuration()

        if args.command == "compare":
            passed, metrics = compare_files(
                args.reference,
                args.candidate,
                args.reference_frequency_index,
                args.candidate_frequency_index,
                args.tolerances,
            )
            for key, value in metrics.items():
                print(f"{key}={value:.9g}")
            return 0 if passed else 1
    except (FileNotFoundError, RuntimeError, ValueError) as error:
        raise SystemExit(str(error)) from error

    raise AssertionError(f"unhandled command: {args.command}")


if __name__ == "__main__":
    raise SystemExit(main())
