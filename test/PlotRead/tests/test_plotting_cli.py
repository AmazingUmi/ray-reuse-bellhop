from __future__ import annotations

import os
from pathlib import Path
import tempfile
import unittest

os.environ.setdefault("MPLBACKEND", "Agg")

import numpy as np

from bellhop_io_py.cli import main
from bellhop_io_py.plotting import transmission_loss
from support import write_little_endian_rectilinear_file


class PlotAndCliTests(unittest.TestCase):
    def setUp(self) -> None:
        self.fixture_directory = tempfile.TemporaryDirectory()
        self.single_frequency_shd = (
            Path(self.fixture_directory.name) / "single.shd"
        )
        write_little_endian_rectilinear_file(
            self.single_frequency_shd, (50.0,)
        )

    def tearDown(self) -> None:
        self.fixture_directory.cleanup()

    def test_transmission_loss(self) -> None:
        np.testing.assert_allclose(
            transmission_loss([1.0 + 0.0j, 0.1 + 0.0j]), [0.0, 20.0]
        )

    def test_cli_creates_plot_and_export(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            output_dir = Path(directory)
            image = output_dir / "field.png"
            archive = output_dir / "field.npz"

            self.assertEqual(
                main(
                    [
                        "plot",
                        str(self.single_frequency_shd),
                        "-o",
                        str(image),
                    ]
                ),
                0,
            )
            self.assertEqual(
                main(
                    [
                        "export",
                        str(self.single_frequency_shd),
                        str(archive),
                    ]
                ),
                0,
            )
            self.assertGreater(image.stat().st_size, 1000)
            with np.load(archive) as exported:
                self.assertEqual(exported["pressure"].shape, (1, 1, 2, 3))
                self.assertEqual(float(exported["frequency_hz"]), 50.0)


if __name__ == "__main__":
    unittest.main()
