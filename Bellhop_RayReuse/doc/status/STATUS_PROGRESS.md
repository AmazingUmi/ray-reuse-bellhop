# Bellhop RayReuse 当前进度

> 更新日期：2026-09-06
> Feature Parity accepted production HEAD：`0721fb3036ebaa26bbd72fcb20458e9118317457`
> （`0721fb3`）
> Final acceptance documentation commit：`88ba8b7`
> Production Feature Parity：**COMPLETE**
> Remaining F2CPP parity GAP：**0**
> IGR-2 productionization commit：`e7f27054360c53397e54c25bcbcdbb9bffbf1a9b`
> IGR-3A fused TL adaptation commit：`dda1c2c`
> IGR-3B fused Arrival closure commit：`0050f59`
> IGR-3 unified architecture：**ACCEPTED / CLOSED**
> BB-M1 / BB-1（Bellhop Broadband 命名与两层执行模型）：**ACCEPTED**
> （2026-09-06，独立 final review；construction baseline `a3f20d8`，
> 改动本轮保持未提交）
> 当前状态：`Bellhop_F2CPP → Bellhop_RayReuse` Production Feature Parity 序列
> 全部完成（RR-B1～RR-B4、FP-1A～FP-2I 全部 `ACCEPTED / CLOSED`）。
> 最后一个功能批次 FP-2I（Line Source Closure）已全流程验收通过。
> 全仓库功能支持矩阵与对齐报告已同步封板：
> **F2CPP → RayReuse Production Feature Parity COMPLETE（GAP = 0）**。

Feature Parity 封板基线之后，IGR-1～IGR-3 均已完成 construction、Batch
Acceptance 与独立 final review，状态为 `ACCEPTED / CLOSED`。IGR-3A 将统一
fused executor 扩展到当前合法 TL beam families；IGR-3B 将同一 executor
扩展到规则接收网格上的 `G/g/B × A/a`，采用 source-local
`[range][depth][frequency]` broadband Arrival lanes、静态连续 range
parallelism 与 per-frequency source-streamed writer。TL 与 Arrival fused 路径
均不改变 Feature Parity accepted identity，也不扩大 scientific support
matrix；`R/E` 仍不进入 fused Influence execution。
详见
[`REPORT_IGR2_FUSED_INFLUENCE_PRODUCTIONIZATION_2026-09-03.md`](../reports/REPORT_IGR2_FUSED_INFLUENCE_PRODUCTIONIZATION_2026-09-03.md)。
此前 16F performance artifact 记录的是 `bd4816af` 加 dirty worktree 与 binary
SHA-256，没有记录 exact dirty diff hash；因此仅作为本地 acceptance evidence，
不声明为某个可精确重建 commit 的性能 identity。

当前 production implementation：

```text
Current accepted production (IGR-3 CLOSED, BB-1 accepted product surface):
    one Cross-Frequency Fused + Static Range Parallel Influence executor
      + legal TL beam-family sinks (IGR-3A)
      + G/g/B Arrival sink and [R][D][F] broadband lanes (IGR-3B)
    exposed as Bellhop Broadband / bellhop_broadband with
      --execution-mode <nonreuse|reuse>
      + --reuse-mode <serial|frequency|range>
      + --trace-workers / --reuse-workers (defaults 1; BB-1)
```

IGR-3A 已提交于 `dda1c2c`，IGR-3B 已提交于 `0050f59`；两者均经独立 final
review 验收并关闭。权威 scope/closure 见
[`IGR-3_SCOPE_AND_ARCHITECTURE_DECISION.md`](../worklists/IGR-3_SCOPE_AND_ARCHITECTURE_DECISION.md)
与 [`IGR-3B_ARRIVAL_FUSED_INFLUENCE_WORKLIST.md`](../worklists/IGR-3B_ARRIVAL_FUSED_INFLUENCE_WORKLIST.md)。

## Broadband 命名里程碑（BB-M1）

BB-M1 将产品从 Bellhop RayReuse 演进为 **Bellhop Broadband**：executable
`bellhop_broadband`、CMake project `BellhopBroadband`、package
`bellhop-broadband`（`Bellhop_RayReuse/` 目录名与内部 namespace/include 根
保留）。CLI 重组为两层执行模型（`--execution-mode <nonreuse|reuse>` +
`--reuse-mode <serial|frequency|range>`，默认 nonreuse/serial），并行参数
统一为 `--trace-workers` 与 `--reuse-workers`（默认 1）；旧
`parallel`/`fused` 模式值与 `--range-parallel`/`--workers` 选项删除。solver
顶层命名为 `NonReuseSolver` / `ReuseSerialSolver` / `ReuseFreqParaSolver` /
`ReuseRangeParaSolver`；低层 fused 实现术语保留。科学算法、输出语义与
support matrix 未改变（20 组新旧 CLI 对比产品 byte-identical，PRT 归一化
一致）。

- BB-1（实现 Batch）已于 2026-09-06 经独立 final review **ACCEPTED**；
  任务与 evidence 见 [`BB-1_WORKLIST.md`](../worklists/BB-1_WORKLIST.md)。
- BB-2（文档/术语 Batch）已于 2026-09-06 经独立 final review **ACCEPTED**：
  living docs（13 个 CURRENT 文件）迁移到新命名/CLI/默认值，support matrix
  按代码复核修正（GUIDE_USAGE 的 "A/a 不进 fused、IGR-3B 未开始" 等滞后已
  修正），历史文档零改写，新增 "Terminology after BB-M1" 映射入口；evidence
  见 [`BB-2_WORKLIST.md`](../worklists/BB-2_WORKLIST.md)。
- 里程碑整体 **BB-M1 ACCEPTED / CLOSED**（2026-09-06）；closure report：
  [`REPORT_BB_M1_BROADBAND_NAMING_MILESTONE_2026-09-06.md`](../reports/REPORT_BB_M1_BROADBAND_NAMING_MILESTONE_2026-09-06.md)。
- construction baseline：`a3f20d8c0d8ac207766fa0adb083de3526f74fd7`；
  BB-1 与 BB-2 全部改动本轮均保持未提交（按授权）。

`0721fb3` 是 accepted production parity HEAD；`88ba8b7` 是最终验收文档记录
commit，不替代 Feature Parity production acceptance identity。IGR-2 的后续
productionization 位于上述 `e7f2705` commit；IGR-3 的 accepted implementation
位于 `dda1c2c` 与 `0050f59`。

## 最终整体验收入口

- [`REPORT_FEATURE_PARITY_FINAL.md`](../reports/REPORT_FEATURE_PARITY_FINAL.md)：
  repository-level final acceptance、精简 health verification 与 Performance Snapshot；
- [`FEATURE_PARITY_FINAL_ACCEPTANCE_WORKLIST.md`](../worklists/FEATURE_PARITY_FINAL_ACCEPTANCE_WORKLIST.md)：
  最终整体验收执行记录；
- [`REFERENCE_FEATURE_SUPPORT_MATRIX.md`](../reference/REFERENCE_FEATURE_SUPPORT_MATRIX.md)：
  当前 production feature boundary。

## 已完成范围

| Batch | 状态 | 结果 |
|---|---|---|
| RR-B1 | 完成，提交 `daf687f` | V/R/A/G/F、`.trc/.brc`、折线边界、`.ati/.bty`、elastic LL、reflection event raw material/segment identity |
| RR-B2 | 完成，提交 `5e6cc03` | generalized R、directional `.sbp`、per-frequency active/terminal prefix、R-only `Nalpha=1`、Origin-compatible RAY writer |
| RR-B3 | 完成，提交 `5e6cc03` | A/a、E、ArrivalWorkspace/AddArr、Cartesian G/B traversal、逐频 product 与三种执行模式 |
| RR-B4 | 完成 | parser/executable/CLI lifecycle、共享 standard-case adapter、三方与多频回归、文档封板 |
| FP-2A | 完成；Final Review `ACCEPTED` | `Ag/ag/Eg` parser/runtime、ray-centered Arrival/Eigenray traversal、共享 oracle 与多频三模式 parity |
| FP-2B | 完成；Final Review `ACCEPTED` | PCHIP SSP parity、`GeometrySspEvaluator`/`FrequencySspEvaluator`、共享 `munk_pchip` oracle 与三模式一致性 |
| FP-2C | 完成；Final Review `ACCEPTED` | N²-linear SSP `N` parity：real geometry（node jump + 非零 Hessian）与 frequency-local complex N² evaluator、共享 `munk_n2` 三方 oracle、TL/R/A/a/E 与 `nonreuse/reuse/parallel` 一致性 |
| FP-2D | 完成；Final Review `ACCEPTED` | Cubic spline SSP `S` parity：exact not-a-knot coefficient kernel（保留 legacy binary32 `1.0F/6.0F`）、real spline evaluator（节点连续梯度、无 node jump、非零 Hessian）、frequency-local complex spline evaluator（每频独立 coefficients）、共享 `munk_spline` 三方 oracle、TL/R/A/a/E 与三执行模式一致性（trace passes 2/1/1、cache fingerprint 前后不变） |
| FP-2E | 完成；Final Review `ACCEPTED`（2026-08-29） | Quadrilateral SSP `Q`/`.ssp` 二维 point/single/rectilinear 下 TL Cartesian Cerveny `CC`、R、Cartesian GeoHat `G` A/a/E，TL/A/a/E 的 `nonreuse/reuse/parallel` 一致（trace passes 2/1/1、fingerprint `2879552213476552188` 前后不变） |
| FP-2F | 完成；Final Review `ACCEPTED`（2026-08-29） | Source/receiver generalization：multisource（`NSz ≥ 1` point source；per-source frozen fan cache；SHD/ARR/E/R per-source sequencing/header；trace passes `Nfreq×NSz/NSz/NSz`）与 Cartesian paired irregular receiver（run type 第 5 位 `I`、`NRz == NRr`；Cerveny `CC/IC/SC` 恒取 `Rz(1)` legacy 语义、GeoHat/GeoGaussian Cartesian paired 寻址、`PlotType='irregular '`、Cartesian `G/B` A/a/E paired）；三方 oracle 与六 case broadband 三模式逐字节一致 |
| FP-2G | 完成；Final Review `ACCEPTED`（2026-08-29） | Boundary / material closure：canonical curvilinear short format `C` boundary（BND-04，459 角度 intermediate-state oracle 与 SHD 三方 closure，两频三模式 byte-identical）与 flat ordinary elastic halfspace P/S（BND-09，`elastic_halfspace_flat` 与 `elastic_halfspace_fluid_control` 三方 SHD 比较与 shear guard 全部 PASS，两频三模式 byte-identical 与逐频求值确认）；`CS`/`CL` 显式拒绝 |
| FP-2H | 完成；Final Review `ACCEPTED`（2026-08-29） | Attenuation closure：全面闭环 ATT-01～ATT-05，包括 N/F/M/W/Q/L 衰减单位、W 频率与声速依赖性保护、Thorp 回归保护、Francois–Garrison 参数化体积衰减、Biological 多层重叠体积衰减、五大频域 SSP 节点优先转换、边界材料声学衰减穿透、宽带三模式（nonreuse / reuse / parallel）逐字节一致性及 frozen-cache 不可变性 |
| FP-2I | 完成；Final Review `ACCEPTED`（2026-08-29） | Line source closure：全面闭环 SRC-02 与 PRD-08，包括 ENV RunType 第 4 位 `'X'` 解析、`SourceGeometry::{Point, Line}` 模型所有权、Cartesian/Ray-centered Cerveny/GeoHat/GeoGaussian 线声源 ratio 内核、Simple Gaussian 严格拒绝线声源、PressureScaling 线声源柱面扩散与常数因子缩放、ArrivalWriter 到达幅值缩放、`source_geometry_line` 与 `arrival_line_directional_multisource` 三方 oracle 闭环及宽带三模式逐字节一致性 |

## 最终验收基线

- RayReuse Release CTest：41/41 PASSED；
- 仓库全量 pytest：187/187 PASSED；
- Standard cases 单元测试：172/172 PASSED；
- 到达结构全量三方验证（`validate_i8_arrivals.py`）：9/9 PASSED；
- 衰减单位与体积衰减验证（`validate_i4_attenuation_units.py`、`validate_i4_volume_attenuation.py`）：全部 PASSED；
- 边界曲率与弹性半空间验证（`i3_curvilinear_oracle`、`validate_i4_elastic_halfspace.py`）：全部 PASSED；
- 宽带 `nonreuse` / `reuse` / `parallel`：全量支持案例与 profile 跨模式生成产品逐字节一致（`cmp` 0）；
- `--verify-cache` 在 `reuse` 与 `parallel` 模式下 `before == after` 语义指纹严格守恒；
- Origin / F2CPP production 代码零修改（0 diff）；
- `RayPathCache` schema 与 `contentFingerprint()` 算法零改动。

IGR-3 closure 在 clean Release/Werror build 上通过 50/50 CTest；Arrival
三实现回归 9/9、`validate_i8_arrivals.py` 36 comparisons 全部通过。`G/g/B ×
A/a` 的 legacy reuse、fused w1 与 fused w4 ARR 逐字节一致，cache fingerprint
前后不变。

## 冻结状态边界

`RayPathCache` 只保存频率无关的 geometry、trajectory、quadrature 与 raw
reflection event。以下内容不得写回 cache：

- amplitude、phase、complex travel time；
- active/terminal prefix 和 reflection coefficient/result；
- pressure 或 intensity workspace；
- `ArrivalWorkspace`、ArrivalCandidate、Eigenray hits 或 writer 状态。

legacy frequency workers 只生成 frequency-local product；fused executor 的
static range workers 在一个 source-local `[R][D][F]` workspace 上独占连续
range block。TL consumer 仍按频率索引顺序调用；fused Arrival 由每频 writer
按 source 顺序消费 frequency view。当前结构避免 nested parallelism。

## 结论

**Bellhop_F2CPP → Bellhop_RayReuse Production Feature Parity COMPLETE**。
所有 Feature Parity 批次（FP-1A～FP-2I）均已完成独立 Final Review 并标记为 `ACCEPTED / CLOSED`。
当前仓库生产支持面已完全覆盖 F2CPP production 范围，剩余真实 GAP 数量为 **0**。
IGR-1～IGR-3 也均已独立验收并关闭；BB-1 已完成 Bellhop Broadband 产品与
CLI 重构（见上文 Broadband 命名里程碑）。当前 unified fused Influence
architecture 与 TL/Arrival 支持域以本状态页和支持矩阵为准。
