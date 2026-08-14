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
RAYREUSE_EXECUTION_MODES = ("nonreuse", "reuse", "parallel")
DECLARABLE_BEAM_FAMILY_MARKERS = (
    "Ray centered beams",
    "Geometric hat beams in Cartesian coordinates",
    "Geometric hat beams in ray-centered coordinates",
    "Geometric gaussian beams in Cartesian coordinates",
    "Geometric gaussian beams in ray-centered coordinates",
    "Simple gaussian beams",
)


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

    def run_broadband(
        self,
        working_directory: Path,
        file_root: str,
        frequencies_hz: Sequence[float],
        execution_mode: str,
    ) -> None:
        self.require_available()
        require_rayreuse_execution_mode(execution_mode)
        subprocess.run(
            [
                str(self.executable),
                file_root,
                "--frequencies-hz",
                format_frequency_csv(frequencies_hz),
                "--execution-mode",
                execution_mode,
            ],
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
    ray_file: str | None
    status: str


def stage_companion_files(
    definition: CaseDefinition, run_directory: Path, file_root: str
) -> None:
    seen_suffixes: set[str] = set()
    for source in definition.companion_files:
        if not source.is_file():
            raise FileNotFoundError(
                f"case companion file does not exist: {source}"
            )
        suffix = source.suffix
        if not suffix or suffix in seen_suffixes:
            raise ValueError(
                f"{definition.case_id}: companion files require unique suffixes"
            )
        seen_suffixes.add(suffix)
        shutil.copyfile(source, run_directory / f"{file_root}{suffix}")


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
                / "release"
                / "bellhop_f2cpp"
            ),
            enabled=True,
        ),
        "rayreuse": VersionAdapter(
            name="rayreuse",
            executable=(
                PROJECT_ROOT
                / "Bellhop_RayReuse"
                / "build"
                / "release"
                / "bellhop_rayreuse"
            ),
            enabled=True,
        ),
    }


def frequency_label(index: int, frequency_hz: float) -> str:
    value = format(frequency_hz, ".12g").replace(".", "p")
    return f"f{index:03d}_{value}Hz"


def format_frequency_csv(frequencies_hz: Sequence[float]) -> str:
    frequencies = tuple(float(value) for value in frequencies_hz)
    if not frequencies:
        raise ValueError("broadband frequency list must not be empty")
    if any(
        current >= following
        for current, following in zip(frequencies, frequencies[1:])
    ):
        raise ValueError(
            "broadband frequencies must be strictly increasing"
        )
    return ",".join(format(value, ".17g") for value in frequencies)


def require_rayreuse_execution_mode(execution_mode: str) -> None:
    if execution_mode not in RAYREUSE_EXECUTION_MODES:
        raise ValueError(
            f"unknown RayReuse execution mode: {execution_mode}"
        )


def declared_beam_family_marker(definition: CaseDefinition) -> str:
    declared = tuple(
        marker
        for marker in DECLARABLE_BEAM_FAMILY_MARKERS
        if marker in definition.prt_markers
    )
    if len(declared) > 1:
        raise ValueError(
            f"{definition.case_id}: multiple beam-family PRT markers declared"
        )
    return declared[0] if declared else "Cartesian beams"


def validate_print_output(
    definition: CaseDefinition,
    print_path: Path,
) -> None:
    if not print_path.is_file() or print_path.stat().st_size == 0:
        raise RuntimeError(f"missing print output: {print_path}")

    print_contents = print_path.read_text(errors="replace")
    receiver_grid_marker = (
        "Irregular grid"
        if "Irregular grid" in definition.prt_markers
        else "Rectilinear receiver grid"
    )
    if "Semi-coherent TL calculation" in definition.prt_markers:
        field_mode_marker = "Semi-coherent TL calculation"
    elif "Incoherent TL calculation" in definition.prt_markers:
        field_mode_marker = "Incoherent TL calculation"
    else:
        field_mode_marker = "Coherent TL calculation"
    beam_family_marker = declared_beam_family_marker(definition)
    common_markers = (
        (field_mode_marker, beam_family_marker, receiver_grid_marker)
        if definition.output_kind == "shd"
        else ("Ray trace run", receiver_grid_marker)
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


def validate_pressure_slice(
    definition: CaseDefinition,
    reader: ShdReader,
    frequency_index: int,
) -> None:
    pressure = reader.read(frequency_index=frequency_index).pressure
    if not np.isfinite(pressure).all():
        raise RuntimeError(
            f"{definition.case_id}: non-finite pressure at frequency index "
            f"{frequency_index}"
        )
    if not np.any(pressure):
        raise RuntimeError(
            f"{definition.case_id}: pressure is entirely zero at frequency "
            f"index {frequency_index}"
        )


def validate_output(
    definition: CaseDefinition,
    frequency_hz: float,
    print_path: Path,
    output_path: Path,
) -> None:
    validate_print_output(definition, print_path)
    if definition.output_kind == "ray":
        if not output_path.is_file() or output_path.stat().st_size == 0:
            raise RuntimeError(f"missing ray output: {output_path}")
        unexpected_shade = output_path.with_suffix(".shd")
        if unexpected_shade.exists():
            raise RuntimeError(
                f"ray run unexpectedly produced shade output: {unexpected_shade}"
            )
        return
    shade_path = output_path
    unexpected_ray = output_path.with_suffix(".ray")
    if unexpected_ray.exists():
        raise RuntimeError(
            f"shade run unexpectedly retained ray output: {unexpected_ray}"
        )
    if not shade_path.is_file() or shade_path.stat().st_size == 0:
        raise RuntimeError(f"missing shade output: {shade_path}")

    reader = ShdReader(shade_path)
    if definition.expected_dimensions is None:
        raise RuntimeError(
            f"{definition.case_id}: SHD dimensions are not configured"
        )
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
    validate_pressure_slice(definition, reader, 0)


def validate_broadband_output(
    definition: CaseDefinition,
    frequencies_hz: Sequence[float],
    execution_mode: str,
    print_path: Path,
    shade_path: Path,
) -> None:
    frequencies = tuple(float(value) for value in frequencies_hz)
    require_rayreuse_execution_mode(execution_mode)
    validate_print_output(definition, print_path)
    print_contents = print_path.read_text(errors="replace")
    print_lines = {
        line.strip() for line in print_contents.splitlines()
    }
    expected_mode_marker = {
        "nonreuse": "execution mode = broadband non-reuse",
        "reuse": "execution mode = broadband reuse",
        "parallel": "execution mode = broadband parallel reuse",
    }[execution_mode]
    expected_trace_passes = (
        len(frequencies) if execution_mode == "nonreuse" else 1
    )
    for marker in (
        expected_mode_marker,
        f"Trace passes = {expected_trace_passes}",
    ):
        if marker not in print_lines:
            raise RuntimeError(
                f"{definition.case_id}: broadband {execution_mode} PRT "
                f"marker missing: {marker!r}"
            )
    if not shade_path.is_file() or shade_path.stat().st_size == 0:
        raise RuntimeError(f"missing shade output: {shade_path}")

    reader = ShdReader(shade_path)
    expected_dimensions = (
        len(frequencies),
        *definition.expected_dimensions[1:],
    )
    if reader.header.dimensions != expected_dimensions:
        raise RuntimeError(
            f"{definition.case_id}: broadband SHD dimensions "
            f"{reader.header.dimensions} != {expected_dimensions}"
        )
    actual_frequencies = tuple(
        float(value) for value in reader.header.frequencies_hz
    )
    if actual_frequencies != frequencies:
        raise RuntimeError(
            f"{definition.case_id}: broadband SHD frequency axis "
            f"{actual_frequencies} != {frequencies}"
        )
    for frequency_index in range(len(frequencies)):
        validate_pressure_slice(definition, reader, frequency_index)


def process_rayreuse_broadband(
    definition: CaseDefinition,
    profile_name: str,
    adapter: VersionAdapter,
    stage: str,
    results_root: Path,
    frequencies: tuple[float, ...],
    launch_angle_counts: dict[str, int],
    execution_mode: str,
) -> Path:
    if definition.output_kind != "shd":
        raise ValueError("RayReuse broadband runner requires SHD output")
    require_rayreuse_execution_mode(execution_mode)
    if len(frequencies) < 2:
        raise ValueError(
            f"{definition.case_id}/{profile_name}: broadband profile must "
            "contain at least two frequencies"
        )
    frequency_csv = format_frequency_csv(frequencies)
    launch_angle_count = launch_angle_counts["final"]
    case_result_root = (
        results_root / adapter.name / definition.case_id / profile_name
    )
    run_directory = case_result_root / "broadband"
    file_root = f"{definition.case_id}_{profile_name}_broadband"
    environment_path = run_directory / f"{file_root}.env"
    print_path = run_directory / f"{file_root}.prt"
    shade_path = run_directory / f"{file_root}.shd"
    ray_path = run_directory / f"{file_root}.ray"
    manifest_path = case_result_root / "run_manifest.json"
    if stage in ("run", "test"):
        manifest_path.unlink(missing_ok=True)

    if stage in ("generate", "test"):
        if run_directory.exists():
            shutil.rmtree(run_directory)
        run_directory.mkdir(parents=True)
        environment_path.write_text(
            definition.render_origin_environment(
                frequencies[0], launch_angle_count
            ),
            encoding="utf-8",
        )
        stage_companion_files(definition, run_directory, file_root)
        status = "generated"
    else:
        if not environment_path.is_file():
            raise RuntimeError(
                f"{environment_path} does not exist; run generate first"
            )
        status = "existing"

    if stage in ("run", "test"):
        for stale_output in (
            print_path,
            shade_path,
            ray_path,
            Path(str(shade_path) + ".tmp"),
            Path(str(ray_path) + ".tmp"),
        ):
            stale_output.unlink(missing_ok=True)
        adapter.run_broadband(
            run_directory,
            file_root,
            frequencies,
            execution_mode,
        )
        status = "completed"

    if stage in ("validate", "test"):
        validate_broadband_output(
            definition,
            frequencies,
            execution_mode,
            print_path,
            shade_path,
        )
        status = "passed"

    relative_environment = str(
        environment_path.relative_to(case_result_root)
    )
    relative_print = (
        str(print_path.relative_to(case_result_root))
        if print_path.exists()
        else None
    )
    relative_shade = (
        str(shade_path.relative_to(case_result_root))
        if shade_path.exists()
        else None
    )
    records = [
        RunRecord(
            frequency_index=index,
            frequency_hz=frequency_hz,
            file_root=file_root,
            environment_file=relative_environment,
            print_file=relative_print,
            shade_file=relative_shade,
            ray_file=None,
            status=status,
        )
        for index, frequency_hz in enumerate(frequencies)
    ]
    print(
        f"{adapter.name}/{definition.case_id}/{profile_name}/broadband/"
        f"{stage}: {status.upper()}"
    )

    manifest = {
        "schema_version": 1,
        "version": adapter.name,
        "executable": str(adapter.executable),
        "case_id": definition.case_id,
        "profile": profile_name,
        "last_stage": stage,
        "description": definition.description,
        "output_kind": definition.output_kind,
        "source_references": definition.source_references,
        "frequencies_hz": frequencies,
        "design_frequency_hz": max(frequencies),
        "shared_launch_angle_count": launch_angle_count,
        "launch_angle_counts": launch_angle_counts,
        "launch_angle_range_deg": [
            definition.minimum_angle_deg,
            definition.maximum_angle_deg,
        ],
        "execution_model": "single_broadband_invocation",
        "execution_mode": execution_mode,
        "broadband_run": {
            "working_directory": "broadband",
            "file_root": file_root,
            "frequencies_argument": frequency_csv,
            "execution_mode_argument": execution_mode,
            "expected_solver_invocations": 1,
            "frequency_slices_share_output": True,
        },
        "runs": [asdict(record) for record in records],
    }
    manifest_path.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    return manifest_path


def process_case(
    definition: CaseDefinition,
    profile_name: str,
    adapter: VersionAdapter,
    stage: str,
    results_root: Path,
    rayreuse_execution_mode: str = "nonreuse",
) -> Path:
    if adapter.name not in VERSIONS:
        adapter.require_available()
        raise ValueError(f"unknown version adapter: {adapter.name}")

    frequencies = definition.frequencies(profile_name)
    launch_angle_counts = definition.launch_angle_counts(frequencies)
    if adapter.name == "rayreuse" and profile_name != "single":
        return process_rayreuse_broadband(
            definition,
            profile_name,
            adapter,
            stage,
            results_root,
            frequencies,
            launch_angle_counts,
            rayreuse_execution_mode,
        )

    launch_angle_count = launch_angle_counts["final"]
    case_result_root = (
        results_root / adapter.name / definition.case_id / profile_name
    )
    case_result_root.mkdir(parents=True, exist_ok=True)
    manifest_path = case_result_root / "run_manifest.json"
    if stage in ("run", "test"):
        manifest_path.unlink(missing_ok=True)

    records: list[RunRecord] = []
    for index, frequency_hz in enumerate(frequencies):
        label = frequency_label(index, frequency_hz)
        run_directory = case_result_root / label
        file_root = f"{definition.case_id}_{label}"
        environment_path = run_directory / f"{file_root}.env"
        print_path = run_directory / f"{file_root}.prt"
        shade_path = run_directory / f"{file_root}.shd"
        ray_path = run_directory / f"{file_root}.ray"
        output_path = (
            shade_path if definition.output_kind == "shd" else ray_path
        )

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
            stage_companion_files(definition, run_directory, file_root)
            status = "generated"
        else:
            if not environment_path.is_file():
                raise RuntimeError(
                    f"{environment_path} does not exist; run generate first"
                )
            status = "existing"

        if stage in ("run", "test"):
            for stale_output in (
                print_path,
                shade_path,
                ray_path,
                Path(str(shade_path) + ".tmp"),
                Path(str(ray_path) + ".tmp"),
            ):
                stale_output.unlink(missing_ok=True)
            adapter.run_single(run_directory, file_root)
            status = "completed"

        if stage in ("validate", "test"):
            validate_output(
                definition, frequency_hz, print_path, output_path
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
                    if definition.output_kind == "shd" and shade_path.exists()
                    else None
                ),
                ray_file=(
                    str(ray_path.relative_to(case_result_root))
                    if definition.output_kind == "ray" and ray_path.exists()
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
        "output_kind": definition.output_kind,
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
    rayreuse_execution_mode: str = "nonreuse",
) -> int:
    definitions = discover_cases(STANDARD_CASES_ROOT / "cases")
    selected, explicit = select_cases(definitions, requested_cases)
    adapter = default_adapters(None)[version]
    if executable is not None:
        adapter = VersionAdapter(
            name=adapter.name,
            executable=executable.resolve(),
            enabled=True,
        )
    if stage in ("run", "test"):
        adapter.require_available()

    completed = 0
    for definition in selected:
        if version not in definition.supported_versions:
            if explicit:
                raise ValueError(
                    f"{definition.case_id}: version {version!r} is not supported"
                )
            print(
                f"{version}/{definition.case_id}/{profile}: "
                "SKIP (version not supported)"
            )
            continue
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
            rayreuse_execution_mode,
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
    parser.add_argument(
        "--rayreuse-execution-mode",
        choices=RAYREUSE_EXECUTION_MODES,
        default="nonreuse",
        help=(
            "execution mode passed only to RayReuse multi-frequency runs "
            "(default: nonreuse)"
        ),
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
    batch_parser.add_argument(
        "--rayreuse-execution-mode",
        choices=RAYREUSE_EXECUTION_MODES,
        default="nonreuse",
        help=(
            "execution mode passed only to RayReuse multi-frequency runs "
            "(default: nonreuse)"
        ),
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
                rayreuse_execution_mode=args.rayreuse_execution_mode,
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
                        rayreuse_execution_mode=args.rayreuse_execution_mode,
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
