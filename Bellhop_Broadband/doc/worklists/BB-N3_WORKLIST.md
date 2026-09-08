# BB-N3 — Sanitizer Debug CTest Parity Exclusion

Goal:
- Exclude the heavy parity tests from the Debug sanitizer CTest in the engineering gate.
- Keep the full test matrix in every Release CTest (fast gate + isolated Release in engineering gate).

Evidence (local sanitizer run, build/debug CTestCostData):
- broadband.reuse.range.rc_parity 3705.5s, hat_parity 462.4s, cc_intensity_parity 352.1s,
  geometric_gaussian_parity 261.2s, cc_parity 141.2s, simple_gaussian_parity 71.9s
  → ~4994s of ~5084s total (>98%).
- All other tests ≤ 38.7s; fused_arrival_parity is 2.8s and stays in the sanitizer run.

Scope guard:
- CI/gate policy only. No production code, no CMake test registration change,
  no test assertion/oracle change, Release matrices untouched.

### A01 [STANDARD] Debug sanitizer CTest exclusion
Status: DONE
Reviewer: PASS

Goal:
- engineering_gate.sh debug CTest excludes `broadband\.reuse\.range\..*parity` via --exclude-regex.

Acceptance:
- Exactly the six heavy range parity tests excluded; all other 44 debug tests still run.
- Release CTest invocations unchanged (full 50-test matrix).

Evidence:
- `ctest --preset debug -N --exclude-regex ...` selects 44 tests (six parity excluded).
- Full local debug run with exclusion: 44/44 passed, ~1.5 min wall.

Blockers:
- none
