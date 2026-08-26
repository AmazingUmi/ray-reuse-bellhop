# Bellhop RayReuse 功能支持矩阵

> 封板日期：2026-08-20；FP-1A～FP-1G 更新：2026-08-26
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
| TL | point/single-source-depth/rectilinear/C-linear SSP 下的 Cartesian Cerveny `CC/IC/SC + {F,M,W}{D,S,Z} + P/V/H`、Cartesian GeoHat `CG/IG/SG`（含 `^`/blank alias）、Cartesian GeoGaussian `CB/IB/SB` 与 coherent Cartesian Simple Gaussian `CS`；Cartesian Cerveny 的 P/V/H 按 Origin/F2CPP legacy contract 被解析、保存并写入 PRT，但 Cartesian Influence 不施加 component derivative，三者数值相同；D/S/Z 在 frequency-independent reflection 时对完整 dynamic-ray `RN` jump 分别倍增、保留、清零；F/M/W epsilon 按每个目标频率、每条 ray 及 F2CPP/Origin evaluation order 计算，W 使用 real epsilon 与 real-q KMAH crossing，F/M 使用 positive-imaginary epsilon 与 complex-q branch crossing；非 Cerveny family 只允许 P 与 standard curvature，且不能接收 Cerveny width/curvature tail；C 使用 complex-pressure workspace，前三个 family 的 I/S 使用逐频 intensity workspace，`IS/SS` 明确拒绝；GeoHat I/S 按 attenuated real constant 平方后单次乘 linear hat weight，GeoGaussian I/S 按 `sqrt(2π) × power × GaussianWeight` 且权重只乘一次；G/B/S 均使用 geometric point normalization；GeoGaussian width/membership 与 Simple Gaussian contribution 均逐频精确计算；directional `.sbp` 与适用的 S Lloyd factor 在逐 ray Project 前共用 source-amplitude 路径；单频 SHD，以及多频 `nonreuse/reuse/parallel` SHD |
| R | 单频 `R/RG/RGO`、directional `.sbp`、显式 `Nalpha=1`、逐频 active/terminal prefix、Origin-compatible `.ray` |
| Arrivals | ASCII `A`、binary `a`；Cartesian geometric hat/Gaussian `G/B`；frequency-local `ArrivalWorkspace` 与 AddArr 语义 |
| Eigenray | `E` 的 Cartesian `G/B` traversal；receiver hit 对应的冻结 ray prefix 与 `.ray` |
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

- ray-centered `g`；
- irregular receiver；
- line source；
- multisource parity；
- 3D / N×2D；
- beam shift；
- ray-centered geometric Gaussian；
- ray-centered GeoHat TL、ray-centered Cerveny，以及本矩阵未列出的 beam/coordinate family；
- 新 BARR 算法、SIMD、新性能优化；
- F2CPP/RayReuse shared-library 重构。

延期项不是 silent fallback 的许可。未来若进入 scope，必须作为新的独立阶段
重新定义输入、逐频状态边界和验收证据。
