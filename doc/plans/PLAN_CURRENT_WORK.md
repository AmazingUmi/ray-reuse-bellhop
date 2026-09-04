# 当前工作与待决事项

> 更新日期：2026-09-04

## 当前状态

| 范围 | 状态 | 当前入口 |
|---|---|---|
| F2CPP 二维单频复刻 | 已封板的 production reference | [`../../Bellhop_F2CPP/doc/status/STATUS_PROGRESS.md`](../../Bellhop_F2CPP/doc/status/STATUS_PROGRESS.md) |
| F2CPP → RayReuse Feature Parity | `RR-B1`～`RR-B4`、`FP-1A`～`FP-2I` 全部 `ACCEPTED / CLOSED`；当前无 active Feature Parity Batch | [`REPORT_FEATURE_PARITY_FINAL.md`](../../Bellhop_RayReuse/doc/reports/REPORT_FEATURE_PARITY_FINAL.md) |
| RayReuse Feature Parity production baseline | `Production Feature Parity: COMPLETE`；`Remaining F2CPP parity GAP: 0`；accepted parity HEAD `0721fb3` | [`../../Bellhop_RayReuse/doc/status/STATUS_PROGRESS.md`](../../Bellhop_RayReuse/doc/status/STATUS_PROGRESS.md) |
| Origin oracle | GNU Fortran/gfortran 为唯一支持工具链，继续作为科学与文件行为基准 | [`../../Bellhop_origin/doc/README.md`](../../Bellhop_origin/doc/README.md) |
| IGR-1（Cross-Frequency Cartesian Cerveny Influence Fusion） | **`ACCEPTED / CLOSED`（2026-09-02，独立 final review）**。CC coherent fused 参考路径以 `--execution-mode fused` 落地为 opt-in 实验模式；数值上与现行 reuse 全程 bitwise 一致（cache fingerprint / raw workspace / scaled workspace / SHD SHA-256 四级 gate 全 PASS）；geometry 计数精确降为 1/Nf、frequency-kernel 计数不变；**wall-time 判定 `NOT_VIABLE`**（2F/8F/16F 的 `fused/reuse` 约为 1.425/1.038/1.077，归因见 R06 §8：几何非瓶颈、fused 布局损失内存局部性），fused 当时不成为默认生产路径 | [`REPORT_IGR1_CC_FUSION_IMPLEMENTATION.md`](../../Bellhop_RayReuse/doc/reports/REPORT_IGR1_CC_FUSION_IMPLEMENTATION.md)、[`REPORT_IGR1_CC_FUSION_BATCH_ACCEPTANCE.md`](../../Bellhop_RayReuse/doc/reports/REPORT_IGR1_CC_FUSION_BATCH_ACCEPTANCE.md)、[`IGR1_CC_FUSION_FINAL_REVIEW_2026-09-02.md`](../../Bellhop_RayReuse/doc/reviews/IGR1_CC_FUSION_FINAL_REVIEW_2026-09-02.md)、[`IGR-1_CC_FUSION_WORKLIST.md`](../../Bellhop_RayReuse/doc/worklists/IGR-1_CC_FUSION_WORKLIST.md) |
| IGR-2（Fused Influence Productionization & Optional Range Parallelism） | **`ACCEPTED / CLOSED`（2026-09-03，独立 final review）**。L1/L1c layout 升格为支持域内 RayReuse 主路径，`[R][D][F]` 为 production pressure hot layout；静态连续 range parallel 为显式可选优化，开启时默认 4 workers。legacy `reuse`/frequency-`parallel` 兼容保留并在 fused 可替代域内 deprecated；`nonreuse` 保留 reference/global default。16F 本地 dirty-worktree evidence 的 median wall：w1 86.133s、w2 45.585s、w4 27.742s、w8 22.953s，w8 speedup 3.75x；artifact 未记录 exact dirty diff hash，不能作为可精确重建的 commit identity | [`REPORT_IGR2_FUSED_INFLUENCE_PRODUCTIONIZATION_2026-09-03.md`](../../Bellhop_RayReuse/doc/reports/REPORT_IGR2_FUSED_INFLUENCE_PRODUCTIONIZATION_2026-09-03.md)、[`IGR2_FINAL_REVIEW_2026-09-03.md`](../../Bellhop_RayReuse/doc/reviews/IGR2_FINAL_REVIEW_2026-09-03.md) |
| IGR-3（Unified Fused Influence Architecture） | **`ACCEPTED / CLOSED`（2026-09-04，独立 final review）**。IGR-3A（`dda1c2c`）完成所有合法 TL beam-family adaptation；IGR-3B（`0050f59`）完成 `G/g/B × A/a` fused sink、`[R][D][F]` broadband Arrival layout、静态 range parallelism 与 source-streamed transactional writer。R/E 边界未改变 | [`IGR-3_SCOPE_AND_ARCHITECTURE_DECISION.md`](../../Bellhop_RayReuse/doc/worklists/IGR-3_SCOPE_AND_ARCHITECTURE_DECISION.md)、[`IGR-3B_ARRIVAL_FUSED_INFLUENCE_WORKLIST.md`](../../Bellhop_RayReuse/doc/worklists/IGR-3B_ARRIVAL_FUSED_INFLUENCE_WORKLIST.md) |

Feature Parity baseline 已冻结，IGR-1、IGR-2 与 IGR-3 均已
`ACCEPTED / CLOSED`。当前没有 active IGR construction Batch；后续候选方向
不会从本计划自动启动。

## 已决与后续事项

- [x] 选择新的 RayReuse research/performance 目标。已决定（2026-09-01）：
  cross-frequency influence geometry fusion（transient reuse），取代旧 IGR-0 的
  persistent geometry cache 原型方向。
- [x] 批准并执行 IGR-1（2026-09-02 全流程完成，`ACCEPTED / CLOSED`；性能结论
  `NOT_VIABLE`，correctness 完整保持）。
- [x] 基于 IGR-1p layout 与 static range-parallel 证据进入 IGR-2；当前
  construction 已完成，acceptance finding remediation 已完成。
- [x] IGR-2 Batch Acceptance 与独立 final review 通过（2026-09-03）；
  `ACCEPTED / CLOSED`。
- [x] IGR-3 高层 scope/architecture direction 已冻结并按两个串行 Batch 完成。
- [x] IGR-3A：remaining TL beam-family adaptation，独立验收后提交 `dda1c2c`。
- [x] IGR-3B：Arrival fused contribution、broadband Arrival layout 与
  source-streamed writer，独立验收后提交 `0050f59`。
- [ ] 决定远端 CI 首次运行、分支保护和公开发布前置条件；这些需要外部权限或
  产品决策，不属于本地代码未完成项。

## 非活动候选方向

以下项目属于 RayReuse research/performance 或 product extension 候选，不是
F2CPP parity GAP，也不属于已冻结的 IGR-3A/IGR-3B scope：

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
