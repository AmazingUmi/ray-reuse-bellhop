# BB-N2 — CI Gate Refactor & Slimming

Goal:
- Split the single 45-min CI job into fast-quality (push/PR main gate) and engineering (heavy) jobs.
- `quality_gate.sh` becomes the fast gate; `engineering_gate.sh` absorbs Debug sanitizer CTest + isolation.
- Remove duplicate Release full CTest runs (was: release + isolated release in one job; debug also in same job).
- Optional conservative CTest parallelism via `BROADBAND_CTEST_JOBS`, only where provably safe.
- Broadband identity cleanup in active CI (verified: no rayreuse residuals in workflow/scripts).

Scope guard:
- CI/gate structure only. No production code, solver, test oracle, assertions, CLI, namespace changes.

### A01 [STANDARD] Split quality_gate.sh into fast gate
Status: DONE
Reviewer: PASS

Goal:
- `quality_gate.sh`: Release configure/build/CTest + standard_cases Python + PlotRead + independence check only.

Acceptance:
- No Debug CTest, no isolation rebuild, no static analysis, no package.

Evidence:
- Local run: fast gate passed (Release CTest + Python + independence).

### A02 [STANDARD] Extend engineering_gate.sh
Status: DONE
Reviewer: PASS

Goal:
- Add Debug sanitizer configure/build/CTest and isolated Release build/test to `engineering_gate.sh`.
- Keep check_format, static analysis, package/install/version smoke.

Acceptance:
- Engineering gate covers all checks from section 8 of the batch spec.
- No duplicate of the fast gate's normal Release full CTest.

Evidence:
- Local targeted smoke: Debug CTest + isolation steps executed.

### A03 [STANDARD] Workflow two-job split
Status: DONE
Reviewer: PASS

Goal:
- Job fast-quality: uv setup + quality_gate.sh, timeout 45.
- Job engineering: needs fast-quality, uv setup + engineering_gate.sh, timeout 90.

Acceptance:
- YAML valid; heavy checks no longer block fast feedback; no monolithic gate invocation.

Evidence:
- Local YAML structure review; push triggers both jobs.

### A04 [STANDARD] CTest parallelism decision
Status: DONE
Reviewer: PASS

Goal:
- Support `BROADBAND_CTEST_JOBS` in gate scripts; CI sets 2.

Acceptance:
- Safety proven: each CTest test is a distinct executable; temp outputs use unique timestamps or per-executable fixed names; no shared working directories.

Evidence:
- cmake/AddBroadbandTest.cmake (one executable per test); writer tests use time-suffixed temp dirs.

Blockers:
- none
