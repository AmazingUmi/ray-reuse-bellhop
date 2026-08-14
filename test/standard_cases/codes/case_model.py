from __future__ import annotations

from dataclasses import dataclass
import math
from pathlib import Path
import re
import tomllib


TOKEN_PATTERN = re.compile(r"@[A-Z0-9_]+@")
OUTPUT_KINDS = {
    "shd",
    "ray",
    "arrivals_ascii",
    "arrivals_binary",
    "eigenray",
}


@dataclass(frozen=True)
class CaseDefinition:
    case_id: str
    directory: Path
    description: str
    output_kind: str
    template_path: Path
    companion_files: tuple[Path, ...]
    source_references: tuple[str, ...]
    sound_speed_at_source_mps: float
    water_depth_m: float
    maximum_range_m: float
    minimum_angle_deg: float
    maximum_angle_deg: float
    explicit_launch_angle_count: int | None
    expected_dimensions: tuple[int, ...] | None
    prt_markers: tuple[str, ...]
    prt_forbidden_markers: tuple[str, ...]
    profiles: dict[str, dict[str, object]]
    supported_versions: tuple[str, ...]

    def frequencies(self, profile_name: str) -> tuple[float, ...]:
        try:
            profile = self.profiles[profile_name]
        except KeyError as error:
            raise ValueError(
                f"{self.case_id}: unknown profile {profile_name!r}"
            ) from error

        values = profile.get("frequencies_hz")
        if values is not None:
            frequencies = tuple(float(value) for value in values)
        else:
            start = float(profile["start_hz"])
            stop = float(profile["stop_hz"])
            count = int(profile["count"])
            if count < 2:
                raise ValueError(
                    f"{self.case_id}/{profile_name}: linear grid count must be >= 2"
                )
            step = (stop - start) / (count - 1)
            frequencies = tuple(start + index * step for index in range(count))

        if not frequencies or any(value <= 0.0 for value in frequencies):
            raise ValueError(
                f"{self.case_id}/{profile_name}: frequencies must be positive"
            )
        if tuple(sorted(frequencies)) != frequencies:
            raise ValueError(
                f"{self.case_id}/{profile_name}: frequencies must be ascending"
            )
        return frequencies

    def launch_angle_counts(
        self, frequencies: tuple[float, ...]
    ) -> dict[str, int]:
        if self.explicit_launch_angle_count is not None:
            return {
                "explicit": self.explicit_launch_angle_count,
                "final": self.explicit_launch_angle_count,
            }
        design_frequency = max(frequencies)
        phase_count = max(
            int(
                0.3
                * self.maximum_range_m
                * design_frequency
                / 1500.0
            ),
            300,
        )
        depth_count = int(
            math.pi
            / math.atan(
                self.water_depth_m / (10.0 * self.maximum_range_m)
            )
        )
        angular_step = math.sqrt(
            self.sound_speed_at_source_mps
            / (6.0 * design_frequency * self.maximum_range_m)
        )
        angle_span = math.radians(
            self.maximum_angle_deg - self.minimum_angle_deg
        )
        sufficiency_count = 2 + int(angle_span / angular_step)
        final_count = max(phase_count, depth_count, sufficiency_count)
        return {
            "phase_criterion": phase_count,
            "depth_criterion": depth_count,
            "sufficiency_check": sufficiency_count,
            "final": final_count,
        }

    def shared_launch_angle_count(self, frequencies: tuple[float, ...]) -> int:
        return self.launch_angle_counts(frequencies)["final"]

    def render_origin_environment(
        self, frequency_hz: float, launch_angle_count: int
    ) -> str:
        contents = self.template_path.read_text(encoding="utf-8")
        contents = contents.replace(
            "@FREQUENCY_HZ@", format(frequency_hz, ".12g")
        )
        contents = contents.replace("@NALPHA@", str(launch_angle_count))
        remaining = TOKEN_PATTERN.findall(contents)
        if remaining:
            raise ValueError(
                f"{self.template_path}: unresolved template tokens {remaining}"
            )
        return contents


def load_case(case_directory: Path) -> CaseDefinition:
    manifest_path = case_directory / "case.toml"
    with manifest_path.open("rb") as stream:
        raw = tomllib.load(stream)

    launch = raw["launch"]
    validation = raw["validation"]
    launch_policy = str(launch.get("policy", "shared_fmax"))
    if launch_policy not in {"shared_fmax", "explicit"}:
        raise ValueError(
            f"{manifest_path}: launch.policy must be shared_fmax or explicit"
        )
    explicit_launch_angle_count: int | None = None
    if launch_policy == "explicit":
        explicit_launch_angle_count = int(launch.get("count", 0))
        if explicit_launch_angle_count <= 0:
            raise ValueError(
                f"{manifest_path}: explicit launch policy requires count > 0"
            )
    output_kind = str(raw.get("output_kind", "shd"))
    if output_kind not in OUTPUT_KINDS:
        raise ValueError(
            f"{manifest_path}: output_kind must be one of "
            f"{', '.join(sorted(OUTPUT_KINDS))}"
        )
    raw_dimensions = validation.get("shd_dimensions")
    if output_kind == "shd" and raw_dimensions is None:
        raise ValueError(
            f"{manifest_path}: SHD cases require validation.shd_dimensions"
        )
    if output_kind != "shd" and raw_dimensions is not None:
        raise ValueError(
            f"{manifest_path}: {output_kind} cases must not define "
            "shd_dimensions"
        )
    if raw_dimensions is not None:
        dimensions = tuple(int(value) for value in raw_dimensions)
        if len(dimensions) != 7 or any(value <= 0 for value in dimensions):
            raise ValueError(
                f"{manifest_path}: validation.shd_dimensions must contain "
                "seven positive integers"
            )
    else:
        dimensions = None
    supported_versions = tuple(
        raw.get("compatibility", {}).get(
            "versions", ("origin", "f2cpp", "rayreuse")
        )
    )
    known_versions = {"origin", "f2cpp", "rayreuse"}
    if not supported_versions or len(set(supported_versions)) != len(
        supported_versions
    ) or any(version not in known_versions for version in supported_versions):
        raise ValueError(
            f"{manifest_path}: compatibility.versions must contain unique "
            "known versions"
        )
    return CaseDefinition(
        case_id=str(raw["id"]),
        directory=case_directory,
        description=str(raw["description"]),
        output_kind=output_kind,
        template_path=case_directory / str(raw["origin_template"]),
        companion_files=tuple(
            case_directory / str(value)
            for value in raw.get("companion_files", [])
        ),
        source_references=tuple(raw.get("source_references", [])),
        sound_speed_at_source_mps=float(
            launch["sound_speed_at_source_mps"]
        ),
        water_depth_m=float(launch["water_depth_m"]),
        maximum_range_m=float(launch["maximum_range_m"]),
        minimum_angle_deg=float(launch["minimum_angle_deg"]),
        maximum_angle_deg=float(launch["maximum_angle_deg"]),
        explicit_launch_angle_count=explicit_launch_angle_count,
        expected_dimensions=dimensions,
        prt_markers=tuple(validation.get("prt_markers", [])),
        prt_forbidden_markers=tuple(
            validation.get("prt_forbidden_markers", [])
        ),
        profiles=dict(raw["profiles"]),
        supported_versions=supported_versions,
    )


def discover_cases(cases_root: Path) -> dict[str, CaseDefinition]:
    definitions: dict[str, CaseDefinition] = {}
    for manifest_path in sorted(cases_root.glob("*/case.toml")):
        definition = load_case(manifest_path.parent)
        if definition.case_id in definitions:
            raise ValueError(f"duplicate case id: {definition.case_id}")
        definitions[definition.case_id] = definition
    return definitions
