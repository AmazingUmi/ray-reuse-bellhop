# 当前工作与待决事项

> 更新日期：2026-09-02

## 当前状态

| 范围 | 状态 | 当前入口 |
|---|---|---|
| F2CPP 二维单频复刻 | 已封板的 production reference | [`../../Bellhop_F2CPP/doc/status/STATUS_PROGRESS.md`](../../Bellhop_F2CPP/doc/status/STATUS_PROGRESS.md) |
| F2CPP → RayReuse Feature Parity | `RR-B1`～`RR-B4`、`FP-1A`～`FP-2I` 全部 `ACCEPTED / CLOSED`；当前无 active Feature Parity Batch | [`REPORT_FEATURE_PARITY_FINAL.md`](../../Bellhop_RayReuse/doc/reports/REPORT_FEATURE_PARITY_FINAL.md) |
| RayReuse production baseline | `Production Feature Parity: COMPLETE`；`Remaining F2CPP parity GAP: 0`；accepted production HEAD `0721fb3` | [`../../Bellhop_RayReuse/doc/status/STATUS_PROGRESS.md`](../../Bellhop_RayReuse/doc/status/STATUS_PROGRESS.md) |
| Origin oracle | GNU Fortran/gfortran 为唯一支持工具链，继续作为科学与文件行为基准 | [`../../Bellhop_origin/doc/README.md`](../../Bellhop_origin/doc/README.md) |
| IGR-1（Cross-Frequency Cartesian Cerveny Influence Fusion） | **`ACCEPTED / CLOSED`（2026-09-02，独立 final review）**。CC coherent fused 参考路径以 `--execution-mode fused` 落地为 opt-in 实验模式；数值上与现行 reuse 全程 bitwise 一致（cache fingerprint / raw workspace / scaled workspace / SHD SHA-256 四级 gate 全 PASS）；geometry 计数精确降为 1/Nf、frequency-kernel 计数不变；**wall-time 判定 `NOT_VIABLE`**（2F/8F/16F fused 均慢于 reuse：0.70/0.96/0.93，归因见 R06 §8：几何非瓶颈、fused 布局损失内存局部性），fused 不成为默认生产路径 | [`REPORT_IGR1_CC_FUSION_IMPLEMENTATION.md`](../../Bellhop_RayReuse/doc/reports/REPORT_IGR1_CC_FUSION_IMPLEMENTATION.md)、[`REPORT_IGR1_CC_FUSION_BATCH_ACCEPTANCE.md`](../../Bellhop_RayReuse/doc/reports/REPORT_IGR1_CC_FUSION_BATCH_ACCEPTANCE.md)、[`IGR1_CC_FUSION_FINAL_REVIEW_2026-09-02.md`](../../Bellhop_RayReuse/doc/reviews/IGR1_CC_FUSION_FINAL_REVIEW_2026-09-02.md)、[`IGR-1_CC_FUSION_WORKLIST.md`](../../Bellhop_RayReuse/doc/worklists/IGR-1_CC_FUSION_WORKLIST.md) |

Feature Parity baseline 已冻结。当前没有已批准、正在实施的 Feature Parity Batch，
也不要从已关闭 Worklist、历史计划或 performance finding 中自动恢复任务。
IGR-1 已关闭且其 fused 路径判定为 wall-time `NOT_VIABLE`；**IGR 整体未宣告完成**，
任何 IGR-2 方向（其他 beam family 融合、frequency blocking、并行融合、layout-tuned
变体等）都需要用户依据 IGR-1 实测数据明确批准后才启动。

## 待用户决定

- [x] 选择新的 RayReuse research/performance 目标。已决定（2026-09-01）：
  cross-frequency influence geometry fusion（transient reuse），取代旧 IGR-0 的
  persistent geometry cache 原型方向。
- [x] 批准并执行 IGR-1（2026-09-02 全流程完成，`ACCEPTED / CLOSED`；性能结论
  `NOT_VIABLE`，correctness 完整保持）。
- [ ] 是否基于 IGR-1 数据进入任何后续 IGR 批次（建议排序见
  [`REPORT_IGR1_CC_FUSION_IMPLEMENTATION.md`](../../Bellhop_RayReuse/doc/reports/REPORT_IGR1_CC_FUSION_IMPLEMENTATION.md) §8；
  默认建议：停止 serial-fusion 方向的 IGR 优化，wall-time 需求转向 parallel 路径）。
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
