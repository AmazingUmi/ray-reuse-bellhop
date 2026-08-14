from __future__ import annotations

from pathlib import Path
import struct
import tempfile
import unittest


CODES_ROOT = Path(__file__).resolve().parents[1]
import sys

sys.path.insert(0, str(CODES_ROOT))

from arrivals_io import parse_ascii_arrivals, parse_binary_arrivals


def ascii_fixture() -> str:
    return """'2D'
250.0
2 10.0 20.0
2 5.0 15.0
2 100.0 200.0
1
1
2.0 10.0 0.1 0.0 -30.0 5.0 0 1
0
1
3.0 20.0 0.2 0.0 30.0 -5.0 1 0
0
2
0
0
0
0
"""


def record(payload: bytes) -> bytes:
    return struct.pack("<i", len(payload)) + payload + struct.pack("<i", len(payload))


def binary_fixture() -> bytes:
    output = bytearray()
    output += record(b"'2D'")
    output += record(struct.pack("<f", 250.0))
    output += record(struct.pack("<i2f", 2, 10.0, 20.0))
    output += record(struct.pack("<i2f", 2, 5.0, 15.0))
    output += record(struct.pack("<i2d", 2, 100.0, 200.0))
    for maximum, cells in (
        (1, ((2.0, 10.0, 0.1, 0.0, -30.0, 5.0, 0.0, 1.0), (), (3.0, 20.0, 0.2, 0.0, 30.0, -5.0, 1.0, 0.0), ())),
        (2, ((), (), (), ())),
    ):
        output += record(struct.pack("<i", maximum))
        for arrivals in cells:
            output += record(struct.pack("<i", len(arrivals) // 8 if arrivals else 0))
            if arrivals:
                output += record(struct.pack("<8f", *arrivals))
    return bytes(output)


class ArrivalReaderTests(unittest.TestCase):
    def write(self, root: Path, name: str, payload: bytes | str) -> Path:
        path = root / name
        if isinstance(payload, str):
            path.write_text(payload, encoding="ascii")
        else:
            path.write_bytes(payload)
        return path

    def test_ascii_and_binary_have_canonical_semantics(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            ascii_product = parse_ascii_arrivals(self.write(root, "a.arr", ascii_fixture()))
            binary_product = parse_binary_arrivals(self.write(root, "b.arr", binary_fixture()))
        self.assertEqual(ascii_product.header.source_depths_m, (10.0, 20.0))
        self.assertEqual(ascii_product.source_count, 2)
        self.assertEqual(ascii_product.cells[0].arrivals[0], binary_product.cells[0].arrivals[0])
        self.assertEqual(ascii_product.cells[2].arrivals[0], binary_product.cells[2].arrivals[0])
        self.assertEqual(ascii_product.cells[1].count, 0)

    def test_ascii_rejects_trailing_text_and_nonfinite_fields(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with self.assertRaisesRegex(ValueError, "trailing"):
                parse_ascii_arrivals(self.write(root, "trailing.arr", ascii_fixture() + "garbage\n"))
            with self.assertRaisesRegex(ValueError, "non-finite"):
                parse_ascii_arrivals(self.write(root, "nan.arr", ascii_fixture().replace("2.0 10.0", "nan 10.0")))

    def test_binary_rejects_marker_mismatch_truncation_endian_and_count(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            valid = binary_fixture()
            bad_marker = bytearray(valid)
            bad_marker[4 + 4] ^= 1
            with self.assertRaisesRegex(ValueError, "marker"):
                parse_binary_arrivals(self.write(root, "marker.arr", bytes(bad_marker)))
            with self.assertRaisesRegex(ValueError, "truncated|invalid|length"):
                parse_binary_arrivals(self.write(root, "short.arr", valid[:-1]))
            with self.assertRaisesRegex(ValueError, "invalid|length"):
                parse_binary_arrivals(self.write(root, "big.arr", b"\x00\x00\x00\x04" + valid[4:]))
            count_bad = bytearray(valid)
            # The first source maximum is the first record after the header.
            offset = 92  # first source maximum-arrivals record
            first_cell_count = offset + 12 + 4
            count_bad[first_cell_count:first_cell_count + 4] = struct.pack("<i", 2)
            with self.assertRaisesRegex(ValueError, "count|maximum"):
                parse_binary_arrivals(self.write(root, "count.arr", bytes(count_bad)))

    def test_irregular_layout_uses_caller_supplied_actual_cell_count(self) -> None:
        irregular = """'2D'
250.0
1 10.0
2 5.0 15.0
2 100.0 200.0
0
0
0
"""
        with tempfile.TemporaryDirectory() as directory:
            path = self.write(Path(directory), "irregular.arr", irregular)
            product = parse_ascii_arrivals(path, receiver_cell_count=2)
            self.assertEqual(len(product.sources[0].cells), 2)
            with self.assertRaisesRegex(ValueError, "truncated"):
                parse_ascii_arrivals(path)


if __name__ == "__main__":
    unittest.main()
