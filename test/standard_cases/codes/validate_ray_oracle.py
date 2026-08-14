#!/usr/bin/env python3
"""Validate Bellhop's optional ray-step oracle without third-party packages."""

from __future__ import annotations

import argparse
import csv
import json
import math
from pathlib import Path


SCHEMA_NAME = "bellhop.fortran.ray_step_oracle"
SUPPORTED_SCHEMA_VERSIONS = {1, 2}
TEXT_COLUMNS = {"point_kind"}
POINT_KINDS = {
    "source",
    "integrated",
    "top_reflection",
    "bottom_reflection",
}
TERMINATION_REASONS = {
    "source_on_or_outside_boundaries",
    "spatial_box_range",
    "spatial_box_depth",
    "amplitude_below_legacy_threshold",
    "two_points_outside_top",
    "two_points_outside_bottom",
    "dynamic_q_overflow",
    "trajectory_storage_limit",
}


def require_close(
    actual: float,
    expected: float,
    message: str,
    *,
    absolute_tolerance: float = 1e-12,
    relative_tolerance: float = 1e-12,
) -> None:
    if not math.isclose(
        actual,
        expected,
        abs_tol=absolute_tolerance,
        rel_tol=relative_tolerance,
    ):
        raise ValueError(f"{message}: {actual!r} != {expected!r}")


def wrapped_phase_difference(left: float, right: float) -> float:
    return math.atan2(math.sin(left - right), math.cos(left - right))


def validate_reflection_events(
    directory: Path,
    manifest: dict[str, object],
    point_rows: list[dict[str, str]],
    expected_reflection_count: int,
    *,
    reflection_frame_mode: str = "orthonormal",
) -> int:
    if reflection_frame_mode not in {"orthonormal", "curvilinear_interpolated"}:
        raise ValueError(
            f"unsupported reflection frame mode: {reflection_frame_mode!r}"
        )
    frequency = float(manifest.get("frequency_hz", 0.0))
    if not math.isfinite(frequency) or frequency <= 0.0:
        raise ValueError("schema v2 requires a positive finite frequency_hz")
    if manifest.get("boundary_curvature_mode") not in {
        "standard",
        "double",
        "zero",
    }:
        raise ValueError("schema v2 has an invalid boundary_curvature_mode")
    if not isinstance(manifest.get("beam_shift_enabled"), bool):
        raise ValueError("schema v2 requires boolean beam_shift_enabled")

    events_path = directory / str(manifest["reflection_events_file"])
    with events_path.open(newline="", encoding="utf-8") as stream:
        reader = csv.DictReader(stream)
        event_rows = list(reader)
        event_columns = reader.fieldnames or []

    text_columns = {"boundary", "boundary_condition"}
    required_columns = {
        "event_index",
        "pre_point_index",
        "post_point_index",
        "boundary",
        "boundary_condition",
        "boundary_segment_index",
        "pre_r_m",
        "pre_z_m",
        "post_r_m",
        "post_z_m",
        "tangent_r",
        "tangent_z",
        "normal_r",
        "normal_z",
        "incident_t_r_s_per_m",
        "incident_t_z_s_per_m",
        "reflected_t_r_s_per_m",
        "reflected_t_z_s_per_m",
        "incident_p1",
        "incident_p2",
        "incident_q1",
        "incident_q2",
        "reflected_p1",
        "reflected_p2",
        "reflected_q1",
        "reflected_q2",
        "incident_sound_speed_m_per_s",
        "reflected_sound_speed_m_per_s",
        "incident_tau_real_s",
        "incident_tau_imag_s",
        "reflected_tau_real_s",
        "reflected_tau_imag_s",
        "tangent_slowness_s_per_m",
        "normal_slowness_s_per_m",
        "boundary_curvature_per_m",
        "reflection_coefficient_real",
        "reflection_coefficient_imag",
        "reflection_magnitude",
        "reflection_phase_rad",
        "incident_amplitude",
        "incident_phase_rad",
        "reflected_amplitude",
        "reflected_phase_rad",
        "coefficient_suppressed",
        "beam_shift_applied",
    }
    missing_columns = required_columns.difference(event_columns)
    if missing_columns:
        raise ValueError(
            "reflection-event CSV is missing columns: "
            + ", ".join(sorted(missing_columns))
        )
    if len(event_rows) != manifest.get("reflection_event_count"):
        raise ValueError("reflection-event CSV/manifest count mismatch")
    if len(event_rows) != expected_reflection_count:
        raise ValueError(
            "reflection-event count does not match derived point rows"
        )

    numeric_columns = [
        column for column in event_columns if column not in text_columns
    ]
    previous_pre_index = 0
    for expected_event_index, row in enumerate(event_rows, start=1):
        try:
            values = {
                column: float(row[column]) for column in numeric_columns
            }
        except (TypeError, ValueError) as error:
            raise ValueError(
                f"reflection event {expected_event_index}: "
                "invalid numeric field"
            ) from error
        if not all(math.isfinite(value) for value in values.values()):
            raise ValueError(
                f"reflection event {expected_event_index}: "
                "non-finite numeric field"
            )

        integer_fields = {
            name: int(row[name])
            for name in (
                "event_index",
                "pre_point_index",
                "post_point_index",
                "boundary_segment_index",
                "coefficient_suppressed",
                "beam_shift_applied",
            )
        }
        for name, integer_value in integer_fields.items():
            if float(integer_value) != values[name]:
                raise ValueError(
                    f"reflection event {expected_event_index}: "
                    f"{name} is not an integer"
                )
        if integer_fields["event_index"] != expected_event_index:
            raise ValueError("reflection event indices are not contiguous")

        pre_index = integer_fields["pre_point_index"]
        post_index = integer_fields["post_point_index"]
        if (
            pre_index <= previous_pre_index
            or post_index != pre_index + 1
            or pre_index < 1
            or post_index > len(point_rows)
        ):
            raise ValueError(
                f"reflection event {expected_event_index}: "
                "invalid pre/post point indices"
            )
        previous_pre_index = pre_index
        if integer_fields["boundary_segment_index"] < 1:
            raise ValueError(
                f"reflection event {expected_event_index}: "
                "boundary segment index must be positive"
            )

        pre_point = point_rows[pre_index - 1]
        post_point = point_rows[post_index - 1]
        boundary = row["boundary"]
        expected_post_kind = {
            "sea_surface": "top_reflection",
            "seabed": "bottom_reflection",
        }.get(boundary)
        if expected_post_kind is None:
            raise ValueError(
                f"reflection event {expected_event_index}: "
                f"invalid boundary {boundary!r}"
            )
        if (
            pre_point["point_kind"] != "integrated"
            or post_point["point_kind"] != expected_post_kind
        ):
            raise ValueError(
                f"reflection event {expected_event_index}: "
                "pre/post rows do not match the boundary"
            )

        boundary_condition = row["boundary_condition"]
        if boundary_condition not in {"V", "R", "F", "A", "G"}:
            raise ValueError(
                f"reflection event {expected_event_index}: "
                "invalid boundary condition"
            )

        point_field_pairs = {
            "pre_r_m": (pre_point, "r_m"),
            "pre_z_m": (pre_point, "z_m"),
            "post_r_m": (post_point, "r_m"),
            "post_z_m": (post_point, "z_m"),
            "incident_t_r_s_per_m": (pre_point, "t_r_s_per_m"),
            "incident_t_z_s_per_m": (pre_point, "t_z_s_per_m"),
            "reflected_t_r_s_per_m": (post_point, "t_r_s_per_m"),
            "reflected_t_z_s_per_m": (post_point, "t_z_s_per_m"),
            "incident_p1": (pre_point, "p1"),
            "incident_p2": (pre_point, "p2"),
            "incident_q1": (pre_point, "q1"),
            "incident_q2": (pre_point, "q2"),
            "reflected_p1": (post_point, "p1"),
            "reflected_p2": (post_point, "p2"),
            "reflected_q1": (post_point, "q1"),
            "reflected_q2": (post_point, "q2"),
            "incident_sound_speed_m_per_s": (pre_point, "c_m_per_s"),
            "reflected_sound_speed_m_per_s": (post_point, "c_m_per_s"),
            "incident_tau_real_s": (pre_point, "tau_real_s"),
            "incident_tau_imag_s": (pre_point, "tau_imag_s"),
            "reflected_tau_real_s": (post_point, "tau_real_s"),
            "reflected_tau_imag_s": (post_point, "tau_imag_s"),
            "incident_amplitude": (pre_point, "amplitude"),
            "incident_phase_rad": (pre_point, "phase_rad"),
            "reflected_amplitude": (post_point, "amplitude"),
            "reflected_phase_rad": (post_point, "phase_rad"),
        }
        for event_name, (point, point_name) in point_field_pairs.items():
            require_close(
                values[event_name],
                float(point[point_name]),
                f"reflection event {expected_event_index} field {event_name}",
            )

        tangent = (values["tangent_r"], values["tangent_z"])
        normal = (values["normal_r"], values["normal_z"])
        tangent_norm = math.hypot(*tangent)
        normal_norm = math.hypot(*normal)
        if reflection_frame_mode == "orthonormal":
            require_close(
                tangent_norm,
                1.0,
                f"reflection event {expected_event_index} tangent norm",
                absolute_tolerance=1e-8,
            )
            require_close(
                normal_norm,
                1.0,
                f"reflection event {expected_event_index} normal norm",
                absolute_tolerance=1e-8,
            )
        else:
            if tangent_norm == 0.0:
                raise ValueError(
                    f"reflection event {expected_event_index}: zero curvilinear frame"
                )
            require_close(
                normal_norm,
                tangent_norm,
                f"reflection event {expected_event_index} curvilinear frame norms",
                absolute_tolerance=1e-12,
            )
        require_close(
            tangent[0] * normal[0] + tangent[1] * normal[1],
            0.0,
            f"reflection event {expected_event_index} frame orthogonality",
            absolute_tolerance=1e-8,
        )

        incident = (
            values["incident_t_r_s_per_m"],
            values["incident_t_z_s_per_m"],
        )
        reflected = (
            values["reflected_t_r_s_per_m"],
            values["reflected_t_z_s_per_m"],
        )
        tangent_slowness = incident[0] * tangent[0] + incident[1] * tangent[1]
        normal_slowness = incident[0] * normal[0] + incident[1] * normal[1]
        require_close(
            values["tangent_slowness_s_per_m"],
            tangent_slowness,
            f"reflection event {expected_event_index} tangent slowness",
        )
        require_close(
            values["normal_slowness_s_per_m"],
            normal_slowness,
            f"reflection event {expected_event_index} normal slowness",
        )
        if normal_slowness <= 0.0:
            raise ValueError(
                f"reflection event {expected_event_index}: "
                "incident ray does not point toward the boundary exterior"
            )
        for component, expected_component in zip(
            reflected,
            (
                incident[0] - 2.0 * normal_slowness * normal[0],
                incident[1] - 2.0 * normal_slowness * normal[1],
            ),
        ):
            require_close(
                component,
                expected_component,
                f"reflection event {expected_event_index} mirror slowness",
            )

        suppressed = integer_fields["coefficient_suppressed"]
        beam_shift = integer_fields["beam_shift_applied"]
        if suppressed not in (0, 1) or beam_shift not in (0, 1):
            raise ValueError(
                f"reflection event {expected_event_index}: invalid flags"
            )
        if beam_shift and not manifest["beam_shift_enabled"]:
            raise ValueError(
                f"reflection event {expected_event_index}: "
                "beam shift applied while disabled in manifest"
            )
        if not beam_shift:
            for name in (
                "pre_r_m",
                "pre_z_m",
                "incident_sound_speed_m_per_s",
                "incident_tau_real_s",
                "incident_tau_imag_s",
                "incident_q1",
                "incident_q2",
            ):
                post_name = {
                    "pre_r_m": "post_r_m",
                    "pre_z_m": "post_z_m",
                    "incident_sound_speed_m_per_s":
                        "reflected_sound_speed_m_per_s",
                    "incident_tau_real_s": "reflected_tau_real_s",
                    "incident_tau_imag_s": "reflected_tau_imag_s",
                    "incident_q1": "reflected_q1",
                    "incident_q2": "reflected_q2",
                }[name]
                require_close(
                    values[post_name],
                    values[name],
                    f"reflection event {expected_event_index} field {post_name}",
                )

        coefficient = (
            values["reflection_coefficient_real"],
            values["reflection_coefficient_imag"],
        )
        coefficient_magnitude = math.hypot(*coefficient)
        coefficient_phase = math.atan2(coefficient[1], coefficient[0])
        require_close(
            values["reflection_magnitude"],
            coefficient_magnitude,
            f"reflection event {expected_event_index} coefficient magnitude",
        )
        require_close(
            wrapped_phase_difference(
                values["reflection_phase_rad"], coefficient_phase
            ),
            0.0,
            f"reflection event {expected_event_index} coefficient phase",
        )
        if boundary_condition == "V":
            require_close(coefficient[0], -1.0, "vacuum coefficient real")
            require_close(coefficient[1], 0.0, "vacuum coefficient imag")
        elif boundary_condition == "R":
            require_close(coefficient[0], 1.0, "rigid coefficient real")
            require_close(coefficient[1], 0.0, "rigid coefficient imag")

        expected_suppressed = (
            boundary_condition in {"A", "G"}
            and coefficient_magnitude < 1e-5
        )
        if bool(suppressed) != expected_suppressed:
            raise ValueError(
                f"reflection event {expected_event_index}: "
                "coefficient suppression flag is inconsistent"
            )
        if suppressed:
            require_close(
                values["reflected_amplitude"],
                0.0,
                f"reflection event {expected_event_index} killed amplitude",
            )
            require_close(
                values["reflected_phase_rad"],
                values["incident_phase_rad"],
                f"reflection event {expected_event_index} killed phase",
            )
        else:
            require_close(
                values["reflected_amplitude"],
                values["incident_amplitude"] * coefficient_magnitude,
                f"reflection event {expected_event_index} reflected amplitude",
                absolute_tolerance=1e-11,
                relative_tolerance=1e-10,
            )
            require_close(
                wrapped_phase_difference(
                    values["reflected_phase_rad"]
                    - values["incident_phase_rad"],
                    coefficient_phase,
                ),
                0.0,
                f"reflection event {expected_event_index} reflected phase",
                absolute_tolerance=1e-11,
                relative_tolerance=1e-10,
            )

    return len(event_rows)


def validate_oracle(
    directory: Path, *, reflection_frame_mode: str = "orthonormal"
) -> dict[str, object]:
    manifest_path = directory / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("schema") != SCHEMA_NAME:
        raise ValueError(f"unexpected oracle schema: {manifest.get('schema')!r}")
    schema_version = manifest.get("schema_version")
    if schema_version not in SUPPORTED_SCHEMA_VERSIONS:
        raise ValueError(
            f"unsupported oracle schema version: "
            f"{schema_version!r}"
        )
    if manifest.get("status") != "complete":
        raise ValueError(f"oracle is not complete: {manifest.get('status')!r}")

    points_path = directory / manifest["points_file"]
    with points_path.open(newline="", encoding="utf-8") as stream:
        reader = csv.DictReader(stream)
        rows = list(reader)
    if not rows:
        raise ValueError("oracle CSV has no data rows")
    if len(rows) != manifest.get("point_count"):
        raise ValueError(
            f"CSV/manifest point-count mismatch: "
            f"{len(rows)} != {manifest.get('point_count')}"
        )

    expected_indices = list(range(1, len(rows) + 1))
    actual_indices = [int(row["point_index"]) for row in rows]
    if actual_indices != expected_indices:
        raise ValueError("point_index is not contiguous and 1-based")
    if rows[0]["point_kind"] != "source":
        raise ValueError("first row is not the source point")
    if any(row["point_kind"] == "source" for row in rows[1:]):
        raise ValueError("source point_kind may appear only in the first row")

    numeric_columns = [
        column for column in rows[0] if column not in TEXT_COLUMNS
    ]
    integrated_steps = 0
    kinds: dict[str, int] = {}
    previous_top_bounces = 0
    previous_bottom_bounces = 0
    for row_index, row in enumerate(rows, start=1):
        kind = row["point_kind"]
        if kind not in POINT_KINDS:
            raise ValueError(
                f"row {row_index}: unsupported point_kind {kind!r}"
            )
        kinds[kind] = kinds.get(kind, 0) + 1
        try:
            values = {
                column: float(row[column]) for column in numeric_columns
            }
        except (TypeError, ValueError) as error:
            raise ValueError(
                f"row {row_index}: invalid numeric field"
            ) from error
        if not all(math.isfinite(value) for value in values.values()):
            raise ValueError(f"row {row_index}: non-finite numeric field")

        step_valid = int(row["step_valid"])
        if step_valid not in (0, 1):
            raise ValueError(f"row {row_index}: invalid step_valid")
        incoming_step_index = int(row["incoming_step_index"])
        top_bounces = int(row["num_top_bounces"])
        bottom_bounces = int(row["num_bottom_bounces"])
        if (
            float(top_bounces) != values["num_top_bounces"]
            or float(bottom_bounces) != values["num_bottom_bounces"]
            or top_bounces < 0
            or bottom_bounces < 0
        ):
            raise ValueError(f"row {row_index}: invalid bounce count")

        if kind == "integrated":
            if step_valid != 1:
                raise ValueError(
                    f"row {row_index}: integrated row has no step data"
                )
            integrated_steps += 1
            if incoming_step_index != integrated_steps:
                raise ValueError(
                    f"row {row_index}: incoming_step_index is not "
                    "strictly contiguous"
                )
            h_m = values["h_m"]
            weights_m = values["hw0_m"] + values["hw1_m"]
            if not math.isclose(h_m, weights_m, rel_tol=2e-15, abs_tol=1e-14):
                raise ValueError(
                    f"row {row_index}: h_m != hw0_m + hw1_m"
                )
        elif step_valid != 0 or incoming_step_index != 0:
            raise ValueError(
                f"row {row_index}: derived/source row unexpectedly owns a step"
            )

        expected_top_bounces = previous_top_bounces
        expected_bottom_bounces = previous_bottom_bounces
        if kind == "top_reflection":
            if row_index == 1 or rows[row_index - 2]["point_kind"] != "integrated":
                raise ValueError(
                    f"row {row_index}: top reflection does not immediately "
                    "follow its integrated incident point"
                )
            expected_top_bounces += 1
        elif kind == "bottom_reflection":
            if row_index == 1 or rows[row_index - 2]["point_kind"] != "integrated":
                raise ValueError(
                    f"row {row_index}: bottom reflection does not immediately "
                    "follow its integrated incident point"
                )
            expected_bottom_bounces += 1
        if (
            top_bounces != expected_top_bounces
            or bottom_bounces != expected_bottom_bounces
        ):
            raise ValueError(
                f"row {row_index}: bounce counters do not match point_kind"
            )
        previous_top_bounces = top_bounces
        previous_bottom_bounces = bottom_bounces

    if integrated_steps != manifest.get("integrated_step_count"):
        raise ValueError(
            f"CSV/manifest integrated-step mismatch: "
            f"{integrated_steps} != "
            f"{manifest.get('integrated_step_count')}"
        )
    reflection_count = (
        kinds.get("top_reflection", 0)
        + kinds.get("bottom_reflection", 0)
    )
    if len(rows) != 1 + integrated_steps + reflection_count:
        raise ValueError(
            "point sequence is not source + integrated steps + reflections"
        )
    termination_reason = manifest.get("termination_reason")
    if termination_reason not in TERMINATION_REASONS:
        raise ValueError(
            f"unsupported termination_reason: {termination_reason!r}"
        )
    reported_nsteps = manifest.get("reported_nsteps")
    expected_nsteps = (
        len(rows) - 1
        if termination_reason == "trajectory_storage_limit"
        else len(rows)
    )
    if reported_nsteps != expected_nsteps:
        raise ValueError(
            f"reported_nsteps mismatch: "
            f"{reported_nsteps!r} != {expected_nsteps}"
        )

    validated_reflection_count = reflection_count
    if schema_version >= 2:
        validated_reflection_count = validate_reflection_events(
            directory,
            manifest,
            rows,
            reflection_count,
            reflection_frame_mode=reflection_frame_mode,
        )

    return {
        "schema_version": schema_version,
        "point_count": len(rows),
        "integrated_step_count": integrated_steps,
        "reflection_event_count": validated_reflection_count,
        "point_kinds": kinds,
        "termination_reason": termination_reason,
        "all_numeric_fields_finite": True,
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "directory",
        type=Path,
        help="directory containing manifest.json and ray_points.csv",
    )
    parser.add_argument(
        "--reflection-frame-mode",
        choices=("orthonormal", "curvilinear_interpolated"),
        default="orthonormal",
        help="frame semantics used by reflection_events.csv",
    )
    args = parser.parse_args()
    summary = validate_oracle(
        args.directory, reflection_frame_mode=args.reflection_frame_mode
    )
    print(json.dumps(summary, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
