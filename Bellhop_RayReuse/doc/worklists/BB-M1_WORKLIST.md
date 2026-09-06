# BB-M1 — Broadband Execution Model & Naming Milestone

Status: **BB-M1 ACCEPTED / CLOSED**（2026-09-06；BB-1 与 BB-2 均经独立 final-reviewer ACCEPTED）
Date: 2026-09-06
Authority: 设计核对轮仅审阅原提案第 1–24 节、修正差异、生成正式 Worklist；施工授权覆盖 BB-1+BB-2 连续执行（同日完成）。
Audit baseline: `a3f20d8c0d8ac207766fa0adb083de3526f74fd7`，branch `feat/igr-influence-geometry-reuse`，初始 working tree clean；construction HEAD = 同 SHA（无漂移）。

## Scope 与执行入口

- [修订后的 Review](<../reviews/BB-M1 — Broadband Execution Model & Naming Milestone_REVIEW_2026-09-06.md>) 保留目标解释；本目录的正式 Worklist 负责施工任务、状态与 gate。
- [BB-1_WORKLIST.md](BB-1_WORKLIST.md)：产品命名、CLI 分层、原路线 dispatch、构建/脚本迁移——**ACCEPTED**（2026-09-06）。
- [BB-2_WORKLIST.md](BB-2_WORKLIST.md)：BB-1 ACCEPTED 后，更新 living docs、核对 support matrix、保留历史——**ACCEPTED**（2026-09-06）。
- 两个 Batch 分别 acceptance/final review；里程碑 closure report：[REPORT_BB_M1_BROADBAND_NAMING_MILESTONE_2026-09-06.md](../reports/REPORT_BB_M1_BROADBAND_NAMING_MILESTONE_2026-09-06.md)。
- 验收时按授权保持未提交；用户后续授权本地 commit，BB-1 实现提交为 `77e53c4`，文档随独立 docs 提交归档，身份见 closure report。未 push/tag。不修改 Origin/F2CPP，不做性能优化、全局 namespace/include 迁移、目录迁移、科学算法修改或 support expansion（施工全程遵守）。

## 1–24 逐节核对

“目标变化”指原提案有意改变接口，不能写成既有功能；“补正”指原表述遗漏了实际边界。
代码定位以 audit baseline 为准；后续 rename 后使用 baseline SHA 查历史符号。

| 节 | 结论 | 修订后的决定与证据 |
|---|---|---|
| 1 | 目标变化＋补正 | 当前仍名 RayReuse；IGR-2/3 的 fused production、legacy reuse/parallel 条件弃用是历史事实。新三策略撤销旧弃用提示；不声称同等性能。E1/E6 |
| 2 | 补正 | NonReuse 逐频、Reuse 共用 frozen geometry；R 仅 trace/write，单频 TL 保留 SingleFrequencySolver。E1 |
| 3 | 历史一致＋补正 | trace seam 已由 PERF-TRACE-PAR-1 接入合法产品；不是新增功能，次数服从 source/product 生命周期。E7 |
| 4 | 历史一致＋补正 | 默认 trace=1，N>1 static；effective=min(requested,launchCount)，不保证创建 N 个 worker。E7 |
| 5 | 目标变化＋修正 | serial/frequency/range 映射旧路线；Range 先分 range，各 worker 逐频 project 后 fused influence，不是全局 projection 后分块。E3 |
| 6 | 内部冲突修正 | Serial 拒绝任何显式 reuse-workers，包括 1；frequency/range 的 1 保留路线，effective 服从原 clamp/resource gate。E2/E3 |
| 7 | 目标变化 | 新增 reuse-mode/reuse-workers，保留 trace-workers；当前 parser 仍是四 execution modes。E2 |
| 8 | 目标变化＋补正 | 顶层 nonreuse 与 trace=1 沿用；旧 parallel 默认硬件并发、旧 range-parallel 默认4，新 CLI 统一1是显式资源默认变更。E1/E2 |
| 9 | 历史一致＋补正 | ENV 正值递增列表与 CLI override 已存在；不宣称 Origin/F2CPP 支持此输入扩展。E8 |
| 10 | 补正 | 八类是支持域内抽象组合；不代表 R/E/单频 TL 全支持。E1/E3 |
| 11 | 补正 | trace/reuse 参数正交；保持原 source 生命周期，无新增全局 barrier，尤其 ARR source streaming。E3/E6 |
| 12 | 显式映射一致 | 原示例的显式 worker 映射成立；省略旧 workers 时须按第8节说明默认变化。E1/E2 |
| 13 | 目标变化 | 删除旧 parallel/fused 值和 workers/range-parallel 参数，不保留 silent alias。E2 |
| 14 | 补正 | queue/memory/frequency-task profiling 仅 Frequency TL；不能开放给 ARR/E。profile-influence 仍仅 Cartesian Cerveny TL。E1/E2 |
| 15 | 目标变化＋补正 | 改 executable/CMake/package/身份；科学产品不变，PRT/stderr 身份、路线文字、旧告警和时间差异需显式归类。E1/E4 |
| 16 | 可选项裁定 | 原文仅建议目录迁移；本轮保留 Bellhop_RayReuse/，减少路径 churn，不把目录名当成功能 gate。 |
| 17 | 与 scope 一致 | 保留 namespace rayreuse、include/rayreuse 和不阻碍产品迁移的内部宏。E4/E5 |
| 18 | 补正 | 冻结四 solver 目标名；ARR/E 产品类保留，按其入口映射；TL support predicate 不拿来判断 ARR。E1/E3/E5 |
| 19 | 与实现一致 | 保留底层 fused workspace/kernel/adapter/sink 术语，禁止全局 Fused→Range。E3 |
| 20 | 目标变化＋补正 | 新 options 区分两层枚举与显式参数；移除的是 CLI workerCount/rangeParallel，内部 settings 不机械改名。E2/E5 |
| 21 | 内部冲突修正 | 与第6节统一；正整数校验、nonreuse 禁 reuse 参数，保留产品 gate。E1/E2 |
| 22 | 补正 | Range TL 单 source；Range ARR 多 source/G/g/B；均需多频、规则且至少2个等距 range；R/E/单频例外见 Review。E1/E3/E6 |
| 23 | 与科学契约一致 | 数学、阈值、状态和科学文件格式冻结；命名及显式 CLI 默认变化不算数值重构。E3/E6/E7 |
| 24 | 补正 | 保留每条路线的 partition/ownership/发布/异常规则，不宣称全部路线统一确定性，不新增调度算法。E3/E7/E9 |

## 证据入口

- E1：[app/main.cpp](../../app/main.cpp)：`printUsage`、`warnIfReplaceableLegacyMode`、`validateProductOptions`、`resolvedWorkerCount`、PRT identity 与各产品 dispatch（基线行 39–111、293–410、460、710–717、880–1565）。
- E2：[command_line.hpp](../../include/rayreuse/io/command_line.hpp) 与 [command_line.cpp](../../src/io/command_line.cpp)：默认、显式标记、旧参数解析与后置校验。
- E3：[reuse_range_para_solver.cpp](../../src/solver/reuse_range_para_solver.cpp)（audit baseline `a3f20d8` 中名为 `fused_ray_reuse_solver.cpp`，BB-1 rename；下列行号为 baseline 行号）：TL gate 59–109、ARR gate 165–207、range worker 274–318、ARR source streaming 516 起。
- E4：[CMakeLists.txt](../../CMakeLists.txt)、[engineering_gate.sh](../../scripts/engineering_gate.sh)、[quality_gate.sh](../../scripts/quality_gate.sh)：产品身份、安装/打包与当前重复构建测试入口。
- E5：[solver headers](../../include/rayreuse/solver/)：四 TL route classes、ArrivalSolver/EigenraySolver 与共用 SingleFrequencySolver/trace settings。
- E6：[IGR-3 scope closure](IGR-3_SCOPE_AND_ARCHITECTURE_DECISION.md)、[IGR-3A](IGR-3A_TL_BEAM_FAMILY_ADAPTATION_WORKLIST.md)、[IGR-3B](IGR-3B_ARRIVAL_FUSED_INFLUENCE_WORKLIST.md)、[IGR-2](IGR-2_FUSED_INFLUENCE_PRODUCTIONIZATION_WORKLIST.md)。对应生产提交 `dda1c2c`、`0050f59`、`e7f2705`，IGR-3 closure `2410f13`。
- E7：[PERF-TRACE-PAR-1](PERF-TRACE-PAR-1_WORKLIST.md)、[single_frequency_solver.cpp](../../src/solver/single_frequency_solver.cpp)；生产提交 `293ee40`。
- E8：[environment_parser.cpp](../../src/io/environment_parser.cpp)：`parseFrequencies` 与 override 选择。
- E9：[arrival_solver.cpp](../../src/solver/arrival_solver.cpp)、[eigenray_solver.cpp](../../src/solver/eigenray_solver.cpp)、[reuse_freq_para_solver.cpp](../../src/solver/reuse_freq_para_solver.cpp)（audit baseline `a3f20d8` 中名为 `parallel_ray_reuse_solver.cpp`，BB-1 rename）：各路线独立的调度/异常/ownership。

## Frozen decisions

- 产品 Bellhop Broadband；executable `bellhop_broadband`；CMake `BellhopBroadband`；package `bellhop-broadband`；目录保留。
- execution=`nonreuse|reuse`；reuse=`serial|frequency|range`；默认 nonreuse、trace=1，reuse 默认 serial，frequency/range workers 默认1。
- 新 CLI 拒绝 Serial 显式 reuse-workers，拒绝 nonreuse 的 reuse 参数，拒绝已删除旧值/参数；不自动换路线。
- TL、ARR、E、R 分派与支持边界以生产 gate 为准，保留 source streaming、frozen geometry、每频独立状态和 writer 顺序。
- 只在 BB-1 acceptance 汇总必要回归；BB-2 文档变更不重跑数学/性能验证。完整最小 gate 见各 Batch。

### D01 [ADVANCED]
Status: DONE
Reviewer: N/A（文档包独立验收见 D02）

Goal:
- 核对第1–24节，修订目标/历史差异，拆分正式 Batch 与最小验证。

Acceptance:
- 24节均有结论；接口有意变化与历史事实区分；已关闭 Batch 不被重新解释。

Evidence:
- 上表及 E1–E9；architect 独立只读核对 Range 调度、source lifetime、support gate 与异常语义。
- 仅修改指定 Review 与新增三个正式 Worklist；生产施工任务保持 TODO。

### D02 [SIMPLE]
Status: DONE
Reviewer: PASS（独立 final-reviewer，2026-09-06；仅本轮文档包 ACCEPTED）

Goal:
- 验证文档链接、条款一致性和 Git scope，完成文档包独立 final review。

Acceptance:
- 无断链/空白错误；findings 闭环。结论仅针对本轮文档包，不表示 BB-1/BB-2 已实现。

Evidence:
- 4份 Markdown、29个本地链接检查 PASS，fenced/inline code 已排除；围栏与尾随空白检查 PASS。
- BB-1 A01–A06、BB-2 B01–B04 均为 TODO，Goal/Acceptance/Evidence 格式检查 PASS；`git diff --check` PASS。
- 独立 `final_document_review` 结论 `ACCEPTED`：仅本轮文档包，无剩余 actionable findings。
- 审查发现的 Review 第25/33节任务编号歧义已改为正式任务映射；旧 range-parallel 的 N=1/default4 映射已补全，均经同一 reviewer re-check。
- 最终 scope：1份 Review 修改、3份 Worklist 新增；production、Origin/F2CPP 无变更；未 stage/commit/push。

## Blockers / findings

- 【历史快照·设计核对轮】无 production blocker；后续施工不属于该轮授权。（施工随后已于同日获授权并完成，见上。）
- 【已关闭】当时 GUIDE_USAGE 仍称 A/a 不进 fused、IGR-3B 未开始，doc/README 仍称 IGR-3 未施工，与 `0050f59`/`2410f13` 冲突——已由 BB-2 B03 修正（evidence 见 [BB-2_WORKLIST.md](BB-2_WORKLIST.md) B03/B04）。
- 【已关闭·收尾 remediation（2026-09-06）】benchmark 报告在 BB-1 改变 execution_mode/identifier 词汇后仍写 schema_version=2，与基线 v2 报告不可区分——已升级 `SCHEMA_VERSION = 3` 并在 GUIDE_BENCHMARKING 增加 v2→v3 映射；PRT 旧标签（fused/parallel reuse wall seconds 等）的保留边界与消费者已文档化于 GUIDE_BENCHMARKING；本 Worklist E3/E9 的旧 solver 路径断链已按 baseline SHA 标注修复。两域原 final-reviewer 复核均 ACCEPTED（Worklist 域一次通过；code/GUIDE 域 F1/F2 修复后通过），见 closure report 收尾 remediation 节。
