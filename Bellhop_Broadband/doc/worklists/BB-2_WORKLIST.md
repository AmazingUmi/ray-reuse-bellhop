# BB-2 — Living Documentation & Terminology Closure

Status: **BB-2 ACCEPTED**（final-reviewer，2026-09-06）→ `BB-M1 ACCEPTED / CLOSED`
Date prepared: 2026-09-06
Prerequisite: [BB-1](BB-1_WORKLIST.md) ACCEPTED（2026-09-06），BB-2 施工已获授权。
Authority: [BB-M1 design](BB-M1_WORKLIST.md)。准备轮（2026-09-06，历史快照）仅生成本计划；BB-1 ACCEPTED 后同日获授权执行 B01–B04 并 ACCEPTED。

## Scope / freeze

- 当前可执行说明采用 Bellhop Broadband、NonReuse / RayReuse、Serial/Frequency/Range、Trace/Reuse workers。
- 保留 `Bellhop_RayReuse/`；保留内部 fused 术语与已关闭 FP/RR/IGR/PERF/EXP 历史结论。
- 历史身份是 dated/frozen/closed 的语义，不以文件是否位于 archive 判定；当前 README/status 即使内容陈旧，也应更新。
- 不改生产代码与数值行为；若发现 BB-1 实现遗漏，回 BB-1 remediation，不以文档掩盖。

### B01 [SIMPLE]
Status: DONE
Reviewer: N/A（B04 集中验收）

Goal:
- 列出受影响当前 docs/脚本说明，分类 CURRENT / HISTORICAL / OBSOLETE。
- 覆盖根/项目/doc/demo README、usage/benchmark/release guides、support matrix、status、当前 architecture 与 developer instructions。

Acceptance:
- 每个活跃命令入口有归属；历史内容不全局替换；已过时的 current 文档不误归历史而漏改。

Evidence:
- 清单 [BB-2_B01_DOC_INVENTORY.md](BB-2_B01_DOC_INVENTORY.md)：CURRENT 13 文件/约 54 处；核对后无需更新 11 个；HISTORICAL 14 条目（FP 序列快照、HDF5 决策、9 个 FP-2 workreports、archive 树、test/legacy）；OBSOLETE 0。
- 五大更新点：GUIDE_USAGE 整体旧四模式模型 + "A/a 不进 fused/IGR-3B 未开始" 错误声明；三个索引级 "IGR-3 未施工" 冲突（根 README、doc/README×2）；Bellhop_RayReuse/README 旧模式散文与错误默认值；GUIDE_BENCHMARKING 全部旧协议示例；test/standard_cases README+REFERENCE_SNAPSHOTS 的 runner 契约描述。

### B02 [ADVANCED]
Status: DONE
Reviewer: N/A（B04 集中验收）

Goal:
- 根据 BB-1 最终 production gate 复核 NonReuse/Serial/Frequency/Range 对 TL/A/a/E/R、source、beam、receiver、频率数的支持。
- 修正 GUIDE_USAGE 的 A/a 不进 fused、IGR-3B 未开始以及 doc/README 的 IGR-3 未施工等已确认滞后。

Acceptance:
- Range TL 单 source，Range ARR source streaming 支持多 source；规则、等距且至少2 range；SingleFrequency/R/E 例外齐全。
- TL predicate 与 ARR 独立 gate，profiling/resource 参数域正确；文档不反向扩大能力。

Evidence:
- 事实表 [BB-2_B02_SUPPORT_MATRIX_FACTS.md](BB-2_B02_SUPPORT_MATRIX_FACTS.md)（全部结论附 文件:行 证据与错误文案原文）。
- 关键事实：Range ARR 多 source production 化且 source-local（trace source i → 全频累积 → consumer → fingerprint → 下一 source，无全局 all-source barrier；reuse_range_para_solver.cpp:516-568）；Range TL 单 source + beam family/run-mode 边界（SimpleGaussian 仅 coherent 等）；E 拒绝 Range；R 单频、显式 reuse 拒绝、trace-workers 合法。
- 参数精确边界：`--verify-cache` 打印/校验域为 R、ARR/E 全路线、TL 三条 reuse 路线（TL nonreuse 不打印）；`--profile-influence` = TL + Cartesian Cerveny（含单频）；frequency TL worker 是 memory-budget-aware activeFrequencyLimit，ARR/E frequency worker 是纯 min(N, 频数)；range min(N, rangeCount)；workers=1 不改路线。
- REFERENCE_FEATURE_SUPPORT_MATRIX.md 支持域与代码一致，差异集中命名/PRT 标签/默认 worker 数（:45-54 等，B03 修正）。

### B03 [SIMPLE]
Status: DONE
Reviewer: N/A（B04 集中验收）

Goal:
- 更新 living docs、迁移旧默认 worker 说明、撤销当前推荐中的旧弃用提示，补充历史术语映射入口。
- 报告 `REPORT_BB_M1_BROADBAND_NAMING_MILESTONE_YYYY-MM-DD.md` 使用实际完成日期，记录 baseline、实现/文档 commit（未提交则明确）、验证与最终 verdict。

Acceptance:
- 当前示例使用新 CLI；历史旧名称仍可解释；无夸大性能、历史完成状态或 clean/commit 声明。

Evidence:
- 更新 13 个 markdown（即 B01 清单的 13 项 CURRENT 文件，含 doc/plans/PLAN_CURRENT_WORK.md 与 doc/architecture/ARCHITECTURE_BELLHOP_RAY_REUSE.md）。核心：GUIDE_USAGE 全量 CLI/fused 支持域段重写（含 "A/a 不进 fused、IGR-3B 未开始" 错误声明修正）；三个索引 "IGR-3 未施工" 修正；Bellhop_RayReuse/README 模式列表与默认值；GUIDE_BENCHMARKING 协议示例；support matrix 命名/PRT 标签/默认值；STATUS_PROGRESS 追加 BB-M1/BB-1（历史条目未重写）；test runner 契约描述。
- 历史术语映射入口：根 doc/README.md 新增 "Terminology after BB-M1" 小节（Review §31 全表 + 默认值变化 + fused 内部术语说明）。
- Closure report：`doc/reports/REPORT_BB_M1_BROADBAND_NAMING_MILESTONE_2026-09-06.md`（Before/After、baseline SHA、BB-1/BB-2 均如实记录未提交、tests/parity/support audit/protected refs、BB-1 ACCEPTED、BB-2 verdict 标 pending B04、未写 CLOSED）。
- 验证：13+2 文件与新报告全部相对链接 0 broken（fenced/inline 排除）；命令抽查 3 条真实执行（version/help/runner choices 拒绝旧值）；残留旧名 token 均为有意保留（删除说明/映射表/历史叙述/fence）；`git diff --check` clean。
- 未发现 BB-1 实现遗漏（B02 事实表与文档修正一致，无需回 BB-1 remediation）。

### B04 [ADVANCED]
Status: DONE
Reviewer: ACCEPTED（独立 final-reviewer，2026-09-06；F1/F2 remediation 后复核）

Goal:
- 集中文档/Git 校验与独立 final review，检查支持矩阵和历史完整性。

Acceptance:
- 新/改 Markdown 链接有效，跳过 fenced/inline code；活跃命令无旧 CLI；`git diff --check` PASS；protected refs 和 production 无本 Batch 变更。
- final-reviewer 只能 ACCEPTED / CHANGES_REQUIRED；finding 修正后回同一 reviewer。通过后才记录 `BB-2 ACCEPTED` 与 `BB-M1 ACCEPTED / CLOSED`。

Evidence:
- 首轮 VERDICT: CHANGES_REQUIRED（2 findings，均为记录准确性）：F1 PLAN_CURRENT_WORK.md:16 提前标 `ACCEPTED / CLOSED`；F2 B03 evidence "15 个/清单外遗漏" 计数不实。
- Remediation（coordinator，单行文本修正）：F1 改为 `BB-1 ACCEPTED（2026-09-06）；BB-M1 closure pending BB-2 final review`；F2 改为 "更新 13 个 markdown（即 B01 清单的 13 项 CURRENT 文件，含 PLAN/ARCHITECTURE）"。同一 final-reviewer 复核确认两处闭环。
- 复核 VERDICT: **ACCEPTED**，八项全 PASS：137 个相对链接（13 个 B03 修改文件 + 2 个 B01/B02 新增清单 + 新报告等 17 个新/改 markdown，剔 fenced/inline）全部有效；7 个 living docs 活跃命令仅余删除/迁移说明语境旧 token，独立 smoke 确认旧值 fused 被拒（rc=2）；支持矩阵与 B02 事实表逐项一致无反向扩大；archive/workreports/decisions/历史快照 diff 全空；closure report 如实（未提交、无 CLOSED 预声明、无性能 overclaim）；BB-2 零生产文件；terminology 映射与 Review §31 一致；STATUS_PROGRESS 追加式。
- 最终 Git scope：63 files / +1442 / −4797（BB-1 + BB-2 + 既有文档成果）；`git diff --check` clean；无 staged；Origin/F2CPP 零改动。
- 据此记录：**BB-2 ACCEPTED**、**BB-M1 ACCEPTED / CLOSED**（2026-09-06）。

## 最小验证与停止边界

- 文档检查以受影响文件/链接/命令和源码 gate 为主。命令示例优先引用 BB-1 已通过命令；新增且无证据的命令仅做对应 smoke。
- 不重跑 CTest、全仓 pytest、Origin/F2CPP parity、benchmark/RSS；源码未变时沿用 BB-1 验证。
- 不为纯术语改动新增测试；support semantics 和历史完整性集中在 B04 独立验收。
- BB-2 ACCEPTED 后关闭里程碑；BB-PERF-1 仅 FOLLOW-UP，未获授权不启动，不自动 tag/push。

## Blockers / findings

- 无未闭环 finding。BB-2 ACCEPTED；BB-M1 ACCEPTED / CLOSED（2026-09-06）。
- 验收时按授权保持未提交；用户后续授权本地 commit。BB-1 实现为 `77e53c4`，BB-2 文档随本 Worklist 所在独立 docs 提交归档，身份见 closure report；未 push/tag。
- BB-PERF-1 仅 FOLLOW-UP，未获授权不启动。
