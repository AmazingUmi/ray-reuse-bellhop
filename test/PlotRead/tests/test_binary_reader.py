from __future__ import annotations

from pathlib import Path
import struct
import tempfile
import unittest

import numpy as np

from bellhop_io_py.shd import ShdFormatError, ShdReader
from support import (
    write_big_endian_tl_file,
    write_little_endian_rectilinear_file,
)


class BinaryShdTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory()
        directory = Path(self.temporary_directory.name)
        self.single_frequency_shd = directory / "single.shd"
        self.multi_frequency_shd = directory / "multi.shd"
        write_little_endian_rectilinear_file(
            self.single_frequency_shd, (50.0,)
        )
        write_little_endian_rectilinear_file(
            self.multi_frequency_shd, (100.0, 200.0, 300.0)
        )

    def tearDown(self) -> None:
        self.temporary_directory.cleanup()

    def test_header_matches_reference_case(self) -> None:
        header = ShdReader(self.single_frequency_shd).header

        self.assertEqual(header.plot_type, "rectilin")
        self.assertEqual(header.dimensions, (1, 1, 1, 1, 1, 2, 3))
        np.testing.assert_array_equal(header.frequencies_hz, [50.0])
        np.testing.assert_allclose(header.source_depths_m, [10.0])
        np.testing.assert_allclose(header.receiver_depths_m, [20.0, 30.0])
        np.testing.assert_allclose(
            header.receiver_ranges_m, [1000.0, 2000.0, 3000.0]
        )

    def test_first_pressure_value_matches_raw_record(self) -> None:
        reader = ShdReader(self.single_frequency_shd)
        field = reader.read()
        with self.single_frequency_shd.open("rb") as stream:
            stream.seek(10 * reader.header.record_bytes)
            real, imaginary = struct.unpack("<ff", stream.read(8))

        self.assertEqual(field.pressure.shape, (1, 1, 2, 3))
        self.assertEqual(field.pressure.dtype, np.complex64)
        self.assertEqual(field.pressure[0, 0, 0, 0], complex(real, imaginary))

    def test_frequency_value_and_index_select_the_same_records(self) -> None:
        reader = ShdReader(self.multi_frequency_shd)
        by_index = reader.read(frequency_index=1)
        by_value = reader.read(frequency_hz=201.0)

        self.assertEqual(by_index.frequency_hz, 200.0)
        self.assertEqual(by_value.frequency_index, 1)
        np.testing.assert_array_equal(by_index.pressure, by_value.pressure)

    def test_invalid_selector_is_rejected(self) -> None:
        reader = ShdReader(self.single_frequency_shd)
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
        data = self.single_frequency_shd.read_bytes()
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "truncated.shd"
            path.write_bytes(data[:-4])
            with self.assertRaises(ShdFormatError):
                ShdReader(path)


if __name__ == "__main__":
    unittest.main()
