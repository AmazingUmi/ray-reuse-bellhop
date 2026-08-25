# Bellhop F2CPP 文档索引

F2CPP 是独立的 C++20 二维单频 Bellhop 实现。I0～I8、I9-B1～B4 已封板，
性能 P1～P4-02 已完成并暂停；本组件当前没有活动实施计划。

## 当前文档

| 目的 | 文档 |
|---|---|
| 构建、运行与测试 | [`guides/USAGE.md`](./guides/USAGE.md) |
| 当前支持、差异与延期边界 | [`reference/FEATURE_SUPPORT_MATRIX.md`](./reference/FEATURE_SUPPORT_MATRIX.md) |
| 数值接口与中间状态契约 | [`reference/INTERMEDIATE_STATE_CONTRACT.md`](./reference/INTERMEDIATE_STATE_CONTRACT.md) |
| 当前封板状态与验证摘要 | [`status/PROGRESS.md`](./status/PROGRESS.md) |
| 性能阶段证据 | [`reports/PERFORMANCE.md`](./reports/PERFORMANCE.md) |
| 分 iteration 验证报告 | [`reports/README.md`](./reports/README.md) |
| 已完成计划和任务过程 | [`archive/README.md`](./archive/README.md) |

项目级下一步只从
[`doc/plans/CURRENT_WORK.md`](../../doc/plans/CURRENT_WORK.md) 进入；不要从
`archive/` 自动恢复复刻任务。

## 目录语义

- `guides/`：当前可执行的操作方法；
- `reference/`：稳定支持矩阵和数值契约；
- `status/`：当前状态与最新汇总；
- `reports/`：性能与验证证据；
- `archive/`：已完成的构建计划、复刻路线、派生快照和任务过程。
