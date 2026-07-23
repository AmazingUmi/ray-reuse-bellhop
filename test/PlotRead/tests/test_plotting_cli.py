from __future__ import annotations

import os
from pathlib import Path
import tempfile
import unittest

os.environ.setdefault("MPLBACKEND", "Agg")

import numpy as np

from bellhop_io_py.cli import main
from bellhop_io_py.plotting import transmission_loss
from support import SINGLE_FREQUENCY_SHD


class PlotAndCliTests(unittest.TestCase):
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
                main(["plot", str(SINGLE_FREQUENCY_SHD), "-o", str(image)]), 0
            )
            self.assertEqual(
                main(["export", str(SINGLE_FREQUENCY_SHD), str(archive)]), 0
            )
            self.assertGreater(image.stat().st_size, 1000)
            with np.load(archive) as exported:
                self.assertEqual(exported["pressure"].shape, (1, 1, 201, 501))
                self.assertEqual(float(exported["frequency_hz"]), 50.0)


if __name__ == "__main__":
    unittest.main()
