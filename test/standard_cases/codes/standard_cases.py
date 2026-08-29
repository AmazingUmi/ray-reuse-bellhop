from __future__ import annotations

import argparse
from dataclasses import asdict, dataclass
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
from typing import Sequence


CODES_ROOT = Path(__file__).resolve().parent
STANDARD_CASES_ROOT = CODES_ROOT.parent
PROJECT_ROOT = STANDARD_CASES_ROOT.parents[1]
COVERAGE_PATH = STANDARD_CASES_ROOT / "coverage.toml"
PLOTREAD_ROOT = PROJECT_ROOT / "test" / "PlotRead"
sys.path.insert(0, str(PLOTREAD_ROOT))
sys.path.insert(0, str(CODES_ROOT))

import numpy as np

from bellhop_io_py.shd import ShdReader
from arrivals_io import parse_ascii_arrivals, parse_binary_arrivals
from case_model import CaseDefinition, discover_cases
from compare_fields import compare_files
from coverage_manifest import load_coverage_manifest
from eigenray_io import parse_eigenray


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
    arrival_file: str | None = None
    product_sha256: str | None = None


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def reset_run_directory(path: Path) -> None:
    """Clear generated contents while preserving the directory itself."""
    path.mkdir(parents=True, exist_ok=True)
    # External macOS volumes can expose AppleDouble ``._*`` sidecars that
    # disappear while their corresponding file is removed. Treat that as an
    # already-completed cleanup so repeated runs remain reliable.
    for child in tuple(path.iterdir()):
        try:
            if child.is_dir():
                shutil.rmtree(child, ignore_errors=True)
            else:
                child.unlink(missing_ok=True)
        except FileNotFoundError:
            pass


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


def frequency_product_name(
    file_root: str, index: int, frequency_hz: float, extension: str
) -> str:
    if not extension.startswith("."):
        raise ValueError("frequency product extension must start with '.'")
    # Keep this aligned with the executable and the existing directory label.
    value = (
        format(frequency_hz, ".12g")
        .replace(".", "p")
        .replace("+", "p")
        .replace("-", "m")
    )
    return f"{file_root}_f{index:03d}_{value}Hz{extension}"


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
    version: str | None = None,
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
    if definition.output_kind == "shd":
        common_markers = (field_mode_marker, beam_family_marker, receiver_grid_marker)
    elif definition.output_kind == "ray":
        common_markers = ("Ray trace run", receiver_grid_marker)
    elif definition.output_kind == "eigenray":
        common_markers = ("Eigenray trace run", receiver_grid_marker)
    elif definition.output_kind == "arrivals_ascii":
        common_markers = ("Arrivals calculation, ASCII  file output", receiver_grid_marker)
    else:
        common_markers = ("Arrivals calculation, binary file output", receiver_grid_marker)
    version_markers = (
        definition.version_prt_markers.get(version, ())
        if version is not None
        else ()
    )
    for marker in common_markers + definition.prt_markers + version_markers:
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
    version: str | None = None,
) -> None:
    validate_print_output(definition, print_path, version)
    expected_suffix = {
        "shd": ".shd",
        "ray": ".ray",
        "eigenray": ".ray",
        "arrivals_ascii": ".arr",
        "arrivals_binary": ".arr",
    }[definition.output_kind]
    if output_path.suffix != expected_suffix:
        raise RuntimeError(
            f"{definition.case_id}: output path {output_path} does not match "
            f"{definition.output_kind}"
        )
    if not output_path.is_file() or output_path.stat().st_size == 0:
        raise RuntimeError(f"missing product output: {output_path}")
    for temporary in output_path.parent.glob("*.tmp"):
        raise RuntimeError(f"{definition.case_id}: unexpected temporary product: {temporary}")
    for suffix, label in ((".shd", "shade"), (".ray", "ray"), (".arr", "arrival")):
        candidate = output_path.with_suffix(suffix)
        if suffix != expected_suffix and candidate.exists():
            if suffix == ".ray" and expected_suffix == ".shd":
                raise RuntimeError(f"{definition.case_id}: shade run unexpectedly retained ray output: {candidate}")
            raise RuntimeError(f"{definition.case_id}: unexpected {label} output: {candidate}")
    if definition.output_kind == "ray":
        return
    if definition.output_kind == "eigenray":
        result = parse_eigenray(output_path)
        if not np.isclose(result.header.frequency_hz, frequency_hz):
            raise RuntimeError(f"{definition.case_id}: eigenray frequency mismatch")
        return
    if definition.output_kind == "arrivals_ascii":
        result = parse_ascii_arrivals(
            output_path,
            receiver_cell_count=definition.arrival_receiver_cell_count,
        )
        if not np.isclose(result.header.frequency_hz, frequency_hz):
            raise RuntimeError(f"{definition.case_id}: ASCII ARR frequency mismatch")
        return
    if definition.output_kind == "arrivals_binary":
        result = parse_binary_arrivals(
            output_path,
            receiver_cell_count=definition.arrival_receiver_cell_count,
        )
        if not np.isclose(result.header.frequency_hz, frequency_hz):
            raise RuntimeError(f"{definition.case_id}: binary ARR frequency mismatch")
        return
    shade_path = output_path

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
    validate_print_output(definition, print_path, "rayreuse")
    print_contents = print_path.read_text(errors="replace")
    print_lines = {
        line.strip() for line in print_contents.splitlines()
    }
    expected_mode_marker = {
        "nonreuse": "execution mode = broadband non-reuse",
        "reuse": "execution mode = broadband reuse",
        "parallel": "execution mode = broadband parallel reuse",
    }[execution_mode]
    # Frozen multi-source statistics (FP-2F worklist §1.5):
    # non-reuse traces once per (frequency, source); reuse/parallel trace
    # each source fan exactly once.
    expected_trace_passes = (
        len(frequencies) * definition.source_depth_count
        if execution_mode == "nonreuse"
        else definition.source_depth_count
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


def validate_broadband_product_outputs(
    definition: CaseDefinition,
    frequencies_hz: Sequence[float],
    execution_mode: str,
    print_path: Path,
    product_paths: Sequence[Path],
) -> None:
    """Validate independent per-frequency ARR/E products from one run."""
    require_rayreuse_execution_mode(execution_mode)
    validate_print_output(definition, print_path, "rayreuse")
    print_lines = {
        line.strip() for line in print_path.read_text(errors="replace").splitlines()
    }
    expected_mode_marker = {
        "nonreuse": "execution mode = broadband non-reuse",
        "reuse": "execution mode = broadband reuse",
        "parallel": "execution mode = broadband parallel reuse",
    }[execution_mode]
    # Frozen multi-source statistics (FP-2F worklist §1.5): see
    # validate_broadband_output.
    expected_trace_passes = (
        len(frequencies_hz) * definition.source_depth_count
        if execution_mode == "nonreuse"
        else definition.source_depth_count
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
    if len(product_paths) != len(frequencies_hz):
        raise RuntimeError(
            f"{definition.case_id}: expected one product per frequency"
        )
    for path, frequency_hz in zip(product_paths, frequencies_hz):
        if not path.is_file() or path.stat().st_size == 0:
            raise RuntimeError(f"missing frequency product output: {path}")
        if definition.output_kind == "eigenray":
            result = parse_eigenray(path)
        elif definition.output_kind == "arrivals_ascii":
            result = parse_ascii_arrivals(
                path,
                receiver_cell_count=definition.arrival_receiver_cell_count,
            )
        elif definition.output_kind == "arrivals_binary":
            result = parse_binary_arrivals(
                path,
                receiver_cell_count=definition.arrival_receiver_cell_count,
            )
        else:
            raise ValueError(
                "independent broadband products require ARR or Eigenray output"
            )
        if not np.isclose(result.header.frequency_hz, frequency_hz):
            raise RuntimeError(
                f"{definition.case_id}: frequency product header mismatch"
            )
    expected_products = {path.resolve() for path in product_paths}
    file_root = print_path.stem
    published_products = {
        candidate.resolve()
        for pattern in (
            f"{file_root}_f*.arr",
            f"{file_root}_f*.ray",
            f"{file_root}.f*.arr",
            f"{file_root}.f*.ray",
        )
        for candidate in print_path.parent.glob(pattern)
    }
    unexpected_products = published_products - expected_products
    if unexpected_products:
        raise RuntimeError(
            f"{definition.case_id}: unexpected stale frequency products: "
            f"{sorted(str(path) for path in unexpected_products)}"
        )
    for temporary in print_path.parent.glob("*.tmp"):
        raise RuntimeError(
            f"{definition.case_id}: unexpected temporary product: {temporary}"
        )


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
    if definition.output_kind == "ray":
        raise ValueError(
            f"{definition.case_id}/{profile_name}: multi-frequency R products "
            "are explicitly unsupported"
        )
    if definition.output_kind not in {
        "shd",
        "arrivals_ascii",
        "arrivals_binary",
        "eigenray",
    }:
        raise ValueError(
            f"{definition.case_id}: unsupported RayReuse broadband output kind "
            f"{definition.output_kind!r}"
        )
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
    product_extension = (
        ".ray" if definition.output_kind == "eigenray" else ".arr"
    )
    product_paths = tuple(
        run_directory
        / frequency_product_name(
            file_root, index, frequency_hz, product_extension
        )
        for index, frequency_hz in enumerate(frequencies)
    )
    manifest_path = case_result_root / "run_manifest.json"
    if stage in ("run", "test"):
        manifest_path.unlink(missing_ok=True)

    if stage in ("generate", "test"):
        reset_run_directory(run_directory)
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
        for product_path in product_paths:
            product_path.unlink(missing_ok=True)
            Path(str(product_path) + ".tmp").unlink(missing_ok=True)
        for pattern in (f"{file_root}_f*", f"{file_root}.f*"):
            for stale_product in run_directory.glob(pattern):
                if stale_product.suffix in {".arr", ".ray", ".tmp"}:
                    stale_product.unlink(missing_ok=True)
        adapter.run_broadband(
            run_directory,
            file_root,
            frequencies,
            execution_mode,
        )
        status = "completed"

    if stage in ("validate", "test"):
        if definition.output_kind == "shd":
            validate_broadband_output(
                definition,
                frequencies,
                execution_mode,
                print_path,
                shade_path,
            )
        else:
            validate_broadband_product_outputs(
                definition,
                frequencies,
                execution_mode,
                print_path,
                product_paths,
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
    product_hash = sha256_file(shade_path) if shade_path.is_file() else None
    records = [
        RunRecord(
            frequency_index=index,
            frequency_hz=frequency_hz,
            file_root=file_root,
            environment_file=relative_environment,
            print_file=relative_print,
            shade_file=relative_shade,
            ray_file=(
                str(product_paths[index].relative_to(case_result_root))
                if definition.output_kind == "eigenray"
                and product_paths[index].is_file()
                else None
            ),
            status=status,
            arrival_file=(
                str(product_paths[index].relative_to(case_result_root))
                if definition.output_kind in {"arrivals_ascii", "arrivals_binary"}
                and product_paths[index].is_file()
                else None
            ),
            product_sha256=(
                sha256_file(product_paths[index])
                if product_paths[index].is_file()
                else product_hash
            ),
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
        "coverage_tags": definition.coverage_tags,
        "test_sets": definition.test_sets,
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
            "frequency_slices_share_output": definition.output_kind == "shd",
            "frequency_products_independent": definition.output_kind != "shd",
            "frequency_product_pattern": (
                f"{file_root}_fNNN_<frequency>Hz"
                if definition.output_kind != "shd"
                else None
            ),
            "publish_order_is_frequency_order": True,
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
        arrival_path = run_directory / f"{file_root}.arr"
        output_path = (
            shade_path
            if definition.output_kind == "shd"
            else ray_path
            if definition.output_kind in {"ray", "eigenray"}
            else arrival_path
        )

        if stage in ("generate", "test"):
            reset_run_directory(run_directory)
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
                arrival_path,
                Path(str(shade_path) + ".tmp"),
                Path(str(ray_path) + ".tmp"),
                Path(str(arrival_path) + ".tmp"),
            ):
                stale_output.unlink(missing_ok=True)
            adapter.run_single(run_directory, file_root)
            status = "completed"

        if stage in ("validate", "test"):
            validate_output(
                definition, frequency_hz, print_path, output_path,
                adapter.name,
            )
            status = "passed"

        product_hash = sha256_file(output_path) if output_path.is_file() else None

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
                    if definition.output_kind in {"ray", "eigenray"} and ray_path.exists()
                    else None
                ),
                status=status,
                arrival_file=(
                    str(arrival_path.relative_to(case_result_root))
                    if definition.output_kind in {"arrivals_ascii", "arrivals_binary"}
                    and arrival_path.exists()
                    else None
                ),
                product_sha256=product_hash,
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
        "coverage_tags": definition.coverage_tags,
        "test_sets": definition.test_sets,
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
    requested_test_sets: list[str] | None = None,
) -> int:
    definitions = discover_cases(STANDARD_CASES_ROOT / "cases")
    if requested_cases and requested_test_sets:
        raise ValueError("--case and --test-set cannot be used together")
    if requested_test_sets:
        coverage = load_coverage_manifest(COVERAGE_PATH, definitions)
        case_ids = coverage.case_ids_for_sets(
            requested_test_sets, definitions
        )
        selected = [definitions[case_id] for case_id in case_ids]
        explicit = True
    else:
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
    parser.add_argument(
        "--test-set",
        action="append",
        dest="test_sets",
        help="functional test set from coverage.toml; repeat to combine sets",
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

    suite_parser = subparsers.add_parser(
        "suite", help="run relevant F2CPP CTest plus one functional case set"
    )
    suite_parser.add_argument(
        "--test-set", action="append", dest="test_sets", required=True
    )
    suite_parser.add_argument("--profile", default="single")
    suite_parser.add_argument("--executable", type=Path)
    suite_parser.add_argument(
        "--build-dir",
        type=Path,
        default=PROJECT_ROOT / "Bellhop_F2CPP" / "build" / "release",
    )
    suite_parser.add_argument(
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
    coverage = load_coverage_manifest(COVERAGE_PATH, definitions)
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
    print("Test sets:")
    for name, test_set in coverage.test_sets.items():
        case_ids = coverage.case_ids_for_sets((name,), definitions)
        print(f"  {name}: {test_set.description} ({len(case_ids)} cases)")
    print("Cases and coverage:")
    for definition in definitions.values():
        profiles = ", ".join(definition.profiles)
        case_coverage = coverage.cases[definition.case_id]
        print(
            f"  {definition.case_id}: profiles={profiles}; "
            f"sets={','.join(case_coverage.test_sets)}; "
            f"tags={','.join(case_coverage.tags)}"
        )
    return 0


def run_f2cpp_suite(
    *,
    test_sets: list[str],
    profile: str,
    executable: Path | None,
    build_dir: Path,
    results_root: Path,
) -> int:
    definitions = discover_cases(STANDARD_CASES_ROOT / "cases")
    coverage = load_coverage_manifest(COVERAGE_PATH, definitions)
    coverage.case_ids_for_sets(test_sets, definitions)
    regex = "|".join(
        f"({coverage.test_sets[name].ctest_regex})"
        for name in test_sets
    )
    subprocess.run(
        [
            "ctest",
            "--test-dir",
            str(build_dir.resolve()),
            "--output-on-failure",
            "-R",
            regex,
        ],
        check=True,
    )
    return run_selection(
        stage="test",
        version="f2cpp",
        profile=profile,
        requested_cases=None,
        executable=executable,
        results_root=results_root.resolve(),
        requested_test_sets=test_sets,
    )


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        if args.command in STAGES:
            run_selection(
                stage=args.command,
                version=args.version,
                profile=args.profile,
                requested_cases=args.cases,
                requested_test_sets=args.test_sets,
                executable=args.executable,
                results_root=args.results_root.resolve(),
                rayreuse_execution_mode=args.rayreuse_execution_mode,
            )
            return 0

        if args.command == "suite":
            run_f2cpp_suite(
                test_sets=args.test_sets,
                profile=args.profile,
                executable=args.executable,
                build_dir=args.build_dir,
                results_root=args.results_root,
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
