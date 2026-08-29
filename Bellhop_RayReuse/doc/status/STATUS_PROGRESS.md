# Bellhop RayReuse 当前进度

> 更新日期：2026-08-29
> 当前状态：FP-2F 与 FP-2G 均已完成全流程验收并 CLOSED，结论分别为
> `FP-2F ACCEPTED`（commit `763c585`）与 `FP-2G ACCEPTED`（2026-08-29）。
> 当前批次 FP-2H（Attenuation Closure）已自动启动并进入 DESIGN 阶段。
> 串行批次（FP-2F→FP-2G→FP-2H）进度快照见
> [`STATUS_FEATURE_PARITY_SEQUENCE_2026-08-29.md`](STATUS_FEATURE_PARITY_SEQUENCE_2026-08-29.md)。
> 此前批次 FP-2E 的独立 Re-Final Review 结论为 `FP-2E ACCEPTED`
> （2026-08-29）。

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
| FP-2E | 完成；Re-Final Review `ACCEPTED`（2026-08-29） | Quadrilateral SSP `Q`/`.ssp` 的已验证范围严格限于二维 single point source、single source depth、rectilinear receivers：`.ssp` reader 与二维 grid、real geometry、frequency-local projection，以及 `q_range_dependent_cross_gradient`/`q_range_independent_control` oracle；产品仅声明 TL Cartesian Cerveny `CC`、R、Cartesian GeoHat `G` A/a/E，TL/A/a/E 的 `nonreuse/reuse/parallel` 一致（trace passes 2/1/1、fingerprint `2879552213476552188` 前后不变）。其他 Q beam/option 组合即使机制可达也未独立 oracle 验证，不声明 parity；3D/N×2D/line/multisource/irregular 不属本批次 |
| FP-2F | 完成；Final Review `ACCEPTED`（2026-08-29） | Source/receiver generalization：multisource（`NSz ≥ 1` point source；per-source frozen fan cache；SHD/ARR/E/R per-source sequencing/header；trace passes `Nfreq×NSz/NSz/NSz`）与 Cartesian paired irregular receiver（run type 第 5 位 `I`、`NRz == NRr`；Cerveny `CC/IC/SC` 恒取 `Rz(1)` legacy 语义、GeoHat/GeoGaussian Cartesian paired 寻址、`PlotType='irregular '`、Cartesian `G/B` A/a/E paired）；8 个三方 oracle case 与六 case broadband 三模式逐字节一致；line source 仍 `GAP` |
| FP-2G | 完成；Final Review `ACCEPTED`（2026-08-29） | Boundary / material closure：canonical curvilinear short format `C` boundary（BND-04，459 角度 intermediate-state oracle 与 SHD 三方 closure，两频三模式 byte-identical）与 flat ordinary elastic halfspace P/S（BND-09，`elastic_halfspace_flat` 与 `elastic_halfspace_fluid_control` 三方 SHD 比较与 shear guard 全部 PASS，两频三模式 byte-identical 与逐频求值确认）；`CS`/`CL` 显式拒绝，curvilinear × long format/halfspace/multisource/irregular/Q 不声明 parity |

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

FP-2G（boundary/material closure）已获得独立 Final Review `ACCEPTED` 结论并 CLOSED；执行期权威状态见 `doc/worklists/FP-2G_BOUNDARY_MATERIAL_CLOSURE_WORKLIST.md` 与 Batch Report `doc/workreports/FP-2G_BOUNDARY_MATERIAL_CLOSURE_BATCH_REPORT.md`。按序列授权，当前已自动进入 FP-2H（Attenuation Closure）DESIGN 阶段。
