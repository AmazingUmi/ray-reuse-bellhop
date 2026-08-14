from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import tomllib
from typing import Iterable


REQUIRED_TAG_PREFIXES = (
    "product:",
    "source:",
    "receiver:",
    "geometry:",
    "pattern:",
    "beam:",
    "ssp:",
    "boundary:",
)


@dataclass(frozen=True)
class TestSet:
    name: str
    description: str
    ctest_regex: str


@dataclass(frozen=True)
class CaseCoverage:
    case_id: str
    tags: tuple[str, ...]
    test_sets: tuple[str, ...]


@dataclass(frozen=True)
class CoverageManifest:
    test_sets: dict[str, TestSet]
    cases: dict[str, CaseCoverage]

    def case_ids_for_sets(
        self, names: Iterable[str], case_order: Iterable[str]
    ) -> tuple[str, ...]:
        requested = tuple(names)
        if not requested:
            raise ValueError("at least one test set is required")
        unknown = sorted(set(requested) - set(self.test_sets))
        if unknown:
            raise ValueError(f"unknown test sets: {', '.join(unknown)}")
        selected = {
            case_id
            for case_id, coverage in self.cases.items()
            if set(coverage.test_sets).intersection(requested)
        }
        return tuple(case_id for case_id in case_order if case_id in selected)


def _unique_strings(
    values: object, *, label: str, allow_empty: bool = False
) -> tuple[str, ...]:
    if not isinstance(values, list) or any(
        not isinstance(value, str) or not value for value in values
    ):
        raise ValueError(f"{label} must be a list of non-empty strings")
    result = tuple(values)
    if not allow_empty and not result:
        raise ValueError(f"{label} must not be empty")
    if len(set(result)) != len(result):
        raise ValueError(f"{label} must not contain duplicates")
    return result


def load_coverage_manifest(
    path: Path, expected_case_ids: Iterable[str]
) -> CoverageManifest:
    with path.open("rb") as stream:
        raw = tomllib.load(stream)
    if raw.get("schema_version") != 1:
        raise ValueError(f"{path}: unsupported coverage schema_version")

    raw_sets = raw.get("sets")
    raw_cases = raw.get("cases")
    if not isinstance(raw_sets, dict) or not isinstance(raw_cases, dict):
        raise ValueError(f"{path}: sets and cases tables are required")

    test_sets: dict[str, TestSet] = {}
    for name, value in raw_sets.items():
        if not isinstance(value, dict):
            raise ValueError(f"{path}: set {name!r} must be a table")
        description = value.get("description")
        ctest_regex = value.get("ctest_regex")
        if not isinstance(description, str) or not description:
            raise ValueError(f"{path}: set {name!r} requires a description")
        if not isinstance(ctest_regex, str) or not ctest_regex:
            raise ValueError(f"{path}: set {name!r} requires ctest_regex")
        test_sets[name] = TestSet(name, description, ctest_regex)

    expected = tuple(expected_case_ids)
    missing = sorted(set(expected) - set(raw_cases))
    extra = sorted(set(raw_cases) - set(expected))
    if missing or extra:
        raise ValueError(
            f"{path}: coverage cases differ from discovered cases; "
            f"missing={missing}, extra={extra}"
        )

    cases: dict[str, CaseCoverage] = {}
    for case_id in expected:
        value = raw_cases[case_id]
        if not isinstance(value, dict):
            raise ValueError(f"{path}: case {case_id!r} must be a table")
        tags = _unique_strings(value.get("tags"), label=f"{case_id}.tags")
        memberships = _unique_strings(
            value.get("sets"), label=f"{case_id}.sets"
        )
        unknown_sets = sorted(set(memberships) - set(test_sets))
        if unknown_sets:
            raise ValueError(
                f"{path}: case {case_id!r} uses unknown sets {unknown_sets}"
            )
        for prefix in REQUIRED_TAG_PREFIXES:
            matches = [tag for tag in tags if tag.startswith(prefix)]
            if len(matches) != 1:
                raise ValueError(
                    f"{path}: case {case_id!r} requires exactly one "
                    f"{prefix.rstrip(':')} tag"
                )
        cases[case_id] = CaseCoverage(case_id, tags, memberships)

    return CoverageManifest(test_sets=test_sets, cases=cases)
