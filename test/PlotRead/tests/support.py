from __future__ import annotations

from pathlib import Path
import struct


def write_little_endian_rectilinear_file(
    path: Path, frequencies_hz: tuple[float, ...]
) -> None:
    """Create a compact, deterministic Bellhop rectilinear SHD fixture."""
    record_bytes = 164
    ntheta = nsx = nsy = nsz = 1
    nrz, nrr = 2, 3
    records = [
        bytearray(record_bytes)
        for _ in range(10 + len(frequencies_hz) * nrz)
    ]

    def pack(record: int, fmt: str, *values: object) -> None:
        struct.pack_into(f"<{fmt}", records[record], 0, *values)

    pack(0, "i80s", record_bytes // 4, b"Synthetic rectilinear SHD fixture")
    pack(1, "10s", b"rectilin  ")
    pack(
        2,
        "7i2d",
        len(frequencies_hz),
        ntheta,
        nsx,
        nsy,
        nsz,
        nrz,
        nrr,
        frequencies_hz[0],
        0.0,
    )
    pack(3, f"{len(frequencies_hz)}d", *frequencies_hz)
    pack(4, "d", 0.0)
    pack(5, "d", 0.0)
    pack(6, "d", 0.0)
    pack(7, "f", 10.0)
    pack(8, "2f", 20.0, 30.0)
    pack(9, "3d", 1000.0, 2000.0, 3000.0)

    for frequency_index in range(len(frequencies_hz)):
        for receiver_depth_index in range(nrz):
            values: list[float] = []
            for receiver_range_index in range(nrr):
                base = (
                    100 * (frequency_index + 1)
                    + 10 * (receiver_depth_index + 1)
                    + receiver_range_index
                    + 1
                )
                values.extend((base / 1000.0, -base / 2000.0))
            record = 10 + frequency_index * nrz + receiver_depth_index
            pack(record, "6f", *values)

    path.write_bytes(b"".join(records))


def write_big_endian_tl_file(path: Path) -> None:
    """Create a minimal FIELD3D-style SHD fixture."""
    record_bytes = 164
    records = [bytearray(record_bytes) for _ in range(16)]

    def pack(record: int, fmt: str, *values: object) -> None:
        struct.pack_into(f">{fmt}", records[record], 0, *values)

    pack(0, "i80s", record_bytes // 4, b"Big-endian TL reference")
    pack(1, "10s", b"TL        ")
    pack(2, "7i2d", 1, 1, 3, 2, 1, 1, 2, 100.0, 0.0)
    pack(3, "d", 100.0)
    pack(4, "d", 0.0)
    pack(5, "2d", 0.0, 3000.0)
    pack(6, "2d", 0.0, 1000.0)
    pack(7, "f", 20.0)
    pack(8, "f", 30.0)
    pack(9, "2d", 1000.0, 2000.0)
    for index in range(6):
        pack(10 + index, "4f", index, -index, index + 0.5, -(index + 0.5))
    path.write_bytes(b"".join(records))
