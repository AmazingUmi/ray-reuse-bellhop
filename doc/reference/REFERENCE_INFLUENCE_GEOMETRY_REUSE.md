# Influence Geometry Reuse 理论与数值契约

> **状态：HISTORICAL IGR-1 THEORY BASELINE — SUPERSEDED FOR FUTURE SCOPE。**
> 本文保留 IGR-1 的理论分解、数值顺序与验收语义，不代表当前 Batch 状态或
> IGR-3 support boundary。IGR-1/IGR-2 已 `ACCEPTED / CLOSED`；当前
> user-frozen IGR-3 direction 以
> [`IGR-3_SCOPE_AND_ARCHITECTURE_DECISION.md`](../../Bellhop_RayReuse/doc/worklists/IGR-3_SCOPE_AND_ARCHITECTURE_DECISION.md)
> 为准。

## 1. 适用范围与生产基准

IGR-1 当时的首个拟议范围是 TL、Cartesian Cerveny、coherent pressure、rectilinear / uniform receiver ranges、single source、shared frozen fan、all-frequency fusion、serial reference path。本文描述的 exact loop hierarchy 只对该历史范围作冻结；其他 beam family、parallel、multisource、frequency interpolation、SIMD、GPU 均不由本文授权。

生产基准事实：

- `RayPathCache` 是 immutable、frequency-independent 的 frozen geometry；schema 不变；
- `FrequencyProjector::project()` 仍逐 ray、逐 frequency 精确生成 `RayFrequencyState`；
- 当前 Cartesian Cerveny 累加顺序是 ray → segment → range → depth → image；
- coherent Cartesian pressure 在 Influence 之后执行逐频 scaling，scale 含 `sqrt(frequency)`；
- writer 只消费已经 scale 的 per-frequency workspace。

源码锚点：`serial_ray_reuse_solver.cpp:29-99`、`single_frequency_solver.cpp:181-393`、`frequency_projector.cpp:93-203`、`cartesian_cerveny_influence.cpp:294-488, 633-837`、`pressure_scaling.cpp:25-106`、`shd_writer.cpp:138-224`。

## 2. Exact geometry / frequency-state decomposition

本文采用 **Option C：Exact geometry / frequency-state decomposition**，不再宣称对所有有损介质存在一个 universal multiplicative “Exact Factorization Theorem”。原因是 production 的有损复走时

$$
\tau_c(f)=\int \frac{1}{c+i c_i(f)}\,ds
$$

的实部和虚部都可能随频率变化。几何可以精确共享，但频率状态仍须逐频精确构造。

无损时，$c_i=0$，有

$$
\tau_c=\tau_{\rm real},\qquad
e^{-i\omega\tau_c}=e^{-i\omega\tau_{\rm real}}.
$$

有损时，如以 frozen lossless 几何走时 $\tau_{\rm real}$ 为参考，精确恒等式为

$$
e^{-i\omega\tau_c(f)}
=e^{-i\omega\tau_{\rm real}}
\,e^{-i\omega[\Re\tau_c(f)-\tau_{\rm real}]}
\,e^{\omega\Im\tau_c(f)}.
$$

后两个因子是 complex frequency-local correction，不能冻结到 geometry cache。实现上的 authoritative gate 是同一 binary / compiler / FP 环境中的 intermediate 和 final parity 验证，而不是代数措辞本身。

## 3. Cartesian Cerveny 精确边界

### 3.1 可在 frequency loop 外计算的量

在固定 source、ray、segment、crossed receiver range、receiver depth、image 上，下列量是 frequency-independent geometry：

- segment crossing topology：退化段过滤、range 越界、`firstUpper / secondUpper`；
- interpolation weight

  $$W=\frac{r_{\rm receiver}-r_L}{r_R-r_L};$$

- interpolated position、slowness、real sound speed；
- receiver depth 与插值 ray depth 的相对几何；
- image 的 $\Delta z$、$\Delta z^2$ 与 polarity；
- frozen dynamic-ray bases `p1/p2/q1/q2`、path geometry、reflection events 与 quadrature。

这些值由一次 receiver geometry traversal 生成，并被块内频率立即消费；v1 不将其持久化为 `Nray × Nsegment × Nreceiver` 结构。

### 3.2 必须保留在 frequency-local 层的量

下列量必须逐频精确求值：

- `RayFrequencyState[f]`：complex travel time、source/reflection amplitude evolution、reflection phase、active prefix；
- epsilon(f)；
- `p(f)=p1+epsilon(f)p2`、`q(f)=q1+epsilon(f)q2`、gamma(f)、KMAH/branch state；
- 用共享 W 对 q(f)、tau(f)、gamma(f) 的插值；
- principal 与 corrected principal；
- `gamma.imag() > 0` guard；
- window、Hermite taper、phase、complex exponential；
- image contribution、per-frequency `imageSum[f]` 与 `workspace[f]` 累加。

IGR 消除的是 geometry duplication，不是 frequency physics。

### 3.3 Cartesian Cerveny image 的符号

`evaluateImageContribution()` 的 production 定义为：

| Image | $\Delta z$ | polarity |
|---|---:|---:|
| True | $z_r-z$ | $+1$ |
| Surface | $-z_r+2z_{\rm surface}-z$ | $-1$ |
| Bottom | $-z_r+2z_{\rm bottom}-z$ | $+1$ |

这里不能只复用 $\Delta z^2$ 而忽略符号，因为相位包含线性项

$$
t_z\Delta z
$$

且最终 contribution 还乘以 polarity。对每个 image，frequency-local kernel 为：

$$
\begin{aligned}
w_{\rm metric}(f)&=-\omega(f)\Im\gamma(f)\Delta z^2,\\
\phi(f)&=\omega(f)[\tau(f)+t_z\Delta z+\gamma(f)\Delta z^2]-\phi_{\rm refl}(f),\\
\delta u(f)&={\rm polarity}\,A(f)\,T(f,\Delta z)\,e^{-i\phi(f)}.
\end{aligned}
$$

window rejection、taper rejection 和 exponential 都属于 frequency kernel。

## 4. 其他家族的分类纠正

这些公式用于防止今后误分类，不扩大 IGR-1 scope。

### 4.1 Simple Gaussian

Simple Gaussian 不是“effective distance = receiver range”或“CPA = simple depth difference”。当前 production 在 segment/range crossing 上计算：

$$
\begin{aligned}
\Delta z &= z_r-z_{\rm interp},\\
d_{\rm segment} &= \sqrt{(r_R-r_L)^2+(z_R-z_L)^2},\\
d_{\rm closest} &= \frac{|\Delta z(r_R-r_L)|}{d_{\rm segment}},\\
d_{\rm offray} &= \sqrt{\Delta z^2-d_{\rm closest}^2},\\
s_{\rm legacy} &= (i_R+W)\,\Delta s_{\rm configured},\\
d_{\rm effective} &= s_{\rm legacy}+d_{\rm offray},\\
\theta_{\rm offset} &= \arctan\!\left(\frac{d_{\rm closest}}{d_{\rm effective}}\right).
\end{aligned}
$$

`closestPointDistance`、`offRayDistance`、`legacyArcLength`、`effectiveDistance` 与 `angularOffset` 均须按这些 production 公式分类；phase 仍逐频。

### 4.2 Geometric Gaussian

GeoGaussian 的 near-field sigma 使用

$$
\sigma_{\rm near}(f)=0.2\,f\,\Re\tau_c(f).
$$

production 直接读取 `RayFrequencyState::complexTravelTime.real()`。因此有损情况下它是 frequency-local；不能用 frozen `tau_real` 替代。`wavelengthSigma = pi*c/f`、最终 sigma、window membership、delay、phase 也都是 frequency-local。

## 5. Active-prefix 契约

production 语义冻结为：

1. source point 初始 `active = true`；
2. source amplitude 和 frequency-local reflection multiplier 决定累计 projected amplitude；
3. `<0.005F` 是累计 projected amplitude 的 active cutoff；
4. active 单调从 true 变为 false；
5. first inactive terminal point 被保留；以它为右端点的 segment 仍可贡献；
6. 仅当 segment 左端点 inactive 时，该 frequency 的后续 suffix 被抑制；
7. volume attenuation 通过 complex travel time 进入场贡献，不直接修改 active flag。

因此：

> Per-frequency active-prefix differences arise from frequency-local source/reflection amplitude evolution and the cumulative active cutoff, not directly from volume attenuation.

对一个 frequency block，定义

$$
\text{unionPrefix}=\max_f \text{reachablePointCount}(f).
$$

共享 segment traversal 走到 `unionPrefix`；但每个频率在每个 segment 上仍执行等价于

```cpp
if (!frequencyState[f].points[leftIndex].active) {
  continue;
}
```

的独立 guard。禁止使用 reference frequency prefix。

## 6. 最终融合循环与生命周期

### 6.1 唯一权威 hierarchy

Cartesian Cerveny v1 冻结以下结构。frequency 有两个 frequency-local 消费点：range-local beam-state preparation，以及 image 内完整 kernel；两者都位于 ray traversal 内部。

```text
Trace source fan once
        ↓
Frozen RayPathCache
        ↓
for source                                      // v1: one source
    allocate raw workspace[f = 0 .. Nf-1]       // long-lived

    for ray
        for frequency
            project exact RayFrequencyState[f]
            compute epsilon[f]
            precompute p/q/gamma/kmah[f]

        unionPrefix = max reachable prefix over frequencies

        for segment in ascending order
            build per-frequency active mask from left endpoint
            if no frequency is active: continue

            shared crossing topology

            for crossed receiver range in ascending order
                shared W
                shared interpolated position/slowness/real c
                rangeEligible[f] = activeMask[f]

                for frequency where rangeEligible[f]
                    interpolate q[f]/tau[f]/gamma[f]
                    if gamma[f].imag() > 0
                        rangeEligible[f] = false
                        continue
                    compute principal/corrected[f]

                if no frequency is rangeEligible: continue receiver range

                for receiver depth in ascending order
                    initialize imageSum[f] = 0 for rangeEligible frequencies

                    for image in True → Surface → Bottom order
                        shared Delta-z / Delta-z-squared / polarity

                        for frequency where rangeEligible[f]
                            window
                            taper
                            phase
                            exponential
                            image contribution
                            imageSum[f] += image contribution

                    for frequency where rangeEligible[f]
                        contribution = corrected[f] * imageSum[f]
                        workspace[f] += contribution

    for frequency
        scaleCoherentCartesianPressure(workspace[f])
        consumer / writeFrequency(f)
```

选择 `image → frequency` 而不是 `frequency → image`，是为了让 $\Delta z/\Delta z^2$/polarity 每个 image 只生成一次。`activeMask[f]` 表示 segment 左端点状态；每个 crossed range 都从它重新初始化 `rangeEligible[f]`，再对该频率的插值 `gamma.imag()>0` 清零 range eligibility。只有 `rangeEligible[f]` 为真的频率才初始化/使用 `corrected[f]`、进入 depth/image kernel 并写入 workspace；若全部频率在该 range 被拒绝，则直接跳过该 range。瞬时 `imageSum[f]` 保持每个 eligible 频率内部的 True → Surface → Bottom addition order；depth 完成后仍只对该频率 workspace 做一次 addition，与 production 一致。

### 6.2 生命周期

```text
Trace
→ Frozen RayPathCache
→ per-ray multi-frequency projection
→ fused Influence accumulation into raw workspace[f]
→ per-frequency Pressure Scaling
→ consumer
→ SHD write
```

计时必须至少区分 Trace、Project、Influence、Scale 与 consumer/write；不得把 Scale 混入 write，也不得省略 `scaleSeconds`。

## 7. Conditions designed to preserve bitwise parity

以下是为保持 bitwise parity 设计的条件，不称为“充分必要定理”；implementation gate verification 才是 authoritative：

- 固定 frequency f 观察到的 ray 顺序不变：ray0 → ray1 → ray2；
- 每 ray 内 segment → range → depth → image 顺序不变；
- image 内 True → Surface → Bottom 的 `imageSum[f]` 加法顺序不变；
- 每个 depth 对 `workspace[f]` 的 addition 次数与位置不变；
- W、position、slowness、real c、Delta-z 等共享几何保持相同输入、类型与 operation sequence；
- q/tau/gamma interpolation、principal、window、taper、phase、exp、contribution 保持现有 per-frequency operation sequence；
- 不引入 tree reduction、ray-local field reduction、thread-local reduction、SIMD reassociation、depth/image reorder。

物理循环从 frequency-major 改为 ray-major 不会改变固定 f 的 contribution 投影序列：其他频率的运算写入不同 workspace，不在 `workspace[f]` 的 addition stream 中。

### 7.1 四级数值门禁

- **Level A — Frozen cache integrity:** `RayPathCache` fingerprint before == after。
- **Level B — Unscaled workspace parity:** 对每个 frequency，在同一 binary / compiler / FP 环境下，raw fused `workspace[f]` 与 raw current-reuse `workspace[f]` bitwise identical。
- **Level C — Scaled workspace parity:** 逐频执行 Pressure Scaling 后，scaled fused `workspace[f]` 与 scaled current-reuse `workspace[f]` bitwise identical。
- **Level D — Final product parity:** `SHA256(fused SHD) == SHA256(reference reuse SHD)`。

若 intermediate workspace gate 在施工时无法暴露，必须记录具体工程 blocker 并进入 design review；不得静默跳过 Level B/C 而只比较 SHD。

## 8. Counter model

### 8.1 Geometry-side counters

- `geometrySegmentEvaluations`：共享 segment candidate / eligibility geometry 求值次数；
- `geometryRangeEvaluations`：共享 crossed receiver-range topology 与 W/position/slowness/c 求值次数；
- `geometryDepthEvaluations`：共享 receiver-depth geometry 单元次数；
- `geometryImageGeometryEvaluations`：共享 image 的 Delta-z/Delta-z-squared/polarity 构造次数。

在相同 active-union work、理想 `Bf=Nf` 且 prefix 差异不主导时，这些计数相对 frequency-major baseline 应显著接近 baseline / Nf。它们不是对所有输入的严格等式，因为 union prefix 可能长于某些 frequency 自身 prefix。

### 8.2 Frequency-kernel counters

- `frequencyRangeKernelEvaluations`：某个已构造 range geometry 在一个 active frequency 上执行 q/tau/gamma interpolation、principal 与 range-local guards 的次数；
- `frequencyImageKernelEvaluations`：对已构造的 receiver/image geometry，在某一 frequency 上执行完整 frequency-local Cerveny image kernel 的次数；
- `windowRejections`：frequency image kernel 被 window 拒绝次数；
- `taperRejections`：通过 window 后被 taper 拒绝次数；
- `nonzeroImageContributions`：产生非零 image contribution 的次数。

这些 frequency-kernel 计数通常不会降低到 baseline / Nf，因为 frequency physics 仍逐频精确求值。禁止再把旧 `imageEvaluations` 同时解释成 geometry evaluation 和 frequency image kernel evaluation。

## 9. 内存模型

正式模型为

$$
M_{\rm total}\approx M_{\rm frozen\ cache}+B_f M_{\rm field}
+M_{\rm per-ray\ frequency\ temporaries}+M_{\rm small\ orchestration}.
$$

Long-lived：`Bf × FrequencyWorkspace`，跨所有 rays 持续存在直到 scale 和 consumer；single-source v1 中 `Bf=Nf`。

Per-ray temporary：`Nf × RayFrequencyState`、`Nf × Cerveny precompute`、`Nf × epsilon`、active mask 与 `imageSum[f]`；一条 ray 完成后释放或复用。

如果未来内存约束要求 `Bf<Nf`，frequency blocking 仅是同一算法的 execution policy：每个 block 内仍执行同一 fused traversal。v1 不实现 automatic blocking。

明确禁止在 v1 重新引入 `Nray × Nsegment × Nreceiver` persistent geometry storage。segment-range stencil、receiver geometry cache、depth interval 等只保留为 future candidate；full receiver-depth/image materialization 继续 REJECTED。

## 10. 架构结论

```text
Trajectory reuse
    → Cross-frequency Influence fusion
        → receiver geometry once
        → immediate exact per-frequency consumption
```

不是：

```text
build persistent geometry cache
    → replay cache frequency by frequency
```

本文只记录 IGR-1 的历史 scope 和 acceptance semantics。IGR-3 的 future scope
由当前 scope decision 单独冻结，不能从本文的 IGR-1 batch-local 限制推导。
