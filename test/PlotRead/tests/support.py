from __future__ import annotations

from pathlib import Path
import struct


PROJECT_ROOT = Path(__file__).resolve().parents[3]
SINGLE_FREQUENCY_SHD = (
    PROJECT_ROOT / "test/test_origin_bellhop/MunkB_Coh_CervenyC.shd"
)
MULTI_FREQUENCY_SHD = (
    PROJECT_ROOT / "test/test_ray_reuse/MunkB_Coh_CervenyC_MultiFreq.shd"
)


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
