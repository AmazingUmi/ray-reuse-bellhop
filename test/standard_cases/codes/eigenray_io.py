"""EOF-based reader for Bellhop ``E`` eigenray ``.ray`` products.

Ordinary ``R`` files are deliberately left to ``validate_i6_ray_trace``;
their reader uses the header's fixed source/launch block count.  Eigenray
files use the same header but append only the blocks that converged, so they
must be consumed until an exact EOF.
"""

from __future__ import annotations

from dataclasses import dataclass
import math
from pathlib import Path
import re


@dataclass(frozen=True)
class EigenrayHeader:
    title: str
    frequency_hz: float
    source_counts: tuple[int, int, int]
    launch_counts: tuple[int, int]
    top_depth_m: float
    bottom_depth_m: float
    plot_type: str


@dataclass(frozen=True)
class EigenrayBlock:
    launch_angle_deg: float
    top_bounces: int
    bottom_bounces: int
    points_m: tuple[tuple[float, float], ...]

    @property
    def point_count(self) -> int:
        return len(self.points_m)


@dataclass(frozen=True)
class EigenrayOutput:
    header: EigenrayHeader
    rays: tuple[EigenrayBlock, ...]

    @property
    def blocks(self) -> tuple[EigenrayBlock, ...]:
        return self.rays


_TOKEN = re.compile(r"'[^'\n]*'|\"[^\"\n]*\"|\S+")


class _Tokens:
    def __init__(self, path: Path) -> None:
        self.path = path
        self.tokens = _TOKEN.findall(path.read_text(encoding="ascii"))
        self.index = 0

    def take(self, label: str) -> str:
        if self.index >= len(self.tokens):
            raise ValueError(f"{self.path}: truncated while reading {label}")
        token = self.tokens[self.index]
        self.index += 1
        return token


def _float(token: str, path: Path, label: str) -> float:
    try:
        value = float(token.replace("D", "E").replace("d", "e"))
    except ValueError as error:
        raise ValueError(f"{path}: invalid {label}: {token!r}") from error
    if not math.isfinite(value):
        raise ValueError(f"{path}: non-finite {label}")
    return value


def _ints(tokens: _Tokens, count: int, label: str) -> tuple[int, ...]:
    values = tuple(_float(tokens.take(label), tokens.path, label) for _ in range(count))
    integers = tuple(int(value) for value in values)
    if any(float(integer) != value for integer, value in zip(integers, values)):
        raise ValueError(f"{tokens.path}: {label} requires integer values")
    return integers


def _quoted(tokens: _Tokens, label: str) -> str:
    token = tokens.take(label)
    if len(token) < 2 or token[0] not in "'\"" or token[-1] != token[0]:
        raise ValueError(f"{tokens.path}: {label} must be quoted")
    return token[1:-1].strip()


def parse_eigenray(path: str | Path) -> EigenrayOutput:
    file_path = Path(path)
    if not file_path.is_file() or file_path.stat().st_size == 0:
        raise ValueError(f"missing or empty eigenray file: {file_path}")
    tokens = _Tokens(file_path)
    title = _quoted(tokens, "title")
    frequency = _float(tokens.take("frequency"), file_path, "frequency")
    source_counts = _ints(tokens, 3, "source counts")
    launch_counts = _ints(tokens, 2, "launch counts")
    top_depth = _float(tokens.take("top depth"), file_path, "top depth")
    bottom_depth = _float(tokens.take("bottom depth"), file_path, "bottom depth")
    plot_type = _quoted(tokens, "plot type").lower()
    if plot_type not in {"rz", "xyz"}:
        raise ValueError(f"{file_path}: unsupported plot type {plot_type!r}")
    if any(value <= 0 for value in (*source_counts, *launch_counts)):
        raise ValueError(f"{file_path}: source and launch counts must be positive")

    blocks: list[EigenrayBlock] = []
    while tokens.index < len(tokens.tokens):
        launch_angle = _float(tokens.take("eigenray launch angle"), file_path, "launch angle")
        point_count, top_bounces, bottom_bounces = _ints(tokens, 3, "eigenray counts")
        if point_count <= 0:
            raise ValueError(f"{file_path}: eigenray point count must be positive")
        if top_bounces < 0 or bottom_bounces < 0:
            raise ValueError(f"{file_path}: eigenray bounce counts must be non-negative")
        points = []
        coordinate_count = 3 if plot_type == "xyz" else 2
        for point_index in range(point_count):
            coordinates = tuple(
                _float(
                    tokens.take(f"eigenray point {point_index} coordinate {axis}"),
                    file_path,
                    "point coordinate",
                )
                for axis in range(coordinate_count)
            )
            points.append(coordinates)
        blocks.append(EigenrayBlock(launch_angle, top_bounces, bottom_bounces, tuple(points)))
    return EigenrayOutput(
        EigenrayHeader(title, frequency, source_counts, launch_counts, top_depth, bottom_depth, plot_type),
        tuple(blocks),
    )


read_eigenray = parse_eigenray
