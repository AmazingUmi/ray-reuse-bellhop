#!/usr/bin/env python3
"""Exercise the F2CPP CLI's atomic I8 product lifecycle.

The validator deliberately runs from a fresh private root.  Its JSON contains
only stable fixture names and normalised command paths, so an unchanged binary
and source tree produce byte-identical evidence even though the temporary run
directory is intentionally random.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
from typing import Final


PRODUCT_SUFFIXES: Final = ("shd", "ray", "arr")
SEQUENCE: Final = (
    ("coherent_1", "cli_coherent.env", "shd"),
    ("ray", "cli_ray.env", "ray"),
    ("arrivals_ascii", "cli_arrival_ascii.env", "arr"),
    ("arrivals_binary", "cli_arrival_binary.env", "arr"),
    ("eigenray", "cli_eigenray_gaussian.env", "ray"),
    ("coherent_2", "cli_coherent.env", "shd"),
)
ZERO_SEQUENCE: Final = ("eigenray_zero", "cli_eigenray_zero.env", "ray")
PROJECT_ROOT: Final = Path(__file__).resolve().parents[3]
SOURCE_FILES: Final = (
    "Bellhop_F2CPP/app/main.cpp",
    "Bellhop_F2CPP/src/io/arrival_writer.cpp",
    "Bellhop_F2CPP/src/io/eigenray_writer.cpp",
    "Bellhop_F2CPP/src/io/ray_prefix_writer.cpp",
    "Bellhop_F2CPP/src/io/ray_writer.cpp",
    "Bellhop_F2CPP/src/io/shd_writer.cpp",
)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def identity(path: Path) -> dict[str, object]:
    if not path.is_file():
        raise ValueError(f"required file is absent: {path}")
    return {"sha256": sha256(path), "mtime_ns": path.stat().st_mtime_ns}


def product_info(path: Path) -> dict[str, object]:
    if not path.exists():
        return {"type": "missing", "size_bytes": 0, "sha256": None}
    if path.is_file():
        return {"type": "file", "size_bytes": path.stat().st_size, "sha256": sha256(path)}
    if path.is_dir():
        return {"type": "directory", "size_bytes": 0, "sha256": None}
    return {"type": "other", "size_bytes": 0, "sha256": None}


def _copy_fixture(fixture_dir: Path, fixture: str, root: Path) -> None:
    source = fixture_dir / fixture
    if not source.is_file():
        raise ValueError(f"fixture is absent: {source}")
    shutil.copyfile(source, root.with_suffix(".env"))


def _remove(path: Path) -> None:
    if path.is_dir() and not path.is_symlink():
        shutil.rmtree(path)
    else:
        path.unlink(missing_ok=True)


def _prepare_stale_products(root: Path) -> None:
    for suffix in PRODUCT_SUFFIXES:
        path = root.with_suffix(f".{suffix}")
        _remove(path)
        path.write_bytes(f"stale-{suffix}\n".encode("ascii"))
        temporary = root.with_suffix(f".{suffix}.tmp")
        _remove(temporary)
        temporary.write_bytes(f"stale-{suffix}-temporary\n".encode("ascii"))


def _run(executable: Path, root: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(executable), str(root)], text=True, capture_output=True, check=False
    )


def _formal_products(root: Path) -> dict[str, dict[str, object]]:
    return {suffix: product_info(root.with_suffix(f".{suffix}")) for suffix in PRODUCT_SUFFIXES}


def _require_success_state(root: Path, expected: str) -> dict[str, dict[str, object]]:
    products = _formal_products(root)
    for suffix, info in products.items():
        if suffix == expected:
            if info["type"] != "file" or not info["size_bytes"]:
                raise ValueError(f"expected nonempty .{suffix} product was not published")
        elif info["type"] != "missing":
            raise ValueError(f"stale incompatible .{suffix} product was retained")
        temporary = root.with_suffix(f".{suffix}.tmp")
        if temporary.exists():
            raise ValueError(f"temporary .{suffix}.tmp leaked after CLI run")
    return products


def _step(executable: Path, fixture_dir: Path, root: Path, name: str, fixture: str, expected: str) -> dict[str, object]:
    _prepare_stale_products(root)
    _copy_fixture(fixture_dir, fixture, root)
    result = _run(executable, root)
    if result.returncode != 0:
        raise ValueError(f"{name}: CLI failed: {result.stderr.strip()}")
    products = _require_success_state(root, expected)
    return {
        "name": name,
        "fixture": fixture,
        "fixture_identity": identity(fixture_dir / fixture),
        "expected_product": expected,
        "status": "passed",
        "returncode": result.returncode,
        "formal_products": products,
        "stale_final_cleanup": True,
        "stale_temporary_cleanup": True,
    }


def _parse_failure(executable: Path, root: Path, preserved: str) -> dict[str, object]:
    old = product_info(root.with_suffix(f".{preserved}"))
    if old["type"] != "file":
        raise ValueError("parse-failure precondition has no valid formal product")
    root.with_suffix(".env").write_text("invalid environment\n", encoding="ascii")
    result = _run(executable, root)
    new = product_info(root.with_suffix(f".{preserved}"))
    if result.returncode == 0 or new != old:
        raise ValueError("parse failure did not preserve the prior formal product")
    if any(root.with_suffix(f".{suffix}.tmp").exists() for suffix in PRODUCT_SUFFIXES):
        raise ValueError("parse failure leaked a temporary product")
    return {"name": "parse_failure_preserves_shd", "status": "passed", "returncode": result.returncode, "preserved_product": preserved, "formal_products": _formal_products(root)}


def _solve_failure(executable: Path, fixture_dir: Path, root: Path, preserved: str) -> dict[str, object]:
    old = product_info(root.with_suffix(f".{preserved}"))
    if old["type"] != "file":
        raise ValueError("solve-failure precondition has no valid formal product")
    _copy_fixture(fixture_dir, "cli_solver_failure.env", root)
    shutil.copyfile(fixture_dir / "cli_solver_failure.ssp", root.with_suffix(".ssp"))
    result = _run(executable, root)
    new = product_info(root.with_suffix(f".{preserved}"))
    if result.returncode == 0 or new != old:
        raise ValueError("solve failure did not preserve the prior formal product")
    if "abnormal ray termination" not in result.stderr and "solve failure" not in result.stderr:
        raise ValueError("failure fixture did not reach the solver stage")
    if any(root.with_suffix(f".{suffix}.tmp").exists() for suffix in PRODUCT_SUFFIXES):
        raise ValueError("solve failure leaked a temporary product")
    return {
        "name": "solve_failure_preserves_shd",
        "status": "passed",
        "returncode": result.returncode,
        "preserved_product": preserved,
        "fixture_identities": {
            name: identity(fixture_dir / name)
            for name in ("cli_solver_failure.env", "cli_solver_failure.ssp")
        },
        "formal_products": _formal_products(root),
    }


def _blocked_publish(executable: Path, fixture_dir: Path, root: Path) -> dict[str, object]:
    old = product_info(root.with_suffix(".shd"))
    if old["type"] != "file":
        raise ValueError("blocked-publish precondition has no valid SHD product")
    _copy_fixture(fixture_dir, "cli_arrival_ascii.env", root)
    blocked = root.with_suffix(".arr")
    _remove(blocked)
    blocked.mkdir()
    try:
        result = _run(executable, root)
        new = product_info(root.with_suffix(".shd"))
        if result.returncode == 0 or new != old:
            raise ValueError("blocked ARR publication did not preserve the prior SHD product")
        if root.with_suffix(".arr.tmp").exists():
            raise ValueError("blocked ARR publication leaked a temporary product")
        return {"name": "blocked_arrival_publish_preserves_shd", "status": "passed", "returncode": result.returncode, "preserved_product": "shd", "formal_products": _formal_products(root)}
    finally:
        _remove(blocked)


def validate(executable: Path, fixture_dir: Path) -> dict[str, object]:
    executable = executable.resolve()
    fixture_dir = fixture_dir.resolve()
    if not executable.is_file() or not fixture_dir.is_dir():
        raise ValueError("executable or fixture directory is absent")
    sources = {relative: identity(PROJECT_ROOT / relative) for relative in SOURCE_FILES if (PROJECT_ROOT / relative).is_file()}
    if not sources:
        raise ValueError("no output-lifecycle sources were found")
    with tempfile.TemporaryDirectory(prefix="bellhop_f2cpp_i8_output_safety_") as directory:
        root = Path(directory) / "case"
        steps = [_step(executable, fixture_dir, root, *entry) for entry in SEQUENCE]
        steps.append(_step(executable, fixture_dir, root, *ZERO_SEQUENCE))
        # Restore the formal product whose preservation is tested below.
        steps.append(_step(executable, fixture_dir, root, "coherent_preservation_seed", "cli_coherent.env", "shd"))
        steps.append(_solve_failure(executable, fixture_dir, root, "shd"))
        steps.append(_parse_failure(executable, root, "shd"))
        steps.append(_blocked_publish(executable, fixture_dir, root))
    return {
        "schema_version": 1,
        "validator": "i8_output_safety",
        "status": "passed",
        "command": ["validate_i8_output_safety.py", "--executable", "<executable>", "--fixture-dir", "<fixture-dir>", "--output", "<report>"],
        "validator_source": identity(Path(__file__).resolve()),
        "executable": identity(executable),
        "sources": sources,
        "steps": steps,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--fixture-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    report = validate(args.executable, args.fixture_dir)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"I8 output-safety validation: PASSED ({args.output})")


if __name__ == "__main__":
    main()
