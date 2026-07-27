from __future__ import annotations

from dataclasses import dataclass
import math
from pathlib import Path
import re
import tomllib


TOKEN_PATTERN = re.compile(r"@[A-Z0-9_]+@")


@dataclass(frozen=True)
class CaseDefinition:
    case_id: str
    directory: Path
    description: str
    template_path: Path
    source_references: tuple[str, ...]
    sound_speed_at_source_mps: float
    water_depth_m: float
    maximum_range_m: float
    minimum_angle_deg: float
    maximum_angle_deg: float
    expected_dimensions: tuple[int, ...]
    prt_markers: tuple[str, ...]
    prt_forbidden_markers: tuple[str, ...]
    profiles: dict[str, dict[str, object]]

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
    return CaseDefinition(
        case_id=str(raw["id"]),
        directory=case_directory,
        description=str(raw["description"]),
        template_path=case_directory / str(raw["origin_template"]),
        source_references=tuple(raw.get("source_references", [])),
        sound_speed_at_source_mps=float(
            launch["sound_speed_at_source_mps"]
        ),
        water_depth_m=float(launch["water_depth_m"]),
        maximum_range_m=float(launch["maximum_range_m"]),
        minimum_angle_deg=float(launch["minimum_angle_deg"]),
        maximum_angle_deg=float(launch["maximum_angle_deg"]),
        expected_dimensions=tuple(
            int(value) for value in validation["shd_dimensions"]
        ),
        prt_markers=tuple(validation.get("prt_markers", [])),
        prt_forbidden_markers=tuple(
            validation.get("prt_forbidden_markers", [])
        ),
        profiles=dict(raw["profiles"]),
    )


def discover_cases(cases_root: Path) -> dict[str, CaseDefinition]:
    definitions: dict[str, CaseDefinition] = {}
    for manifest_path in sorted(cases_root.glob("*/case.toml")):
        definition = load_case(manifest_path.parent)
        if definition.case_id in definitions:
            raise ValueError(f"duplicate case id: {definition.case_id}")
        definitions[definition.case_id] = definition
    return definitions
