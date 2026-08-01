#!/usr/bin/env python3
"""Run Clang's static analyzer from a CMake compilation database."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import shlex
import subprocess
import sys
from typing import Sequence


OPTIONS_WITH_VALUES = {"-MF", "-MT", "-MQ", "-o"}
OPTIONS_WITHOUT_VALUES = {"-MD", "-MMD", "-MP", "-c"}


def analyzer_command(entry: dict[str, object]) -> list[str]:
    raw_arguments = entry.get("arguments")
    if isinstance(raw_arguments, list):
        arguments = [str(value) for value in raw_arguments]
    else:
        raw_command = entry.get("command")
        if not isinstance(raw_command, str):
            raise ValueError(
                "compilation database entry needs arguments or command"
            )
        arguments = shlex.split(raw_command)
    if not arguments:
        raise ValueError("compilation database command must not be empty")

    filtered = [
        arguments[0],
        "--analyze",
        "-Xanalyzer",
        "-analyzer-output=text",
    ]
    index = 1
    while index < len(arguments):
        argument = arguments[index]
        if argument in OPTIONS_WITH_VALUES:
            index += 2
            continue
        if argument in OPTIONS_WITHOUT_VALUES:
            index += 1
            continue
        filtered.append(argument)
        index += 1
    return filtered


def run_database(path: Path) -> None:
    entries = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(entries, list) or not entries:
        raise ValueError("compilation database must be a non-empty list")

    compiler = analyzer_command(entries[0])[0]
    version = subprocess.run(
        [compiler, "--version"],
        check=True,
        capture_output=True,
        text=True,
    ).stdout
    if "clang" not in version.lower():
        raise ValueError("static analysis requires a Clang-family compiler")

    for entry in entries:
        if not isinstance(entry, dict):
            raise ValueError("compilation database entries must be objects")
        directory = entry.get("directory")
        if not isinstance(directory, str):
            raise ValueError("compilation database entry needs a directory")
        source = entry.get("file", "unknown source")
        print(f"Analyzing {source}", flush=True)
        subprocess.run(
            analyzer_command(entry),
            cwd=directory,
            check=True,
        )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Run Clang static analysis from compile_commands.json."
    )
    parser.add_argument("compilation_database", type=Path)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        run_database(args.compilation_database.resolve())
    except (OSError, subprocess.CalledProcessError, ValueError) as error:
        print(f"run_static_analysis.py: {error}", file=sys.stderr)
        return 1
    print("Bellhop_RayReuse static analysis passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
