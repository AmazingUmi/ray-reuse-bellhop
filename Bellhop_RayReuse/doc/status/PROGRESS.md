# Bellhop RayReuse 当前进度

> 更新日期：2026-08-25
> 当前状态：RayReuse Feature Sync 的 RR-B1～RR-B4 已关闭；暂停等待下一阶段决定。

## 已完成范围

| Batch | 状态 | 结果 |
|---|---|---|
| RR-B1 | 完成，提交 `daf687f` | V/R/A/G/F、`.trc/.brc`、折线边界、`.ati/.bty`、elastic LL、reflection event raw material/segment identity |
| RR-B2 | 完成，提交 `5e6cc03` | generalized R、directional `.sbp`、per-frequency active/terminal prefix、R-only `Nalpha=1`、Origin-compatible RAY writer |
| RR-B3 | 完成，提交 `5e6cc03` | A/a、E、ArrivalWorkspace/AddArr、Cartesian G/B traversal、逐频 product 与三种执行模式 |
| RR-B4 | 完成 | parser/executable/CLI lifecycle、共享 standard-case adapter、三方与多频回归、文档封板 |

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

Feature Sync 已满足关闭条件，当前没有获批的新实施阶段。最新 Influence
审计建议先评估无损的 Influence Geometry Reuse（IG-0），再决定是否进入带
误差预算的频率重建（FI-0）；两者仍是候选路线，不自动启动。统一入口见
[`CURRENT_WORK.md`](../../../doc/plans/CURRENT_WORK.md)，技术证据见
[`INFLUENCE_FREQUENCY_AUDIT.md`](../reports/INFLUENCE_FREQUENCY_AUDIT.md)。
