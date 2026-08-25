# Bellhop RayReuse 文档索引

RayReuse 是独立的 C++20 多频轨迹复用实现。A～H 与 RR-B1～RR-B4 已完成；
当前没有已批准的下一实施阶段，研究候选统一从项目级当前工作清单进入。

## 当前文档

| 目的 | 文档 |
|---|---|
| R/A/a/E 与多频运行 | [`guides/USAGE.md`](./guides/USAGE.md) |
| 性能基准 | [`guides/BENCHMARKING.md`](./guides/BENCHMARKING.md) |
| 单线程三模型微基准 | [`guides/SINGLE_THREAD_MICROBENCHMARK.md`](./guides/SINGLE_THREAD_MICROBENCHMARK.md) |
| 内部发布验证 | [`guides/RELEASE.md`](./guides/RELEASE.md) |
| 支持、差异与延期边界 | [`reference/FEATURE_SUPPORT_MATRIX.md`](./reference/FEATURE_SUPPORT_MATRIX.md) |
| 当前状态 | [`status/PROGRESS.md`](./status/PROGRESS.md) |
| 验证、矩阵与审计报告 | [`reports/README.md`](./reports/README.md) |
| HDF5 延后决策 | [`decisions/HDF5_SCHEMA_DECISION.md`](./decisions/HDF5_SCHEMA_DECISION.md) |
| 已完成计划和历史基准 | [`archive/README.md`](./archive/README.md) |

项目级下一步见
[`doc/plans/CURRENT_WORK.md`](../../doc/plans/CURRENT_WORK.md)。Influence 频率复用
审计给出了 IG-0/FI-0 候选路线，但在用户选择前仍属于报告结论，不是活动计划。

## 目录语义

- `guides/`：当前可执行的操作方法；
- `reference/`：稳定支持边界；
- `status/`：当前封板状态；
- `reports/`：带日期/提交身份的验证结果和审计；
- `decisions/`：已冻结工程决策；
- `archive/`：完成计划、派生记录和被替代的性能/矩阵证据。
