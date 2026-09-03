# Bellhop RayReuse 功能支持矩阵

> 封板日期：2026-08-20；FP-1A～FP-2B 更新：2026-08-27；FP-2C/FP-2D/FP-2E 更新：2026-08-28；FP-2F/FP-2G/FP-2H/FP-2I 最终封板：2026-08-29；最终分类措辞统一：2026-08-30
> 适用范围：当前二维、点声源与线声源（含多 source depths）、规则网格与 Cartesian 配对不规则接收网格的 RayReuse 生产实现。
> 输入和命令以 [`GUIDE_USAGE.md`](../guides/GUIDE_USAGE.md) 为准。
> Feature Parity accepted production HEAD：`0721fb3`；final acceptance
> documentation commit：`88ba8b7`；IGR-2 productionization commit：`e7f2705`。
> Production Feature Parity：**COMPLETE**；Remaining F2CPP parity GAP：**0**。

> **Current vs future:** 当前已验收的 IGR-2 fused production implementation
> 仅覆盖其 Cartesian Cerveny TL 支持域，并可选用静态 receiver-range
> parallelism。IGR-3 已冻结将同一 execution architecture 扩展到其他 TL
> beam kernels 与 Arrival sink 的 future direction，但 construction 尚未开始；
> 本矩阵不提前声明这些扩展已经可用。

## 状态定义

- **Fully supported / PARITY**：F2CPP production-supported，且 RayReuse 已有
  parser → model → runtime → product → regression/oracle 的完整证据链。
- **F2CPP_OUT_OF_SCOPE**：F2CPP 本身没有正式 production support；不计入
  RayReuse 的 F2CPP parity GAP。
- **RAYREUSE_EXTENSION / DEFERRED**：RayReuse-only 能力或未来研究候选；已支持
  extension 与未批准 deferred 项必须分别明确标注，二者都不构成 F2CPP parity GAP。
- **Intentional divergence**：为安全或多频生命周期有意采用不同外部行为，
  不改变声明范围内的产品数值语义。

## Fully supported / PARITY

> **组合范围说明：** 下表按功能轴汇总支持面，不表示所有列出的声源、接收网格、
> SSP、beam family 与产品都做了 Cartesian-product 组合验收。尤其
> quadrilateral `Q` / `.ssp` 的 accepted FP-2E slice 仅为二维、single point
> source、single source depth、rectilinear receivers；TL Cartesian Cerveny
> `CC`、单频 R，以及 Cartesian GeoHat `G` 的 A/a/E。`Q` 与 line source、
> multisource、irregular receiver、ray-centered family 或其他 beam/product
> 的机制可达组合未建立独立 oracle，本矩阵不声明这些组合为已验证 parity。

| 能力 | 当前范围 |
|---|---|
| TL | point 与 line source、single/multisource source depth、rectilinear 与 Cartesian paired-irregular 接收网格、C-linear、PCHIP、N²-linear、cubic-spline 或 quadrilateral SSP 下的 Cartesian Cerveny `CC/IC/SC + {F,M,W}{D,S,Z} + P/V/H`、ray-centered Cerveny `CR/IR/SR + {F,M,W}{D,S,Z} + P/V/H`、Cartesian GeoHat `CG/IG/SG`（含 `^`/blank alias）、ray-centered GeoHat `Cg/Ig/Sg`、Cartesian GeoGaussian `CB/IB/SB` 与 coherent Cartesian Simple Gaussian `CS`；两个 ray-centered family 都要求至少两个等间距 receiver ranges。Cartesian Cerveny 的 P/V/H 是数值相同的 legacy selector；ray-centered Cerveny V/H 按 Origin 的 normal/along derivative 公式计算，并保留 persistent image-normal flip、逐 image I/S power、receiver-level KMAH 与 Hermite-once 语义。ray-centered GeoHat 则沿实际反射轨迹按 `c*slowness` normal 做 depth projection/range crossing，没有 image loop、persistent flip、epsilon、gamma、KMAH 或 Hermite window；`q` 与 complex delay 线性插值，sound speed/amplitude 取右端点、reflection phase 取左端点，q crossing 增加 `π/2`。D/S/Z 在 frequency-independent reflection 时对完整 dynamic-ray `RN` jump 分别倍增、保留、清零；F/M/W epsilon 按每个目标频率、每条 ray 及 F2CPP/Origin evaluation order 计算，W 使用 real epsilon 与 real-q KMAH crossing，F/M 使用 positive-imaginary epsilon 与 complex-q branch crossing；非 Cerveny family 只允许 P 与 standard curvature，且不能接收 Cerveny width/curvature tail；C 使用 complex-pressure workspace，Cerveny/G/B 的 I/S 使用逐频 intensity workspace，`IS/SS` 明确拒绝；GeoHat I/S 按 attenuated real constant 平方后单次乘 linear hat weight，GeoGaussian I/S 按 `sqrt(2π) × power × GaussianWeight` 且权重只乘一次；G/B/S 均使用 geometric point normalization；GeoGaussian width/membership 与 Simple Gaussian contribution 均逐频精确计算；directional `.sbp` 与适用的 S Lloyd factor 在逐 ray Project 前共用 source-amplitude 路径；线声源（RunType 4th `'X'`）使用 ratio=1.0 及 `-4.0*sqrt(pi)*beamScale` 全距离柱面扩散因子；单频 SHD，以及多频 `nonreuse/reuse/parallel` SHD |
| R | 单频 `R/RG/RGO`、directional `.sbp`、显式 `Nalpha=1`、逐频 active/terminal prefix、Origin-compatible `.ray`；多 source depths 时 per-source ray blocks 与 header `1 1 NSz`（source depth 升序），仍为单频产品（多频 R 明确拒绝；run type `R...I` irregular 拒绝） |
| Arrivals | ASCII `A`、binary `a`；Cartesian geometric hat/Gaussian `G/B` 与 ray-centered GeoHat `g`；frequency-local `ArrivalWorkspace` 与 AddArr 语义；点声源（`1/sqrt(range)`）与线声源（`4.0*sqrt(pi)`）幅值缩放；多 source depths 时 per-source 块与 header source depths（ASCII 行/binary count record，source depth 升序）；Cartesian `G/B` 支持 paired irregular receivers（ray-centered `g` 保持 single-source/rectilinear 范围） |
| Eigenray | `E` 的 Cartesian `G/B` 与 ray-centered GeoHat `g` traversal；receiver hit 对应的冻结 ray prefix 与 `.ray`；多 source depths 时 per-source 段落与 header `1 1 NSz`（source 升序）；Cartesian `G/B` 支持 paired irregular receivers（ray-centered `g` 保持 single-source/rectilinear 范围） |
| 声源几何（Source Geometry） | 支持点声源（Point Source，RunType 4th 为 blank 或 `'R'`）与线声源（Line Source，RunType 4th 为 `'X'`）。线声源在 Cartesian/Ray-centered Cerveny、Geometric Hat 与 Geometric Gaussian 中使用 line ratio，在 PressureScaling 中应用 `-4.0*sqrt(pi)*beamScale` 扩散因子（包含 0 距离），在 ArrivalWriter 中应用 `4.0*sqrt(pi)` 到达幅值因子；Simple Gaussian 严格要求点声源并拒绝线声源；`source_geometry_line` 与 `arrival_line_directional_multisource` 标准算例通过三方 oracle 验证 |
| 多源（multisource / NSz ≥ 1） | point/line source、source depth 数 `≥1`（输入乱序时按 depth 升序 `stable_sort`）；同一 launch fan 对每个 source depth 独立 trace，每 source 一个独立 frozen `RayPathCache`（cache schema 与 `contentFingerprint()` 算法不变，`NSz==1` 逐字节复现既有行为与 fingerprint 值）；TL（各已支持 beam family）、R（保持单频）、A/a、E 的 per-source 产品 sequencing/header 与 F2CPP/Origin 一致；`nonreuse/reuse/parallel` 三模式逐频产品一致，trace passes 冻结语义为 `Nfreq×NSz / NSz / NSz`，reuse/parallel per-source cache fingerprint 前后一致 |
| Cartesian irregular receiver | run type 第 5 位 `I`，paired 语义 = `NRz == NRr`、`receiversPerRange() == 1`、`depthAt(depthIndex, rangeIndex) == depths[rangeIndex]`。TL 适用面：Cartesian Cerveny `CC/IC/SC`（注意：irregular 下按 Origin/F2CPP legacy 语义恒取首深度 `Rz(1)`，而非 paired `Rz(ir)`——RayReuse 与 reference 逐字节同构）、Cartesian GeoHat `CG/IG/SG` 与 Cartesian GeoGaussian `CB/IB/SB` 按 paired 寻址；产品适用面：Cartesian `G/B` 的 A/a/E paired traversal/writer；SHD `PlotType` 写 `irregular `。ray-centered family 与 Simple Gaussian `CS` 拒绝 irregular |
| SSP | 当前 production-supported SSP 为 C-linear、PCHIP `P`、N²-linear `N`、cubic spline `S` 与 quadrilateral `Q`。PCHIP 计算 F2CPP/Origin 兼容的 Hermite 系数、单调性限制器、连续一阶导数与二阶导数 `d²c/dz²`，并精确进入 dynamic ray 与逐频投影。N²-linear 按逐段线性 N² 插值（`c = 1/sqrt(N²)`）进入 real geometry：节点梯度不连续，沿用与 C-linear 相同的 reduced-step node jump；段内 N² 曲率产生非零二阶导数（nonzero Hessian）并进入 dynamic ray；逐频投影使用 frequency-local complex N² evaluator（节点声速先按目标频率转为复数再形成复 N² 系数），幅相、复走时与反射结果保持逐频临时状态，不写回 frozen `RayPathCache`。N²-linear 覆盖当前 TL/R/A/a/E 合法产品范围，多频 `nonreuse/reuse/parallel` 结果一致。Cubic spline `S` 使用逐行迁移 F2CPP production 的 exact not-a-knot 系数构造（含 2/3/4+ node 分支与 legacy binary32 `1.0F/6.0F` 舍入）：real geometry 提供 value/一阶/二阶导数，spline 梯度在节点连续（`ContinuousAtNodes`，不执行 C/N² 的 node jump），非零 Hessian 进入 dynamic ray，profile 外使用首/末段 cubic polynomial extrapolation；逐频投影先按目标频率转换节点 attenuation、再每频独立构造复系数并复用同一 kernel，spline interior imaginary 只做 finite 校验，幅相、复走时与反射结果保持逐频临时状态，不写回 frozen `RayPathCache`。Cubic spline 覆盖当前 TL/R/A/a/E 合法产品范围，多频 `nonreuse/reuse/parallel` SHD 逐字节一致，`nonreuse/reuse/parallel` trace passes 为 2/1/1，reuse/parallel frozen cache fingerprint 前后不变。Quadrilateral `Q` 通过同根 `.ssp` 文件提供二维 range-dependent 声速矩阵（range count ≥2、ranges 严格递增、维度匹配与有限值/正实声速校验，km→m 转换；`.ssp` 缺失或维度不匹配显式失败，禁止 fallback）：real geometry 按 F2CPP/Origin `Quad` 语义在 cell 内 bilinear 插值并提供 `cr/cz/crz`（`crr=czz=0`）与 density depth 插值，深度与距离梯度在 cell 边界不连续；tracer 对越界 trial step 分别把步长缩减到 depth/range grid line（depth 优先的 corner crossing 只执行单次 jump），保留 F2CPP `minimumStep = 1e-3 × nominalStepLength` 下限；range 越出 `.ssp` 网格显式失败，不做外推或 clamp。逐频投影中 real `c(r,z)` 只在 geometry trace 阶段决定冻结轨迹，imaginary attenuation 由 `.env` reference depth profile 逐节点按目标频率转换后仅沿 depth 插值，不从 `.ssp` 生成二维衰减；transient `rangeSegmentIndex` 只存在于 sample/step/limit 与 tracer 局部状态，不进入 `RayPath`/`RayPathCache` |
| 衰减（Attenuation） | PARITY 范围覆盖 ATT-01～ATT-05：基础衰减单位 `N`（Nepers/m）、`F`（dB/m-kHz，以当前频率折算）、`M`（dB/m）、`W`（dB/wavelength，以当前频率与当前换算实声速折算）、`Q`（品质因数）及 `L`（损耗参数）；体积衰减模型支持 None、Thorp `T`、Francois–Garrison `F`（含 20°C 粘滞项分支与 FMA 算式，以参数 mean depth 换算）及 Biological `B`（0–200 层，支持重叠与端点闭区间，以评估深度换算，逐层折算后累加）；产品级标准算例 oracle 覆盖 C-linear 直达成场（N/F/M/W/Q/L 单位与 T/FG/Biological 体积衰减）；五大频域 SSP 后端（C/N/P/S/Q，Q 衰减使用参考节点实声速）及边界声学（普通声学半空间、弹性半空间纵横波衰减与 `1.0e20` legacy 深度；粒度边界隔离体积衰减）均接入衰减并通过完整组件测试闭环证明；环境级衰减参数由 `Environment` 不可变拥有，不污染冻结 `RayPathCache`，所有宽带 profile 在 `nonreuse/reuse/parallel` 下输出逐字节一致且 `--verify-cache` before==after 严格守恒 |
| 边界 | top/bottom `V/R/A/G/F`、`.trc/.brc`、piecewise-linear `.ati/.bty`（`LS`/`LL`）、canonical curvilinear short format `C` `.ati/.bty`（仅限 V/R 材料 short format，`CS`/`CL` 显式拒绝）、flat ordinary elastic halfspace P/S、acoustic/elastic `LL` |
| 输出生命周期 | PRT、SHD、RAY、ARR；原子单文件发布、模式切换清理、失败后不留下部分正式产品或 `.tmp` |
| 状态所有权 | geometry/trajectory 与 raw reflection event 位于 frozen `RayPathCache`（multisource 下每 source 一个独立 frozen fan cache，source depth 为频率无关属性）；幅相、复走时、active prefix、反射结果、Arrival/Eigenray 产品均为逐频临时状态 |
| 并行 | worker 数运行时配置；legacy `parallel` 下 A/a/E 与 TL 支持外层 frequency parallelism，frequency worker 只读共享 per-source cache vector，consumer 按频率索引串行发布 per-source workspace 序列，不共享可变 writer；当前 IGR-2 fused Cartesian Cerveny TL 另支持显式 static contiguous range parallelism，各 worker 独占 range block，默认请求 4 workers |

## Supported RAYREUSE_EXTENSION

多频 A/a/E 不跨频 merge。每频独立文件使用：

```text
<root>_f000_<frequency-token>Hz.arr
<root>_f001_<frequency-token>Hz.ray
```

`frequency-token` 使用 12 位有效数字，并把小数点替换为 `p`。frequency index、
header frequency 和文件名一一对应；`nonreuse/reuse/parallel` 的每频产品必须
逐字节一致。Origin 没有明确的多频 R 产品，因此多频 R 明确拒绝。

## Approved IGR-3 future direction（not current support）

IGR-3 将 Cross-Frequency Fused + Static Range Parallelism 作为统一 Influence
execution architecture。IGR-3A 适配 remaining TL beam families；IGR-3B 在
IGR-3A 独立验收并提交后，适配 `A/a` 的 geometric contribution paths 与
broadband Arrival layout。`A/a` 当前通过 Geometric Hat/Gaussian
influence-style receiver traversal 产生 `ArrivalCandidate`；与 TL 的主要差异是
contribution sink 和 output data structure，而不是与 Influence architecture
无关。

`R` ray product 不进入该 fused Influence accumulation path；`E` 也不进入
IGR-3 construction，但因可能共享 geometric helper/traversal 而属于 regression
boundary。该 future decision 不改变当前 `R/E` 产品行为。权威 handoff 见
[`IGR-3_SCOPE_AND_ARCHITECTURE_DECISION.md`](../worklists/IGR-3_SCOPE_AND_ARCHITECTURE_DECISION.md)。

## Intentional divergence

| 项目 | RayReuse 行为 |
|---|---|
| 多频产品 | SHD 保持一个多频文件；A/a/E 使用逐频独立文件，不伪造 Origin 未定义的跨频容器 |
| 发布安全 | writer 先写 `.tmp` 再发布；一次多频运行任一 solve/publish 失败会清理本次产品 |
| 解析失败 | 在新产品生命周期开始前失败时保留旧有效产品，避免破坏最后一次成功结果；PRT 写入非零失败诊断 |
| 输入校验 | 对未知 run type、beam family、sidecar、非有限值和未支持组合提前报错 |
| 并行所有权 | legacy `parallel` 选择 frequency worker；当前 fused CC TL 可选 static range worker。两者都不把 frequency/source/receiver 任一层冻结为永久 owner，也不设置 4T/8T 程序级上限 |

## Non-parity boundaries

### F2CPP_OUT_OF_SCOPE（超出 F2CPP 生产支持范围）

- 3D / Bellhop3D / N×2D；
- Beam shift；
- Ray-centered geometric Gaussian；
- 解析式连续 SSP 公式（非离散网格）。

### RAYREUSE_EXTENSION / DEFERRED（RayReuse 远期扩展候选）

- HDF5 容器格式；
- 多频 R 合并容器；
- SIMD 向量化加速；
- frequency interpolation；
- BARR 到达结构算法。

这些候选不是 active task，也不是 silent fallback 的许可。未来若进入 scope，
必须作为新的独立工作重新定义输入、逐频状态边界和验收证据。
