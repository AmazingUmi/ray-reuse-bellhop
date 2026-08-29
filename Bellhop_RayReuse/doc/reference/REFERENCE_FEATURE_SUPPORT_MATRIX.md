# Bellhop RayReuse 功能支持矩阵

> 封板日期：2026-08-20；FP-1A～FP-2B 更新：2026-08-27；FP-2C/FP-2D/FP-2E 更新：2026-08-28；FP-2F 更新：2026-08-29
> 适用范围：当前二维、point-source（含多 source depths）、rectilinear 与 Cartesian paired-irregular receiver 的 RayReuse 实现。
> 输入和命令以 [`GUIDE_USAGE.md`](../guides/GUIDE_USAGE.md) 为准。

## 状态定义

- **Fully supported**：正式 executable 可端到端使用，并有组件测试和代表性
  Origin/F2CPP 对照。
- **RayReuse broadband extension**：超出 Origin 单文件产品语义，但保持每频
  独立、稳定命名和跨执行模式一致。
- **Intentional divergence**：为安全或多频生命周期有意采用不同外部行为，
  不改变声明范围内的产品数值语义。
- **Deferred**：本轮未实现；parser 或 CLI 必须明确拒绝，不能退化到近似路径。

## Fully supported

| 能力 | 当前范围 |
|---|---|
| TL | point/single-source-depth/rectilinear/C-linear、PCHIP、N²-linear 或 cubic-spline SSP 下的 Cartesian Cerveny `CC/IC/SC + {F,M,W}{D,S,Z} + P/V/H`、ray-centered Cerveny `CR/IR/SR + {F,M,W}{D,S,Z} + P/V/H`、Cartesian GeoHat `CG/IG/SG`（含 `^`/blank alias）、ray-centered GeoHat `Cg/Ig/Sg`、Cartesian GeoGaussian `CB/IB/SB` 与 coherent Cartesian Simple Gaussian `CS`；两个 ray-centered family 都要求至少两个等间距 receiver ranges。Cartesian Cerveny 的 P/V/H 是数值相同的 legacy selector；ray-centered Cerveny V/H 按 Origin 的 normal/along derivative 公式计算，并保留 persistent image-normal flip、逐 image I/S power、receiver-level KMAH 与 Hermite-once 语义。ray-centered GeoHat 则沿实际反射轨迹按 `c*slowness` normal 做 depth projection/range crossing，没有 image loop、persistent flip、epsilon、gamma、KMAH 或 Hermite window；`q` 与 complex delay 线性插值，sound speed/amplitude 取右端点、reflection phase 取左端点，q crossing 增加 `π/2`。D/S/Z 在 frequency-independent reflection 时对完整 dynamic-ray `RN` jump 分别倍增、保留、清零；F/M/W epsilon 按每个目标频率、每条 ray 及 F2CPP/Origin evaluation order 计算，W 使用 real epsilon 与 real-q KMAH crossing，F/M 使用 positive-imaginary epsilon 与 complex-q branch crossing；非 Cerveny family 只允许 P 与 standard curvature，且不能接收 Cerveny width/curvature tail；C 使用 complex-pressure workspace，Cerveny/G/B 的 I/S 使用逐频 intensity workspace，`IS/SS` 明确拒绝；GeoHat I/S 按 attenuated real constant 平方后单次乘 linear hat weight，GeoGaussian I/S 按 `sqrt(2π) × power × GaussianWeight` 且权重只乘一次；G/B/S 均使用 geometric point normalization；GeoGaussian width/membership 与 Simple Gaussian contribution 均逐频精确计算；directional `.sbp` 与适用的 S Lloyd factor 在逐 ray Project 前共用 source-amplitude 路径；单频 SHD，以及多频 `nonreuse/reuse/parallel` SHD。`Q`/`.ssp` 的 Fully supported TL 范围单独限定为二维 single point source、single source depth、rectilinear receivers 下的 Cartesian Cerveny `CC`；其他 Q+TL beam/option 组合即使机制上可 dispatch，也未建立独立 oracle，不声明 Fully supported 或 parity |
| R | 单频 `R/RG/RGO`、directional `.sbp`、显式 `Nalpha=1`、逐频 active/terminal prefix、Origin-compatible `.ray`；多 source depths 时 per-source ray blocks 与 header `1 1 NSz`（source depth 升序），仍为单频产品（多频 R 明确拒绝；run type `R...I` irregular 拒绝） |
| Arrivals | ASCII `A`、binary `a`；Cartesian geometric hat/Gaussian `G/B` 与 ray-centered GeoHat `g`；frequency-local `ArrivalWorkspace` 与 AddArr 语义；多 source depths 时 per-source 块与 header source depths（ASCII 行/binary count record，source depth 升序）；Cartesian `G/B` 支持 paired irregular receivers（ray-centered `g` 保持 single-source/rectilinear 范围） |
| Eigenray | `E` 的 Cartesian `G/B` 与 ray-centered GeoHat `g` traversal；receiver hit 对应的冻结 ray prefix 与 `.ray`；多 source depths 时 per-source 段落与 header `1 1 NSz`（source 升序）；Cartesian `G/B` 支持 paired irregular receivers（ray-centered `g` 保持 single-source/rectilinear 范围） |
| 多源（multisource / NSz ≥ 1） | point source、source depth 数 `≥1`（输入乱序时按 depth 升序 `stable_sort`）；同一 launch fan 对每个 source depth 独立 trace，每 source 一个独立 frozen `RayPathCache`（cache schema 与 `contentFingerprint()` 算法不变，`NSz==1` 逐字节复现既有行为与 fingerprint 值）；TL（各已支持 beam family）、R（保持单频）、A/a、E 的 per-source 产品 sequencing/header 与 F2CPP/Origin 一致；`nonreuse/reuse/parallel` 三模式逐频产品一致，trace passes 冻结语义为 `Nfreq×NSz / NSz / NSz`，reuse/parallel per-source cache fingerprint 前后一致。multisource × `Q`、multisource × ray-centered family 等机制可达但未独立 oracle 验证的组合不声明 Fully supported 或 parity |
| Cartesian irregular receiver | run type 第 5 位 `I`，paired 语义 = `NRz == NRr`、`receiversPerRange() == 1`、`depthAt(depthIndex, rangeIndex) == depths[rangeIndex]`。TL 适用面：Cartesian Cerveny `CC/IC/SC`（注意：irregular 下按 Origin/F2CPP legacy 语义恒取首深度 `Rz(1)`，而非 paired `Rz(ir)`——RayReuse 与 reference 逐字节同构）、Cartesian GeoHat `CG/IG/SG` 与 Cartesian GeoGaussian `CB/IB/SB` 按 paired 寻址；产品适用面：Cartesian `G/B` 的 A/a/E paired traversal/writer；SHD `PlotType` 写 `irregular `。ray-centered family 与 Simple Gaussian `CS` 拒绝 irregular；irregular × `Q` 未独立 oracle 验证，不声明 parity |
| SSP | 当前 production-supported SSP 为 C-linear、PCHIP `P`、N²-linear `N`、cubic spline `S` 与 quadrilateral `Q`。PCHIP 计算 F2CPP/Origin 兼容的 Hermite 系数、单调性限制器、连续一阶导数与二阶导数 `d²c/dz²`，并精确进入 dynamic ray 与逐频投影。N²-linear 按逐段线性 N² 插值（`c = 1/sqrt(N²)`）进入 real geometry：节点梯度不连续，沿用与 C-linear 相同的 reduced-step node jump；段内 N² 曲率产生非零二阶导数（nonzero Hessian）并进入 dynamic ray；逐频投影使用 frequency-local complex N² evaluator（节点声速先按目标频率转为复数再形成复 N² 系数），幅相、复走时与反射结果保持逐频临时状态，不写回 frozen `RayPathCache`。N²-linear 覆盖当前 TL/R/A/a/E 合法产品范围，多频 `nonreuse/reuse/parallel` 结果一致。Cubic spline `S` 使用逐行迁移 F2CPP production 的 exact not-a-knot 系数构造（含 2/3/4+ node 分支与 legacy binary32 `1.0F/6.0F` 舍入）：real geometry 提供 value/一阶/二阶导数，spline 梯度在节点连续（`ContinuousAtNodes`，不执行 C/N² 的 node jump），非零 Hessian 进入 dynamic ray，profile 外使用首/末段 cubic polynomial extrapolation；逐频投影先按目标频率转换节点 attenuation、再每频独立构造复系数并复用同一 kernel，spline interior imaginary 只做 finite 校验，幅相、复走时与反射结果保持逐频临时状态，不写回 frozen `RayPathCache`。Cubic spline 覆盖当前 TL/R/A/a/E 合法产品范围，多频 `nonreuse/reuse/parallel` SHD 逐字节一致，`nonreuse/reuse/parallel` trace passes 为 2/1/1，reuse/parallel frozen cache fingerprint 前后不变。Quadrilateral `Q` 通过同根 `.ssp` 文件提供二维 range-dependent 声速矩阵（range count ≥2、ranges 严格递增、维度匹配与有限值/正实声速校验，km→m 转换；`.ssp` 缺失或维度不匹配显式失败，禁止 fallback）：real geometry 按 F2CPP/Origin `Quad` 语义在 cell 内 bilinear 插值并提供 `cr/cz/crz`（`crr=czz=0`）与 density depth 插值，深度与距离梯度在 cell 边界不连续；tracer 对越界 trial step 分别把步长缩减到 depth/range grid line（depth 优先的 corner crossing 只执行单次 jump），保留 F2CPP `minimumStep = 1e-3 × nominalStepLength` 下限；range 越出 `.ssp` 网格显式失败，不做外推或 clamp。逐频投影中 real `c(r,z)` 只在 geometry trace 阶段决定冻结轨迹，imaginary attenuation 由 `.env` reference depth profile 逐节点按目标频率转换后仅沿 depth 插值，不从 `.ssp` 生成二维衰减；transient `rangeSegmentIndex` 只存在于 sample/step/limit 与 tracer 局部状态，不进入 `RayPath`/`RayPathCache`。Quadrilateral 的产品 oracle 仅覆盖 TL（Cartesian Cerveny `CC`）、R 与 Cartesian GeoHat `G` A/a/E（二维 single point source、single source depth、rectilinear receivers；既有 single/broadband Q profiles）；TL/A/a/E 的 `nonreuse/reuse/parallel` 验证结果一致，trace passes 为 2/1/1，reuse/parallel frozen cache fingerprint 前后不变。其他 Q beam family/option 组合（包括完整 C/I/S × F/M/W × D/S/Z × P/V/H、ray-centered Cerveny/GeoHat、GeoGaussian、Simple Gaussian 与 ray-centered `g` 产品）即使机制上可达，也未独立 oracle 验证，不声明 parity。line source、3D 与 N×2D 不属本支持范围；multisource 与 Cartesian irregular receiver 虽已按各自条目在非 `Q` SSP 下支持，`Q` 与 multisource/irregular receiver 的组合未独立 oracle 验证，不声明 parity |
| 边界 | top/bottom `V/R/A/G/F`、`.trc/.brc`、piecewise-linear `.ati/.bty`、acoustic/elastic `LL` |
| 输出生命周期 | PRT、SHD、RAY、ARR；原子单文件发布、模式切换清理、失败后不留下部分正式产品或 `.tmp` |
| 状态所有权 | geometry/trajectory 与 raw reflection event 位于 frozen `RayPathCache`（multisource 下每 source 一个独立 frozen fan cache，source depth 为频率无关属性）；幅相、复走时、active prefix、反射结果、Arrival/Eigenray 产品均为逐频临时状态 |
| 并行 | worker 数运行时配置；当前 A/a/E 与 TL 支持外层 frequency parallelism，frequency worker 只读共享 per-source cache vector，consumer 按频率索引串行发布 per-source workspace 序列，不共享可变 writer |

## RayReuse broadband extension

多频 A/a/E 不跨频 merge。每频独立文件使用：

```text
<root>_f000_<frequency-token>Hz.arr
<root>_f001_<frequency-token>Hz.ray
```

`frequency-token` 使用 12 位有效数字，并把小数点替换为 `p`。frequency index、
header frequency 和文件名一一对应；`nonreuse/reuse/parallel` 的每频产品必须
逐字节一致。Origin 没有明确的多频 R 产品，因此多频 R 明确拒绝。

## Intentional divergence

| 项目 | RayReuse 行为 |
|---|---|
| 多频产品 | SHD 保持一个多频文件；A/a/E 使用逐频独立文件，不伪造 Origin 未定义的跨频容器 |
| 发布安全 | writer 先写 `.tmp` 再发布；一次多频运行任一 solve/publish 失败会清理本次产品 |
| 解析失败 | 在新产品生命周期开始前失败时保留旧有效产品，避免破坏最后一次成功结果；PRT 写入非零失败诊断 |
| 输入校验 | 对未知 run type、beam family、sidecar、非有限值和未支持组合提前报错 |
| 并行所有权 | 当前实现选择 frequency worker，但不把 frequency/source/receiver 任一层冻结为永久 owner，也不设置 4T/8T 程序级上限 |

## Deferred

- line source；
- 3D / N×2D；
- beam shift；
- ray-centered geometric Gaussian；
- 本矩阵未列出的 beam/coordinate family；
- 新 BARR 算法、SIMD、新性能优化；
- F2CPP/RayReuse shared-library 重构。

延期项不是 silent fallback 的许可。未来若进入 scope，必须作为新的独立阶段
重新定义输入、逐频状态边界和验收证据。
