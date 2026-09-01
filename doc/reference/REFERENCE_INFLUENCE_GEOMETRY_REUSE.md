# Bellhop 场强积分与几何复用（Influence Geometry Reuse）理论基础

> **SUPERSEDED (PARTIAL) — 2026-09-01（实现策略部分）。** 理论基础（精确因式分解定理、
> G/M/F/T/O 分类）**继续有效**，并且现在正是 transient cross-frequency fusion 的论证依据。
> §4 起的**实现策略**（CC Segment-Range Stencil 持久缓存、per-frequency replay 复杂度模型，
> 含 L224-228 的 $T_{\text{IGR}}$ 模型）已被 fusion 方向取代：
> 见 [`../../Bellhop_RayReuse/doc/reports/REPORT_IGR0_REVISION_CROSS_FREQUENCY_FUSION_2026-09-01.md`](../../Bellhop_RayReuse/doc/reports/REPORT_IGR0_REVISION_CROSS_FREQUENCY_FUSION_2026-09-01.md)。
> 本文档理论部分按原样保留。

> **文档标识：** `REFERENCE_INFLUENCE_GEOMETRY_REUSE`
> **前置参考：** [`REFERENCE_RAY_DYNAMIC_EQUATIONS.html`](REFERENCE_RAY_DYNAMIC_EQUATIONS.html)（射线轨迹与动态射线追踪方程推导）
> **适用范围：** 二维海洋声学多频水声传播建模、宽带射线-波束求和方法（Beam Summation）、几何复用理论（Influence Geometry Reuse, IGR）
> **核心定理：** 空间几何投影与高频振荡相位的**精确因式分解定理（Exact Factorization Theorem）**

---

## 0. 符号约定与体系映射

本节对齐 [`REFERENCE_RAY_DYNAMIC_EQUATIONS.html`](REFERENCE_RAY_DYNAMIC_EQUATIONS.html) 的基础符号，并建立场强积分（Influence）与几何复用专属的数学符号体系：

### 0.1 空间坐标与网格符号

| 符号 | 物理与数学含义 | 坐标系与维度 |
|---|---|---|
| $(r, z)$ | 全局柱坐标/笛卡尔坐标（水平距离 $r$，深度 $z$，向下为正） | 全局坐标系 $\mathbb{R}^2$ |
| $s$ | 沿中心射线轨迹的弧长参数 | 射线随体坐标 |
| $n$ | 垂直于射线中心轨迹的法向距离（有向标量） | 射线随体坐标 |
| $(r_j, z_k)$ | 离散接收器网格点（水平第 $j$ 点，垂直深度第 $k$ 点） | 观测空间网格 $\mathcal{R} \times \mathcal{Z}$ |
| $z_{srf}, z_{bot}$ | 局部海面与海底的垂直水深边界 | 全局几何边界 |
| $\Delta z$ | 接收点相对于射线轴线或虚源镜像的垂直位移 | 局部几何距离 |

### 0.2 射线学与动态波束符号

| 符号 | 物理与数学含义 | 定义式与属性 |
|---|---|---|
| $c(r,z)$ | 介质无损实声速场 | $c: \mathbb{R}^2 \to \mathbb{R}^+$ |
| $c_i(r,z,f)$ | 介质吸收衰减引起的等效虚声速 | 频率依赖量 $c_i(f) \ge 0$ |
| $\mathbf{p} = (\xi, \zeta)^T$ | 射线无损慢度向量（Slowness Vector） | $\|\mathbf{p}\| = 1/c(r,z)$ |
| $\mathbf{e}_s = c\mathbf{p} = (c\xi, c\zeta)^T$ | 射线单位切向向量 | $\|\mathbf{e}_s\| = 1$ |
| $\mathbf{e}_n = (c\zeta, -c\xi)^T$ | 射线单位法向向量（正向指向法向右侧/下方） | $\|\mathbf{e}_n\| = 1, \mathbf{e}_s \cdot \mathbf{e}_n = 0$ |
| $\tau_{\text{real}}(s)$ | 射线沿线无损实传播时间（Real Travel Time） | $\tau_{\text{real}}(s) = \int_0^s \frac{1}{c} ds'$ |
| $\tau_c(s, f)$ | 介质吸收衰减下的复传播时间（Complex Travel Time） | $\tau_c(s, f) = \int_0^s \frac{1}{c + i c_i(f)} ds'$ |
| $q_1(s), q_2(s)$ | 动态射线追踪的 2 组实数无量纲基解（Point / Parallel 源） | 满足 $\ddot{q} = -c^{-1} c_{nn} q$ |
| $p_1(s), p_2(s)$ | 动态射线追踪的 2 组慢度扰动实数基解 | $p_k = c^{-1} \dot{q}_k$ |
| $\epsilon(f)$ | 初始高斯波束复参数（Beam Epsilon） | $\epsilon \in \mathbb{C}, \text{Im}(\epsilon) \ge 0$ |
| $q_{VB}(s, f), p_{VB}(s, f)$ | Cerveny 复动态射线解 | $q = q_1 + \epsilon q_2, p = p_1 + \epsilon p_2$ |
| $\gamma(s, f)$ | 笛卡尔 Cerveny 复波束曲率/展宽二次型参数 | $\gamma \in \mathbb{C}, \text{Im}(\gamma) \le 0$ |
| $\omega = 2\pi f$ | 声波圆频率 | $\omega \in \mathbb{R}^+$ |
| $k_0 = \omega / c_0$ | 参考波数 | $k_0 \in \mathbb{R}^+$ |

---

## 1. 理论背景：从波动方程到波束叠加积分

### 1.1 Helmholtz 方程与渐近波束表示

在非均匀无损/弱吸收流体介质中，定常点声源激励下的声压场满足非齐次 Helmholtz 方程：
$$\nabla \cdot \left(\frac{1}{\rho(\mathbf{x})} \nabla P(\mathbf{x}, \omega)\right) + \frac{\omega^2}{\rho(\mathbf{x}) c^2(\mathbf{x}, \omega)} P(\mathbf{x}, \omega) = -4\pi \delta(\mathbf{x} - \mathbf{x}_s)$$

在高频渐近极限（$k_0 L \gg 1$）下，标准几何声学将总波场展开为离散本征射线的叠加：
$$P(\mathbf{x}, \omega) = \sum_{r \in \text{eigenrays}} A_r(\mathbf{x}) e^{-i \omega \tau_r(\mathbf{x})}$$
然而，标准几何射线在焦散区（Caustics）存在振幅发散奇点（$q \to 0 \implies A \to \infty$），在影区（Shadow Zones）预测场强为零。

**高斯波束积分法（Gaussian Beam Summation / Cerveny Influence）** 通过在每条中心射线周围配置具有横向复曲率的高斯波束包络，将观测点 $\mathbf{x} = (r, z)$ 处的总声压表示为对整个发射角扇面（Launch Fan）连续积分的渐近展开式：
$$P(\mathbf{x}, \omega) = \int_{\alpha_{\min}}^{\alpha_{\max}} \Psi(\mathbf{x}; \alpha, \omega) \, d\alpha$$

离散化为包含 $N_{\text{rays}}$ 条射线、角间隔为 $\Delta\alpha$ 的求和形式：
$$P(r_j, z_k, \omega) = \sum_{\text{ray}=1}^{N_{\text{rays}}} \Delta\alpha \cdot \Psi_{\text{ray}}(r_j, z_k, \omega)$$

---

### 1.2 射线段场强贡献的积分分解

在数值离散实现中，每条中心射线被离散追踪为 $N_{\text{pts}}$ 个离散节点 $\{\mathbf{x}_0, \mathbf{x}_1, \dots, \mathbf{x}_{N-1}\}$。相邻两点构成一个**射线程段（Ray Segment）** $e_i = [\mathbf{x}_{i-1}, \mathbf{x}_i]$。

单条射线对网格点 $(r_j, z_k)$ 的场强影响量，可严格分解为所有有效几何程段及边界虚源镜像的叠加：
$$P(r_j, z_k, \omega) = \sum_{\text{ray}} \sum_{\text{seg}} \sum_{m \in \{\text{True}, \text{Srf}, \text{Bot}\}} \Delta P\big(r_j, z_k; \text{seg}, m, \omega\big)$$

其中 $\Delta P$ 为单镜像程段向单网格点的单次场强增量。

---

## 2. 空间几何与声学因子的精确因式分解定理

### 2.1 核心定理（Exact Factorization Theorem）

> **定理 1 (Influence Factorization Theorem)：**
> 在无反射波束位移（Beam Displacement = 0）的射线理论框架下，任意波束族（Cartesian Cerveny, Ray-Centered, GeoHat, GeoGaussian, Simple Gaussian）的单次场强增量 $\Delta P(r_j, z_k; \text{seg}, m, \omega)$，均可**严格且无损地因式分解**为一个纯空间几何算子 $\mathcal{G}$、一个低维标量混合声学因子 $\mathcal{M}$ 以及一个以无损几何走时为载频的快速旋转相位因子 $\mathcal{E}$ 的乘积：
>
> $$\Delta P(r_j, z_k; \text{seg}, m, \omega) = \mathcal{G}(r_j, z_k; \text{seg}, m) \cdot \mathcal{M}(\text{seg}, m, \omega) \cdot \exp\left[-i \omega \tau_{\text{geom}}(r_j, \text{seg})\right]$$

#### 算子定义：
1. **纯几何算子 $\mathcal{G}$（Frequency-Independent Geometry Operator）：**
   $$\mathcal{G} = \mathcal{F}_{\text{geom}}\big(\mathbf{x}_{l}, \mathbf{x}_{r}, \mathbf{p}_l, \mathbf{p}_r, c_l, c_r, q_{1,l}, q_{1,r}, r_j, z_k, z_{\text{boundary}}\big)$$
   其输入项**完全不包含频率 $f$、波数 $k$ 或波束参数 $\epsilon$**。在给定环境介质几何与接收器网格后，$\mathcal{G}$ 的数值在 IEEE-754 语义下对于所有频率 $f \in \mathcal{F}$ **完全恒定不变**。
2. **混合声学因子 $\mathcal{M}$（Mixed Acoustic Factor）：**
   $$\mathcal{M} = \mathcal{A}_{\text{source}}(f) \cdot \mathcal{A}_{\text{refl}}(f) \cdot \mathcal{K}_{\text{beam}}(f, \text{geom}) \cdot \exp\left[-\omega \tau_{\text{atten}}(f)\right]$$
   包含声源强度、累积边界反射幅度与相位、复波束动态发散修正以及介质吸收引起的阻尼因子。
3. **快速传播相位因子 $\mathcal{E}$（Exact Propagator）：**
   $$\mathcal{E} = \exp\left[-i \omega \tau_{\text{geom}}\right]$$
   基于射线程段插值出的纯无损几何传播时间 $\tau_{\text{geom}}$ 按目标频率精确构造，**严禁进行频域插值**。

---

### 2.2 几何不变量的三重层次体系

在数值计算实现中，几何算子 $\mathcal{G}$ 在空间上解构为三个层级的不变量：

```
Level 1: 空间相交拓扑不变量 (Crossing Topology)
         └── 判定程段 [r_l, r_r] 与接收器水平网格线 r_j 的交集区间

Level 2: 沿线插值几何不变量 (Interpolated Geometry)
         └── 求解交点处的归一化截距 W_j、投影坐标 z(W_j)、切向慢度 p(W_j) 与声速 c(W_j)

Level 3: 横向偏移与覆盖不变量 (Transverse Metric & Membership)
         ├── 笛卡尔坐标系：垂直偏移量 Δz_{jk, m} = z_k - z_{img, m}(W_j)
         └── 射线中心坐标系：法向距离 n_j = |(x_rcv - x(s)) · e_n|
```

---

## 3. 全波束族的几何不变量与解析推导

### 3.1 笛卡尔 Cerveny 波束族（Cartesian Cerveny `CC`）

#### 1. 动态射线解与复慢度构造
在射线程段两端节点 $i \in \{l, r\}$，由冻结实动态基底 $(p_1, p_2, q_1, q_2)$ 结合复束宽参数 $\epsilon(f)$ 线性组合出复动态射线量：
$$p_{VB}(s_i, f) = p_1(s_i) + \epsilon(f) p_2(s_i), \qquad q_{VB}(s_i, f) = q_1(s_i) + \epsilon(f) q_2(s_i)$$

#### 2. 笛卡尔复波束曲率张量 $\gamma$
由近轴程函展开，笛卡尔坐标系下的复波束二次型矩阵标量分量 $\gamma(s)$ 表达式为：
$$\gamma(s, f) = \frac{1}{2}\left[\frac{p_{VB}(s, f)}{q_{VB}(s, f)} t_r^2(s) + 2\frac{\nabla c(s) \cdot \mathbf{e}_n(s)}{c^2(s)} t_z(s) t_r(s) - \frac{\nabla c(s) \cdot \mathbf{e}_s(s)}{c^2(s)} t_z^2(s)\right]$$
其中 $(t_r, t_z) = c(s)\mathbf{p}(s) = \mathbf{e}_s(s)$ 为切向单位向量分量。

**【几何分解析构】：**
我们将 $\gamma(s, f)$ 拆解为纯几何张量分量与混合动态因子的乘积：
$$\gamma(s, f) = \frac{1}{2} \left(\frac{p_{VB}}{q_{VB}}\right) \cdot \mathcal{G}_{\text{tangent}} + \mathcal{G}_{\text{gradient}}$$
其中：
$$\mathcal{G}_{\text{tangent}} = t_r^2(s) = c^2(s) \xi^2(s) \quad (\text{纯几何量 } \mathbf{G})$$
$$\mathcal{G}_{\text{gradient}} = \frac{t_r t_z}{c^2} (\nabla c \cdot \mathbf{e}_n) - \frac{t_z^2}{2c^2} (\nabla c \cdot \mathbf{e}_s) \quad (\text{纯几何量 } \mathbf{G})$$

#### 3. 接收点水平跨越与线性插值
当射线程段 $[r_l, r_r]$ 跨越接收距离 $r_j$ 时，精确线性内插权重为：
$$W_j = \frac{r_j - r_l}{r_r - r_l} \in [0, 1] \quad (\text{纯几何量 } \mathbf{G})$$
交点处的几何属性完全由线性插值确定：
$$\mathbf{x}(W_j) = \mathbf{x}_l + W_j(\mathbf{x}_r - \mathbf{x}_l), \quad \mathbf{p}(W_j) = \mathbf{p}_l + W_j(\mathbf{p}_r - \mathbf{p}_l), \quad c(W_j) = c_l + W_j(c_r - c_l)$$
$$\tau_{\text{geom}}(W_j) = \tau_{\text{real}, l} + W_j(\tau_{\text{real}, r} - \tau_{\text{real}, l}) \quad (\text{无损介质纯几何量 } \mathbf{G})$$

#### 4. 镜像几何位移与场强解析式
对于接收深度 $z_k$，定义 3 类虚源镜像位移：
$$\Delta z_{jk, \text{True}} = z_k - z(W_j)$$
$$\Delta z_{jk, \text{Srf}} = z_k - (2z_{srf} - z(W_j)) = z_k + z(W_j) - 2z_{srf}$$
$$\Delta z_{jk, \text{Bot}} = z_k - (2z_{bot} - z(W_j)) = z_k + z(W_j) - 2z_{bot}$$

单镜像复声压贡献解析表达式为：
$$\Delta P_m(r_j, z_k, \omega) = \text{polarity}_m \cdot C_0(f) \cdot \text{Taper}\left(\Delta z_{jk, m}, R_{\max}(f)\right) \cdot \exp\left[-i \omega \Phi_m(r_j, z_k, f)\right]$$
其中发散常数与综合相位分别为：
$$C_0(f) = \text{ratio}_{\text{src}} \cdot \text{KMAH} \cdot \sqrt{\frac{c(W_j) |\epsilon(f)|}{q_{VB}(W_j, f)}} \cdot A_{\text{refl}}(f)$$
$$\Phi_m(r_j, z_k, f) = \tau_c(W_j, f) + \zeta(W_j) \Delta z_{jk, m} + \gamma(W_j, f) \Delta z_{jk, m}^2 - \frac{\phi_{\text{refl}}(f)}{\omega}$$

---

### 3.2 射线中心波束族（Ray-Centered Beams）

在射线随体正交坐标系 $(s, n)$ 下，接收点 $\mathbf{x}_{\text{rcv}} = (r_j, z_k)$ 向射线轴线作正交投影：
$$(\mathbf{x}_{\text{rcv}} - \mathbf{x}(s)) \cdot \mathbf{e}_s(s) = 0 \implies s = s_{\text{proj}}$$

法向有向距离标量为：
$$n = (\mathbf{x}_{\text{rcv}} - \mathbf{x}(s_{\text{proj}})) \cdot \mathbf{e}_n(s_{\text{proj}})$$

射线中心波束的横向衰减直接由法向二次型控制：
$$\Delta P(r_j, z_k, \omega) \propto \exp\left[-i \omega \left(\tau(s_{\text{proj}}) + \frac{1}{2} \frac{p_{VB}(s_{\text{proj}})}{q_{VB}(s_{\text{proj}})} n^2\right)\right]$$

**【几何不变量特征】：**
- 投影弧长 $s_{\text{proj}}$ 与法向距离 $n$ 是纯几何空间距离（**G**）；
- **拓扑限制：** 穿过海面/海底时的法向反转状态机依赖于频率局部的活动前缀长度，因此全局射线中心跨越模版不可跨频共享（判定为 INVALID），但单段法向距离 $n$ 仍为严格几何不变量。

---

### 3.3 几何帽波束族（Geometric Hat `GeoHat`）

在几何帽波束模型中，中心射线管的物理发散由实数动态射线基底 $q_1(s)$ 严格表征。

#### 1. 纯几何波束半宽
定义特征角度发散参考参量 $q_0 = c_0 / \Delta\alpha$（纯模型常数）。
几何帽在弧长 $s$ 处的物理有效半宽 $L(s)$ 为：
$$L(s) = \left|\frac{q_1(s)}{q_0}\right| \quad (\text{严格频率无关几何量 } \mathbf{G})$$

#### 2. 纯几何覆盖判定（Spatial Membership）
接收点 $\mathbf{x}_{\text{rcv}}$ 获得该射线程段有效声场贡献的充要条件为：
$$|n| < L(s) \quad (\text{严格频率无关几何拓扑 } \mathbf{G/T})$$
此空间覆盖几何关系在所有频率下 **100% 恒定**。

#### 3. 线性帽权重核函数
落在帽形截面内的无量纲几何加权因子为：
$$W_{\text{hat}}(n, s) = \frac{L(s) - |n|}{L(s)} \in (0, 1] \quad (\text{严格频率无关几何量 } \mathbf{G})$$

单次场强增量解析式为：
$$\Delta P(r_j, z_k, \omega) = \text{ratio}_{\text{src}} \cdot \sqrt{\frac{c(s)}{|q_1(s)|}} \cdot A_{\text{refl}}(f) \cdot W_{\text{hat}}(n, s) \cdot \exp\left[-i\left(\omega \tau_c(s, f) - \phi_{\text{refl}}(f) - \phi_{\text{caustic}}\right)\right]$$
**推论：** GeoHat 的几何遍历、空间覆盖与加权核 $W_{\text{hat}}$ 完全不含频率依赖项，具有最高等级的几何复用理论价值。

---

### 3.4 几何高斯与简单高斯波束族（GeoGaussian / Simple Gaussian）

#### 1. 几何高斯等效束宽构造
几何高斯波束综合考虑了几何扩展 $\sigma_g$、近场波动过渡 $\sigma_{nf}$ 与低频波长极限 $\sigma_\lambda$：
$$\sigma_g(s) = \left|\frac{q_1(s)}{q_0}\right| \quad (\text{纯几何项 } \mathbf{G})$$
$$\sigma_{nf}(s, f) = 0.2 \cdot f \cdot \tau_{\text{real}}(s), \qquad \sigma_\lambda(s, f) = \frac{\pi c(s)}{f} \quad (\text{频率依赖项 } \mathbf{M/F})$$
$$\sigma_1(s, f) = \max\Big(\sigma_g(s), \, \min\big(\sigma_{nf}(s, f), \sigma_\lambda(s, f)\big)\Big)$$

横向高斯核为：
$$W_G(n, s, f) = \sqrt{\frac{\sigma_g(s)}{\sigma_1(s, f)}} \exp\left[-\frac{1}{2} \left(\frac{n}{\sigma_1(s, f)}\right)^2\right]$$

#### 2. 简单高斯（Simple Gaussian / Bucker Model）
以射线弦为基准，定义接收点的最近接近点（CPA）与有效传播距离 $d_{\text{eff}}$：
$$\text{CPA} = z_{\text{proj}} - z_k \quad (\mathbf{G}), \qquad d_{\text{eff}} = r_j \quad (\mathbf{G})$$
偏转角 $\theta$ 与高斯角权重核为：
$$\theta = \arctan\left(\frac{\text{CPA}}{d_{\text{eff}}}\right) \quad (\mathbf{G})$$
$$W_{\text{simple}}(\theta) = \exp\left(-A \theta^2\right), \quad A = -\frac{4\ln 0.98}{\Delta\alpha^2} \quad (\text{纯几何模型常量 } \mathbf{G})$$
**推论：** 简单高斯模型的空间加权核 $W_{\text{simple}}(\theta)$ 是纯几何空间不变量。

---

## 4. 几何复用（IGR）算法理论与离散结构设计

### 4.1 射线-网格相交拓扑与程段跨越模版（Segment-Range Stencil）

#### 复杂度分析定理
在未引入几何复用前，传统单频求解器在处理 $N_f$ 个频率的宽带声场时，每频均需执行程段遍历与网格搜索：
$$T_{\text{baseline}} = O\left(N_f \cdot N_{\text{rays}} \cdot N_{\text{segs}} \cdot T_{\text{search}}\right) + O\left(N_f \cdot N_{\text{crossings}} \cdot N_{\text{depths}} \cdot N_{\text{images}}\right)$$

引入 **CC-only Segment-Range Crossing Topology Cache** 后，空间相交搜索与插值几何仅在主线程单次执行：
$$T_{\text{IGR}} = \underbrace{O\left(N_{\text{rays}} \cdot N_{\text{segs}} \cdot T_{\text{search}}\right)}_{\text{单次构建开销 } T_{\text{build}}} + \underbrace{O\left(N_f \cdot N_{\text{crossings}} \cdot N_{\text{depths}} \cdot N_{\text{images}}\right)}_{\text{无冗余纯声学线性流式重放}}$$

#### 连续流式数据结构设计（64-bit IEEE-754 对齐）
为杜绝精度损失并保证现代 CPU 的硬件预取效率，跨越模版组织为平坦连续数组（Flat Contiguous Buffer）：

$$\mathcal{C}_{\text{stencil}} = \Big\{ \big(\text{rangeIndex}_m, W_m, z_m, t_{z,m}, c_m, \text{leftIdx}_m, \text{rightIdx}_m\big) \Big\}_{m=1}^{N_{\text{crossings}}}$$

```
+---------------------------------------------------------------------------------------------------+
| Flat Stencil Record (64 Bytes Aligned)                                                            |
| double weight (8B) | double z (8B) | double tz (8B) | double c (8B)                               |
| uint32 rangeIndex (4B) | uint32 leftPointIdx (4B) | uint32 rightPointIdx (4B) | uint32 flags (4B) |
| padding / alignment (16B)                                                                         |
+---------------------------------------------------------------------------------------------------+
```

---

### 4.2 射线点正交基与梯度投影束（Point Geometry Bundle）

在射线节点预先完成几何投影与环境声速梯度点积，构造每个声源专属的冻结属性束：
$$\mathcal{B}_{\text{point}}(s_i) = \left\{ \mathbf{e}_s(s_i), \, \mathbf{e}_n(s_i), \, c^2(s_i), \, \nabla c(s_i) \cdot \mathbf{e}_s, \, \nabla c(s_i) \cdot \mathbf{e}_n, \, t_r^2(s_i), \, t_z^2(s_i) \right\}$$

**理论收益：** 消除多频循环中 $N_f \times N_{\text{points}}$ 次重复对 `SoundSpeedEvaluator::evaluate()` 的声速梯度查询与向量点积开销。

---

## 5. 边界反射、介质衰减与相干模式的理论一致性

### 5.1 有损介质复走时与无损走时的分离代数

介质复声速模型引入等效吸收衰减 $\alpha(f, z)$，定义复波数与复慢度：
$$k_c(f, z) = \frac{\omega}{c(z)} + i \alpha(f, z) \implies \frac{1}{c_{\text{complex}}(z, f)} = \frac{1}{c(z)} - i \frac{\alpha(f, z)}{\omega} = \frac{1}{c(z) + i c_i(z, f)}$$

沿冻结步进积分的复走时严格满足：
$$\tau_c(s, f) = \int_0^s \frac{c(s') - i c_i(s', f)}{c^2(s') + c_i^2(s', f)} \, ds' = \tau_{\text{lossy, real}}(s, f) - i \int_0^s \frac{c_i(s', f)}{c^2(s') + c_i^2(s', f)} \, ds'$$

**【理论公理】：**
1. **无损介质（$c_i = 0$）：** $\tau_c(s) = \tau_{\text{real}}(s) = \int_0^s \frac{1}{c} ds'$，与频率完全无关，属于纯几何不变量（**G**）；
2. **有损介质（$c_i > 0$）：** 复慢度实部 $\frac{c}{c^2 + c_i^2(f)}$ 显式依赖频率，故有损走时实部与虚部均属于频率局部量（**F**）。几何复用模版仅能缓存无损实走时，有损走时必须逐频累加。

---

### 5.2 边界反射乘子与活动前缀（Active Prefix）截断代数

设一条射线在传播路径上经历 $K$ 次边界碰撞事件 $\{e_1, e_2, \dots, e_K\}$。

#### 1. 前缀扫描乘积系统（Prefix Scan Algebra）
第 $k$ 次反射后的射线幅度与累加相位定义为：
$$A_k(f) = A_0 \prod_{j=1}^k |R_j(f, \theta_j)|, \qquad \Phi_k(f) = \sum_{j=1}^k \arg R_j(f, \theta_j)$$

#### 2. 活动截断算子（Active Truncation Operator）
1. **源端初始状态：** 声源起点 $\mathbf{x}_0$ 初始赋值为 $\text{active}_0 = \text{true}$；
2. **单次反射系数抑制（Raw Suppression）：** 在声学半空间（Acoustic Half-Space）边界反射计算中，若单次未抑制反射系数模值 $|R_k^{\text{raw}}| < 10^{-5}\text{F}$，则执行离散抑制（Suppression）：
   $$|R_k| = 0.0, \quad \arg R_k = 0.0, \quad \text{suppressed}_k = \text{true}$$
3. **累计振幅活动截断（Cumulative Active Cutoff）：** 在每次声源发射或边界反射更新后，计算累计投影振幅 $A_k(f)$。定义活动指示函数：
   $$\chi_k(f) = \begin{cases}
   1, & A_k(f) \ge A_{\text{threshold}} \ (0.005\text{F}) \\
   0, & A_k(f) < 0.005\text{F}
   \end{cases}$$
4. **介质吸收衰减：** 介质体吸收通过复走时虚部 $\text{Im}(\tau_c)$ 引入衰减，**不直接修改 $\text{active}$ 标志**。

前缀有效性满足单调单向阻断律：
$$\text{active}_k(f) = \prod_{j=1}^k \chi_j(f) \implies \text{active}_k(f) \le \text{active}_{k-1}(f)$$

**【终端程段合法性定理】：**
设 $k^*$ 为首个满足 $\text{active}_{k^*}(f) = 0$ 的节点索引。根据 Bellhop Legacy `Beam%Nsteps` 规范，程段 $[\mathbf{x}_{k^*-1}, \mathbf{x}_{k^*}]$ 的左端点 $\text{active}_{k^*-1} = 1$，该程段依然具有合法的物理贡献，必须纳入求和；仅当左端点 $\text{active} = 0$ 时，后续射线尾缀才被彻底截断。

---

### 5.3 相干（C）、非相干（I）与半相干（S）场强累加泛函

定义空间单贡献算子 $\delta u(r_j, z_k; \text{ray}, m, f)$：

1. **相干模式（Coherent `C`）：**
   $$P_{\text{coherent}}(r_j, z_k, f) = \sum_{\text{ray}} \left( \sum_{m} \delta u(r_j, z_k; \text{ray}, m, f) \right)$$
2. **非相干模式（Incoherent `I`）：**
   - 笛卡尔 Cerveny：先在束内执行镜像相干叠加，再跨射线功率累加：
     $$I_{\text{Cart}}(r_j, z_k, f) = \sum_{\text{ray}} \left| \sum_{m} \delta u(r_j, z_k; \text{ray}, m, f) \right|^2$$
   - 射线中心 Cerveny / GeoHat：逐镜像直接功率累加：
     $$I_{\text{RC}}(r_j, z_k, f) = \sum_{\text{ray}} \sum_{m} \left| \delta u(r_j, z_k; \text{ray}, m, f) \right|^2 \cdot \text{Taper}_m$$
3. **半相干模式（Semicoherent `S`）：**
   采用与非相干相同的累加泛函，但在源端引入 Lloyd 镜像调制初始幅度：
   $$A_{\text{source}, S}(f, \alpha) = A_0 \sqrt{2} \left|\sin\left(\frac{2\pi f}{c_0} z_{\text{src}} \sin\alpha\right)\right|$$

---

### 5.4 IEEE-754 浮点数结合律与 Bitwise Parity 严格证明

在计算机浮点运算体系（IEEE-754）中，浮点加法不满足结合律：$(a + b) + c \neq a + (b + c)$。

> **定理 2 (Bitwise Parity Theorem)：**
> 设 $P_{\text{base}}$ 为未启用几何复用的即时计算声压，$\widetilde{P}_{\text{IGR}}$ 为基于几何复用模版重放计算的声压。$\widetilde{P}_{\text{IGR}} \equiv P_{\text{base}}$ 达成逐位二进制对齐（Bitwise Byte-Identical）的**充分必要条件**为：
> 1. **算术一致性：** 模版存储或重放时计算的插值几何量 $(W_j, z_j, t_{z,j}, c_j)$ 保持完全相同的双精度（binary64）浮点操作流水线；
> 2. **累加保序性：** 模版重放遍历严格遵循中心射线升序（$\text{rayIndex} = 0 \to N-1$）、射线程段升序（$\text{segIndex} = 2 \to N_{\text{pts}}-1$）以及镜像升序（$\text{True} \to \text{Srf} \to \text{Bot}$）；
> 3. **离散分支一致性：** 波束窗口筛选、Hermite 截断以及活动前缀阻断的离散布尔条件与即时计算完全一致。

---

## 6. 总结：从轨迹复用（RTR）到几何复用（IGR）的理论演进

```
+---------------------------------------------------------------------------------------------------+
|                                  Bellhop Broadband Reuse Hierarchy                                |
+---------------------------------------------------------------------------------------------------+
|  [Tier 1: RTR]  Ray Trajectory Reuse (轨迹复用 - 已完成)                                            |
|                 ├── 理论基础：Hamiltonian 射线系统与近轴变分方程与频率无关                             |
|                 ├── 复用对象：RayState 轨迹点、动态基底 p1/p2/q1/q2、步进权重、边界反射几何正交基         |
|                 └── 性能贡献：消除多频重复 Trace 耗时（Amdahl 理论上限 1.020x）                       |
+---------------------------------------------------------------------------------------------------+
|  [Tier 2: IGR]  Influence Geometry Reuse (场强几何复用 - 当前阶段)                                   |
|                 ├── 理论基础：空间相交拓扑、距离内插权重与横向几何偏移在所有频率严格恒定 (Factorization)   |
|                 ├── 复用对象：Segment-Range Crossing 模版流、点几何投影束、GeoHat 接收网格空间覆盖      |
|                 └── 性能贡献：直接消除最热 Influence 循环中的全部跨越查找与重复插值算术                  |
+---------------------------------------------------------------------------------------------------+
|  [Tier 3: SFR]  Selective Frequency Reconstruction (慢变量选择性重构 - 未来研究方向)                |
|                 ├── 理论基础：剥离快速旋转相位 exp(-iωτ) 后的慢变声学因子具有频域光滑性                  |
|                 ├── 研究对象：单事件 log|R_j(f)|、连续展开相位、横向高斯核参数在非临界区的稀疏重构        |
|                 └── 理论约束：严密拓扑保护、离散分支 Guard、显式误差预算与全自动 Exact Fallback          |
+---------------------------------------------------------------------------------------------------+
```

---
*文档编制完成，作为 Bellhop_RayReuse 几何复用架构设计的首要理论基础与数学规范。*
