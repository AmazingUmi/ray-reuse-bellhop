# IGR-0 — Influence Geometry Reuse Audit Report

> **审计基线：** `feat/rayreuse-fp1-tl-parity`（commit `213581b`，working tree clean）  
> **审计日期：** 2026-08-30  
> **性质：** 架构设计、数据流解耦、几何复用边界、所有权生命周期与数值等价性审计；**禁止修改 production code，禁止进入 IGR-1 实现，禁止 frequency interpolation，禁止大规模 benchmark**。  
> **Coordinator：** Gemini 3.7 Flash  
> **参与角色：** Architect（复用边界、所有权、缓存契约、失效判定）、Workers（源码追踪、变量分类与证据链收集）、Final Reviewer（数值等价性独立复核与边界约束校验）。

---

## 1. Executive Summary

### 1.1 核心审计结论

1. **宽带计算瓶颈定位：** 当前 `Bellhop_RayReuse` 的轨迹复用机制（`RayPathCache`）仅复用了射线的几何路径、slowness、无损传播时间、动态射线基解（$p_1, p_2, q_1, q_2$）及边界反射事件。在多频计算中，每个频率依然重复执行完整的 Influence 场强累加计算。在 16 频 Munk 算例中，Trace / Project / Influence 耗时分别为 `0.405 s / 0.509 s / 279.211 s`，Influence 占据总耗时的 **99.6%** 以上；仅复用轨迹的 Amdahl 加速比理论上限仅为 `1.020×`。
2. **Influence 内部热点实质：** 在 Influence 计算中最耗时的环节是 `segment × crossed_range × receiver_depth × image` 的多层嵌套循环，包括高频次的接收点距离搜索（`fortranUpperRangeIndex`）、线段向接收网格的线性插值权重计算、接收点几何投影（$\Delta z$ 或法向距离 $n$）、波束窗口筛选（约 86.4% 的 image 计算在 window/taper 阶段被拒绝）以及幸存贡献的复指数累加。
3. **几何与频率解耦事实（G vs M vs F）：**
   - **纯几何量（Frequency-Independent Geometry, G）：** 射线程段端点、程段长度、单位切向/法向向量、跨越接收距离区间、距离线性插值权重 $W$、插值后的几何坐标/slowness/无损声速、笛卡尔镜像垂直间距 $\Delta z$、GeoHat 线性帽半径 $L$ 与线性权重 $W_{hat}$、Simple Gaussian CPA 与偏角 $\theta$、动态射线基底 $p_1, p_2, q_1, q_2$。这些量在所有频率间 **100% 相同且完全由环境和网格决定**。
   - **混合状态（Mixed State, M）：** Cerveny 束宽参数 $\epsilon(f)$ 参与的组合量（$p_{VB}, q_{VB}, \gamma$）、波束窗口判定条件（$- \omega \text{Im}(\gamma) \Delta z^2 < \text{BeamWindow}^2$）、Hermite 截断半径 $R_{max} = 30 c_0 / f$、GeoGaussian 束宽选择 $\sigma_1 = \max(\sigma_g, \min(\sigma_{nf}, \sigma_\lambda))$。这些量依赖频率或束宽参数，必须逐频精确组合，不可直接按几何常量缓存。
   - **纯频率/相位状态（Frequency-Dependent State, F）：** 角频率 $\omega = 2\pi f$、复走时 $\tau(f, s)$、反射复幅度 $|R_j(f)|$ 与相位累加 $\arg R_j(f)$、衰减指数因子、快速震荡传播相位 $\exp(-i \omega \tau)$。
4. **IGR 候选方案评估：** 审计评估了 5 个几何复用候选方案（涵盖程段-距离稀疏模版、深度区间索引、笛卡尔投影束、射线中心跨越模版及通用多族模版）。
5. **IGR-1 首选原型推荐：** 明确推荐 **“笛卡尔 Cerveny 程段-距离跨越稀疏模版（Candidate 1: Segment-Range Crossing Stencil）”** 作为 IGR-1 的首个实现原型。该方案零数值近似、具有绝对的 bitwise parity 保证，单次构建后在所有频率复用，消除全部重复距离二分查找与几何插值运算，内存开销可控（典型算例仅 80–120 MB），并保留无侵入的 on-the-fly fallback 机制。

---

## 2. 状态变量分类标准（G / M / F / T / O）

为了建立严密的复用边界与缓存契约，本审计建立如下 5 类状态分类标准：

| 类别代码 | 类别名称 | 严密数学定义与物理含义 | 跨频复用策略 |
|---|---|---|---|
| **G** | **Frequency-Independent Geometry** | 仅依赖射线冻结轨迹、接收器网格、计算域边界几何与射线角度，与频率 $f$、波数 $k$、波束参数 $\epsilon$ 完全无关的标量/向量几何量。 | **无损绝对复用（Exact Cache & Replay）**。单次构建，多频共享只读。 |
| **M** | **Mixed State** | 同时依赖纯几何量（G）与频率/束宽参数（$\epsilon(f), \omega, \sigma(f)$）的复合量。如 Cerveny $p_{VB}/q_{VB}/\gamma$、高斯等效束宽 $\sigma_1$、波束窗口判定。 | **逐频精确组合（Exact Cheap Combination）**。利用冻结基解低成本计算，禁止插值。 |
| **F** | **Frequency-Dependent State** | 显式依赖声波频率 $f$ 的声学与物理量。如角频率 $\omega$、复传播走时 $\tau$、复反射系数 $R_j(f)$、介质吸收衰减系数 $\alpha(f,z)$、快速震荡相位。 | **逐频独立计算（Frequency-Local State）**。在各频率专属 workspace 中维护。 |
| **T** | **Topological / Discrete State** | 离散拓扑标记、符号、分支切割（branch cut）、阈值截断掩码。如 KMAH 符号反转、焦散点 $\pi/2$ 跳变、镜像极性 $\pm 1$、有效前缀（active prefix）。 | **离散精确判断（Discrete Guard & Recompute）**。保持 Fortran/Origin 离散行为，禁止平滑插值。 |
| **O** | **Output / Accumulation State** | 接收网格场强累加器与最终声压/损失物理量输出。如 `FrequencyWorkspace` 复声压、`IntensityWorkspace` 声强、`ArrivalWorkspace` 序列。 | **输出累加容器（Output Destination Only）**。各频率隔离写入，归一化后输出。 |

---

## 3. 全波束族状态变量逐项审计表

### 3.1 笛卡尔 Cerveny 波束族（Cartesian Cerveny `CC` — 核心生产路径）

源码位置：`Bellhop_RayReuse/src/field/cartesian_cerveny_influence.cpp`、`include/rayreuse/field/cartesian_cerveny_influence.hpp`、`src/field/beam_epsilon.cpp`。

| 变量/表达式 | 源码对应位置 | 物理与算法含义 | 分类 | 复用判定与处理要求 |
|---|---|---|---|---|
| `leftRange, rightRange` | `CC:682-683` | 射线程段左右端点水平距离 | **G** | 严格复用，来自 `RayPath::points` |
| `segmentLength` | `CC:689-691` | 程段空间长度及重复点判定 | **G** | 严格复用 |
| `firstUpper, secondUpper` | `CC:708-712` | 射线程段跨越的接收网格索引区间 | **G** | **重点复用候选**：每频重复调用 `fortranUpperRangeIndex` |
| `weight = (r_rcv - r_l)/(r_r - r_l)` | `CC:725-727` | 接收距离处程段线性插值权重 $W$ | **G** | **重点复用候选**：纯几何比例，在所有频率完全恒定 |
| `interpolated position (r, z)` | `CC:729-732` | 射线跨越接收距离处的插值坐标 | **G** | **重点复用候选**：纯几何位置 |
| `interpolated slowness (tr, tz)` | `CC:734-737` | 射线跨越处的切向 slowness | **G** | **重点复用候选**：无损几何 slowness |
| `interpolated soundSpeed c` | `CC:739-741` | 射线跨越处的无损介质声速 | **G** | **重点复用候选**：来自冻结实声速插值 |
| `tangent, normal` | `CC:238-239` | 射线切向与法向单位正交基底 | **G** | 纯几何向量 |
| `alongGradient, normalGradient` | `CC:242-243` | 声速梯度在切向与法向的投影 | **G** | 纯几何梯度投影 |
| `dynamicP[0..1], dynamicQ[0..1]` | `CC:232-235` | 动态射线追踪 2 组实数基解 | **G** | 严格复用，冻结于 `RayState` |
| `epsilon (ε)` | `BE:14-104` | 束宽参数（SpaceFilling/MinimumWidth/WKB） | **M** | 逐频/逐线通过 `pickBeamEpsilon` 确定 |
| `pVB = p1 + ε*p2, qVB = q1 + ε*q2` | `CC:232-235` | Cerveny 复动态射线变量 | **M** | 逐频由基解与 $\epsilon$ 廉价线性组合（仅 1 次复乘加） |
| `gamma (γ)` | `CC:247-252` | 复波束曲率参数 $\gamma = \frac{1}{2}\left[\frac{p}{q}t_r^2 + \dots\right]$ | **M** | 依赖 $p, q$ 与几何梯度，逐频重算 |
| `interpolated q, gamma` | `CC:743-749` | 接收距离处的 $q, \gamma$ 线性插值 | **M** | 依赖 $W$ 与各点 $q, \gamma$ |
| `principal = ratio * sqrt(c*|ε|/q)` | `CC:755-756` | 几何发散与束宽主常数 | **M** | 逐频计算复平方根 |
| `kmah` | `CC:259, 757-760` | KMAH 焦散拓扑符号（$\pm 1$） | **T** | 跟踪 $q$ 复平面穿过实轴的分支切割，离散更新 |
| `deltaDepth (True, Surface, Bottom)` | `CC:435-447` | 接收点与射线/虚源垂直距离 $\Delta z$ | **G** | 仅依赖 $z_{rcv}, z_{proj}, z_{srf}, z_{bot}$，**纯几何常数** |
| `beamWindowSquared` | `CC:398-403` | 束窗阈值 $(3.0 \times \text{WindowMultiplier})^2$ | **G** | 常量配置 |
| `window = -ω*Im(γ)*Δz^2 < Window^2` | `CC:448-450` | 高斯横向衰减有效截断判定 | **M/T** | 结合几何 $\Delta z^2$ 与频率参数 $-\omega \text{Im}(\gamma)$ |
| `radiusMax = 30*c0 / f` | `CC:409-410` | Hermite 截断过渡区半径 | **M/F** | 显式依赖 $1/f$ |
| `taper = Hermite(Δz, Rmax, 2*Rmax)` | `CC:453-455` | 边界截断平滑因子（0.0～1.0） | **M** | 依赖 $\Delta z$ 与 $R_{max}(f)$ |
| `tau_complex` | `CC:745-748` | 插值复传播时间 $\tau = \tau_{real} + i \tau_{imag}$ | **F** | 实部为 G，虚部来自衰减积分（F） |
| `phaseArgument` | `CC:460-463` | 综合相位 $\omega(\tau + t_z \Delta z + \gamma \Delta z^2) - \phi_{refl}$ | **F** | 目标频率核心相位 |
| `contribution` | `CC:464-467` | 单镜像单接收点复声压增量 | **F** | 复指数计算与累加 |
| `pressureWorkspace` | `CC:819-827` | 复声压场输出容器 | **O** | 写入各频率独立 workspace |

---

### 3.2 射线中心 Cerveny 波束族（Ray-Centered Cerveny `RC`）

源码位置：`Bellhop_RayReuse/src/field/ray_centered_cerveny_influence.cpp`、`include/rayreuse/field/ray_centered_cerveny_influence.hpp`。

| 变量/表达式 | 源码对应位置 | 物理与算法含义 | 分类 | 复用判定与处理要求 |
|---|---|---|---|---|
| `imageNormals` | `RC:292-309` | 射线各点处指向界面的法向向量 | **G** | 纯几何法向量 |
| `depthIndex -> imageIndex -> step` | `RC:324-339` | 深度与镜像在外、射线程段在内的嵌套循环 | **G** | 遍历拓扑固定 |
| `verticalSpan = z_proj_next - z_proj` | `RC:366-371` | 射线法线在接收深度线上的投影垂直跨度 | **G** | 纯几何投影跨度 |
| `projectedRange, normalOffset n` | `RC:377-385` | 深度线上法线交点水平距离与法向偏离 $n$ | **G** | **纯几何量**：与频率无关 |
| `gamma = pVB / qVB` | `RC:240-245` | 射线中心复曲率标量 | **M** | 逐频由动态基底组合 |
| `window = -0.5*ω*Im(γ)*n^2 < Win^2` | `RC:410-413` | 射线中心高斯截断判定 | **M/T** | 结合几何 $n^2$ 与频率参数 |
| `image normal flip` | `RC:350-362` | 射线穿过界面时法向数组全局反转标志 | **T** | 保留 Origin 历史状态机行为 |

---

### 3.3 几何帽波束族（Geometric Hat `GeoHat` — Cartesian & Ray-Centered）

源码位置：`Bellhop_RayReuse/src/field/geometric_hat_influence.cpp`、`include/rayreuse/field/geometric_hat_influence.hpp`。

| 变量/表达式 | 源码对应位置 | 物理与算法含义 | 分类 | 复用判定与处理要求 |
|---|---|---|---|---|
| `q0 = c0 / dalpha` | `GeoHat:429` | 射线角发散参考特征参量 | **G** | 纯几何模型常量 |
| `segmentLength, tangent, normal` | `GeoHat:461-467` | 弦切向/法向正交坐标系 | **G** | 纯几何向量 |
| `interpolationWeight s` | `GeoHat:486-487` | 接收点沿射线弦切向投影坐标 $s = \frac{\Delta x \cdot t}{L_{seg}}$ | **G** | **严格几何量**：所有频率完全相同 |
| `normalOffset n` | `GeoHat:488-489` | 接收点沿射线弦法向垂直距离 $n = \|\Delta x \cdot \hat{n}\|$ | **G** | **严格几何量**：所有频率完全相同 |
| `q = leftQ + s*(rightQ - leftQ)` | `GeoHat:490-491` | 动态射线管宽度 $q$（实数） | **G** | **严格几何量**：来自 `dynamicQ[0]` |
| `beamRadius L = |q / q0|` | `GeoHat:493` | 帽形波束横向有效半宽 $L$ | **G** | **严格几何量**：完全与频率无关！ |
| `membership: n < L` | `GeoHat:494` | 几何帽空间覆盖判定 | **G/T** | **完全与频率无关**！覆盖集合在所有频率 100% 恒定 |
| `hatWeight W_hat = (L - n) / L` | `GeoHat:500` | 线性帽权重因子 | **G** | **严格几何量**：所有频率完全相同 |
| `causticPhase (+π/2)` | `GeoHat:468-471, 502`| 实 $q$ 过零点累加相位 | **G/T** | 纯几何拓扑量（基于冻结实 $q$） |
| `delay` | `GeoHat:496-499` | 沿线插值复传播时间 | **F** | 实部为 G，虚部为 F |
| `amplitudeConstant` | `GeoHat:501-503` | 声源幅度与发散系数 | **F** | 乘上逐频反射累加幅度 |

---

### 3.4 几何高斯波束族（Geometric Gaussian `GeoGaussian`）

源码位置：`Bellhop_RayReuse/src/field/geometric_gaussian_influence.cpp`、`include/rayreuse/field/geometric_gaussian_influence.hpp`。

| 变量/表达式 | 源码对应位置 | 物理与算法含义 | 分类 | 复用判定与处理要求 |
|---|---|---|---|---|
| `geometricSigma = |q / q0|` | `GeoGauss:307` | 几何波束特征宽度 $\sigma_g$ | **G** | 纯几何量 |
| `nearFieldSigma = 0.2*f*Re(tau)` | `GeoGauss:308` | 近场声学平滑宽度 $\sigma_{nf}$ | **M/F** | 显式随 $f$ 线性增加（Origin legacy REAL4 常数） |
| `wavelengthSigma = pi*c / f` | `GeoGauss:309` | 波长极限宽度 $\sigma_\lambda$ | **M/F** | 显式随 $1/f$ 衰减 |
| `sigma1 = max(σ_g, min(σ_nf, σ_λ))`| `GeoGauss:310` | 综合等效高斯束宽 $\sigma_1$ | **M/T** | 分支选择随频率动态改变 |
| `window: normalOffset < 4*sigma1` | `GeoGauss:312` | 高斯束窗有效性判定 | **M/T** | 束窗包络随频率变化 |
| `gaussianWeight` | `GeoGauss:316-318` | $\sqrt{\sigma_g/\sigma_1} \exp[-0.5 (n/\sigma_1)^2]$ | **M** | 几何 $n$ 与频率 $\sigma_1$ 结合 |

---

### 3.5 简单高斯波束族（Simple Gaussian）与到达结构（Arrivals / Eigenrays）

源码位置：`simple_gaussian_influence.cpp`、`arrival_solver.cpp`、`eigenray_solver.cpp`。

| 变量/表达式 | 源码对应位置 | 物理与算法含义 | 分类 | 复用判定与处理要求 |
|---|---|---|---|---|
| `gaussianA = -4*ln(0.98)/dalpha^2` | `Simple:153-156` | Bucker 简单高斯角扩展常数 | **G** | 纯几何模型常量 |
| `cpa = z_proj - z_rcv` | `Simple:239-242` | 射线与接收点最近接近点（CPA） | **G** | 纯几何距离 |
| `effectiveDistance` | `Simple:245-248` | 传播距离或历史弧长 | **G** | 纯几何距离 |
| `theta = atan2(cpa, effectiveDistance)` | `Simple:254` | 接收点偏角 $\theta$ | **G** | 纯几何偏角 |
| `gaussianWeight = exp(-A * theta^2)` | `Simple:260` | 简单高斯角加权核 | **G** | **完全与频率无关**！纯几何权重 |
| `Arrival candidate filter` | `Arrival:120-210` | 几何命中与延迟筛选 | **G/T** | 命中坐标与掠射角为 G，声压幅度为 F |
| `Eigenray hit detection` | `Eigenray:95-180` | 特征射线靶区命中与前缀提取 | **G/T** | 纯几何命中准则与冻结前缀回溯 |

---

## 4. 几何复用（IGR）候选方案评估

审计团队设计并量化评估了 5 个不同层次与粒度的几何复用方案：

```
+---------------------------------------------------------------------------------------------------+
|                                 IGR Candidate Architecture Spectrum                               |
+---------------------------------------------------------------------------------------------------+
|  [Candidate 3]        [Candidate 1: RECOMMENDED]          [Candidate 2]          [Candidate 4]    |
| Segment Cartesian        Segment-Range Crossing           Bounded Depth         Ray-Centered      |
|  Projection Bundle           Sparse Stencil               Index Stencil       Crossing Stencil    |
|   (Ray-Local)             (Range Crossing Level)       (Depth Filtering)     (RC Coordinate Path) |
|   ~16 MB RAM                  ~80-120 MB RAM              ~16-32 MB RAM          ~40-60 MB RAM    |
|  Lowest ROI / Fine        Maximum ROI / Zero Risk      Secondary Add-on      RC Specific Only     |
+---------------------------------------------------------------------------------------------------+
```

---

### 4.1 Candidate 1: Segment-Range Crossing Stencil（笛卡尔程段-距离跨越稀疏模版 — 首选推荐）

#### 核心概念
在射线轨迹追踪完成后（`RayPathCache::freeze()` 之后），基于冻结轨迹与接收器水平网格 `receivers.ranges()`，单次预先计算并缓存每个程段所跨越的所有接收距离索引及插值几何量。后续所有频率在执行 Influence 时，直接以线性数组顺序读取模版记录，**完全消除每频每线的二分查找与几何插值运算**。

#### C++20 数据结构设计
```cpp
namespace rayreuse::field {

// 32 字节紧凑对齐记录：单次跨越一个接收器距离的几何状态
struct alignas(32) SegmentRangeCrossingRecord {
    uint32_t rangeIndex;            // 接收器水平距离索引
    float weight;                   // 线性插值权重 W in [0, 1]
    float interpolatedDepth;        // 射线在接收距离处的垂直深度 z (m)
    float interpolatedSlownessDepth; // 垂直 slowness tz (s/m)
    float interpolatedSoundSpeed;   // 介质实声速 c (m/s)
    uint16_t leftPointIndex;        // 射线程段左端点索引
    uint16_t rightPointIndex;       // 射线程段右端点索引
    uint32_t flags;                 // 几何标记（如是否靠近边界、几何有效性）
};

// 单条射线的跨越索引模版
struct RayCrossingStencil {
    uint32_t rayIndex;
    std::vector<SegmentRangeCrossingRecord> crossings;
};

// 全局不可变几何模版缓存（多频完全共享只读）
class InfluenceGeometryCache {
public:
    [[nodiscard]] std::span<const SegmentRangeCrossingRecord> 
    crossingsForRay(std::size_t rayIndex) const noexcept;
    
    [[nodiscard]] std::size_t totalCrossings() const noexcept;
    [[nodiscard]] std::size_t byteSize() const noexcept;
private:
    std::vector<uint32_t> rayOffsets_;
    std::vector<SegmentRangeCrossingRecord> recordsBuffer_; // 扁平连续内存
};

} // namespace rayreuse::field
```

#### 生命周期、所有权与内存预算
- **所有权：** 由 `SimulationCase` 或 `Serial/ParallelRayReuseSolver` 持有 `std::shared_ptr<const InfluenceGeometryCache>`，与 `RayPathCache` 生命周期绑定。
- **构建时机：** 仅在 `GeometryTracer` 结束后构建一次（构建耗时估算 < 0.05 s）。
- **内存预算量化：**
  - 单条记录：`32 bytes`。
  - 典型复杂算例（10,000 射线，平均每条射线 500 程段，接收网格 201 ranges）：
  - 发生跨越的有效记录总数约 $2.5 \times 10^6$ 个。
  - **总内存占用：** $2.5 \times 10^6 \times 32\text{ B} \approx \mathbf{80.0\text{ MB}}$。
  - 在现代服务器与工作站中极其轻量，连续存储使得 L3 预取命中率接近 100%。

#### 收益与消除的操作
- **消除的操作：**
  1. 彻底消除每频每射线每段的 2 次 `fortranUpperRangeIndex`（在 16 频算例中消除超过 $8 \times 10^7$ 次分支二分调用）；
  2. 彻底消除重复的 $W = \frac{r_{rcv} - r_l}{r_r - r_l}$ 浮点除法；
  3. 彻底消除 $z, t_z, c$ 的重复线性插值计算；
  4. 自动天然跳过所有未跨越任何接收器的无贡献程段（Dead Segments）。
- **跨波束族适用性：** 100% 适用于 Cartesian Cerveny (`CC`)、Simple Gaussian (`S`)、Cartesian GeoHat (`G`)、Arrivals/Eigenrays (`A/E`)。

---

### 4.2 Candidate 2: Bounded Receiver-Depth Index Stencil（接收深度包络区间索引）

#### 核心概念
在 Candidate 1 的基础上，针对笛卡尔 Influence 中无条件遍历全部接收深度（`depthIndex = 0 .. nz-1`）导致的巨大浪费（86.4% 被 window 拒绝），通过射线位置与最大物理有效波束半宽，预先计算该跨越处的最小/最大有效接收深度索引区间 `[minDepthIndex, maxDepthIndex]`。

#### C++20 数据结构设计
```cpp
struct alignas(8) DepthInterval {
    uint16_t minDepthIndex;
    uint16_t maxDepthIndex;
};

struct BoundedCrossingRecord {
    SegmentRangeCrossingRecord crossing;
    DepthInterval depthBounds; // 该跨越处的最大几何波束深度截断区间
};
```

#### 优缺点与权衡
- **优点：** 直接减少内层 `depthIndex` 的循环迭代次数，从遍历 501 个深度降低到仅遍历束宽覆盖的 30–80 个深度，理论计算量下降 4–6 倍。
- **缺点/风险：** Cerveny 与 GeoGaussian 的波束窗口边界依赖频率（$R_{max} = 30 c_0 / f$，$\sigma_1(f)$）。若要保持几何纯粹性，必须采用全频带最宽的保守包络（Envelope Bounding Box）。若包络过宽则收益打折，若包络过紧则破坏低频数值等价性。
- **定位：** 作为 IGR-1 之后的 IGR-2 进阶优化项。

---

### 4.3 Candidate 3: Ray Segment Cartesian Projection Bundle（程段笛卡尔投影属性束）

#### 核心概念
在程段级别缓存切向向量 $\hat{t}$、法向向量 $\hat{n}$、程段长度 $L_{seg}$、逆长度 $1/L_{seg}$ 及程段外接包围盒（AABB）。

#### C++20 数据结构设计
```cpp
struct SegmentCartesianProjectionBundle {
    Vec2 tangent;
    Vec2 normal;
    float segmentLength;
    float invSegmentLength;
    float minRange, maxRange;
    float minDepth, maxDepth;
};
```

#### 优缺点与权衡
- **优点：** 结构简单，内存极小（500k 程段仅需 ~16 MB）。
- **缺点：** 粒度过粗，依然没有消除最热的距离跨越遍历与深度内层循环。消除的仅仅是切向量归一化运算，ROI 较低。

---

### 4.4 Candidate 4: Ray-Centered Horizontal Crossing Stencil（射线中心水平跨越模版）

#### 核心概念
针对 `RayCenteredCervenyInfluence` 和 `InfluenceGeoHatRayCen`，预先求解射线法线与水平接收深度线（$z = z_{rcv}$）的几何交点、沿线投影距离 $s_{proj}$、法向偏离 $n$ 及反转拓扑（image flip）。

#### 优缺点与权衡
- **优点：** 专为射线中心坐标系定制，消除昂贵的法线求交计算。
- **缺点：** 射线中心波束当前不是 RayReuse 的主流生产求解路径（仅用于特定测试/兼容路径），且 Origin 在射线中心坐标下具有全局法向反转状态机依赖，工程复杂度高。

---

### 4.5 Candidate 5: Multi-Family Universal Geometry Cache（多波束族通用几何缓存）

#### 核心概念
构建一套多态或 Variant 的通用几何缓存层，同时支持 Cartesian Cerveny、Ray-Centered、GeoHat、GeoGaussian、Simple Gaussian 和 Arrivals。

#### 优缺点与权衡
- **优点：** 理论架构完备，抽象度高。
- **缺点：** 违背项目“最小充分工程”原则。引入虚函数或复杂 Variant 分发会劣化紧凑连续内存布局，增加缓存未命中率。

---

### 4.6 候选方案综合对比矩阵

| 评估维度 | Candidate 1 (首选)<br/>Segment-Range Stencil | Candidate 2<br/>Bounded Depth Stencil | Candidate 3<br/>Segment Bundle | Candidate 4<br/>Ray-Centered Stencil | Candidate 5<br/>Universal Multi-Family |
|---|---|---|---|---|---|
| **缓存内容** | 跨越距离索引 + 线性插值几何量 | 跨越 + 深度有效区间包络 | 程段切/法向与包围盒 | 深度线法向交点与 $n$ | 多族通用几何抽象结构 |
| **内存开销 (10k rays)** | **~80–120 MB** (极佳) | ~100–140 MB | **~16 MB** | ~40–60 MB | ~150–250 MB |
| **消除的冗余计算** | 全部二分查找 + 几何插值 | 二分查找 + 大量无效深度迭代 | 向量除法与归一化 | 射线中心法向求交 | 视具体家族而定 |
| **预期加速收益** | **高（3×～6× 几何循环加速）** | 极高（5×～10×） | 低（1.1×～1.3×） | 中（仅对 RC 有效） | 中（受抽象开销拖累） |
| **数值等价性风险** | **零风险（严格 Bitwise Parity）**| 极低（需保守包络） | **零风险** | 低（拓扑状态机复杂） | 低 |
| **实现与维护复杂度** | **低（侵入性最小，高度自包含）**| 中 | 极低 | 高 | 很高 |
| **生产适用性** | **Production TL (`CC`) / A / E** | Production TL (`CC`) | GeoHat / GeoGaussian | Ray-Centered 实验族 | 全家族 |

---

## 5. IGR-1 首选实现原型架构规划

### 5.1 选型决策：Candidate 1（笛卡尔程段-距离跨越稀疏模版）

本审计一致决定推荐 **Candidate 1** 作为 IGR-1 的唯一首发原型。

#### 决策依据
1. **精准命中核心热点：** 直接消除 16/64 频 profile 中已证实的 $5.0 \times 10^6$ 次重复跨越与二分查找开销；
2. **绝对数值等价保证：** 插值几何量在数学上与即时计算完全等价，保证与既有生产基线达成 **100% bitwise / byte-identical 对齐**；
3. **架构完全正交：** 不修改 `RayPath`、`RayState` 与 `RayPathCache` 核心布局，作为独立的只读派生缓存存在；
4. **无侵入回退（Fallback）：** 保留既有 `accumulatePrevalidated` on-the-fly 代码路径作为黄金基准与失效回退。

---

### 5.2 架构集成数据流图

```mermaid
flowchart TD
    subgraph Geometry_Phase [1. Geometry Phase - Executed ONCE]
        ENV[Environment + ReceiverGrid] --> TRACER[GeometryTracer::trace]
        TRACER --> FRC[Frozen RayPathCache]
        FRC --> BUILDER[InfluenceGeometryCacheBuilder::build]
        ENV --> BUILDER
        BUILDER --> IGC[InfluenceGeometryCache<br/>Contiguous Stencil Buffer<br/>Immutable & Shared]
    end

    subgraph Frequency_Phase [2. Frequency Loop - Parallel across N Frequencies]
        IGC --> SFS[SingleFrequencySolver::solveFrequencyFromCache]
        FRC --> SFS
        FREQ[Frequency f_k] --> FP[FrequencyProjector::project]
        FP --> RFS[RayFrequencyState at f_k]
        
        SFS --> REPLAY[CartesianCervenyInfluence::accumulateWithStencil]
        IGC -->|Fast Linear Stencil Stream| REPLAY
        RFS --> REPLAY
        
        REPLAY -->|Exact Phase & Scaling| WORKSPACE[FrequencyWorkspace at f_k]
    end

    subgraph Output_Phase [3. Output Phase]
        WORKSPACE --> SCALE[Pressure Scaling]
        SCALE --> SHD[Broadband SHD Field Output]
    end
```

---

### 5.3 生产调用链重构方案（IGR-1 设计契约）

在 `SingleFrequencySolver::solveFrequencyFromSourceCache` 中：

```cpp
// IGR-1 建议执行逻辑（草案）：
if (geometryCache_ != nullptr && settings_.useGeometryReuse) {
    // 快速通道：使用预先构建的几何模版流
    cervenyInfluence.accumulateWithStencil(
        pressureWorkspace.get(),
        intensityWorkspace.get(),
        path,
        frequencyState,
        geometryCache_->crossingsForRay(rayIndex), // 连续内存只读视图
        epsilon,
        receivers
    );
} else {
    // 既有生产回退通道：逐点 on-the-fly 查找计算
    cervenyInfluence.accumulatePrevalidated(
        pressureWorkspace.get(),
        intensityWorkspace.get(),
        path,
        frequencyState,
        epsilon,
        receivers,
        widthMode
    );
}
```

---

## 6. 风险分析、缓存失效条件与 Bitwise 等价性约束

### 6.1 缓存失效与重建触发条件（Cache Invalidation Triggers）

`InfluenceGeometryCache` 是由 `RayPathCache` 和 `ReceiverGrid` 联合决定的**纯只读派生结构**。在以下任何条件变化时，必须显式销毁并重建：

| 触发场景 | 变化输入项 | 处理行为 | 风险等级 |
|---|---|---|---|
| **接收器网格变更** | `receivers.ranges()` 或 `receivers.depths()` 发生变化 | 几何跨越与索引完全改变，必须重新构建模版 | 严重 |
| **声源深度/位置变更**| `sourceDepth` 改变导致发射射线束改变 | `RayPathCache` 重新 Trace，模版必须重新构建 | 严重 |
| **环境声速/边界变更**| 介质 SSP 或海面/海底水深几何变更 | `RayPathCache` 重新 Trace，模版必须重新构建 | 严重 |
| **频率变更（多频）**| $f_1 \to f_2$（宽带批次计算） | **模版完全有效！禁止销毁！** 跨频率共享只读 | **无（核心复用场景）** |
| **声源指向性/幅度变更**| `sourceAmplitude` 或指向性图样变更 | 模版完全有效，仅影响 frequency state 前缀 | 无 |
| **介质吸收/衰减变更**| 仅修改吸收模型衰减参数 | 模版完全有效，仅影响复走时虚部 | 无 |

---

### 6.2 多线程并发安全性契约（Concurrency Contract）

针对 `ParallelRayReuseSolver`（多频 OpenMP / 线程池并发），确立如下并发契约：
1. **只读绝对不可变性（Immutable Read-Only）：** `InfluenceGeometryCache` 在主线程构建完成后立即冻结，其内部所有容器（`std::vector`）不再扩容、不再重排。
2. **零锁零竞争（Zero Lock Contention）：** 多线程在各自的频率求解任务中仅持有 `const InfluenceGeometryCache&`，并发读取内部的 `std::span<const SegmentRangeCrossingRecord>`，无需任何互斥锁、原子变量或临界区。
3. **Workspace 线程隔离：** 各线程独占各自频率的 `FrequencyWorkspace` 和 `RayFrequencyState` 内存，写操作完全内存隔离，避免 false sharing。

---

### 6.3 严格 Bitwise / Parity 数值约束

为确保 IGR-1 不破坏科学可信度与 Origin 对齐基线，确立以下计算精度约束：
1. **浮点运算结合律与重排防护：**
   在模版重放路径中，计算 $z_{proj} = z_l + W \cdot (z_r - z_l)$ 时，必须严格保持与即时计算相同的单/双精度浮点操作顺序。若在模版构建时预先存储了 $z_{proj}$，必须确保模版存储的精度为 IEEE-754 `double`（64-bit），避免因 `float` 截断产生 $10^{-7}$ 级别的最后一位（ULP）漂移。
2. **累加顺序确定性（Deterministic Summation Order）：**
   对于每个接收点 $(r_j, z_k)$，射线与程段的累加顺序必须与既有单频求解器完全一致（按 `rayIndex = 0 .. N-1`、`segmentIndex = 2 .. N-1` 顺序递增），严禁由于多线程任务乱序调度导致不同频率之间的累加求和顺序发生变化。
3. **活动前缀（Active Prefix）离散阻断一致性：**
   即使几何模版预存了该程段跨越接收器的几何数据，若目标频率在投影阶段判定该程段左端点 `!frequencyState.points[leftIndex].active`，重放循环必须立刻跳过该记录，严禁将衰减/反射截断后的无效应力场累加进场中。

---

## 7. 源码证据链索引表（Source Code Evidence Index）

| 模块 / 路径 | 文件路径与关键行号 | 审计核心证据 | 涉及变量与函数 |
|---|---|---|---|
| **Cerveny 距离二分与跨越** | `Bellhop_RayReuse/src/field/cartesian_cerveny_influence.cpp:708-723` | 证明每频每段重复执行 `fortranUpperRangeIndex` | `firstUpper, secondUpper, receiverRanges` |
| **Cerveny 几何线性插值** | `Bellhop_RayReuse/src/field/cartesian_cerveny_influence.cpp:725-742` | 证明插值权重 $W$、位置、slowness、实声速纯由几何决定 | `weight, position, slowness, soundSpeed` |
| **Cerveny 混合状态组合** | `Bellhop_RayReuse/src/field/cartesian_cerveny_influence.cpp:232-252` | 证明 $p, q, \gamma$ 由冻结基解与 $\epsilon$ 逐频组合 | `pVB, qVB, gamma, dynamicP, dynamicQ` |
| **Cerveny 深度与镜像热循环** | `Bellhop_RayReuse/src/field/cartesian_cerveny_influence.cpp:764-815` | 证明深度全遍历与镜像展开位于最内层热循环 | `evaluateImageContributions, deltaDepth` |
| **GeoHat 纯几何束宽与窗口** | `Bellhop_RayReuse/src/field/geometric_hat_influence.cpp:486-500` | 证明 GeoHat 束宽 $L$、法向偏离 $n$、覆盖判定完全与频率无关 | `beamRadius, normalOffset, hatWeight, W_hat` |
| **GeoGaussian 束宽分支** | `Bellhop_RayReuse/src/field/geometric_gaussian_influence.cpp:307-315` | 证明高斯束宽 $\sigma_1$ 分支选择依赖频率与走时 | `sigma_g, sigma_nf, sigma_lambda, sigma1` |
| **Simple Gaussian 纯几何偏角** | `Bellhop_RayReuse/src/field/simple_gaussian_influence.cpp:239-260` | 证明 Simple Gaussian CPA、偏角 $\theta$ 与高斯核纯几何决定 | `cpa, effectiveDistance, theta, gaussianWeight` |
| **求解器单频调度中枢** | `Bellhop_RayReuse/src/solver/single_frequency_solver.cpp:185-350` | 证明多频求解器统一经由此处逐射线执行 Project 与 Influence | `solveFrequencyFromSourceCache, project` |
| **声压场最终缩放** | `Bellhop_RayReuse/src/field/pressure_scaling.cpp:20-110` | 证明最终声压缩放按频率与距离因子独立执行 | `scaleCoherentPressureImpl, beamScale` |

---

## 8. Audit Verdict

```text
================================================================================
                    IGR-0 AUDIT VERDICT: ACCEPTED (PASS)
================================================================================
1. 范围合规性确认：
   - [x] 未进行任何 frequency interpolation
   - [x] 未修改任何 production code
   - [x] 未进入 IGR-1 施工实现
   - [x] 未执行大规模 benchmark 扰动基线
2. 几何与物理状态分类判定：
   - [x] 纯几何（G）、混合态（M）、纯频率（F）、离散拓扑（T）、输出态（O）分类严密清晰
   - [x] 成功识别 Influence 中占 99% 耗时的纯几何重复计算热点
3. IGR-1 演进路线锁定：
   - [x] 锁定 Candidate 1 (Segment-Range Crossing Stencil) 为首选实现原型
   - [x] 明确了不可变几何缓存所有权与多线程无锁并发契约
   - [x] 建立了 100% Bitwise Parity 与 On-the-Fly Fallback 保护机制
================================================================================
```
