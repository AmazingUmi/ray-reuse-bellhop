# RayReuse 报告索引

报告是特定日期和代码身份上的证据快照，不承担当前待办职责。

| 报告 | 日期/基线 | 作用 |
|---|---|---|
| [`REPORT_FEATURE_PARITY_FINAL.md`](./REPORT_FEATURE_PARITY_FINAL.md) | 2026-08-30；production `0721fb3`；final acceptance docs `88ba8b7` | F2CPP → RayReuse 最终功能盘点、HEAD 健康门、性能快照与正式 verdict |
| [`REPORT_F2CPP_RAYREUSE_PARITY_2026-08-25.md`](./REPORT_F2CPP_RAYREUSE_PARITY_2026-08-25.md) | 2026-08-25～29，FP-1A～FP-2I | production feature-axis parity 审计与逐项证据索引 |
| [`REPORT_IGR0_REVISION_CROSS_FREQUENCY_FUSION_2026-09-01.md`](./REPORT_IGR0_REVISION_CROSS_FREQUENCY_FUSION_2026-09-01.md) | 2026-09-01；production inspected at `b33abfb` | IGR 架构冻结：Cross-Frequency Influence Geometry Fusion（IGR-1 的设计来源；IGR-1 本体见下） |
| [`REPORT_IGR1_CC_FUSION_IMPLEMENTATION.md`](./REPORT_IGR1_CC_FUSION_IMPLEMENTATION.md) | 2026-09-02；IGR-1 `ACCEPTED / CLOSED` | **IGR-1 收口报告**：实现架构、A–D 级 parity、计数器、性能与内存结论（wall-time 判定 `NOT_VIABLE`；fused 保留为 opt-in 实验模式） |
| [`REPORT_IGR1_CC_FUSION_BATCH_ACCEPTANCE.md`](./REPORT_IGR1_CC_FUSION_BATCH_ACCEPTANCE.md) | 2026-09-02 | IGR-1 batch acceptance 清单与证据汇总 |
| [`REPORT_IGR1_R01_BASELINE.md`](./REPORT_IGR1_R01_BASELINE.md) | 2026-09-02 | IGR-1 R01 计数器拆分与 frequency-major baseline（2F/8F/16F） |
| [`REPORT_IGR1_R05_PARITY.md`](./REPORT_IGR1_R05_PARITY.md) | 2026-09-02 | IGR-1 R05 Level A–D parity 矩阵（13 用例 + 4 fixture，全部 bitwise PASS） |
| [`REPORT_IGR1_R06_PERFORMANCE.md`](./REPORT_IGR1_R06_PERFORMANCE.md) | 2026-09-02 | IGR-1 R06 性能与内存验收（`NOT_VIABLE` 判定与归因；32F/64F 延后） |
| [`REPORT_INFLUENCE_FREQUENCY_AUDIT_2026-08-25.md`](./REPORT_INFLUENCE_FREQUENCY_AUDIT_2026-08-25.md) | 2026-08-25，`8300c89` | historical / partially superseded architecture audit；保留数据流与热点证据，不是当前 IGR implementation roadmap |
| [`REPORT_CROSS_COMPILER_H4_2026-08-07.md`](./REPORT_CROSS_COMPILER_H4_2026-08-07.md) | 2026-08-07 | AppleClang/GCC 构建、数值和资源矩阵 |
| [`REPORT_MODEL_MATRIX_06E390F_2026-08-01.md`](./REPORT_MODEL_MATRIX_06E390F_2026-08-01.md) | 2026-08-01，`06e390f` | 三模型 single/broadband 数值矩阵 |
| [`REPORT_LOCAL_VALIDATION_C417095_2026-08-01.md`](./REPORT_LOCAL_VALIDATION_C417095_2026-08-01.md) | 2026-08-01，`c417095` | H1～H3 微基准和中间状态验证 |

被新证据取代的模型矩阵和性能实验保存在 [`../archive/`](../archive/)。当前状态
以 [`../status/STATUS_PROGRESS.md`](../status/STATUS_PROGRESS.md) 为准，下一工作只从
[`../../../doc/plans/PLAN_CURRENT_WORK.md`](../../../doc/plans/PLAN_CURRENT_WORK.md) 进入。
