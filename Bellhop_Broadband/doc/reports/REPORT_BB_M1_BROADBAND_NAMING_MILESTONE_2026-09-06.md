# BB-M1 — Broadband Execution Model & Naming Milestone Closure Report

Date: 2026-09-06
Milestone design: `Bellhop_RayReuse/doc/reviews/BB-M1 — Broadband Execution Model & Naming Milestone_REVIEW_2026-09-06.md`（文件名含空格，不设超链接）
Implementation worklist: [`../worklists/BB-1_WORKLIST.md`](../worklists/BB-1_WORKLIST.md)
Documentation worklist: [`../worklists/BB-2_WORKLIST.md`](../worklists/BB-2_WORKLIST.md)

本报告按 Review §35 记录 BB-M1 的 Before/After CLI 契约、身份、验证证据与
验收结论。里程碑整体 `CLOSED` 的记录由 coordinator 在 BB-2 final review
`ACCEPTED` 后补记，本报告不代写该状态。

## Before

```text
bellhop_rayreuse

execution-mode:
  nonreuse
  reuse
  parallel
  fused

parallel controls:
  trace-workers
  workers
  range-parallel
```

旧默认值：execution-mode `nonreuse`；旧 `parallel` 未指定 `--workers` 时
使用硬件并发数（不可用时回退 1）；旧 `fused --range-parallel` 未指定
`--workers` 时默认请求 4 workers；未开启 range parallel 的旧 `fused` 为
单 worker。

## After

```text
bellhop_broadband

execution-mode:
  nonreuse
  reuse

reuse-mode:
  serial
  frequency
  range

parallel controls:
  trace-workers
  reuse-workers
```

新默认值（BB-M1 §8 冻结）：`--execution-mode` 默认 `nonreuse`；
`--trace-workers` 默认 1（服务全部产品、全部路线的 trace 阶段）；
reuse 下 `--reuse-mode` 默认 `serial`、`--reuse-workers` 默认 1（仅
frequency/range 接受，serial 拒绝任何显式值，含 1）。旧
`--execution-mode parallel|fused`、`--range-parallel`、`--workers` 均已
删除，使用即被拒绝（unknown option / invalid value）。

产品身份：Bellhop Broadband；CMake project `BellhopBroadband`；package
`bellhop-broadband`；PRT 头 `BELLHOP BROADBAND`、完成行
`Bellhop Broadband completed successfully`、路线两行
`execution mode = broadband|single-frequency reuse` + `reuse mode = …`。
`Bellhop_RayReuse/` 目录名、内部 namespace/include 根与低层 fused 实现
术语（fused kernel/workspace/sink/adapter）按设计保留。

旧 → 新命令映射（BB-M1 §12）：

| 旧 | 新 |
|---|---|
| `--execution-mode nonreuse [--trace-workers N]` | 同形（仅程序名换 `bellhop_broadband`） |
| `--execution-mode reuse` | `--execution-mode reuse --reuse-mode serial` |
| `--execution-mode parallel --workers N` | `--execution-mode reuse --reuse-mode frequency --reuse-workers N` |
| `--execution-mode fused` | `--execution-mode reuse --reuse-mode range --reuse-workers 1` |
| `--execution-mode fused --range-parallel --workers N` | `--execution-mode reuse --reuse-mode range --reuse-workers N` |

默认值变化提醒：旧 parallel（硬件并发数）与旧 range-parallel（默认 4）的
命令迁移到新 CLI 后默认请求 1；要保留原并行资源请求必须显式写出
`--reuse-workers N`。历史术语映射表见根
[`doc/README.md`](../../../doc/README.md) 的 "Terminology after BB-M1"。

## 身份记录

- pre-milestone baseline SHA：`a3f20d8c0d8ac207766fa0adb083de3526f74fd7`
  （BB-1 audit baseline 与 construction HEAD 一致，无漂移）。
- BB-1 milestone commit：`77e53c45e5a828c0d2f3b7dffb5dc84772cb3f46`，
  `refactor(broadband): establish broadband execution model and reuse naming`。
  包含实现、构建/CI、runner、schema v3 remediation 与相关测试。
- BB-2 documentation commit：本报告首次加入仓库的独立 docs 提交，主题为
  `docs(broadband): align documentation with broadband naming milestone`；
  同时归档 Review、Worklist、支持矩阵、迁移指南与验收证据。
  精确 SHA 可由 `git log --diff-filter=A --format=%H --` 后接本报告路径查询，
  避免在提交内容中嵌入其自身 SHA。
- 验收与 remediation 当时按授权保持未提交；用户随后明确授权本地 commit，
  因此拆分以上两个提交。下文历史验收记录中的“未提交”描述保留其当时语义。
  本轮未 push、未 tag，未启动 BB-PERF-1。

## Tests

（BB-1 Batch Acceptance 集中执行；BB-2 为纯文档批次，未重跑数值回归。）

- Release full CTest：50/50 passed（同一 Release 构建，Werror=ON）。
- Targeted pytest（实际改动的 runner/matrix 测试文件）：
  `test_benchmark_rayreuse` + `test_standard_cases` + `test_model_matrix`
  = 55 passed + 50 subtests。
- Demo 端到端：reuse-serial 与 reuse-frequency 两路线 run PASS（新 CLI +
  新 PRT marker）。
- CLI 层：`rayreuse.unit.command_line`（表驱动：默认、非法值、组合冲突、
  旧选项/旧值拒绝、正整数边界、资源参数产品限制）与 `rayreuse.cli`
  help/version/missing_root 3/3。
- Install/package smoke：install 前缀出现 `bin/bellhop_broadband`
  （`Bellhop Broadband 0.1.0`）；CPack 生成
  `bellhop-broadband-0.1.0-Darwin-arm64.tar.gz`。

## Output parity

- A01 冻结 fixture 的新旧 CLI 对比共 20 组（8 组 TL：四路线 × trace=1/2，
  frequency/range 交叉 reuse-workers=1/2；12 组产品：ARR ASCII 四路线、
  ARR binary range、Eigenray 三路线、R×2、单频 TL×2）：产品文件（SHD/ARR/
  RAY）全部 **byte-identical**（SHA-256 严格相等）。
- PRT 冻结归一化后一致；归一化仅限预先冻结项（两处身份串、路线行映射、
  worker 行名、旧 range parallel 行删除、旧弃用告警、时间）。requested/
  effective worker、Trace passes（nonreuse `Nfreq×NSz` / reuse `NSz`）与
  cache fingerprint（`2271226459307825052`）数值不变。
- Gate 拒绝复核：Range TL 多 source、Range ARR 单频/不等距 range、Range
  Eigenray、R+reuse、单频 TL+reuse、serial+显式 reuse-workers(1)、旧值
  `parallel`/`fused`、旧选项 `--workers`/`--range-parallel` 均正确拒绝。
- Evidence 目录：`Bellhop_RayReuse/build/bb1_evidence/`（git-ignored；
  baseline binary 副本、冻结清单、20 组新旧输出与 compare manifest）。
- 无性能 claim：BB-M1/BB-1 不做 8 路线性能比较（Review §36 non-goal）。

## Support matrix audit

BB-2 B02 以 production code 为 source of truth 完成支持矩阵核对（事实表：
[`../worklists/BB-2_B02_SUPPORT_MATRIX_FACTS.md`](../worklists/BB-2_B02_SUPPORT_MATRIX_FACTS.md)），
BB-2 B03 已按该表修正 living documents：

- TL：nonreuse + reuse 三路线全部支持；Range 路线限多频、单 source、规则
  等距网格（≥2 等距 range），beam family 覆盖 Cerveny（两坐标系）、
  GeoHat（两坐标系）、Cartesian GeoGaussian 的全部合法 run mode，
  SimpleGaussian 仅 coherent；单频 TL 只走 SingleFrequencySolver 并拒绝
  显式 reuse。
- ARR（A/a）：四路线支持；全路线限 GeometricHat/GeometricGaussian family；
  Range 路线另限 ≥2 频与规则等距网格，允许多 source 并按 source streaming
  执行（一次只持有一个 source 的 frozen cache 与 broadband workspace）。
- Eigenray：nonreuse/serial/frequency 三路线；Range 拒绝。
- R：仅单频 nonreuse（显式 reuse 拒绝）；`--trace-workers` 合法。
- 诊断/资源参数域：`--output-queue-capacity`/`--memory-budget-mib`/
  `--profile-frequency-tasks` 仅 reuse+frequency 多频 TL；
  `--profile-influence` 仅 Cartesian Cerveny TL（全路线）；`--verify-cache`
  指纹打印覆盖 R、A/a、E 与 reuse 三路线 TL（nonreuse/单频 TL 不打印）。

支持域本身与 BB-M1 前一致（old route capability 一一映射，无扩大/缩小）；
文档差异集中在命名、PRT 标签与默认 worker 数，已全部修正。

## Protected references status

- `Bellhop_origin/`：零改动（diff 与 untracked 均为空）。
- `Bellhop_F2CPP/`：零改动（diff 与 untracked 均为空）。

## Final reviewer verdict

- BB-1：**ACCEPTED**（高级 final-reviewer 独立验收，2026-09-06；12 项验收
  问题全部 PASS，evidence 见
  [`../worklists/BB-1_WORKLIST.md`](../worklists/BB-1_WORKLIST.md) A06）。
- BB-2：**ACCEPTED**（独立 final-reviewer，2026-09-06；首轮 CHANGES_REQUIRED
  的 2 项记录准确性 finding 经 remediation 后由同一 reviewer 复核闭环；
  八项验收全 PASS，evidence 见
  [`../worklists/BB-2_WORKLIST.md`](../worklists/BB-2_WORKLIST.md) B04）。

## Milestone status

**BB-M1 — ACCEPTED / CLOSED（2026-09-06）**

BB-1 与 BB-2 均经独立 final-reviewer ACCEPTED；里程碑完成 Review §38
Definition of Done 全部条目（executable/身份/两层 CLI/solver 命名/科学
冻结/输出保持/脚本迁移/文档/支持矩阵/历史完整性/protected refs/
milestone report）。验收当时全部修改保持未提交/未 stage；用户后续授权的两个本地提交见
上方身份记录，milestone tag 未创建。BB-PERF-1 仅
FOLLOW-UP，未获授权不启动。

## 收尾 remediation（2026-09-06，里程碑 CLOSED 后的限定范围修正）

用户授权四项，只做相关文档/runner 验证（未重跑 CTest、oracle 或性能
矩阵），修复后经原域 final-reviewer 复核：

1. **三个 Worklist 状态/Authority/已关闭 findings**：BB-1/BB-2 的准备轮
   Authority 声明标注为历史快照并补记同日授权执行；BB-M1 的准备轮
   blockers 标【历史快照】、GUIDE_USAGE/IGR-3 滞后 finding 标【已关闭】
   并指向 BB-2 B03/B04 evidence。历史记录零删除。
2. **BB-M1_WORKLIST E3/E9 断链**：两个旧 solver 路径链接改为现名
   （`reuse_range_para_solver.cpp` / `reuse_freq_para_solver.cpp`），显式
   标注 audit baseline `a3f20d8` 中的旧文件名，E3 行号保持 baseline 行号
   语义（经 reviewer 对照 baseline 内容逐行验证）。
3. **benchmark 报告 schema 词汇/版本不一致**：BB-1 已改变报告的
   execution_mode 值域与 identifier 语法但仍写 schema_version=2，与
   BB-M1 前的 v2 报告不可区分。处理决定为**升级版本**：
   `benchmark_rayreuse.py` `SCHEMA_VERSION = 3`（附版本注释）；GUIDE_
   BENCHMARKING 新增 "报告 schema 版本（v3）" 小节与 v2→v3 映射表
   （execution_mode 值、identifier 语法 `parallel-w*`/`fused-range-w*`/
   裸 `fused`↔`reuse-range-w1`、PRT 字段 requested/effective），并修正
   原 "报告 schema 不变" 的错误表述。配置字段名
   （parallel_workers/fused_range_workers）与 CLI 旗标名为有意保留的
   reuse-workers 轴，彻底更名才需要后续再 bump。
4. **PRT 旧标签保留边界**：GUIDE_BENCHMARKING 新增 "PRT 标签的保留
   边界与消费者" 小节——`fused/parallel reuse wall seconds`、
   `non-reuse/reuse wall seconds`（benchmark wall 映射与 IGR-3A A08 差异
   描述消费）、`single-frequency non-reuse/reuse`（生产者事实，无代码
   消费者）；规范路线身份仍以两层新标签为准。

**验证（targeted）**：`test_benchmark_rayreuse.py` 25 passed + 14
subtests；真实 micro benchmark（constant_speed_direct 2频，
reuse-frequency-w2 + reuse-range-w2，0 预热 1 计量，`--allow-dirty`）产出
`build/benchmarks/bb1_remediation_schema_v3_smoke.json`：schema_version=3、
新 identifier、`requested_reuse_worker_count` 字段、跨配置 SHD 一致门
通过；映射表对照仓内真实 v2 报告（igr3a_a09_level_f.json）逐项相符；
改动文档链接检查通过；`git diff --check` clean。

**复核**：Worklist 域（BB-2 原 final-reviewer 角色）一次通过 ACCEPTED；
code/GUIDE 域（BB-1 原 final-reviewer 角色）首轮 CHANGES_REQUIRED
（F1 v2 identifier 语法误写 `fused-w*` 且缺 effective/裸 fused 映射；
F2 PRT 表两处消费者声明不实），修复后同一 reviewer 复核 ACCEPTED
（映射经基线代码、真实 v2 报告、v3 代码三重验证）。

Remediation 验收时 Git 状态：全部修改保持未提交/未 stage；Origin/F2CPP 零改动；
BB-PERF-1 未启动。
