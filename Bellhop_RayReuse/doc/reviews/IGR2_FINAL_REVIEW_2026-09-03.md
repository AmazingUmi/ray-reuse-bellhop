# IGR-2 Final Review — 2026-09-03

## Verdict

**ACCEPTED**

No critical or actionable finding remains.

## Independent review summary

- The fused production scope, global `nonreuse` fallback, and scoped legacy
  `reuse`/frequency-`parallel` deprecation match the frozen design.
- Static contiguous range partitioning shares the cache read-only, gives each
  worker exclusive pressure ranges, creates no atomic accumulation or full
  per-worker workspace, and preserves each cell's ray accumulation order.
- `--range-parallel`, default four workers, explicit worker overrides, clamp,
  and invalid combinations are covered.
- Raw/scaled parity, divergent prefixes, cache fingerprint, and 2F/16F SHD
  byte identity pass.
- After remediation, Release CTest passes 43/43; pytest passes 192/192 with
  399 subtests; standard-case unittest passes 177/177.
- The 16F benchmark statistics were independently recomputed from the JSON and
  match the report. Its dirty-source identity limitation is disclosed without
  overclaiming a reconstructible commit.
- `git diff --check` passes; `Bellhop_origin/` and `Bellhop_F2CPP/` are
  untouched; rejected dynamic tiles, blocking, and L1b code are absent.

IGR-2 may be marked `ACCEPTED / CLOSED` and committed as one scoped change. It
does not authorize starting IGR-3.
