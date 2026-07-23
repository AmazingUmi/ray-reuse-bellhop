"""Read Acoustic Toolbox binary and ASCII shade files.

The binary layout follows ``RWSHDFile.f90``: ten fixed-size header records,
followed by one complex-single pressure record for every
frequency/source/bearing/source-depth/receiver-depth combination.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import struct
from typing import Final

import numpy as np
from numpy.typing import NDArray


FloatArray = NDArray[np.float64]
ComplexArray = NDArray[np.complex64]
_HEADER_RECORDS: Final = 10


class ShdFormatError(ValueError):
    """Raised when a file does not follow the supported SHD layout."""


@dataclass(frozen=True, slots=True)
class ShdHeader:
    title: str
    plot_type: str
    frequencies_hz: FloatArray
    nominal_frequency_hz: float
    attenuation: float
    bearings_deg: FloatArray
    source_x_m: FloatArray
    source_y_m: FloatArray
    source_depths_m: FloatArray
    receiver_depths_m: FloatArray
    receiver_ranges_m: FloatArray
    record_bytes: int
    byte_order: str

    @property
    def dimensions(self) -> tuple[int, int, int, int, int, int, int]:
        """Return ``(frequency, bearing, sx, sy, sz, rz, range)`` sizes."""
        return (
            self.frequencies_hz.size,
            self.bearings_deg.size,
            self.source_x_m.size,
            self.source_y_m.size,
            self.source_depths_m.size,
            self.receiver_depths_m.size,
            self.receiver_ranges_m.size,
        )

    @property
    def receivers_per_range(self) -> int:
        return 1 if self.plot_type == "irregular" else self.receiver_depths_m.size


@dataclass(frozen=True, slots=True)
class PressureField:
    """One frequency and one horizontal source position from an SHD file.

    ``pressure`` is indexed as
    ``[bearing, source_depth, receiver_depth, receiver_range]``.
    """

    header: ShdHeader
    pressure: ComplexArray
    frequency_index: int
    source_x_index: int
    source_y_index: int

    @property
    def frequency_hz(self) -> float:
        return float(self.header.frequencies_hz[self.frequency_index])

    @property
    def source_x_m(self) -> float:
        return float(self.header.source_x_m[self.source_x_index])

    @property
    def source_y_m(self) -> float:
        return float(self.header.source_y_m[self.source_y_index])


class ShdReader:
    """Validated, slice-oriented reader for a binary ``.shd``/``.grn`` file."""

    def __init__(self, filename: str | Path):
        self.path = Path(filename)
        if not self.path.is_file():
            raise FileNotFoundError(f"SHD file does not exist: {self.path}")
        self._file_size = self.path.stat().st_size
        self._byte_order, self._record_bytes = self._detect_layout()
        self.header = self._read_header()
        self._validate_file_size()

    def _detect_layout(self) -> tuple[str, int]:
        if self._file_size < 4:
            raise ShdFormatError(f"{self.path}: file is too small to be an SHD file")
        with self.path.open("rb") as stream:
            first_word = stream.read(4)
        candidates: list[tuple[str, int]] = []
        for byte_order in ("<", ">"):
            record_words = struct.unpack(f"{byte_order}i", first_word)[0]
            record_bytes = 4 * record_words
            if (
                record_words >= 41
                and record_bytes * _HEADER_RECORDS <= self._file_size
                and self._file_size % record_bytes == 0
            ):
                candidates.append((byte_order, record_bytes))
        if len(candidates) != 1:
            raise ShdFormatError(
                f"{self.path}: cannot determine a valid SHD record size and byte order"
            )
        return candidates[0]

    def _read_record_array(
        self, record: int, dtype: str, count: int
    ) -> np.ndarray:
        item_size = np.dtype(dtype).itemsize
        if count * item_size > self._record_bytes:
            raise ShdFormatError(
                f"{self.path}: record {record + 1} is too short for {count} values"
            )
        with self.path.open("rb") as stream:
            stream.seek(record * self._record_bytes)
            data = np.fromfile(stream, dtype=f"{self._byte_order}{dtype}", count=count)
        if data.size != count:
            raise ShdFormatError(f"{self.path}: truncated record {record + 1}")
        return data

    def _read_header(self) -> ShdHeader:
        with self.path.open("rb") as stream:
            first_record = stream.read(self._record_bytes)
            stream.seek(self._record_bytes)
            plot_record = stream.read(10)
            stream.seek(2 * self._record_bytes)
            dimension_record = stream.read(44)

        if len(first_record) < 84 or len(dimension_record) != 44:
            raise ShdFormatError(f"{self.path}: truncated SHD header")

        title = first_record[4:84].decode("ascii", errors="replace").rstrip("\x00 ")
        plot_type = plot_record.decode("ascii", errors="replace").rstrip("\x00 ")
        values = struct.unpack(f"{self._byte_order}7i2d", dimension_record)
        nfreq, ntheta, nsx, nsy, nsz, nrz, nrr = values[:7]
        dimensions = (nfreq, ntheta, nsx, nsy, nsz, nrz, nrr)
        if any(value <= 0 for value in dimensions):
            raise ShdFormatError(f"{self.path}: invalid SHD dimensions {dimensions}")

        frequencies = self._read_record_array(3, "f8", nfreq).astype(np.float64)
        bearings = self._read_record_array(4, "f8", ntheta).astype(np.float64)

        if plot_type.startswith("TL"):
            source_x = np.linspace(
                *self._read_record_array(5, "f8", 2), nsx, dtype=np.float64
            )
            source_y = np.linspace(
                *self._read_record_array(6, "f8", 2), nsy, dtype=np.float64
            )
        else:
            source_x = self._read_record_array(5, "f8", nsx).astype(np.float64)
            source_y = self._read_record_array(6, "f8", nsy).astype(np.float64)

        return ShdHeader(
            title=title,
            plot_type=plot_type,
            frequencies_hz=frequencies,
            nominal_frequency_hz=float(values[7]),
            attenuation=float(values[8]),
            bearings_deg=bearings,
            source_x_m=source_x,
            source_y_m=source_y,
            source_depths_m=self._read_record_array(7, "f4", nsz).astype(np.float64),
            receiver_depths_m=self._read_record_array(8, "f4", nrz).astype(np.float64),
            receiver_ranges_m=self._read_record_array(9, "f8", nrr).astype(np.float64),
            record_bytes=self._record_bytes,
            byte_order="little" if self._byte_order == "<" else "big",
        )

    def _validate_file_size(self) -> None:
        nfreq, ntheta, nsx, nsy, nsz, _, _ = self.header.dimensions
        data_records = (
            nfreq
            * nsx
            * nsy
            * ntheta
            * nsz
            * self.header.receivers_per_range
        )
        expected_size = (_HEADER_RECORDS + data_records) * self._record_bytes
        if self._file_size != expected_size:
            raise ShdFormatError(
                f"{self.path}: size is {self._file_size} bytes; "
                f"header dimensions require {expected_size} bytes"
            )

    @staticmethod
    def _nearest_index(axis: FloatArray, value: float) -> int:
        return int(np.abs(axis - value).argmin())

    def read(
        self,
        *,
        frequency_hz: float | None = None,
        frequency_index: int | None = None,
        source_x_km: float | None = None,
        source_y_km: float | None = None,
    ) -> PressureField:
        """Read one frequency and horizontal source position.

        Frequencies and source coordinates are selected by nearest value.
        Python indices are zero-based.
        """
        if frequency_hz is not None and frequency_index is not None:
            raise ValueError("choose frequency_hz or frequency_index, not both")
        if (source_x_km is None) != (source_y_km is None):
            raise ValueError("source_x_km and source_y_km must be supplied together")

        if frequency_index is None:
            frequency_index = (
                0
                if frequency_hz is None
                else self._nearest_index(self.header.frequencies_hz, frequency_hz)
            )
        if not 0 <= frequency_index < self.header.frequencies_hz.size:
            raise IndexError(
                f"frequency_index {frequency_index} is outside "
                f"[0, {self.header.frequencies_hz.size - 1}]"
            )

        if source_x_km is None:
            source_x_index = source_y_index = 0
        else:
            source_x_index = self._nearest_index(
                self.header.source_x_m, source_x_km * 1000.0
            )
            source_y_index = self._nearest_index(
                self.header.source_y_m, source_y_km * 1000.0
            )

        _, ntheta, _, nsy, nsz, _, nrr = self.header.dimensions
        nrz_data = self.header.receivers_per_range
        first_data_record = _HEADER_RECORDS + (
            (
                (
                    frequency_index * self.header.source_x_m.size
                    + source_x_index
                )
                * nsy
                + source_y_index
            )
            * ntheta
            * nsz
            * nrz_data
        )
        selected_records = ntheta * nsz * nrz_data
        words_per_record = self._record_bytes // 4
        raw_records = np.memmap(
            self.path,
            mode="r",
            dtype=f"{self._byte_order}f4",
            offset=first_data_record * self._record_bytes,
            shape=(selected_records, words_per_record),
        )
        interleaved = np.asarray(raw_records[:, : 2 * nrr])
        pressure = np.asarray(
            interleaved[:, 0::2] + 1j * interleaved[:, 1::2],
            dtype=np.complex64,
        ).reshape(ntheta, nsz, nrz_data, nrr)

        return PressureField(
            header=self.header,
            pressure=pressure,
            frequency_index=frequency_index,
            source_x_index=source_x_index,
            source_y_index=source_y_index,
        )


def _read_ascii_shd(
    filename: str | Path,
    *,
    frequency_hz: float | None,
    frequency_index: int | None,
) -> PressureField:
    path = Path(filename)
    with path.open("r", encoding="ascii") as stream:
        title = stream.readline().rstrip()
        plot_type = stream.readline().rstrip()
        tokens = stream.read().split()
    try:
        values = np.asarray(tokens, dtype=np.float64)
    except ValueError as error:
        raise ShdFormatError(f"{path}: non-numeric value in ASCII shade data") from error

    cursor = 0

    def take(count: int) -> FloatArray:
        nonlocal cursor
        result = values[cursor : cursor + count]
        cursor += count
        if result.size != count:
            raise ShdFormatError(f"{path}: truncated ASCII shade file")
        return result

    raw_dimensions = take(5)
    if np.any(raw_dimensions <= 0) or np.any(
        raw_dimensions != raw_dimensions.astype(np.int64)
    ):
        raise ShdFormatError(f"{path}: invalid ASCII dimensions")
    dimensions = raw_dimensions.astype(np.int64)
    nfreq, ntheta, nsz, nrz, nrr = map(int, dimensions)
    nominal_frequency_hz, attenuation = take(2)
    frequencies = take(nfreq)
    bearings = take(ntheta)
    source_depths = take(nsz)
    receiver_depths = take(nrz)
    receiver_ranges = take(nrr)
    expected_pressure_values = nfreq * ntheta * nsz * nrz * 2 * nrr
    interleaved = take(expected_pressure_values).reshape(
        nfreq, ntheta, nsz, nrz, 2 * nrr
    )
    if cursor != values.size:
        raise ShdFormatError(f"{path}: unexpected trailing ASCII shade values")

    if frequency_hz is not None and frequency_index is not None:
        raise ValueError("choose frequency_hz or frequency_index, not both")
    if frequency_index is None:
        frequency_index = (
            0
            if frequency_hz is None
            else int(np.abs(frequencies - frequency_hz).argmin())
        )
    if not 0 <= frequency_index < nfreq:
        raise IndexError(f"frequency_index {frequency_index} is outside [0, {nfreq - 1}]")

    selected = interleaved[frequency_index]
    pressure = np.asarray(
        selected[..., 0::2] + 1j * selected[..., 1::2], dtype=np.complex64
    )
    header = ShdHeader(
        title=title,
        plot_type=plot_type.strip(),
        frequencies_hz=frequencies,
        nominal_frequency_hz=float(nominal_frequency_hz),
        attenuation=float(attenuation),
        bearings_deg=bearings,
        source_x_m=np.zeros(1),
        source_y_m=np.zeros(1),
        source_depths_m=source_depths,
        receiver_depths_m=receiver_depths,
        receiver_ranges_m=receiver_ranges,
        record_bytes=0,
        byte_order="text",
    )
    return PressureField(header, pressure, frequency_index, 0, 0)


def read_shd(
    filename: str | Path,
    *,
    frequency_hz: float | None = None,
    frequency_index: int | None = None,
    source_x_km: float | None = None,
    source_y_km: float | None = None,
) -> PressureField:
    """Read a binary or ASCII shade file using a consistent result model."""
    path = Path(filename)
    is_ascii = path.name.upper() == "ASCFIL" or path.suffix.lower() in {".asc", ".txt"}
    if is_ascii:
        if source_x_km is not None or source_y_km is not None:
            raise ValueError("ASCII shade files do not contain horizontal source axes")
        return _read_ascii_shd(
            path, frequency_hz=frequency_hz, frequency_index=frequency_index
        )
    return ShdReader(path).read(
        frequency_hz=frequency_hz,
        frequency_index=frequency_index,
        source_x_km=source_x_km,
        source_y_km=source_y_km,
    )
