# Bellhop RayReuse 文档索引

RayReuse 是独立的 C++20 多频轨迹复用实现。`RR-B1`～`RR-B4` 与
`FP-1A`～`FP-2I` 已全部 `ACCEPTED / CLOSED`：

```text
Production Feature Parity: COMPLETE
Remaining F2CPP parity GAP: 0

Accepted production HEAD: 0721fb3
Final acceptance documentation commit: 88ba8b7
```

当前没有 active Feature Parity Batch。新的 research/performance 候选只有在用户
明确批准后才进入 active work。

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
| HDF5 延后决策 | [`decisions/DECISION_HDF5_SCHEMA.md`](./decisions/DECISION_HDF5_SCHEMA.md) |
| 已完成计划和历史基准 | [`archive/README.md`](./archive/README.md) |

新的候选方向统一从
[`doc/plans/PLAN_CURRENT_WORK.md`](../../doc/plans/PLAN_CURRENT_WORK.md) 进入；
Influence Geometry Reuse、frequency interpolation、BARR、SIMD、HDF5 等当前均非
active Feature Parity task。

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
