from __future__ import annotations

from pathlib import Path
import struct
import tempfile
import unittest

import numpy as np

from bellhop_io_py.shd import ShdFormatError, ShdReader
from support import (
    MULTI_FREQUENCY_SHD,
    SINGLE_FREQUENCY_SHD,
    write_big_endian_tl_file,
)


class BinaryShdTests(unittest.TestCase):
    def test_header_matches_reference_case(self) -> None:
        header = ShdReader(SINGLE_FREQUENCY_SHD).header

        self.assertEqual(header.plot_type, "rectilin")
        self.assertEqual(header.dimensions, (1, 1, 1, 1, 1, 201, 501))
        np.testing.assert_array_equal(header.frequencies_hz, [50.0])
        np.testing.assert_allclose(header.source_depths_m, [1000.0])
        np.testing.assert_allclose(
            header.receiver_depths_m[[0, -1]], [0.0, 5000.0]
        )
        np.testing.assert_allclose(
            header.receiver_ranges_m[[0, -1]], [0.0, 100000.0]
        )

    def test_first_pressure_value_matches_raw_record(self) -> None:
        reader = ShdReader(SINGLE_FREQUENCY_SHD)
        field = reader.read()
        with SINGLE_FREQUENCY_SHD.open("rb") as stream:
            stream.seek(10 * reader.header.record_bytes)
            real, imaginary = struct.unpack("<ff", stream.read(8))

        self.assertEqual(field.pressure.shape, (1, 1, 201, 501))
        self.assertEqual(field.pressure.dtype, np.complex64)
        self.assertEqual(field.pressure[0, 0, 0, 0], complex(real, imaginary))

    def test_frequency_value_and_index_select_the_same_records(self) -> None:
        reader = ShdReader(MULTI_FREQUENCY_SHD)
        by_index = reader.read(frequency_index=7)
        by_value = reader.read(frequency_hz=201.0)

        self.assertEqual(by_index.frequency_hz, 200.0)
        self.assertEqual(by_value.frequency_index, 7)
        np.testing.assert_array_equal(by_index.pressure, by_value.pressure)

    def test_invalid_selector_is_rejected(self) -> None:
        reader = ShdReader(SINGLE_FREQUENCY_SHD)
        with self.assertRaises(IndexError):
            reader.read(frequency_index=1)
        with self.assertRaises(ValueError):
            reader.read(source_x_km=0.0)

    def test_big_endian_tl_coordinates_and_source_offset(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "field3d.shd"
            write_big_endian_tl_file(path)
            reader = ShdReader(path)
            field = reader.read(source_x_km=1.8, source_y_km=0.9)

        self.assertEqual(reader.header.byte_order, "big")
        np.testing.assert_allclose(reader.header.source_x_m, [0.0, 1500.0, 3000.0])
        np.testing.assert_allclose(reader.header.source_y_m, [0.0, 1000.0])
        self.assertEqual((field.source_x_index, field.source_y_index), (1, 1))
        np.testing.assert_array_equal(field.pressure[0, 0, 0], [3 - 3j, 3.5 - 3.5j])

    def test_truncated_file_is_rejected(self) -> None:
        data = SINGLE_FREQUENCY_SHD.read_bytes()
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "truncated.shd"
            path.write_bytes(data[:-4])
            with self.assertRaises(ShdFormatError):
                ShdReader(path)


if __name__ == "__main__":
    unittest.main()
