from __future__ import annotations

from pathlib import Path
import tempfile
import time
import unittest

import sys

CODES_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(CODES_ROOT))

from arrivals_io import ArrivalCell, ArrivalRecord, ArrivalsHeader, ArrivalsProduct, ArrivalSource
from validate_i8_arrivals import _effects, _manifest, _require_executable_identity, _require_fresh, compare_arrival_products


def product(arrivals: tuple[ArrivalRecord, ...]) -> ArrivalsProduct:
    return ArrivalsProduct(
        ArrivalsHeader("2D", 1000.0, (0.0,), (0.0,), (50.0,), (50.0,), (100.0,), (0.0,)),
        (ArrivalSource((ArrivalCell(arrivals),), len(arrivals)),),
    )


def arrival(amplitude: float = 1.0) -> ArrivalRecord:
    return ArrivalRecord(amplitude, 12.0, 0.1, 0.0, -10.0, 10.0, 0, 1)


class ArrivalParityTests(unittest.TestCase):
    def test_order_count_and_ulp_gates(self) -> None:
        baseline = product((arrival(1.0), arrival(2.0)))
        self.assertEqual(compare_arrival_products(baseline, baseline, "same")["arrival_records"], 2)
        with self.assertRaisesRegex(ValueError, "amplitude"):
            compare_arrival_products(baseline, product((arrival(2.0), arrival(1.0))), "reordered")
        with self.assertRaisesRegex(ValueError, "count"):
            compare_arrival_products(baseline, product((arrival(1.0),)), "count")
        with self.assertRaisesRegex(ValueError, "ULP"):
            compare_arrival_products(product((arrival(1.0),)), product((arrival(1.01),)), "threshold")
        two_cells = ArrivalSource((ArrivalCell((arrival(),)),) * 2, 1)
        left_header = ArrivalsProduct(
            ArrivalsHeader("2D", 1000.0, (0.0,), (0.0,), (50.0,), (25.0, 50.0), (100.0,), (0.0,)),
            (two_cells,),
        )
        mismatched_header = ArrivalsProduct(
            ArrivalsHeader("2D", 1000.0, (0.0,), (0.0,), (50.0,), (50.0,), (100.0, 200.0), (0.0,)),
            (two_cells,),
        )
        with self.assertRaisesRegex(ValueError, "dimensions"):
            compare_arrival_products(left_header, mismatched_header, "header")

    def test_missing_zero_effect_is_rejected(self) -> None:
        reflected = arrival()
        reflected = ArrivalRecord(
            reflected.amplitude, 360.0, reflected.delay_real_seconds,
            reflected.delay_imag_seconds, reflected.source_declination_degrees,
            reflected.receiver_declination_degrees, 1, 1,
        )
        base = product((reflected, reflected))
        zero = product(())
        alternate = product((arrival(2.0),)).sources[0]
        multi_source = ArrivalsProduct(base.header, (base.sources[0], alternate))
        irregular = ArrivalsProduct(
            ArrivalsHeader("2D", 1000.0, (0.0,), (0.0,), (50.0,), (20.0, 50.0, 80.0), (100.0,), (0.0,)),
            (ArrivalSource((ArrivalCell((reflected,)),) * 3, 1),),
        )
        products = {
            "arrival_geometric_hat_ascii": base,
            "arrival_geometric_hat_binary": base,
            "arrival_geometric_hat_ray_centered": base,
            "arrival_geometric_hat_ray_centered_binary": base,
            "arrival_geometric_gaussian_irregular": irregular,
            "arrival_line_directional_multisource": multi_source,
            "arrival_multi_source": multi_source,
            "arrival_multi_source_binary": multi_source,
            "arrival_zero": zero,
        }
        _effects(products)
        products["arrival_zero"] = base
        with self.assertRaisesRegex(ValueError, "zero-arrival"):
            _effects(products)
        products["arrival_zero"] = zero
        products["arrival_multi_source"] = ArrivalsProduct(base.header, (base.sources[0],))
        with self.assertRaisesRegex(ValueError, "point-source multi-source"):
            _effects(products)

    def test_freshness_and_manifest_identity_rejections(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            executable, environment, product_path = (root / "bellhop", root / "case.env", root / "case.arr")
            for path in (executable, environment, product_path):
                path.write_text("x", encoding="ascii")
            now = time.time_ns()
            product_path.touch()
            executable.touch()
            with self.assertRaisesRegex(ValueError, "stale"):
                _require_fresh(product_path, environment, executable, root / "run_manifest.json")
            manifest = root / "origin" / "expected" / "single" / "run_manifest.json"
            manifest.parent.mkdir(parents=True)
            manifest.write_text('{"version":"origin","case_id":"wrong","output_kind":"arrivals_ascii","last_stage":"test"}', encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "identity"):
                _manifest(root, "origin", "expected")
            with self.assertRaisesRegex(ValueError, "executable identity"):
                _require_executable_identity({"executable": str(root / "other")}, executable, manifest)


if __name__ == "__main__":
    unittest.main()
