# Bellhop RayReuse 功能支持矩阵

> 封板日期：2026-08-20；FP-1A～FP-2B 更新：2026-08-27；FP-2C/FP-2D 更新：2026-08-28
> 适用范围：当前二维、point-source、rectilinear-receiver 的 RayReuse 实现。
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
| TL | point/single-source-depth/rectilinear/C-linear、PCHIP、N²-linear 或 cubic-spline SSP 下的 Cartesian Cerveny `CC/IC/SC + {F,M,W}{D,S,Z} + P/V/H`、ray-centered Cerveny `CR/IR/SR + {F,M,W}{D,S,Z} + P/V/H`、Cartesian GeoHat `CG/IG/SG`（含 `^`/blank alias）、ray-centered GeoHat `Cg/Ig/Sg`、Cartesian GeoGaussian `CB/IB/SB` 与 coherent Cartesian Simple Gaussian `CS`；两个 ray-centered family 都要求至少两个等间距 receiver ranges。Cartesian Cerveny 的 P/V/H 是数值相同的 legacy selector；ray-centered Cerveny V/H 按 Origin 的 normal/along derivative 公式计算，并保留 persistent image-normal flip、逐 image I/S power、receiver-level KMAH 与 Hermite-once 语义。ray-centered GeoHat 则沿实际反射轨迹按 `c*slowness` normal 做 depth projection/range crossing，没有 image loop、persistent flip、epsilon、gamma、KMAH 或 Hermite window；`q` 与 complex delay 线性插值，sound speed/amplitude 取右端点、reflection phase 取左端点，q crossing 增加 `π/2`。D/S/Z 在 frequency-independent reflection 时对完整 dynamic-ray `RN` jump 分别倍增、保留、清零；F/M/W epsilon 按每个目标频率、每条 ray 及 F2CPP/Origin evaluation order 计算，W 使用 real epsilon 与 real-q KMAH crossing，F/M 使用 positive-imaginary epsilon 与 complex-q branch crossing；非 Cerveny family 只允许 P 与 standard curvature，且不能接收 Cerveny width/curvature tail；C 使用 complex-pressure workspace，Cerveny/G/B 的 I/S 使用逐频 intensity workspace，`IS/SS` 明确拒绝；GeoHat I/S 按 attenuated real constant 平方后单次乘 linear hat weight，GeoGaussian I/S 按 `sqrt(2π) × power × GaussianWeight` 且权重只乘一次；G/B/S 均使用 geometric point normalization；GeoGaussian width/membership 与 Simple Gaussian contribution 均逐频精确计算；directional `.sbp` 与适用的 S Lloyd factor 在逐 ray Project 前共用 source-amplitude 路径；单频 SHD，以及多频 `nonreuse/reuse/parallel` SHD |
| R | 单频 `R/RG/RGO`、directional `.sbp`、显式 `Nalpha=1`、逐频 active/terminal prefix、Origin-compatible `.ray` |
| Arrivals | ASCII `A`、binary `a`；Cartesian geometric hat/Gaussian `G/B` 与 ray-centered GeoHat `g`；frequency-local `ArrivalWorkspace` 与 AddArr 语义 |
| Eigenray | `E` 的 Cartesian `G/B` 与 ray-centered GeoHat `g` traversal；receiver hit 对应的冻结 ray prefix 与 `.ray` |
| SSP | 当前 production-supported SSP 为 C-linear、PCHIP `P`、N²-linear `N` 与 cubic spline `S`。PCHIP 计算 F2CPP/Origin 兼容的 Hermite 系数、单调性限制器、连续一阶导数与二阶导数 `d²c/dz²`，并精确进入 dynamic ray 与逐频投影。N²-linear 按逐段线性 N² 插值（`c = 1/sqrt(N²)`）进入 real geometry：节点梯度不连续，沿用与 C-linear 相同的 reduced-step node jump；段内 N² 曲率产生非零二阶导数（nonzero Hessian）并进入 dynamic ray；逐频投影使用 frequency-local complex N² evaluator（节点声速先按目标频率转为复数再形成复 N² 系数），幅相、复走时与反射结果保持逐频临时状态，不写回 frozen `RayPathCache`。N²-linear 覆盖当前 TL/R/A/a/E 合法产品范围，多频 `nonreuse/reuse/parallel` 结果一致。Cubic spline `S` 使用逐行迁移 F2CPP production 的 exact not-a-knot 系数构造（含 2/3/4+ node 分支与 legacy binary32 `1.0F/6.0F` 舍入）：real geometry 提供 value/一阶/二阶导数，spline 梯度在节点连续（`ContinuousAtNodes`，不执行 C/N² 的 node jump），非零 Hessian 进入 dynamic ray，profile 外使用首/末段 cubic polynomial extrapolation；逐频投影先按目标频率转换节点 attenuation、再每频独立构造复系数并复用同一 kernel，spline interior imaginary 只做 finite 校验，幅相、复走时与反射结果保持逐频临时状态，不写回 frozen `RayPathCache`。Cubic spline 覆盖当前 TL/R/A/a/E 合法产品范围，多频 `nonreuse/reuse/parallel` SHD 逐字节一致，`nonreuse/reuse/parallel` trace passes 为 2/1/1，reuse/parallel frozen cache fingerprint 前后不变。`Q`/`.ssp` 不支持 |
| 边界 | top/bottom `V/R/A/G/F`、`.trc/.brc`、piecewise-linear `.ati/.bty`、acoustic/elastic `LL` |
| 输出生命周期 | PRT、SHD、RAY、ARR；原子单文件发布、模式切换清理、失败后不留下部分正式产品或 `.tmp` |
| 状态所有权 | geometry/trajectory 与 raw reflection event 位于 frozen `RayPathCache`；幅相、复走时、active prefix、反射结果、Arrival/Eigenray 产品均为逐频临时状态 |
| 并行 | worker 数运行时配置；当前 A/a/E 与 TL 支持外层 frequency parallelism，consumer 按频率索引串行发布，不共享可变 writer |

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

- Quadrilateral (`Q` / `.ssp`) SSP 插值；
- irregular receiver；
- line source；
- multisource parity；
- 3D / N×2D；
- beam shift；
- ray-centered geometric Gaussian；
- 本矩阵未列出的 beam/coordinate family；
- 新 BARR 算法、SIMD、新性能优化；
- F2CPP/RayReuse shared-library 重构。

延期项不是 silent fallback 的许可。未来若进入 scope，必须作为新的独立阶段
重新定义输入、逐频状态边界和验收证据。
