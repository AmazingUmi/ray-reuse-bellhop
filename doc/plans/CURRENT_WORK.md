# 当前工作与待决事项

> 更新日期：2026-08-25

## 当前状态

| 范围 | 状态 | 当前入口 |
|---|---|---|
| F2CPP 二维单频复刻 | I0～I8、I9-B1～B4 已封板；性能 P1～P4-02 已完成并暂停 | [`../../Bellhop_F2CPP/doc/status/PROGRESS.md`](../../Bellhop_F2CPP/doc/status/PROGRESS.md) |
| RayReuse Feature Sync | RR-B1～RR-B4 已完成；尚未批准下一实施阶段 | [`../../Bellhop_RayReuse/doc/status/PROGRESS.md`](../../Bellhop_RayReuse/doc/status/PROGRESS.md) |
| Origin oracle | GNU Fortran/gfortran 为唯一支持工具链，继续作为行为基准 | [`../../Bellhop_origin/doc/README.md`](../../Bellhop_origin/doc/README.md) |

当前没有已批准、正在实施的数值功能计划。不要从已完成清单或历史计划中自动
恢复任务。

## 待用户决定

- [ ] 选择 RayReuse 下一研究目标。最新审计建议先评估无损的 Influence
  Geometry Reuse（IG-0），再决定是否进入带误差预算的频率重建实验；这些仍是
  候选路线，不是已批准实施项。证据见
  [`INFLUENCE_FREQUENCY_AUDIT.md`](../../Bellhop_RayReuse/doc/reports/INFLUENCE_FREQUENCY_AUDIT.md)。
- [ ] 决定远端 CI 首次运行、分支保护和公开发布前置条件；这些需要外部权限或
  产品决策，不属于本地代码未完成项。

## 非活动 backlog

F2CPP 和 RayReuse 支持矩阵中的 `Deferred`/out-of-scope 项只描述边界，不等于
待办。3D、N×2D、beam shift、HDF5、更多 beam family 或新的并行层，只有在
用户明确选择研究目标后才进入活动计划：

- [`F2CPP 支持矩阵`](../../Bellhop_F2CPP/doc/reference/FEATURE_SUPPORT_MATRIX.md)
- [`RayReuse 支持矩阵`](../../Bellhop_RayReuse/doc/reference/FEATURE_SUPPORT_MATRIX.md)

已完成的项目级阶段清单保存在
[`../archive/项目实施清单-2026-08-14.md`](../archive/项目实施清单-2026-08-14.md)，
仅供追溯。
