# Bellhop RayReuse 当前进度

> 更新日期：2026-08-28
> 当前状态：FP-2D cubic spline SSP `S` parity 已完成施工、Batch Acceptance 与
> FP-2D-R1 remediation；独立 Re-Final Review 结论为 `ACCEPTED`（只关闭 `S`
> slice；`Q`/`.ssp` 仍 deferred）。

## 已完成范围

| Batch | 状态 | 结果 |
|---|---|---|
| RR-B1 | 完成，提交 `daf687f` | V/R/A/G/F、`.trc/.brc`、折线边界、`.ati/.bty`、elastic LL、reflection event raw material/segment identity |
| RR-B2 | 完成，提交 `5e6cc03` | generalized R、directional `.sbp`、per-frequency active/terminal prefix、R-only `Nalpha=1`、Origin-compatible RAY writer |
| RR-B3 | 完成，提交 `5e6cc03` | A/a、E、ArrivalWorkspace/AddArr、Cartesian G/B traversal、逐频 product 与三种执行模式 |
| RR-B4 | 完成 | parser/executable/CLI lifecycle、共享 standard-case adapter、三方与多频回归、文档封板 |
| FP-2A | 完成 | `Ag/ag/Eg` parser/runtime、ray-centered Arrival/Eigenray traversal、共享 oracle 与多频三模式 parity |
| FP-2B | 完成 | PCHIP SSP parity、`GeometrySspEvaluator`/`FrequencySspEvaluator`、共享 `munk_pchip` oracle 与三模式一致性 |
| FP-2C | 完成（待最终验收） | N²-linear SSP `N` parity：real geometry（node jump + 非零 Hessian）与 frequency-local complex N² evaluator、共享 `munk_n2` 三方 oracle、TL/R/A/a/E 与 `nonreuse/reuse/parallel` 一致性；S/Q 仍 deferred |
| FP-2D | 完成；Re-Final Review `ACCEPTED` | Cubic spline SSP `S` parity（只关闭 `S` slice）：exact not-a-knot coefficient kernel（保留 legacy binary32 `1.0F/6.0F`）、real spline evaluator（节点连续梯度、无 node jump、非零 Hessian）、frequency-local complex spline evaluator（每频独立 coefficients）、共享 `munk_spline` 三方 oracle、TL/R/A/a/E 与三执行模式一致性（trace passes 2/1/1、cache fingerprint 前后不变）；FP-2D-R1 已收紧 250 Hz Origin oracle policy、增加 C++ decoded-payload exact gate 与真实跨节点 no-jump regression；`Q`/`.ssp` 仍 deferred |

## RR-B4 验收基线

- RayReuse Release CTest：28/28；
- standard-case Python/tool tests：148/148；
- RayReuse 共享单频案例：20/20；
- R 三方：1 ray、1107 points、top/bottom bounce 3/3、最大坐标误差 0 m；
- A/a 三方：各 89 arrival records，record sequencing、bounce 和全部浮点字段
  最大 0 ULP；zero case 为 0 records；
- E 三方：1418 blocks，最大坐标误差 0 m；zero case 为 0 blocks；
- 两频 A/a/E：`nonreuse/reuse/parallel` 每频产品 SHA-256 一致；
- 两频与 broadband-regression TL：三种模式 SHD SHA-256 一致；
- A/E 三种多频模式的 solver cache fingerprint 前后相同；
- mode switching、indexed stale cleanup、多频 R 拒绝和非零退出通过；
- isolated Release configure/build/CTest 与 `git diff --check` 通过。

## 冻结状态边界

`RayPathCache` 只保存频率无关的 geometry、trajectory、quadrature 与 raw
reflection event。以下内容不得写回 cache：

- amplitude、phase、complex travel time；
- active/terminal prefix 和 reflection coefficient/result；
- pressure 或 intensity workspace；
- `ArrivalWorkspace`、ArrivalCandidate、Eigenray hits 或 writer 状态。

多频 worker 只生成 frequency-local product；writer consumer 按频率索引顺序
串行调用。当前结构避免 nested parallelism，但不规定未来永远由 frequency
层拥有并行。

## 下一步

FP-2D（cubic spline SSP `S`）已完成原 implementation、Batch Acceptance、
FP-2D-R1 remediation 与独立 Re-Final Review，结论为 `ACCEPTED`。修复证据见
`doc/workreports/FP-2D-R1_FINAL_REVIEW_REMEDIATION_REPORT.md`，批次总览见
`doc/workreports/FP-2D_CUBIC_SPLINE_SSP_BATCH_REPORT.md`。除此之外，Feature Sync
没有其他获批的新实施阶段。最新 Influence
审计建议先评估无损的 Influence Geometry Reuse（IG-0），再决定是否进入带
误差预算的频率重建（FI-0）；两者仍是候选路线，不自动启动。统一入口见
[`PLAN_CURRENT_WORK.md`](../../../doc/plans/PLAN_CURRENT_WORK.md)，技术证据见
[`REPORT_INFLUENCE_FREQUENCY_AUDIT_2026-08-25.md`](../reports/REPORT_INFLUENCE_FREQUENCY_AUDIT_2026-08-25.md)。
