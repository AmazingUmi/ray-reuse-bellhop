from __future__ import annotations

from pathlib import Path
import tempfile
import unittest

import numpy as np

from bellhop_io_py.shd import read_shd


class AsciiShdTests(unittest.TestCase):
    def test_all_ascii_frequency_blocks_are_read(self) -> None:
        contents = "\n".join(
            [
                "ASCII reference",
                "rectilin",
                "2 1 1 2 2 100 0",
                "100 200",
                "0",
                "10",
                "20 30",
                "1000 2000",
                "1 2 3 4 5 6 7 8",
                "9 10 11 12 13 14 15 16",
            ]
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "reference.asc"
            path.write_text(contents, encoding="ascii")
            field = read_shd(path, frequency_index=1)

        self.assertEqual(field.pressure.shape, (1, 1, 2, 2))
        np.testing.assert_array_equal(
            field.pressure[0, 0],
            [[9 + 10j, 11 + 12j], [13 + 14j, 15 + 16j]],
        )


if __name__ == "__main__":
    unittest.main()
