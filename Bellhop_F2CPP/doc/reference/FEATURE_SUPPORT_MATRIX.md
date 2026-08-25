# Bellhop F2CPP 二维单频支持矩阵

> 封板日期：2026-08-14
> 适用范围：`Bellhop_F2CPP` 当前二维、单频研究实现。本文是复刻封板后的
> 唯一功能分类表；具体输入格式和命令以 [`USAGE.md`](../guides/USAGE.md) 为准。

## 状态定义

- **Supported**：已端到端实现，并由组件测试及代表性 Origin 对照覆盖。
- **Intentional divergence**：可观察的安全/生命周期行为或内部实现有意不同于
  Fortran，但不改变已声明数值与文件兼容范围。
- **Deferred / out of scope**：不属于当前二维单频研究范围；parser 或模型校验
  必须明确拒绝，不允许静默回退到其他算法。

## Supported

| 能力 | 已封板范围 |
|---|---|
| 计算维度 | 二维、一个水体介质、每次运行一个正频率；一个或多个 source depth |
| TL 产品 | coherent/incoherent/semi-coherent `C/I/S` 的 Cartesian/ray-centered Cerveny、Cartesian geometric hat/Gaussian 与 ray-centered geometric hat；simple Gaussian 限 coherent point-source rectilinear Cartesian |
| Ray 产品 | 非 beam-shift `R/RG/RGO`，方向性 `.sbp`，显式 `Nalpha=1`，Origin-compatible active/terminal prefix 与 `.ray` 顺序 |
| Arrivals | ASCII `A`、GNU sequential-unformatted binary `a`；Cartesian G/B、ray-centered g；重复到达、容量与零到达遵循 `ArrMod::AddArr` |
| Eigenray | `E` 的 Cartesian G/B、ray-centered g；输出命中对应的冻结 ray prefix |
| SSP | C-linear、PCHIP、N²-linear、not-a-knot cubic spline、`Q + .ssp` 二维双线性范围相关 SSP |
| 边界类型 | top/bottom `V/R/A/G/F`；top `.trc` 与 bottom `.brc` tabulated reflection |
| 边界几何/材料 | flat、piecewise-linear `LS`、canonical curvilinear `C` short format；acoustic `LL` fluid 或 elastic P/S long format |
| 衰减 | `N/F/M/W/Q/L`；Thorp、Francois–Garrison、biological 体积衰减 |
| 接收器 | Cartesian rectilinear；Cartesian TL 的 Origin-legacy irregular SHD；A/a/E 的 paired irregular G/B；ray-centered regular/equally-spaced range；R 单 receiver range |
| 波束与声源 | Cerveny `F/M/W × D/S/Z`、1～3 images、point/line source、`.sbp`；A/a/E 使用 G/g/B |
| 文件与生命周期 | PRT、SHD、RAY、ARR；布局/顺序/数值按各冻结 Origin oracle 验收；模式切换清理陈旧异类产品 |
| 研究架构 | 完整只读 `RayPathCache`；复走时、active mask、反射幅相、pressure/intensity 仅存在于逐频临时状态 |
| 线程执行 | 默认串行；单 source Cartesian Cerveny TL 可用 `F2CPP_THREADS` 选择持久 receiver-depth team，保持每 cell 累加顺序与 SHD bitwise 一致 |

Cartesian Cerveny 的 `P/V/H` 和 Cartesian irregular receiver 深度选择保留
Origin 2-D 的可观察 legacy 语义：前者三场相同，后者使用冻结的 legacy
`Rz(1)` 行为。这两项是兼容行为，不应解释为物理 V/H 或修正后的 paired CC。

## Intentional divergence

| 项目 | F2CPP 行为 | 兼容边界 |
|---|---|---|
| 解析与资源安全 | 对非有限值、溢出、资源上限、缺失 sidecar、尾随记录和未支持组合提前报错 | Origin 可能在更晚阶段失败；已支持输入的数值语义不变 |
| 输出发布 | SHD/RAY/ARR 先写临时文件再原子发布；失败保留旧正式产品并清理临时文件 | 文件内容兼容；失败过程和陈旧产品处理有意更安全 |
| 模式切换 | 成功运行后删除同根、非本模式的陈旧正式产品 | 防止误读旧输出，不复刻 Origin 的残留文件行为 |
| 数值与数据布局 | C++ 内部以 binary64 计算并保存完整冻结轨迹，而非复刻 Fortran 工作数组布局 | 输出按 Origin 所需精度/布局写出；科学结果使用冻结容差或精确字段门验收 |
| 错误界面 | 未支持路径统一返回非零状态，并在可用时写入 PRT `FATAL ERROR` | 不承诺复刻 Origin 的具体崩溃位置或错误文本 |

## Deferred / out of current research scope

| 类别 | 明确延期项 |
|---|---|
| 维度与调度 | 3D、N×2D、F2CPP 单进程多频调度；宽带复用由独立 `Bellhop_RayReuse` 负责 |
| 波束 | beam shift、ray-centered geometric Gaussian、A/a/E 的 Cerveny/simple-Gaussian 等未验收 family |
| 接收器 | ray-centered irregular receiver grid |
| SSP/介质 | analytic SSP、多个水体介质、水体 shear 参数 |
| 边界 | `P/W` reflection-coefficient 路径、边界粗糙度、`CS/CL` legacy 写法、curvilinear long format、`G/F + LL` |
| 衰减 | 小写 `m` power-law 输入 |
| 其他 Origin 产品 | 当前矩阵未列出的 3D、N×2D 或专用输出产品 |

上述项不是“自动采用最接近实现”的请求。当前 parser、环境模型或 solver
边界必须在消费前明确拒绝；若未来纳入研究范围，应作为新的、独立 iteration
重新设计和验收。

## Post-replication backlog

复刻封板后只保留性能阶段入口，不在 B4 实施。P1/P2 已于 2026-08-16 完成：

1. TL、R、A、E 四个 workload 已完成分阶段 wall/peak RSS profile；Munk TL 的
   Cartesian Cerveny Influence 占外部 wall `97.27%`；
2. 完整冻结轨迹的 peak RSS/capacity footprint 已记录；分配次数和 cache miss
   留到确有数据布局候选时再用系统工具测量；
3. P2 已完成 Cartesian Cerveny receiver/Influence 的局部循环不变量、receiver
   与 workspace 访问优化，Munk wall/Influence 分别获得 `1.608×/1.639×`
   speedup 且产品逐字节一致；P3-01 profile 进一步确认 compute/math/check 路径
   主导，并接受一个 `1.096×` wall 的 finite-helper scalar 优化；P3-02 又以
   bitwise 一致的 Hermite hot helper 获得 `1.066×` wall speedup。当前没有
   RayPathCache memory-bound 证据；P3-03 又确认每个唯一复相位已只执行一次
   fused sincos 和一次 exp，重复参数仅 `0.484%` 上界，因而没有接受新的
   scalar 改动。P4-01 的真实输入原型进一步排除 SIMD 首选路线：精确 vForce
   不能保持 bitwise，而确定性 receiver-depth 线程分区已有 `2.0～2.5×` 绝对
   wall 潜力且 RSS 近似不变。P4-02 已将其正式化为持久 team：在当前 Apple
   M4 开发机器的 50/250 Hz Munk benchmark 上，4-thread points 分别达到
   `2.276×/2.117×`，所有已测线程档 SHD bitwise 一致，最大 RSS 增量低于
   `0.21 MiB`。1/2/4/8 只是本次参考测量档，不构成推荐上限；用户仍可按其他
   硬件/workload 选择 8/16 或更高线程数。当前 F2CPP 性能阶段暂停，数据布局
   仍没有重构依据。
   详细结果见
   [`PERFORMANCE.md`](../reports/PERFORMANCE.md)。
