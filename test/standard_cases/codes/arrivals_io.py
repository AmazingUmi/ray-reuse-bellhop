"""Independent readers for Bellhop ``.arr`` products.

The reader intentionally knows only the public Fortran file layout.  It does
not import a solver writer, which keeps the standard-case checks useful when a
writer has accidentally changed its encoding.
"""

from __future__ import annotations

from dataclasses import dataclass
import math
from pathlib import Path
import re
import struct


PathLike = str | Path


@dataclass(frozen=True)
class ArrivalRecord:
    amplitude: float
    phase_degrees: float
    delay_real_seconds: float
    delay_imag_seconds: float
    source_declination_degrees: float
    receiver_declination_degrees: float
    top_bounces: int
    bottom_bounces: int

    # Names used by the C++ model are useful to validators too.
    @property
    def phase(self) -> float:
        return self.phase_degrees

    @property
    def phase_radians(self) -> float:
        return math.radians(self.phase_degrees)

    @property
    def delay(self) -> complex:
        return complex(self.delay_real_seconds, self.delay_imag_seconds)

    @property
    def top_bounce_count(self) -> int:
        return self.top_bounces

    @property
    def bottom_bounce_count(self) -> int:
        return self.bottom_bounces


@dataclass(frozen=True)
class ArrivalCell:
    arrivals: tuple[ArrivalRecord, ...]

    @property
    def count(self) -> int:
        return len(self.arrivals)


@dataclass(frozen=True)
class ArrivalSource:
    cells: tuple[ArrivalCell, ...]
    maximum_arrivals: int


@dataclass(frozen=True)
class ArrivalsHeader:
    dimension: str
    frequency_hz: float
    source_x_m: tuple[float, ...]
    source_y_m: tuple[float, ...]
    source_depths_m: tuple[float, ...]
    receiver_depths_m: tuple[float, ...]
    receiver_ranges_m: tuple[float, ...]
    receiver_bearings_deg: tuple[float, ...]

    @property
    def dimensions(self) -> str:
        return self.dimension

    @property
    def source_count(self) -> int:
        return (
            len(self.source_x_m)
            * len(self.source_y_m)
            * len(self.source_depths_m)
        )

    @property
    def receiver_cell_count(self) -> int:
        return (
            len(self.receiver_depths_m)
            * len(self.receiver_ranges_m)
            * len(self.receiver_bearings_deg)
        )


@dataclass(frozen=True)
class ArrivalsProduct:
    header: ArrivalsHeader
    sources: tuple[ArrivalSource, ...]

    @property
    def source_count(self) -> int:
        return len(self.sources)

    @property
    def cells(self) -> tuple[ArrivalCell, ...]:
        return tuple(cell for source in self.sources for cell in source.cells)

    @property
    def maximum_arrivals(self) -> int:
        return max((source.maximum_arrivals for source in self.sources), default=0)


# A deliberately strict tokeniser: a quoted dimension is one token and any
# other non-whitespace text is a numeric token consumed by the typed parser.
_ASCII_TOKEN = re.compile(r"'[^'\n]*'|\"[^\"\n]*\"|\S+")


class _AsciiTokens:
    def __init__(self, path: Path) -> None:
        text = path.read_text(encoding="ascii")
        self.path = path
        self.tokens = _ASCII_TOKEN.findall(text)
        self.index = 0

    def take(self, label: str) -> str:
        if self.index >= len(self.tokens):
            raise ValueError(f"{self.path}: truncated while reading {label}")
        token = self.tokens[self.index]
        self.index += 1
        return token

    def require_end(self) -> None:
        if self.index != len(self.tokens):
            raise ValueError(
                f"{self.path}: unexpected trailing ARR text "
                f"starting with {self.tokens[self.index]!r}"
            )


def _number(token: str, path: Path, label: str) -> float:
    try:
        value = float(token.replace("D", "E").replace("d", "e"))
    except ValueError as error:
        raise ValueError(f"{path}: invalid {label}: {token!r}") from error
    if not math.isfinite(value):
        raise ValueError(f"{path}: non-finite {label}")
    return value


def _integer(value: float, path: Path, label: str, *, nonnegative: bool = False) -> int:
    integer = int(value)
    if float(integer) != value:
        raise ValueError(f"{path}: {label} must be an integer")
    if nonnegative and integer < 0:
        raise ValueError(f"{path}: {label} must be non-negative")
    return integer


def _quoted(token: str, path: Path, label: str) -> str:
    if len(token) < 2 or token[0] not in "'\"" or token[-1] != token[0]:
        raise ValueError(f"{path}: {label} must be quoted")
    return token[1:-1].strip()


def _ascii_vector(tokens: _AsciiTokens, count: int, label: str) -> tuple[float, ...]:
    return tuple(_number(tokens.take(f"{label}[{index}]"), tokens.path, label) for index in range(count))


def _ascii_count(tokens: _AsciiTokens, label: str, *, nonnegative: bool = False) -> int:
    return _integer(_number(tokens.take(label), tokens.path, label), tokens.path, label, nonnegative=nonnegative)


def _read_ascii_header(tokens: _AsciiTokens) -> ArrivalsHeader:
    dimension = _quoted(tokens.take("dimension"), tokens.path, "dimension").upper()
    if dimension not in {"2D", "3D"}:
        raise ValueError(f"{tokens.path}: unsupported ARR dimension {dimension!r}")
    frequency = _number(tokens.take("frequency"), tokens.path, "frequency")

    if dimension == "3D":
        nsx = _ascii_count(tokens, "source x count")
        if nsx <= 0:
            raise ValueError(f"{tokens.path}: source x count must be positive")
        sx = _ascii_vector(tokens, nsx, "source x")
        nsy = _ascii_count(tokens, "source y count")
        if nsy <= 0:
            raise ValueError(f"{tokens.path}: source y count must be positive")
        sy = _ascii_vector(tokens, nsy, "source y")
    else:
        sx, sy = (0.0,), (0.0,)
    nsz = _ascii_count(tokens, "source depth count")
    if nsz <= 0:
        raise ValueError(f"{tokens.path}: source depth count must be positive")
    sz = _ascii_vector(tokens, nsz, "source depth")
    nrz = _ascii_count(tokens, "receiver depth count")
    if nrz <= 0:
        raise ValueError(f"{tokens.path}: receiver depth count must be positive")
    rz = _ascii_vector(tokens, nrz, "receiver depth")
    nrr = _ascii_count(tokens, "receiver range count")
    if nrr <= 0:
        raise ValueError(f"{tokens.path}: receiver range count must be positive")
    rr = _ascii_vector(tokens, nrr, "receiver range")
    if any(value < 0.0 for value in rr):
        raise ValueError(f"{tokens.path}: receiver ranges must be non-negative")
    if dimension == "3D":
        ntheta = _ascii_count(tokens, "receiver bearing count")
        if ntheta <= 0:
            raise ValueError(f"{tokens.path}: receiver bearing count must be positive")
        theta = _ascii_vector(tokens, ntheta, "receiver bearing")
    else:
        theta = (0.0,)
    return ArrivalsHeader(dimension, frequency, sx, sy, sz, rz, rr, theta)


def _ascii_arrival(tokens: _AsciiTokens, source_index: int, cell_index: int, arrival_index: int) -> ArrivalRecord:
    path = tokens.path
    values = [_number(tokens.take(f"source {source_index} cell {cell_index} arrival {arrival_index}"), path, "arrival field") for _ in range(8)]
    # ArrMod writes every arrival component as default REAL/COMPLEX (single
    # precision), including the values rendered by list-directed ASCII I/O.
    values = [struct.unpack("<f", struct.pack("<f", value))[0] for value in values]
    return ArrivalRecord(
        amplitude=values[0], phase_degrees=values[1], delay_real_seconds=values[2],
        delay_imag_seconds=values[3], source_declination_degrees=values[4],
        receiver_declination_degrees=values[5],
        top_bounces=_integer(values[6], path, "top bounces", nonnegative=True),
        bottom_bounces=_integer(values[7], path, "bottom bounces", nonnegative=True),
    )


def _actual_cell_count(
    header: ArrivalsHeader, receiver_cell_count: int | None
) -> int:
    regular_count = header.receiver_cell_count
    if receiver_cell_count is None:
        return regular_count
    if receiver_cell_count <= 0 or receiver_cell_count > regular_count:
        raise ValueError("expected receiver cell count is outside ARR header bounds")
    return receiver_cell_count


def parse_ascii_arrivals(
    path: PathLike, *, receiver_cell_count: int | None = None
) -> ArrivalsProduct:
    file_path = Path(path)
    if not file_path.is_file() or file_path.stat().st_size == 0:
        raise ValueError(f"missing or empty ARR file: {file_path}")
    tokens = _AsciiTokens(file_path)
    header = _read_ascii_header(tokens)
    cell_count = _actual_cell_count(header, receiver_cell_count)
    sources: list[ArrivalSource] = []
    for source_index in range(header.source_count):
        maximum = _ascii_count(tokens, f"source {source_index} maximum arrivals", nonnegative=True)
        cells: list[ArrivalCell] = []
        for cell_index in range(cell_count):
            count = _ascii_count(tokens, f"source {source_index} cell {cell_index} count", nonnegative=True)
            if count > maximum:
                raise ValueError(f"{file_path}: source {source_index} cell {cell_index} count exceeds maximum")
            cells.append(ArrivalCell(tuple(_ascii_arrival(tokens, source_index, cell_index, i) for i in range(count))))
        sources.append(ArrivalSource(tuple(cells), maximum))
    tokens.require_end()
    return ArrivalsProduct(header, tuple(sources))


class _FortranRecords:
    def __init__(self, path: Path) -> None:
        self.path = path
        self.data = path.read_bytes()
        self.offset = 0

    def take(self, label: str, expected_size: int | None = None) -> bytes:
        if self.offset + 4 > len(self.data):
            raise ValueError(f"{self.path}: truncated ARR record marker for {label}")
        size = struct.unpack_from("<i", self.data, self.offset)[0]
        self.offset += 4
        if size < 0 or size > len(self.data) - self.offset - 4:
            raise ValueError(f"{self.path}: invalid little-endian record length for {label}: {size}")
        if expected_size is not None and size != expected_size:
            raise ValueError(f"{self.path}: {label} payload length {size} != {expected_size}")
        payload = self.data[self.offset:self.offset + size]
        self.offset += size
        closing = struct.unpack_from("<i", self.data, self.offset)[0]
        self.offset += 4
        if closing != size:
            raise ValueError(f"{self.path}: ARR record marker mismatch for {label}")
        return payload

    def require_end(self) -> None:
        if self.offset != len(self.data):
            raise ValueError(f"{self.path}: unexpected trailing ARR binary data")


def _binary_i32(records: _FortranRecords, label: str) -> int:
    payload = records.take(label, 4)
    return struct.unpack("<i", payload)[0]


def _binary_vector(records: _FortranRecords, count: int, label: str) -> tuple[float, ...]:
    if count <= 0:
        raise ValueError(f"{records.path}: {label} count must be positive")
    payload = records.take(label, 4 * count)
    values = struct.unpack(f"<{count}f", payload)
    if not all(math.isfinite(value) for value in values):
        raise ValueError(f"{records.path}: non-finite {label}")
    return tuple(float(value) for value in values)


def _binary_axis(
    records: _FortranRecords, label: str, *, item_format: str = "f"
) -> tuple[float, ...]:
    """Read Fortran ``WRITE(unit) count, values(1:count)`` as one record."""
    payload = records.take(label)
    item_size = struct.calcsize(item_format)
    if len(payload) < 4 or (len(payload) - 4) % item_size:
        raise ValueError(f"{records.path}: invalid {label} payload length")
    count = struct.unpack_from("<i", payload, 0)[0]
    if count <= 0 or len(payload) != 4 + item_size * count:
        raise ValueError(f"{records.path}: invalid {label} count or payload length")
    values = struct.unpack_from(f"<{count}{item_format}", payload, 4)
    if not all(math.isfinite(value) for value in values):
        raise ValueError(f"{records.path}: non-finite {label}")
    return tuple(float(value) for value in values)


def _read_binary_header(records: _FortranRecords) -> ArrivalsHeader:
    raw_dimension = records.take("dimension", 4)
    try:
        dimension = raw_dimension.decode("ascii").strip().strip("'\"").upper()
    except UnicodeDecodeError as error:
        raise ValueError(f"{records.path}: invalid ARR dimension bytes") from error
    if dimension not in {"2D", "3D"}:
        raise ValueError(f"{records.path}: unsupported ARR dimension {dimension!r}")
    frequency = struct.unpack("<f", records.take("frequency", 4))[0]
    if not math.isfinite(frequency):
        raise ValueError(f"{records.path}: non-finite frequency")
    if dimension == "3D":
        sx = _binary_axis(records, "source x", item_format="d")
        sy = _binary_axis(records, "source y", item_format="d")
    else:
        sx, sy = (0.0,), (0.0,)
    sz = _binary_axis(records, "source depth")
    rz = _binary_axis(records, "receiver depth")
    rr = _binary_axis(records, "receiver range", item_format="d")
    if any(value < 0.0 for value in rr):
        raise ValueError(f"{records.path}: receiver ranges must be non-negative")
    if dimension == "3D":
        theta = _binary_axis(records, "receiver bearing", item_format="d")
    else:
        theta = (0.0,)
    return ArrivalsHeader(dimension, float(frequency), sx, sy, sz, rz, rr, theta)


def parse_binary_arrivals(
    path: PathLike, *, receiver_cell_count: int | None = None
) -> ArrivalsProduct:
    file_path = Path(path)
    if not file_path.is_file() or file_path.stat().st_size == 0:
        raise ValueError(f"missing or empty ARR file: {file_path}")
    records = _FortranRecords(file_path)
    header = _read_binary_header(records)
    cells_per_source = _actual_cell_count(header, receiver_cell_count)
    sources: list[ArrivalSource] = []
    for source_index in range(header.source_count):
        maximum = _binary_i32(records, f"source {source_index} maximum arrivals")
        if maximum < 0:
            raise ValueError(f"{file_path}: negative maximum arrivals")
        cells: list[ArrivalCell] = []
        for cell_index in range(cells_per_source):
            count = _binary_i32(records, f"source {source_index} cell {cell_index} count")
            if count < 0 or count > maximum:
                raise ValueError(f"{file_path}: invalid source {source_index} cell {cell_index} arrival count")
            arrivals: list[ArrivalRecord] = []
            for arrival_index in range(count):
                values = struct.unpack("<8f", records.take(f"source {source_index} cell {cell_index} arrival {arrival_index}", 32))
                if not all(math.isfinite(value) for value in values):
                    raise ValueError(f"{file_path}: non-finite arrival field")
                arrivals.append(ArrivalRecord(
                    amplitude=float(values[0]), phase_degrees=float(values[1]),
                    delay_real_seconds=float(values[2]), delay_imag_seconds=float(values[3]),
                    source_declination_degrees=float(values[4]), receiver_declination_degrees=float(values[5]),
                    top_bounces=_integer(float(values[6]), file_path, "top bounces", nonnegative=True),
                    bottom_bounces=_integer(float(values[7]), file_path, "bottom bounces", nonnegative=True),
                ))
            cells.append(ArrivalCell(tuple(arrivals)))
        sources.append(ArrivalSource(tuple(cells), maximum))
    records.require_end()
    return ArrivalsProduct(header, tuple(sources))


def parse_arrivals(path: PathLike, *, binary: bool | None = None) -> ArrivalsProduct:
    """Parse an ARR product, selecting ASCII/binary by explicit flag or suffix."""
    file_path = Path(path)
    if binary is None:
        data = file_path.read_bytes()
        # A GNU Fortran sequential record starts with a little-endian length;
        # ASCII ARR starts directly with the quoted dimension token.  Looking
        # at the first record also handles binary files whose bytes happen to
        # be valid ASCII (NUL is legal ASCII text).
        binary = (
            len(data) >= 8
            and struct.unpack_from("<i", data, 0)[0] == 4
            and data[4:8] in {b"'2D'", b"'3D'"}
        )
    return parse_binary_arrivals(file_path) if binary else parse_ascii_arrivals(file_path)


read_ascii_arrivals = parse_ascii_arrivals
read_binary_arrivals = parse_binary_arrivals
read_arrivals = parse_arrivals
