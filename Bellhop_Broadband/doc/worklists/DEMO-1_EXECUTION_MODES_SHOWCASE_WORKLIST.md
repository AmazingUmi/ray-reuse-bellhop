# DEMO-1 — Execution-Mode Routes Showcase

Scope: `demo/` only（新增 execution_modes 展示）；不改 production。

## Frozen Design

- 基础 env 复用 `demo/cases/rayreuse_multifrequency/munk_rayreuse_multifrequency.env`
  （`CC` coherent + Cartesian Cerveny、单源、201×501 等距规则网格、5 频，
  四条 TL 路线全部合法；range 限域条件满足）。
- 四条路线与 CLI 参数：
  - `nonreuse` → `--execution-mode nonreuse`
  - `serial` → `--execution-mode reuse --reuse-mode serial`
  - `frequency` → `--execution-mode reuse --reuse-mode frequency --reuse-workers W`
  - `range` → `--execution-mode reuse --reuse-mode range --reuse-workers W`
  - W 默认 2（`--workers`）；serial 不接受 workers。
- 结果布局：`demo/results/execution_modes/<route_dir>/`，其中
  `route_dir = nonreuse | reuse_serial | reuse_frequency_w<W> | reuse_range_w<W>`；
  聚合 `run_summary.json` 在 `demo/results/execution_modes/`。
- PRT 标记校验（`app/main.cpp`）：
  - nonreuse: `execution mode = broadband nonreuse`
  - reuse 各路线: `execution mode = broadband reuse` + `reuse mode = <serial|frequency|range>`
- 计时来源：PRT 行 `Trace/Project/Influence/Scale/SHD seconds`、
  `Solver wall seconds`、`Total solver and product seconds`（四路线统一存在）。
  timing 图用 `Total solver and product seconds` 为柱顶合计。
- 图（`demo/figures/execution_modes/`，色标/风格沿用 reliability 现约定：
  viridis_r TL、coolwarm 对称差值、共享色标、max |ΔTL| 标题）：
  1. `<stem>_tl_comparison.png`：选定频率 × 四路线，共享 TL 色标；
  2. `<stem>_tl_difference.png`：三条 reuse 路线 − nonreuse，对称色标；
  3. `<stem>_phase_seconds.png`：分相堆叠柱状图（Trace/Project/Influence/Scale/SHD/Other）。
- 一致性指标：每频 `max |ΔTL| (dB)`（mask |p_ref| ≤ 1e-37）与
  `max |Δpressure|`，写入 figure summary JSON；不做 byte-identity 声明。
- **设计修正（施工中发现）**：frequency 路线 PRT 的
  `Project/Influence/Scale seconds` 是跨 worker CPU 汇总（17.80 s CPU >
  10.19 s 墙钟），与 serial/nonreuse 的单线程墙钟语义不可混用；
  计时图由“分相堆叠”改为双面板（墙钟总时长 + Trace 相位），
  分相明细保留在 run_summary.json。
- Makefile：`execution-modes` / `execution-modes-run` / `execution-modes-plot`，
  变量 `EXECUTION_ENV/EXECUTION_ROUTES/EXECUTION_WORKERS/EXECUTION_INDEXES`。
- 不修改 `reliability.py` / `rayreuse_multifrequency.py` 行为，只复用其助手。

### A01 [STANDARD] `demo/codes/execution_modes.py`
Status: DONE
Reviewer: N/A

Goal:
- `check|run|plot|show` 子命令；四路线运行、PRT 标记校验、计时解析、
  一致性指标、三张图与 summary JSON。

Acceptance:
- `run` 后各 `route_dir` 有 env/prt/shd + 聚合 run_summary.json；
- `plot` 产出三图 + summary JSON，指标含每频 max |ΔTL| 与 max |Δpressure|。

### A02 [STANDARD] Makefile + README
Status: DONE
Reviewer: N/A

Goal:
- 新增三个目标与变量；README 新增“执行模式路线对比”一节（含原生调用示例）。

Acceptance:
- `uv run make -C demo execution-modes` 一键可跑；README 与实际行为一致。

### A03 [STANDARD] 单元测试
Status: DONE
Reviewer: N/A

Goal:
- `demo/codes/tests/test_showcase.py` 增补：routes 解析、route 参数构造、
  route_dir 命名、PRT 计时行解析。

Acceptance:
- `uv run make -C demo test` 通过。

### A04 [STANDARD] 证据
Status: DONE
Reviewer: N/A

Goal:
- 默认配置四路线实跑；记录每频 max |ΔTL|、max |Δpressure| 与分相计时。

Acceptance:
- 三条 reuse 路线 vs nonreuse：max |ΔTL| ≤ 1e-4 dB（预期 ~0）；
  timing 图中 nonreuse 的 Trace 相累计（Nfreq 次追踪）与 reuse 路线
  trace-once 的差异可见。

Evidence:
- `demo/figures/execution_modes/munk_rayreuse_multifrequency_summary.json`：
  serial / frequency(w2) / range(w2) 三路线对 nonreuse 的逐频
  `max_abs_tl_difference_db` 与 `max_abs_pressure_difference` 均精确为 0.0
  （全部 5 频 50–250 Hz，逐元素相等），远优于 1e-4 门槛。
- `demo/results/execution_modes/run_summary.json`（PRT 计时，2026-09-08
  Batch Acceptance 全流程重跑）：
  - nonreuse：total 18.87 s，Trace 1.38 s（5 频重追踪累计）
  - serial：total 18.04 s，Trace 0.30 s（trace once）
  - frequency(w2)：total 10.25 s，Trace 0.32 s
  - range(w2)：total 9.97 s，Trace 0.28 s
  - trace-once vs 重追踪的 Trace 相位比约 4.4–4.9x，图中可见；
    并行 reuse 路线墙钟相对 serial 降约 43%、相对 nonreuse 降约 46%
  （计时随机器负载波动，以 run_summary.json 记录为准）。
- 三张图均通过视觉审查（TL 对比共享色标 70–120 dB；差值图三面板
  max |ΔTL| = 0 且对称色标 ±1e-3 dB；计时图标签旋转后无重叠/裁切）。

## Remediation Log

- Final Review R1（CHANGES_REQUIRED）→ 已修复：
  - F1 README 原生调用示例相对路径少一层 `../`（第 4 层目录需
    `../../../../Bellhop_Broadband/...`），已改正并实测解析。
  - F2 test_showcase.py `assertRaisesRegex` 块内不可达行已删除。
  - F3 Worklist Evidence 计时更新为 Batch Acceptance 重跑值并注明波动。
- 复验：`uv run make -C demo test` 7/7；路径解析实测通过；
  summary JSON 指标仍为精确 0.0。

## Final Status

DEMO-1 ACCEPTED（final review R2，2026-09-08）。Batch CLOSED。
