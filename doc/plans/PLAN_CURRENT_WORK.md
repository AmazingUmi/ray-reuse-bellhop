# 当前工作与待决事项

> 更新日期：2026-09-01

## 当前状态

| 范围 | 状态 | 当前入口 |
|---|---|---|
| F2CPP 二维单频复刻 | 已封板的 production reference | [`../../Bellhop_F2CPP/doc/status/STATUS_PROGRESS.md`](../../Bellhop_F2CPP/doc/status/STATUS_PROGRESS.md) |
| F2CPP → RayReuse Feature Parity | `RR-B1`～`RR-B4`、`FP-1A`～`FP-2I` 全部 `ACCEPTED / CLOSED`；当前无 active Feature Parity Batch | [`REPORT_FEATURE_PARITY_FINAL.md`](../../Bellhop_RayReuse/doc/reports/REPORT_FEATURE_PARITY_FINAL.md) |
| RayReuse production baseline | `Production Feature Parity: COMPLETE`；`Remaining F2CPP parity GAP: 0`；accepted production HEAD `0721fb3` | [`../../Bellhop_RayReuse/doc/status/STATUS_PROGRESS.md`](../../Bellhop_RayReuse/doc/status/STATUS_PROGRESS.md) |
| Origin oracle | GNU Fortran/gfortran 为唯一支持工具链，继续作为科学与文件行为基准 | [`../../Bellhop_origin/doc/README.md`](../../Bellhop_origin/doc/README.md) |
| IGR 研究方向（Influence Geometry Reuse → 跨频融合） | 用户已选定主方案：**Cross-Frequency Influence Geometry Fusion（transient reuse via loop restructuring）**；IGR-0 Revision 已于 2026-09-01 完成并通过独立 final-review（`ACCEPTED`，2 项 MINOR findings 已闭环，零 production 改动）；IGR-1 DESIGN 草案已备，**等待用户批准进入 IGR-1 DESIGN/CONSTRUCT** | [`REPORT_IGR0_REVISION_CROSS_FREQUENCY_FUSION_2026-09-01.md`](../../Bellhop_RayReuse/doc/reports/REPORT_IGR0_REVISION_CROSS_FREQUENCY_FUSION_2026-09-01.md)、[`IGR-1_CC_FUSION_DESIGN_DRAFT.md`](../../Bellhop_RayReuse/doc/worklists/IGR-1_CC_FUSION_DESIGN_DRAFT.md)、[`IGR0_REVISION_FINAL_REVIEW_2026-09-01.md`](../../Bellhop_RayReuse/doc/reviews/IGR0_REVISION_FINAL_REVIEW_2026-09-01.md) |

Feature Parity baseline 已冻结。当前没有已批准、正在实施的 Feature Parity Batch，
也不要从已关闭 Worklist、历史计划或 performance finding 中自动恢复任务。
IGR 方向的用户决策已完成（选定 cross-frequency fusion）；IGR-1 施工具备
`DESIGN DRAFT — NOT APPROVED` 状态，在获批前不进入 CONSTRUCT。

## 待用户决定

- [x] 选择新的 RayReuse research/performance 目标。已决定（2026-09-01）：
  cross-frequency influence geometry fusion（transient reuse），取代旧 IGR-0 的
  persistent geometry cache 原型方向。修订记录与 IGR-1 草案见上表 IGR 行。
- [ ] 批准 IGR-1（Cross-Frequency Cartesian Cerveny Influence Fusion）进入
  DESIGN/CONSTRUCT；批准前 `IGR-1_CC_FUSION_DESIGN_DRAFT.md` 保持 NOT APPROVED。
- [ ] 决定远端 CI 首次运行、分支保护和公开发布前置条件；这些需要外部权限或
  产品决策，不属于本地代码未完成项。

## 非活动候选方向

以下项目属于 RayReuse research/performance 或 product extension 候选，不是
F2CPP parity GAP，也不是 active task（注意：Influence Geometry Reuse 已被
选定为当前 IGR 研究方向，不再是候选，见上表）：

- frequency interpolation / reconstruction；
- BARR 等研究型 broadband products；
- SIMD 与其他 Influence 性能优化；
- HDF5 与其他 RayReuse-only 输出扩展；
- 3D、N×2D、beam shift 和新的并行层。

当前 feature boundary 以
[`REFERENCE_FEATURE_SUPPORT_MATRIX.md`](../../Bellhop_RayReuse/doc/reference/REFERENCE_FEATURE_SUPPORT_MATRIX.md)
为准。已完成的项目级阶段清单保存在
[`../archive/PLAN_PROJECT_IMPLEMENTATION_2026-08-14.md`](../archive/PLAN_PROJECT_IMPLEMENTATION_2026-08-14.md)，
仅供历史追溯，不代表当前状态。
