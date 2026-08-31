# IGR-0 — Influence Geometry Reuse Audit Report

> **审计基线：** `main @ ba653229560aac53eb61eeaa5fdb2c2fd3254338`（Audit branch: `feat/igr-influence-geometry-reuse`）
> **审计日期：** 2026-08-30（根据 Codex Re-Final Review 意见完成 Second Remediation 修订）
> **报告状态：** **`READY_FOR_REVIEW`**（整改完成，等待独立最终验收；禁止自封 `ACCEPTED`）
> **性质：** 架构设计、数据流解耦、几何复用边界、所有权生命周期与数值等价性审计；**禁止修改 production code，禁止进入 IGR-1 实现，禁止 frequency interpolation，禁止大规模 benchmark**。
> **Coordinator：** Gemini 3.7 Flash
> **评审输入：** `Bellhop_RayReuse/doc/reviews/IGR0_CODEX_REVIEW_CHANGES_REQUIRED.md`
> **执行 Worklist：** `Bellhop_RayReuse/doc/worklists/IGR-0_INFLUENCE_GEOMETRY_REUSE_AUDIT_WORKLIST.md`

---

## 1. Executive Summary & Baseline Verification

### 1.1 核心审计结论

1. **生产数据流合规性确认：**
   源码核查确认，当前 `Bellhop_RayReuse` 生产数据流严格保持了冻结几何与频率局部状态的单向流动：
   $$\text{Frozen RayPathCache (per-source)} \longrightarrow \text{FrequencyProjector} \longrightarrow \text{RayFrequencyState (frequency-local)} \longrightarrow \text{Influence} \longrightarrow \text{Workspace (frequency-local)}$$
   未将介质衰减、复走时、反射复系数、活动前缀或接收点贡献错误写回 `RayPathCache`。
2. **宽带计算瓶颈与 Amdahl 上限：**
   在多频计算中，每个频率依然重复执行完整的 Influence 场强累加计算。在 16 频 Munk 算例中，Trace / Project / Influence 耗时分别为 `0.405 s / 0.509 s / 279.211 s`，Influence 占据总耗时的 **99.6%** 以上；仅复用 Trace 阶段的理论加速比上限仅为 `1.020×`。
3. **真实热点与遍历特征（基于 F1 Baseline 测量证据）：**
   根据 `REPORT_F1_BASELINE_96F23F8_2026-07-30.md` 的 2 频 Munk 诊断实测（算例规格：10,000 射线累积，201 个 receiver depths，501 个 receiver ranges）：
   - `receiver range evaluations`（跨越次数）：4,972,960 次（单声源 5,000 射线对应 2,486,480 次）；
   - `receiver depth evaluations`：999,564,960 次（$4,972,960 \times 201\text{ depths}$）；
   - `image evaluations`：2,998,694,880 次（$999,564,960 \times 3\text{ images}$）；
   - `window / taper rejections`：2,591,912,648 次（占全部 image 计算的 **86.43%**）；
   - `nonzero image contributions`：406,782,232 次（仅占 **13.57%**）；
   - `validation / precompute / hot loop` 耗时：`0.0002 s / 0.1427 s / 18.4380 s`。

   **纠偏说明：** 接收距离索引查找 `fortranUpperRangeIndex` 在规则均匀网格上为 $O(1)$ 的直接截断与算术 clamp（`std::trunc` + 除法），非二分查找。Influence 的最大绝对耗时位于深度/镜像展开、Hermite taper 评估、复数相位指数计算以及向工作区累加的内层热循环。
4. **IGR-1 首选原型收缩与条件化决策：**
   Candidate 1 严格收缩为 **CC-only per-source Segment–Range Crossing Topology Cache**。冻结 **Candidate 1B（16 字节纯整数拓扑）** 为首选实现原型，**Candidate 1A（64 字节双精度几何）** 降为可选实验变体；保留 **Candidate 6（CC 点几何与梯度投影束）** 作为低风险协同/备选方案。在 IGR-1 启动时，由 measurement-driven 性能实测决定原型采纳决策。

---

## 2. 严密状态变量分类体系（G / M / F / T / O）

本审计确立如下 5 类状态分类标准，彻底纠正早期草案中的物理与声学分类缺陷：

| 类别代码 | 类别名称 | 严密数学定义与物理含义 | 跨频复用与缓存契约 |
|---|---|---|---|
| **G** | **Frequency-Independent Geometry<br/>（纯几何量）** | 仅依赖射线冻结轨迹、接收器网格、边界几何与射线角度，在所有频率完全恒定的量。包括：端点坐标、slowness、实声速 $c$、动态基底 $p_1, p_2, q_1, q_2$、步进积分权重、反射几何正交基、无损介质下的无损走时 $\tau_{real}$、笛卡尔镜像垂直间距 $\Delta z$、GeoHat 线性帽半径 $L$ 与权重 $W_{hat}$、Simple Gaussian CPA 与偏角 $\theta$。 | **无损绝对复用（Exact Cache & Replay）**。<br/>单次构建，多频完全共享只读。 |
| **M** | **Mixed State<br/>（混合状态）** | 同时依赖纯几何量（G）与频率/束宽参数（$\epsilon(f), \omega, \sigma_1(f)$）的复合量。包括：Cerveny $p_{VB}(f) = p_1 + \epsilon p_2$、$q_{VB}(f) = q_1 + \epsilon q_2$、复波束曲率 $\gamma(f)$、几何发散与主常数 $\text{principal} = \text{ratio}\sqrt{c\|\epsilon\|/q}$、波束窗口判定条件（$-\omega \text{Im}(\gamma)\Delta z^2 < \text{BeamWindow}^2$）、Hermite 截断半径 $R_{max} = 30 c_0 / f$、GeoGaussian 等效束宽 $\sigma_1$。 | **逐频精确组合（Exact Cheap Combination）**。<br/>利用冻结基底逐频低成本线性组合，**禁止插值**。 |
| **F** | **Frequency-Dependent State<br/>（纯频率声学量）** | 显式依赖声波频率 $f$ 的声学量。包括：角频率 $\omega = 2\pi f$、**有损介质下的复走时 $\tau_c(f, s)$（其实部与虚部均随频率变化）**、边界复反射系数 $R_j(f)$、介质吸收衰减系数 $\alpha(f,z)$、快速震荡传播相位 $\omega(\tau + t_z \Delta z + \gamma \Delta z^2) - \phi_{refl}$、复指数 $\exp(-i \Phi)$。 | **逐频独立计算（Frequency-Local State）**。<br/>在各频率专属空间维护，禁止跨频污染。 |
| **T** | **Topological / Discrete State<br/>（离散拓扑状态）** | 离散拓扑标记、符号、分支切割、阈值截断掩码。包括：KMAH 符号（$\pm 1$）、GeoHat 焦散跳变（$+\pi/2$）、镜像极性符号、**活动/终止前缀（active prefix）**、GeoGaussian 束宽分支索引、波束窗口与 taper 离散通过掩码。 | **离散精确判断（Discrete Guard）**。<br/>保持 Origin 离散状态机行为，禁止平滑逼近。 |
| **O** | **Output / Accumulation State<br/>（输出累加容器）** | 接收网格场强累加器与最终物理量输出。包括：`FrequencyWorkspace`（复声压场）、`IntensityWorkspace`（声强场）、`ArrivalWorkspace`、`EigenrayHitSink`、最终声压尺度缩放（Pressure Scaling）。 | **输出累加容器（Output Destination Only）**。<br/>各频率内存绝对隔离写入。 |

### 关键声学与物理机制纠正（Codex Findings 闭环）：
1. **`complexTravelTime` 虚实部分类纠偏（Major Finding 1）：**
   在有损介质中，`FrequencyProjector` 沿步长积分 $\int \frac{1}{c(z) + i c_i(z, f)} ds$。其复慢度实部为 $\frac{c}{c^2 + c_i^2(f)}$，虚部为 $\frac{-c_i(f)}{c^2 + c_i^2(f)}$。由于 $c_i(f)$ 随频率变化，**有损介质下复走时的实部和虚部均是频率依赖的（F）**！仅在无损介质（$c_i = 0$）中，走时实部才退化为冻结无损几何量 $\tau_{real}$（G）。
2. **`active / terminal prefix` 与阈值语义精确纠偏（Major Finding 2 & Re-Review）：**
   - **初始状态：** source point 初始为 `active = true`（`frequency_projector.cpp:88-92`）；
   - **Active Cutoff：** $<0.005\text{F}$（`kLegacyActiveAmplitudeThreshold`）是 transition 后对累计 projected amplitude 进行的活动截断判断（`frequency_projector.cpp:61-66`）；
   - **介质吸收：** 介质吸收衰减通过 $\tau_c$ 虚部进入指数项，**不直接修改 `point.active` 标志**；
   - **Suppression Threshold：** $<10^{-5}\text{F}$（`kLegacyCoefficientKillThreshold` in `boundary_acoustics.cpp:15, 169`）**仅是声学半空间（Acoustic Half-Space）分支中对单次 raw reflection coefficient 的抑制阈值**（触发时将 `amplitudeMultiplier` 置 0.0），**不是通用累计反射阈值**；
   - **终端程段进入段：** 当一条射线被截断时，首个 `active == false` 的点作为 legacy `Beam%Nsteps` 终端点保留，以该点为右端点的进入段仍然合法有效；仅当左端点 `!active` 时才抑制后续几何段。

---

## 3. 全波束族状态变量逐项审计表

### 3.1 笛卡尔 Cerveny 波束族（Cartesian Cerveny `CC` — 核心生产路径）

源码位置：`Bellhop_RayReuse/src/field/cartesian_cerveny_influence.cpp`、`include/rayreuse/field/cartesian_cerveny_influence.hpp`、`src/field/beam_epsilon.cpp`。

| 变量 / 表达式 | 源码对应位置 | 物理与算法含义 | 严密分类 | 复用判定与处理要求 |
|---|---|---|---|---|
| `leftRange, rightRange` | `CC:682-683` | 射线程段左右端点水平距离 | **G** | 严格复用，来自 `RayPath::points` |
| `segmentLength` | `CC:689-691` | 程段空间长度及重复点判定 | **G** | 严格复用 |
| `firstUpper, secondUpper` | `CC:708-712` | 射线程段跨越的接收网格索引区间 | **G** | **CC 专用复用候选（Candidate 1）** |
| `weight = (r_rcv - r_l)/(r_r - r_l)` | `CC:725-727` | 接收距离处程段线性插值权重 $W$ | **G** | **CC 专用复用候选（Candidate 1）** |
| `interpolated position (r, z)` | `CC:729-732` | 射线跨越接收距离处的插值坐标 | **G** | **CC 专用复用候选（Candidate 1）** |
| `interpolated slowness (tr, tz)` | `CC:734-737` | 射线跨越处的切向 slowness | **G** | **CC 专用复用候选（Candidate 1）** |
| `interpolated soundSpeed c` | `CC:739-741` | 射线跨越处的无损介质实声速 | **G** | **CC 专用复用候选（Candidate 1）** |
| `tangent, normal` | `CC:238-239` | 射线点切向与法向单位正交基底 | **G** | **点几何束复用候选（Candidate 6）** |
| `alongGradient, normalGradient` | `CC:242-243` | 声速梯度在切向与法向的投影 | **G** | **点几何束复用候选（Candidate 6）** |
| `tangentRange/DepthSquared` | `CC:244-245` | 切向分量平方 $t_r^2, t_z^2$ 与 $c^2$ | **G** | **点几何束复用候选（Candidate 6）** |
| `dynamicP[0..1], dynamicQ[0..1]` | `CC:232-235` | 动态射线追踪 2 组实数基解 | **G** | 严格复用，冻结于 `RayState` |
| `epsilon (ε)` | `BE:14-104` | 束宽参数（SpaceFilling/MinimumWidth/WKB） | **M** | 逐频/逐线通过 `pickBeamEpsilon` 确定 |
| `pVB = p1 + ε*p2, qVB = q1 + ε*q2` | `CC:232-235` | Cerveny 复动态射线变量 | **M** | 逐频由基解与 $\epsilon$ 廉价线性组合（仅 1 次复乘加） |
| `gamma (γ)` | `CC:247-252` | 复波束曲率参数 $\gamma = \frac{1}{2}\left[\frac{p}{q}t_r^2 + \dots\right]$ | **M** | 依赖 $p, q$ 与几何梯度，逐频重算 |
| `interpolated q, gamma` | `CC:743-749` | 接收距离处的 $q, \gamma$ 线性插值 | **M** | 依赖 $W$ 与各点 $q, \gamma$ |
| `principal = ratio * sqrt(c*|ε|/q)` | `CC:755-756` | 几何发散与束宽主常数 | **M** | 逐频计算复平方根 |
| `kmah` | `CC:259, 757-760` | KMAH 焦散拓扑符号（$\pm 1$） | **T** | 跟踪 $q$ 复平面穿过实轴的分支切割，离散更新 |
| `deltaDepth (True, Surface, Bottom)` | `CC:435-447` | 接收点与射线/虚源垂直距离 $\Delta z$ | **G** | 仅依赖 $z_{rcv}, z_{proj}, z_{srf}, z_{bot}$，**纯几何常数** |
| `window = -ω*Im(γ)*Δz^2 < Window^2` | `CC:448-450` | 高斯横向衰减有效截断判定 | **M/T** | 结合几何 $\Delta z^2$ 与频率参数 $-\omega \text{Im}(\gamma)$ |
| `radiusMax = 30*c0 / f` | `CC:409-410` | Hermite 截断过渡区半径 | **M/F** | 显式依赖 $1/f$ |
| `taper = Hermite(Δz, Rmax, 2*Rmax)` | `CC:453-455` | 边界截断平滑因子（0.0～1.0） | **M** | 依赖 $\Delta z$ 与 $R_{max}(f)$ |
| `tau_complex` | `CC:745-748` | 插值复传播时间 $\tau$ | **G (无损) / F (有损)** | 无损介质实部为 G；有损介质实部与虚部均为 F |
| `phaseArgument` | `CC:460-463` | 综合相位 $\omega(\tau + t_z \Delta z + \gamma \Delta z^2) - \phi_{refl}$ | **F** | 目标频率核心相位 |
| `contribution` | `CC:464-467` | 单镜像单接收点复声压增量 | **F** | 复指数计算与累加 |
| `pressureWorkspace` | `CC:819-827` | 复声压场输出容器 | **O** | 写入各频率独立 workspace |

---

### 3.2 其他波束族与组件

- **Ray-Centered Cerveny (`RC`):** 法向向量与投影点为 G，但界面反转拓扑 `image normal flip` 受频率局部 active 前缀影响，**判定为 INVALID**（见 4.4 节）。
- **Geometric Hat (`GeoHat`):** 线性帽半宽 $L = |q_1/q_0|$、法向偏离 $n$、覆盖判定 $|n| < L$ 与线性权重 $W_{hat}$ 均为 G。但完整接收网格模版依赖 receiver ranges 与 depths，在建立完整契约前降级为 `EXPLORATORY / DEFERRED`（见 4.6 节）。
- **Simple Gaussian:** CPA 与偏角 $\theta$ 为 G，但其实际几何图元受 configured step length、launch spacing、closest/off-ray/effective-distance 语义约束，降级为 `EXPLORATORY / DEFERRED`（见 4.6 节）。
- **Frozen Event / Caustic Metadata:** 某些反射/焦散计数原语可能复用，但必须按 frequency-local active prefix 截取，降级为 `EXPLORATORY / DEFERRED`（见 4.6 节）。

---

## 4. 几何复用（IGR）候选方案审查与分类评估

```
+---------------------------------------------------------------------------------------------------+
|                                 IGR Candidate Spectrum (Second Remediation)                       |
+---------------------------------------------------------------------------------------------------+
|  [Candidate 1: IGR-1 PROTOTYPE CANDIDATE]    [Candidate 6: COMPANION]     [Candidate 2: DEFERRED] |
|  CC Segment-Range Crossing Topology Cache    CC Point Geometry Bundle     Bounded Depth Set       |
|  1B (Topology): ~39.8 MB / source            ~107.8 MB / source           MEMORY_NOT_FROZEN       |
|  1A (Double Geom): ~159.1 MB / source (Opt)                                                       |
+---------------------------------------------------------------------------------------------------+
|  [Candidates 3, 7, 8, 9: EXPLORATORY / DEFERRED]                          [Candidates 4, 5]       |
|  Projection Bundle / GeoHat Stencil / Simple Gauss / Event Metadata       INVALID (Formally Void) |
|  All marked MEMORY_NOT_FROZEN, awaiting complete contract                                         |
+---------------------------------------------------------------------------------------------------+
```

---

### 4.1 Candidate 1: CC-only Per-Source Segment–Range Crossing Topology Cache（首选实现候选）

#### 概念与严格范围收缩
- **范围限制：** 严格限定于 **Cartesian Cerveny (`CC`) 生产求解路径**，专用于规则均匀水平距离网格跨越，**禁止扩大到 GeoHat、GeoGaussian 或 Ray-Centered**；非均匀/不规则接收距离网格必须走 existing path / fallback。
- **所有权与生命周期：** 与单个声源的 `RayPathCache` 一一对应，由求解器持有（`std::shared_ptr<const CcSegmentRangeCache>`），纳入多频并行内存预算。
- **双 Schema 变体设计：**
  - **Candidate 1B（首选冻结原型 Schema）：** 16 字节纯整数拓扑记录。存储 `(rangeIndex, leftPointIndex, rightPointIndex, flags)`，重放时由端点 double 即时计算 $W, z_{proj}, t_z, c$；
  - **Candidate 1A（可选实验变体 Schema）：** 64 字节全双精度几何记录。直接预存 double 几何量。

```cpp
namespace rayreuse::field {

// 方案 1B：首选冻结原型 Schema — 紧凑纯整数拓扑记录（16 字节对齐）
struct alignas(16) CcSegmentRangeTopologyRecord {
    uint32_t rangeIndex;             // 接收器水平距离索引 (0 .. nr-1)
    uint32_t leftPointIndex;         // 程段左端点索引 (0 .. 2,000,000)
    uint32_t rightPointIndex;        // 程段右端点索引
    uint32_t flags;                  // 几何与对齐标记
};

// 方案 1A：可选实验变体 Schema — 全双精度几何跨越记录（64 字节对齐）
struct alignas(64) CcSegmentRangeRecordDouble {
    double weight;                   // 线性插值权重 W in [0, 1] (IEEE-754 double)
    double interpolatedDepth;        // 插值垂直深度 z (m)
    double interpolatedSlownessDepth;// 垂直 slowness tz (s/m)
    double interpolatedSoundSpeed;   // 实声速 c (m/s)
    uint32_t rangeIndex;             // 接收器水平距离索引
    uint32_t leftPointIndex;         // 程段左端点索引
    uint32_t rightPointIndex;        // 程段右端点索引
    uint32_t flags;                  // 几何标记
};

// 单声源 CC 几何跨越缓存容器
class CcSegmentRangeCache {
public:
    [[nodiscard]] std::span<const CcSegmentRangeTopologyRecord>
    topologyForRay(std::size_t rayIndex) const noexcept;

    [[nodiscard]] std::size_t totalCrossings() const noexcept;
    [[nodiscard]] std::size_t byteSize() const noexcept;
private:
    std::vector<std::size_t> rayOffsets_; // 使用 std::size_t 防止多射线累计条目数溢出
    std::vector<CcSegmentRangeTopologyRecord> recordsBuffer_;
};

} // namespace rayreuse::field
```

#### 单声源 5k 射线基准内存核算（F1 Population Basis）
- Munk 单声源 5,000 射线对应跨越总次数：$2,486,480$ 次。
- **Candidate 1B（16 字节整数拓扑）：** $2.486 \times 10^6 \times 16\text{ B} \approx \mathbf{39.8\text{ MB / source}}$；
- **Candidate 1A（64 字节双精度几何）：** $2.486 \times 10^6 \times 64\text{ B} \approx \mathbf{159.1\text{ MB / source}}$；
- **多声源与并发说明：** 内存按声源数线性累加，多线程并发工作区内存另行计入，必须受 `selectActiveFrequencyLimit()` 与 `estimatedPeakMemoryBytes` 动态预算控制。

---

### 4.2 Candidate 6: CC Per-Point Geometry & SSP-Gradient Projection Bundle（协同备选候选）

#### 概念与单声源内存核算
在 `precomputeRayValues()`（`CC:220-270`）中预计算每个射线节点的纯几何正交基与环境声速梯度投影。
```cpp
struct alignas(64) CcPointGeometryBundle {
    Vec2 tangent;                   // 16 B
    Vec2 normal;                    // 16 B
    double soundSpeedSquared;       // 8 B
    double alongGradient;           // 8 B
    double normalGradient;          // 8 B
    double tangentRangeSquared;     // 8 B
}; // 64 字节
```
- 单声源 5,000 射线、1,683,973 个射线点基准估算：
  $$1,683,973 \times 64\text{ B} \approx \mathbf{107.8\text{ MB / source}} \quad (\text{纠正早期 215.7 MB 双频累计误算})$$
- **评价：** 纯几何量无精度截断，消除多频下对 SSP 梯度的重复查询，作为 Candidate 1 的低风险协同/备选项。

---

### 4.3 Candidate 2: Bounded Receiver-Depth Candidate Set（重审：DEFERRED / QUESTIONABLE）

- **状态：** **`DEFERRED / QUESTIONABLE`**（标记 **`MEMORY_NOT_FROZEN`**）。
- **依据：** Munk 实际为 201 depths, 501 ranges；深度窗口依赖频率与束宽参数，在未给出解析无漏检证明前不得实施。

---

### 4.4 Candidate 4 & Candidate 5（重审：INVALID）

- **Candidate 4 (Ray-Centered Crossing/Flip):** **`INVALID`**。穿过界面的法向反转状态机受频率局部 active prefix 影响，不能跨频冻结。
- **Candidate 5 (Universal Multi-Family Cache):** **`INVALID`**。多家族几何匹配语义冲突，抽象过度破坏内存局部性。

---

### 4.5 探索性候选方案（Candidates 3, 7, 8, 9: EXPLORATORY / DEFERRED）

由于本轮未建立完整的数据结构、Key、生命周期与失效契约，以下候选统一降级为 **`EXPLORATORY / DEFERRED`**，内存标记为 **`MEMORY_NOT_FROZEN`**：

1. **Candidate 3 (Segment Cartesian Projection Bundle):** 仅作为几何图元参考，未建立完整契约；
2. **Candidate 7 (GeoHat Receiver-Cell Stencil):** 明确该模版**同时依赖 receiver ranges 与 depths**，接收深度/布局变更必须失效重建；
3. **Candidate 8 (Simple Gaussian Depth Kernel):** 明确受现有 Simple Gaussian traversal 中 `configuredStepLength`、`launchSpacing`、closest/off-ray/effective-distance 语义约束；
4. **Candidate 9 (Frozen Event / Caustic Metadata):** 某些事件前缀可能复用，但必须按 frequency-local active prefix 截取，删除“全家族”声明。

---

## 5. 候选方案综合排序与 IGR-1 首发决策

### 5.1 修正后的候选方案综合排序

| 综合排名 | 候选方案编号与名称 | 适用波束族 | 单声源内存基准估算 (5k rays) | 数值等价性设计 | 状态与定位 |
|---|---|---|---|---|---|
| **Top 1** | **Candidate 1: CC-only Segment-Range Crossing Topology Cache** | Cartesian Cerveny (`CC`) | ~39.8 MB (1B 拓扑) / ~159.1 MB (1A 几何) / source | **Designed to preserve bitwise parity** | **VALID / IGR-1 首选原型** |
| **Top 2** | **Candidate 6: CC Per-Point Geometry & SSP-Gradient Bundle** | Cartesian Cerveny (`CC`) | ~107.8 MB / source | **Designed to preserve bitwise parity** | **VALID / 协同备选优化项** |
| **Deferred** | **Candidate 2: Bounded Receiver-Depth Candidate Set** | Cartesian Cerveny (`CC`) | `MEMORY_NOT_FROZEN` | 存在漏检风险，需包络证明 | **DEFERRED / QUESTIONABLE** |
| **Exploratory** | **Candidate 7: GeoHat Receiver-Cell Stencil** | Geometric Hat | `MEMORY_NOT_FROZEN` | 依赖 ranges + depths | **EXPLORATORY / DEFERRED** |
| **Exploratory** | **Candidate 8: Simple Gaussian Depth Kernel Cache** | Simple Gaussian | `MEMORY_NOT_FROZEN` | 受 traversal 语义约束 | **EXPLORATORY / DEFERRED** |
| **Exploratory** | **Candidate 3: Segment Cartesian Projection Bundle** | GeoHat / GeoGauss | `MEMORY_NOT_FROZEN` | 未建立完整契约 | **EXPLORATORY / DEFERRED** |
| **Exploratory** | **Candidate 9: Frozen Event / Caustic Metadata** | A / E 路径 | `MEMORY_NOT_FROZEN` | 需按 active 前缀截取 | **EXPLORATORY / DEFERRED** |
| **Invalid** | **Candidate 4: Ray-Centered Crossing/Flip Stencil** | Ray-Centered | N/A | **破坏拓扑一致性** | **正式作废 (INVALID)** |
| **Invalid** | **Candidate 5: Universal Multi-Family Cache** | 全家族 | N/A | 破坏局部性与紧凑性 | **正式作废 (INVALID)** |

---

### 5.2 IGR-1 首个 Prototype 决策框架（Measurement-Driven）

在 IGR-1 阶段，采用 **Measurement-Driven 决策流程**：

```mermaid
flowchart TD
    START[IGR-1 Phase Start] --> PROBE[Step 1: Benchmark Crossing/Lerp Overhead vs Precompute vs Inner Loop]
    PROBE --> DECIDE{Crossing / Lerp Overhead Significant?}
    DECIDE -->|YES| PROTO1B[Implement Candidate 1B (16B Topology)<br/>Compare with 1A (64B Geom) if viable]
    DECIDE -->|NO| PROTO6[Implement Candidate 6<br/>CC Point Geometry & SSP-Gradient Bundle]
    PROTO1B --> VERIFY[Step 2: Bitwise Byte-Identical Parity Verification]
    PROTO6 --> VERIFY
    VERIFY --> BENCH[Step 3: Munk 16F End-to-End Wall-Time Protocol Verification]
```

#### 必备集成与回退设计：
1. **单声源绑定：** 与对应 `RayPathCache` 一一对应，生命周期绑定于 `Serial/ParallelRayReuseSolver`；
2. **完整 Cache Key：** 严格校验 `RayPathCache::contentFingerprint()`、`receiverRanges` 布局指纹、CC 遍历语义版本号；
3. **保留 On-the-Fly 回退通道：** 求解器保留 `useGeometryReuse` 配置开关作为 recovery mechanism，当未命中或校验失败时自动回退，确保零破坏。

---

## 6. 缓存失效条件、并发安全性与数值等价契约

### 6.1 缓存失效与重建触发条件（Cache Invalidation Triggers）

| 触发场景 | 变化输入项 | 处理行为 | 风险与失效结论 |
|---|---|---|---|
| **接收器水平距离变更** | `receivers.ranges()` 发生变化 | 跨越区间与插值几何完全改变，必须彻底重建 | **必须失效** |
| **接收器深度网格变更** | 仅 `receivers.depths()` 发生变化 | Candidate 1/6 完全有效；Candidate 2/7 必须失效 | 针对 Candidate 2/7 失效 |
| **声源深度/位置变更**| `sourceDepth` 改变 | 射线束重新追踪，所有派生缓存必须重建 | **必须失效** |
| **环境声速/边界变更**| 介质 SSP 或海面/海底几何变更 | 射线束重新追踪，所有派生缓存必须重建 | **必须失效** |
| **频率变更（多频扫描）**| $f_1 \to f_2$（宽带批次计算） | **Candidate 1/6 完全有效，严禁重建！** | **核心复用，禁止失效** |
| **束宽参数模式变更** | `BeamWidthMode` 或 $\epsilon$ 乘子变更 | Candidate 1/6 完全有效；仅影响逐频 $p/q/\gamma$ 组合 | 模版保持有效 |

---

### 6.2 多线程并发安全性契约（Parallel Concurrency Contract）

针对 `ParallelRayReuseSolver`（多频 OpenMP / 线程池并发），确立如下并发契约：
1. **只读不可变性（Immutable Read-Only）：** 几何缓存由主线程单次构建并在多频循环前冻结，多线程仅持有 `const` 引用并发读取；
2. **零锁零原子争用（Zero Lock / Atomic Contention）：** 各工作线程通过 `std::span` 读取连续内存记录，全生命周期无任何互斥锁或原子同步；
3. **工作区绝对内存隔离：** 各线程独占各自频率的 `FrequencyWorkspace` 和 `RayFrequencyState` 内存，杜绝 False Sharing；
4. **动态内存预算受控：** 几何缓存大小纳入 `selectActiveFrequencyLimit()` 与 `estimatedPeakMemoryBytes` 计算，避免多频并发内存溢出。

---

### 6.3 严格 Bitwise / Parity 数值契约

1. **消除人为精度截断：** 模版存储与重放一律采用 IEEE-754 `double`（binary64），禁止使用 `float`；
2. **累加顺序严格确定性：** 模版遍历重放严格保持 `rayIndex = 0 .. N-1`、`segmentIndex = 2 .. N-1` 的升序累加，确保浮点加法结合律顺序与基准一致；
3. **活动前缀与终端点保护：** 重放循环中必须严格复核 `frequencyState.points[leftIndex].active`，保留首个 inactive terminal 点的进入段贡献。

---

## 7. 源码证据链索引表（Source Code Evidence Index）

| 模块 / 路径 | 文件路径与关键行号 | 审计核心证据 | 涉及变量与函数 |
|---|---|---|---|
| **CC 距离查找与截断** | `Bellhop_RayReuse/src/field/cartesian_cerveny_influence.cpp:90-103, 708-723` | 证明 `fortranUpperRangeIndex` 为 $O(1)$ 算术计算，非二分查找 | `fortranUpperRangeIndex, rawIndex, firstUpper` |
| **CC 几何线性插值** | `Bellhop_RayReuse/src/field/cartesian_cerveny_influence.cpp:725-742` | 证明插值权重 $W$、位置、slowness、实声速纯由几何决定 | `weight, position, slowness, soundSpeed` |
| **CC 射线点几何投影** | `Bellhop_RayReuse/src/field/cartesian_cerveny_influence.cpp:236-246` | 证明切向/法向、声速平方、梯度投影纯由几何决定（Candidate 6） | `tangent, normal, alongGradient, normalGradient` |
| **有损介质复走时积分** | `Bellhop_RayReuse/src/field/frequency_projector.cpp:177-192` | 证明有损介质中复走时实部与虚部均随频率变化 | `complexTravelTime, SoundSpeedSample, imaginarySoundSpeed` |
| **活动前缀阈值截断** | `Bellhop_RayReuse/src/field/frequency_projector.cpp:61-66, 88-92` | 证明 source point 初始 active，$<0.005$ 为累计振幅 active cutoff | `amplitudeRemainsActive, kLegacyActiveAmplitudeThreshold` |
| **声学半空间反射抑制** | `Bellhop_RayReuse/src/acoustics/boundary_acoustics.cpp:15, 160-179` | 证明 $<10^{-5}$ 仅为声学半空间单次 raw 反射系数抑制阈值 | `kLegacyCoefficientKillThreshold, classifyBoundaryCoefficient` |
| **并发内存预算调度** | `Bellhop_RayReuse/src/solver/parallel_ray_reuse_solver.cpp:95-125` | 证明 `selectActiveFrequencyLimit` 负责峰值内存预算控制 | `selectActiveFrequencyLimit, estimatedPeakBytes` |
| **F1 基准诊断实测数据** | `Bellhop_RayReuse/doc/archive/benchmarks/REPORT_F1_BASELINE_96F23F8_2026-07-30.md:65-80` | 证明 2 频 Munk 共有 4.97M range (单声源 2.486M), 999.6M depth, 2.999B image calls | `receiver range/depth/image evaluations` |

---

## 8. Codex Findings 整改闭环矩阵

| Finding ID | 严重级别 | 缺陷描述 | 整改落实与报告位置 | 闭环状态 |
|---|---|---|---|---|
| **C1** | **Critical** | Candidate 1/2 使用 `uint16` 索引并降为 `float`，破坏 bitwise parity | 废除 `uint16`/`float`，Schema 升级为 `uint32_t` 与 IEEE-754 `double`；以单声源 5k 射线基准重新核算内存（1B ~39.8 MB, 1A ~159.1 MB per source）；见 4.1 节。 | **CLOSED** |
| **M1** | **Major** | `complexTravelTime` 分类错误（有损实部随频率变化） | 修正分类：有损介质实部与虚部均为 F；无损实部为 G；见 2 节说明。 | **CLOSED** |
| **M2** | **Major** | `active/terminal prefix` 机制与阈值语义描述不准 | 纠正为：source point 初始 active，$<0.005$ 为累计振幅 active cutoff，衰减不改 active，$<10^{-5}$ 仅为半空间单次 raw 反射抑制阈值；见 2 节。 | **CLOSED** |
| **M3** | **Major** | 热点与收益结论缺少证据（`fortranUpperRangeIndex` 为 $O(1)$ 非二分查找） | 纠正为 $O(1)$ 查找；删除所有虚假 3–6x 加速比与固定 $\ge 10\%$ 门槛，改为 measurement-driven；见 1.1 节及 5.2 节。 | **CLOSED** |
| **M4** | **Major** | Munk 维度写反（实际 201 depths, 501 ranges） | 纠正 Munk 维度为 201 depths, 501 ranges；废除基于 501 深度推导的收益；Candidate 2 降为 DEFERRED；见 1.1 节及 4.3 节。 | **CLOSED** |
| **M5** | **Major** | Candidate 1 跨波束族适用性被夸大 | 严格收缩 Candidate 1 范围为 CC-only per-source Segment–Range Stencil；非均匀网格走 existing fallback；见 4.1 节。 | **CLOSED** |
| **M6** | **Major** | Cache key、ownership、invalidation 生命周期不完整 | 补充 per-source 求解器持有模型、完整 Fingerprint 绑定与失效矩阵，纳入 `selectActiveFrequencyLimit` 预算；见 4.1 节、6.1 节及 9.2 节。 | **CLOSED** |
| **M7** | **Major** | Candidate 4 射线中心法向反转拓扑不能跨频共享 | 将 Candidate 4 方案正式标记为 **INVALID** 并详述原因（active prefix 影响 flip parity）；见 4.4 节。 | **CLOSED** |
| **M8** | **Major** | 遗漏 4 个关键几何复用候选并缺乏收缩 | 增补 Candidate 6（实测 ~107.8 MB/source）；将 Candidate 3/7/8/9 降为 `EXPLORATORY / DEFERRED` 并标记 `MEMORY_NOT_FROZEN`；见 4.2 节与 4.5 节。 | **CLOSED** |
| **M9** | **Major** | 缺少 IGR-0 Worklist 与 Batch Acceptance Gates；自封 ACCEPTED | 建立 `IGR-0_WORKLIST.md`，确立 8 项 IGR-0 门禁与 13 项冻结的 IGR-1 门禁，状态保持 `READY_FOR_REVIEW`；见 9 节。 | **CLOSED** |
| **m1** | **Minor** | 行号与当前源码漂移 | 对照当前工作树源码全面校准全部行号；见第 3 节与第 7 节。 | **CLOSED** |
| **m2** | **Minor** | 8 处 git diff trailing whitespace | 全文清理行尾空白字符，确保 `git diff --check` 干净。 | **CLOSED** |
| **m3** | **Minor** | `rayOffsets_` 索引上限保护 | 声明全局偏移必须使用 `std::size_t` 避免跨射线总条数溢出；见 4.1 节。 | **CLOSED** |

---

## 9. Batch Acceptance Gates（验收门禁契约）

### 9.1 IGR-0 阶段审计门禁（Audit Gates — 本阶段验证）

| 门禁 ID | 门禁名称 | 验收标准 | 验证结果 |
|---|---|---|---|
| **GATE-1** | **无生产代码修改门禁** | `git diff -- Bellhop_RayReuse/src Bellhop_RayReuse/include` 为空。 | **PASS**（0 行生产代码修改） |
| **GATE-2** | **无频域插值门禁** | 明确禁止在 IGR 中引入任何频域插值或快速相位近似。 | **PASS**（严格全频精确计算） |
| **GATE-3** | **状态分类准确门禁** | Lossy/Lossless 走时、Active 前缀、Threshold 语义严格符合源码。 | **PASS**（已纠正 M1/M2） |
| **GATE-4** | **Schema 精度与范围门禁** | 索引支持 2,000,000 点（`uint32_t`），几何浮点量保留 `double`。 | **PASS**（已消除 uint16/float） |
| **GATE-5** | **Cache Key 与所有权门禁** | 明确 per-source 生命周期、求解器持有模型、完整的 Cache Invalidation 条件。 | **PASS**（已建立完整契约） |
| **GATE-6** | **无效候选剔除门禁** | Candidate 4 与 Candidate 5 正式标记为 INVALID，Candidate 3/7/8/9 降为 EXPLORATORY。 | **PASS**（已作废并详述原因） |
| **GATE-7** | **证据与热点真实性门禁** | 修正 $O(1)$ 查找、Munk 维度与 5k-ray population accounting，引用 F1 实测数据。 | **PASS**（已纠偏并引用诊断数据） |
| **GATE-8** | **报告状态合规门禁** | 报告状态标记为 `READY_FOR_REVIEW`，附带 Codex 闭环矩阵。 | **PASS**（附完整闭环矩阵） |

---

### 9.2 冻结的 IGR-1 原型实施验收门禁（Frozen IGR-1 Prototype Acceptance Gates — 冻结待实现）

> **说明：** 以下 13 项门禁为 IGR-1 原型实现阶段的**预冻结验收标准**。本轮为只读审计，不包含代码实现，故当前状态标记为 **`FROZEN_FOR_IGR1`（待实施验证）**，严禁伪造 PASS。

| 门禁 ID | 门禁领域 | IGR-1 验收判定标准 | 状态 |
|---|---|---|---|
| **IGR1-GATE-01** | **Lossy/Lossless Complex Travel Time** | 无损走时正确复用实部；有损介质复走时严格按频率逐点重新积分，实部与虚部均不得从冻结几何走时污染。 | `FROZEN_FOR_IGR1` |
| **IGR1-GATE-02** | **Source / Reflection Threshold** | 1. source point 初始 active；<br/>2. 严格执行累计 projected amplitude $<0.005\text{F}$ 的 active cutoff；<br/>3. 衰减不直接修改 active；<br/>4. $<10^{-5}\text{F}$ 仅在声学半空间分支中作为单次 raw reflection coefficient 的抑制阈值（置 0），不得扩大为通用累计反射阈值。 | `FROZEN_FOR_IGR1` |
| **IGR1-GATE-03** | **First Inactive Terminal Segment** | 严格保持 legacy `Beam%Nsteps` 语义：以首个 inactive terminal 点为右端点的进入段必须正常参与场强贡献。 | `FROZEN_FOR_IGR1` |
| **IGR1-GATE-04** | **Regular Receiver Grid** | 规则等间隔水平网格下，重放路径与 on-the-fly 路径产生完全相同的接收距离跨越集合。 | `FROZEN_FOR_IGR1` |
| **IGR1-GATE-05** | **Irregular Receiver Layout & Fallback** | 1. Candidate 1 native 契约仅覆盖当前 CC 支持范围（rectilinear grid、uniform receiver ranges、已支持的 depth/layout variants）；<br/>2. non-uniform receiver ranges 不属于 Candidate 1 重放对齐域，必须显式走 existing path / fallback，严禁静默进入 cache replay。 | `FROZEN_FOR_IGR1` |
| **IGR1-GATE-06** | **C / I / S Coherence Modes** | Coherent (C)、Incoherent (I)、Semicoherent (S) 模式下，重放路径分别正确累加至复声压或声强工作区。 | `FROZEN_FOR_IGR1` |
| **IGR1-GATE-07** | **Multi-Source Isolation** | 多声源场景下，几何缓存严格按声源隔离（per-source），不同声源的缓存不相互覆盖、不产生竞态。 | `FROZEN_FOR_IGR1` |
| **IGR1-GATE-08** | **Serial / Reuse / Parallel** | Serial RayReuse、Parallel RayReuse 以及 Single Frequency 模式下，重放输出完全一致且并发无锁安全。 | `FROZEN_FOR_IGR1` |
| **IGR1-GATE-09** | **Fallback Path Parity** | 求解器在 `useGeometryReuse = false` 或缓存校验不通过时，自动回退到 on-the-fly 路径作为 recovery mechanism，且输出与开启缓存完全一致（需 parity 验证，不预设 zero risk）。 | `FROZEN_FOR_IGR1` |
| **IGR1-GATE-10** | **Byte-Identical Parity** | 对照基线 `main @ ba653229560aac53eb61eeaa5fdb2c2fd3254338`，在所有适用 CC SHD 标准测试用例上，重放输出与基线保持逐字节一致（SHA-256 对齐）；ARR/E/RAY 产品继续使用各自既有 regression/oracle 门禁。 | `FROZEN_FOR_IGR1` |
| **IGR1-GATE-11** | **Peak & Parallel Memory Budget** | 1. 必须实测并记录：cache bytes/source、多声源 total cache bytes、frequency-local workspace bytes 与 parallel peak RSS；<br/>2. 几何缓存 bytes 必须显式纳入 `selectActiveFrequencyLimit()` 与 `estimatedPeakMemoryBytes`；<br/>3. 预算必须综合考虑：shared per-source geometry caches + active-frequency workspaces + downstream multi-source output + bounded overhead；<br/>4. 严禁出现未受控的 $O(N_{source} \times N_{frequency})$ 几何缓存复制；<br/>5. 几何缓存保持 per-source shared read-only；cache-off/nonreuse 模式不构建几何缓存；<br/>6. active-frequency workspace 按声源隔离且顺序稳定；<br/>7. 预算门禁必须依据实测 RSS 判定，严禁仅按结构体理论大小宣称 PASS。 | `FROZEN_FOR_IGR1` |
| **IGR1-GATE-12** | **Origin / F2CPP Oracle** | 全部标准算例继续通过 Origin 科学 Oracle 与 F2CPP 生产对齐测试套件。 | `FROZEN_FOR_IGR1` |
| **IGR1-GATE-13** | **Munk Wall-Time / Prototype Viability** | 1. 必须测量：geometry-cache build time、per-frequency influence time、2F / 16F end-to-end wall time、cache-on vs cache-off、Candidate 1A vs 1B（若均实现）；<br/>2. **Benchmark 协议：** 固定机器/环境配置、排除预热、$\ge 5$ 次重复计量、报告中位数与分散度、条件允许时交替运行、保持相同构建/线程/输入设置；<br/>3. **PASS 条件：** 不允许降低数值正确性；在包含几何缓存构建耗时的前提下，16-frequency 代表性负载必须表现出**超出运行噪声的可重复正向 end-to-end wall-time 净加速收益**；<br/>4. 若净收益不成立，该原型判定为 `NOT_VIABLE`，严禁仅因已完成代码编写而强行合入 production 路径。 | `FROZEN_FOR_IGR1` |

---

## 10. Audit Verdict

```text
================================================================================
                    IGR-0 AUDIT STATUS: READY_FOR_REVIEW
================================================================================
1. 范围与约束遵从性：
   - [x] 未进行任何 frequency interpolation
   - [x] 未修改任何 production code
   - [x] 未进入 IGR-1 施工实现
   - [x] 未执行任何大规模 benchmark 扰动基线
2. Codex 评审整改闭环（Second Remediation）：
   - [x] Threshold 语义精确对齐源码（source 初始 active，0.005 累计 cutoff，1e-5 半空间抑制）
   - [x] 候选方案彻底收缩：Candidate 1B (首选) / 1A (变体)；Candidate 6 (备选)；Candidate 2 (Deferred)；
         Candidate 4/5 (Invalid)；Candidate 3/7/8/9 降为 EXPLORATORY / DEFERRED (MEMORY_NOT_FROZEN)
   - [x] F1 Population 会计纠正（单声源 5k 射线基准：1B ~39.8 MB, 1A ~159.1 MB, 6 ~107.8 MB）
   - [x] 冻结 IGR1-GATE-05 (非均匀 fallback)、IGR1-GATE-10 (CC SHD 范围)、IGR1-GATE-11 (内存预算调度) 与
         IGR1-GATE-13 (Benchmark 严格协议与净收益判据)
================================================================================
```
