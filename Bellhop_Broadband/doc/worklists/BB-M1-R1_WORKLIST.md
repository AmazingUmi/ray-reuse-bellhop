# BB-M1-R1 — Active Naming Cleanup

Status: ACCEPTED (final-reviewer ACCEPTED, 2026-09-07)
Baseline: `38d8e47`，当前分支，初始工作树干净。
Authority: 用户授权 A–F 直接施工；不修改历史 report/worklist，不自动 commit/push。

## Scope / freeze

- execution=`nonreuse|reuse` 与 reuse=`serial|frequency|range` 显式分层；配置 worker 仅 `reuse_workers`，NonReuse/Serial 为 null。
- Benchmark schema v4；旧 v2/v3 JSON 不改。派生 identifier 可用 `reuse-frequency-*`/`reuse-range-*`，不能充当 execution_mode。
- 四个既有路线 wall metric 改 `Solver wall seconds`，测量值/位置不变，不给其他产品新增 metric。
- Benchmark 显式 worker CSV 共用于 Frequency/Range；省略时保留既有 Frequency=8、Range=1 请求及默认路线集合。
- 六个包含 solveStreaming/SHD 的 TL parity 改 range_*；只测 raw accumulation 的 fused_arrival_parity 保留。
- 保留底层 fused kernel/workspace/adapter/sink/raw seam、rayreuse namespace、内部 lineage 和项目目录。
- 不改算法、projector、hot loop、partition/scheduling、support/default execution；不改 Origin/F2CPP。

### A01 [STANDARD]
Status: DONE
Reviewer: N/A

Goal:
- PRT/production 路线文字、solver test target/CTest 与逐项 parity 命名清理。
Acceptance:
- 文案/标识符替换不改变科学计算和调度；新 CTest 路线名准确。
Evidence:
- `app/main.cpp` 四条路线统一输出 `Solver wall seconds`；`unsupportedParallelTuning`→`unsupportedFrequencyReuseTuning`；`ArrivalSolver/EigenraySolver::solveParallel`→`solveFrequency`。
- 六个 fused TL parity 改名 `range_*.cpp`（CTest `rayreuse.reuse.range.*`）；`fused_arrival_parity_test` 保留（raw accumulation seam）。solver 并列测试 label 为 `rayreuse.reuse.serial.solver` / `rayreuse.reuse.frequency.solver` / `rayreuse.reuse.range.solver`。
- command_line unit test 通过；旧 `--range-parallel` 拒绝路径保留验证。

### A02 [STANDARD]
Status: DONE
Reviewer: N/A

Goal:
- Benchmark v4、PRT consumer、单一 reuse_workers 与显式两层参数；同步当前指南。
Acceptance:
- 四种配置 JSON 三字段符合用户契约；旧 flags/配置字段退出活跃实现。
Evidence:
- `benchmark_rayreuse.py`：`SCHEMA_VERSION=4`，配置对象为 execution_mode/reuse_mode/reuse_workers；CLI `--execution-modes/--reuse-modes/--reuse-workers`，无 `--parallel-workers`/`--fused-range-workers`。
- 真实 smoke（constant_speed_direct/broadband_smoke，`--reuse-modes frequency,range --reuse-workers 2`）：JSON `schema_version=4`，`{reuse,frequency,2}` 与 `{reuse,range,2}`；PRT parser 强制唯一 `Solver wall seconds`。
- `GUIDE_BENCHMARKING.md` 升级 v4（v2/v3 历史表保留为映射参考，不改历史 benchmark JSON）。
- `tests/test_benchmark_rayreuse.py` 25 passed。

### A03 [STANDARD]
Status: DONE
Reviewer: N/A

Goal:
- Standard-case/matrix/demo 等活跃 runner、CLI/internal variables 与当前说明显式分层。
Acceptance:
- 不以复合 route 字符串作为 execution_mode；原默认执行路线和产品支持不变。
Evidence:
- `standard_cases.py`：`RAYREUSE_EXECUTION_MODES=(nonreuse,reuse)` + `RAYREUSE_REUSE_MODES`，CLI `--rayreuse-execution-mode nonreuse|reuse` + `--rayreuse-reuse-mode`。
- `model_matrix.py` schema v2：`--execution-modes/--reuse-modes`，默认 nonreuse,reuse × serial,frequency（与旧默认三路线等价）；`model_matrix_gate.sh` 同步。
- `demo/rayreuse_multifrequency.py` schema v2 两层参数；`demo/Makefile` `MULTI_EXECUTION_MODE/MULTI_REUSE_MODE`。
- `tests/test_standard_cases.py` + `test_model_matrix.py` 30 passed。

### A04 [STANDARD]
Status: DONE
Reviewer: ACCEPTED (final-reviewer, 2026-09-07, findings: none)

Goal:
- 最小验收与独立 final review；findings 经 remediation 后同一 reviewer 复核。
Acceptance:
- build、CLI unit、benchmark/standard targeted tests、同一最小 broadband case 的 Frequency/Range smoke；PRT/schema4/active-name 扫描、diff --check、protected refs zero diff。
- 不跑 full CTest/oracle/性能矩阵；不启动后续任务。独立 final-reviewer ACCEPTED 才关闭本 Batch。
Evidence:
- build（release bellhop_broadband）成功；command_line tests 通过。
- benchmark targeted 25 passed；standard/matrix targeted 30 passed。
- smoke：benchmark frequency+range 两配置成功；直接 PRT（frequency/range）均为两层标记 + 唯一 `Solver wall seconds` + `requested/effective reuse worker count`。
- 活跃代码 grep 禁用 pattern 清零（`--parallel-workers`、`--fused-range-workers`、`parallel_workers`、`fused_range_workers`、`* reuse wall seconds`、`rayreuse.parallel.*`、`*_ray_reuse_solver_tests`）。
- `git diff --check` clean；`Bellhop_origin`/`Bellhop_F2CPP` zero diff。
- final-reviewer 独立复核全部 scope 项并重跑 targeted validation，结论 ACCEPTED、findings: none。

### A05 [SIMPLE]（用户追加授权，2026-09-07）
Status: DONE
Reviewer: N/A

Goal:
- standard-case runner CLI 旗标去前缀：`--rayreuse-execution-mode`→`--execution-mode`，`--rayreuse-reuse-mode`→`--reuse-mode`（与可执行文件及 model_matrix `--execution-modes/--reuse-modes` 对齐）；内部 helper/kwargs（`require_rayreuse_execution_mode` 等）语义明确，按原规则保留。
Acceptance:
- 两个子命令 parser（stage/batch）接受新旗标；测试与文档同步；活跃范围无 `rayreuse-*-mode` 残留。
Evidence:
- `tests/test_standard_cases.py` 20 passed + 36 subtests；真实 `generate` 调用（reuse/frequency）成功；`batch --help` 显示新旗标；`git diff --check` clean；Origin/F2CPP zero diff。

## Findings / blockers

- 无。
