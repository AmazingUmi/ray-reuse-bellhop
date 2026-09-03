# Bellhop RayReuse 文档索引

RayReuse 是独立的 C++20 多频轨迹复用实现。`RR-B1`～`RR-B4` 与
`FP-1A`～`FP-2I` 已全部 `ACCEPTED / CLOSED`：

```text
Production Feature Parity: COMPLETE
Remaining F2CPP parity GAP: 0

Accepted production HEAD: 0721fb3
Final acceptance documentation commit: 88ba8b7
```

IGR-2 已 `ACCEPTED / CLOSED`：已保留的 fused layout 是支持域内的
production RayReuse 主路径，可选静态 receiver-range parallelism 已产品化。

## 当前权威入口

| 目的 | 文档 |
|---|---|
| 最终整体验收、smoke 与性能快照 | [`reports/REPORT_FEATURE_PARITY_FINAL.md`](./reports/REPORT_FEATURE_PARITY_FINAL.md) |
| 当前 feature boundary | [`reference/REFERENCE_FEATURE_SUPPORT_MATRIX.md`](./reference/REFERENCE_FEATURE_SUPPORT_MATRIX.md) |
| 当前仓库状态 | [`status/STATUS_PROGRESS.md`](./status/STATUS_PROGRESS.md) |
| Feature Parity sequence 历史快照与最终关闭记录 | [`status/STATUS_FEATURE_PARITY_SEQUENCE_2026-08-29.md`](./status/STATUS_FEATURE_PARITY_SEQUENCE_2026-08-29.md) |
| 产品与 execution mode 用法 | [`guides/GUIDE_USAGE.md`](./guides/GUIDE_USAGE.md) |
| 性能基准协议 | [`guides/GUIDE_BENCHMARKING.md`](./guides/GUIDE_BENCHMARKING.md) |
| 单线程三模型微基准 | [`guides/GUIDE_SINGLE_THREAD_MICROBENCHMARK.md`](./guides/GUIDE_SINGLE_THREAD_MICROBENCHMARK.md) |
| 内部发布验证 | [`guides/GUIDE_RELEASE.md`](./guides/GUIDE_RELEASE.md) |
| 验证、矩阵与审计报告索引 | [`reports/README.md`](./reports/README.md) |
| 当前 IGR 架构冻结：Cross-Frequency Fusion | [`reports/REPORT_IGR0_REVISION_CROSS_FREQUENCY_FUSION_2026-09-01.md`](./reports/REPORT_IGR0_REVISION_CROSS_FREQUENCY_FUSION_2026-09-01.md) |
| IGR 理论与数值契约 | [`../../doc/reference/REFERENCE_INFLUENCE_GEOMETRY_REUSE.md`](../../doc/reference/REFERENCE_INFLUENCE_GEOMETRY_REUSE.md) |
| **IGR-1 收口：CC Fusion `ACCEPTED / CLOSED`（wall-time `NOT_VIABLE`，fused 为 opt-in 实验模式）** | [`reports/REPORT_IGR1_CC_FUSION_IMPLEMENTATION.md`](./reports/REPORT_IGR1_CC_FUSION_IMPLEMENTATION.md) |
| IGR-1 执行 Worklist（R01–R06 全部 DONE） | [`worklists/IGR-1_CC_FUSION_WORKLIST.md`](./worklists/IGR-1_CC_FUSION_WORKLIST.md) |
| IGR-1 冻结设计 | [`worklists/DESIGN_IGR1_CC_FUSION.md`](./worklists/DESIGN_IGR1_CC_FUSION.md) |
| IGR-1 最终独立评审（`ACCEPTED`） | [`reviews/IGR1_CC_FUSION_FINAL_REVIEW_2026-09-02.md`](./reviews/IGR1_CC_FUSION_FINAL_REVIEW_2026-09-02.md) |
| **IGR-2 冻结设计** | [`worklists/DESIGN_IGR2_FUSED_INFLUENCE_PRODUCTIONIZATION.md`](./worklists/DESIGN_IGR2_FUSED_INFLUENCE_PRODUCTIONIZATION.md) |
| **IGR-2 closed worklist** | [`worklists/IGR-2_FUSED_INFLUENCE_PRODUCTIONIZATION_WORKLIST.md`](./worklists/IGR-2_FUSED_INFLUENCE_PRODUCTIONIZATION_WORKLIST.md) |
| **IGR-2 final review (`ACCEPTED`)** | [`reviews/IGR2_FINAL_REVIEW_2026-09-03.md`](./reviews/IGR2_FINAL_REVIEW_2026-09-03.md) |
| HDF5 延后决策 | [`decisions/DECISION_HDF5_SCHEMA.md`](./decisions/DECISION_HDF5_SCHEMA.md) |
| 已完成计划和历史基准 | [`archive/README.md`](./archive/README.md) |

当前路径是：**Trajectory Reuse 已完成 → IGR-1 已 `ACCEPTED / CLOSED` →
IGR-1p 保留 L1/L1c locality layout → IGR-2 已完成 fused CC TL
与可选静态 range parallel productionization**。IGR-2 不扩展 beam/product/source/receiver 支持域；
dynamic tiles、frequency blocking 与 L1b 均未保留。新的候选方向统一从
[`doc/plans/PLAN_CURRENT_WORK.md`](../../doc/plans/PLAN_CURRENT_WORK.md) 进入。

## 目录语义

- `guides/`：当前可执行的操作方法；
- `reference/`：当前稳定支持与契约边界；
- `status/`：当前状态，以及明确标注为 snapshot 的历史 sequence；
- `reports/`：带日期/提交身份的验收、验证和审计证据；
- `worklists/`、`workreports/`：frozen Batch 执行与验收证据；
- `decisions/`：已冻结工程决策；
- `archive/`：完成计划、派生记录和被替代的性能/矩阵证据。

`archive/`、dated reports 与 frozen Batch artifacts 中的“当时尚未完成”、GAP 和
测试数量属于历史事实；不得把它们当作当前状态。当前结论以上述 final report、
support matrix 和 progress status 为准。
