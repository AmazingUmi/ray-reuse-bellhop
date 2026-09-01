# IGR-0 Revision — Cross-Frequency Influence Geometry Fusion（跨频 Influence 几何融合）架构修订报告

> **Batch：** IGR-0-REV（IGR-0 rebaseline）
> **日期：** 2026-09-01
> **基线：** `main @ ba653229560aac53eb61eeaa5fdb2c2fd3254338`；修订分支 `feat/igr-influence-geometry-reuse @ 95a4fef`
> **报告状态：** **`ACCEPTED`**（2026-09-01 独立 final-review 验收通过，verdict 与 MINOR findings 闭环记录见 [`../reviews/IGR0_REVISION_FINAL_REVIEW_2026-09-01.md`](../reviews/IGR0_REVISION_FINAL_REVIEW_2026-09-01.md)）
> **性质：** 架构修订 rebaseline。用户重新决策 IGR 主方案：由 persistent geometry cache 转向 **Cross-Frequency Influence Geometry Fusion（transient reuse via loop restructuring）**。本轮**零 production 改动**（不修改 `Bellhop_RayReuse/src`、`Bellhop_RayReuse/include`、`Bellhop_F2CPP/**`、`Bellhop_origin/**`、tests、cases），仅产出文档。
> **Supersedes（部分取代，见 §D）：**
> - [`REPORT_IGR0_INFLUENCE_GEOMETRY_REUSE_AUDIT.md`](REPORT_IGR0_INFLUENCE_GEOMETRY_REUSE_AUDIT.md) §4（候选排序）/ §5（prototype 决策）/ §6（cache 契约）/ §9.2（13 项 IGR1-GATE）；
> - [`REPORT_INFLUENCE_FREQUENCY_AUDIT_2026-08-25.md`](REPORT_INFLUENCE_FREQUENCY_AUDIT_2026-08-25.md) §15.3（"接入点位于 `solveFrequencyFromCache()` 内"的结论）；
> - [`REFERENCE_INFLUENCE_GEOMETRY_REUSE.md`](../../../doc/reference/REFERENCE_INFLUENCE_GEOMETRY_REUSE.md) §4.1 实现策略复杂度模型（per-frequency replay 模型）。
> **保留有效（不在取代范围）：** G/M/F/T/O 状态分类体系与全部 threshold 语义纠正（见 §D 开头清单）。
> **配套交付物：** IGR-1 设计草案 [`../worklists/IGR-1_CC_FUSION_DESIGN_DRAFT.md`](../worklists/IGR-1_CC_FUSION_DESIGN_DRAFT.md)（DESIGN DRAFT — NOT APPROVED）。

---

## A. Current-State Audit（现状复核）

以下事实均在当前工作树（`feat/igr-influence-geometry-reuse @ 95a4fef`）逐行复核。

### A.1 轨迹复用（trajectory reuse）

- 串行入口 `SerialRayReuseSolver::solveStreaming`：**所有 source 的射线束在频率循环之前 trace 一次**并冻结（`src/solver/serial_ray_reuse_solver.cpp:44-45`）；频率循环为最外层（66-83），每个 `(frequency, source)` 调用 `SingleFrequencySolver::solveFrequencyFromSourceCache`（73-77），该频率全部 source workspace 完成后交给 consumer（82）。streaming 形态下 workspace 被移入 per-frequency vector 并移交 consumer，consumer 之后无状态存活。
- `traceSourceFan` 结束时 `rayCache.freeze()`（`src/solver/single_frequency_solver.cpp:146`）；`solveFrequencyFromSourceCache` 强制要求 frozen cache（192-194）。
- `RayState` 只含 `position / slowness / dynamicP[2] / dynamicQ[2] / soundSpeed / realTravelTime`（`include/rayreuse/ray/ray_path.hpp:13-20`），无任何 frequency 字段；`contentFingerprint()` 只覆盖几何内容。frozen cache 契约见 [`../status/STATUS_PROGRESS.md`](../status/STATUS_PROGRESS.md) L58-70（amplitude、phase、complex travel time、active prefix、reflection result、workspace、Arrival 状态一律不得写回）。

### A.2 频率投影（frequency projection）

- `FrequencyProjector::project()` 为 `const`，按值返回 `RayFrequencyState`；`RayFrequencyPoint = {complexTravelTime, amplitude, reflectionPhase, active}`（`include/rayreuse/field/frequency_workspace.hpp:12-22`）。
- 逐条 path 边遍历一次：reflection 边在**该频率**上求 boundary acoustics（`src/field/frequency_projector.cpp:122-156`）；step 边积分复走时（lossless → 直接取实走时，169-171；一般介质 → `FrequencySspEvaluator` 在 f 上求值，177-191）。active 语义：首点以 source amplitude 播种 active（109-113），此后与 `amplitudeRemainsActive`（阈值 `0.005F` double cast，16、60-65）单调 AND（155、192）。
- Project 实测代价极小：16 频 Munk 全部 Project 合计约 `0.509 s`（[`../archive/benchmarks/REPORT_OFFICIAL_BENCHMARK_C77FF60_2026-07-30.md`](../archive/benchmarks/REPORT_OFFICIAL_BENCHMARK_C77FF60_2026-07-30.md) L75）。

### A.3 Influence 遍历（各家族 loop order）

- 每个 `(frequency, source)` 调用内：`FrequencyWorkspace` / `IntensityWorkspace` 每次分配（`src/solver/single_frequency_solver.cpp:227-239`；布局为 `depthCount × rangeCount` 平坦向量，`frequency_workspace.hpp:24-72`）；`FrequencyProjector` 与各 influence 对象每次构造（240-274）；逐 ray：beam-pattern amplitude、可选 Lloyd mirror（283-292）→ `projector.project`（298-299）→ Cerveny 族逐 ray `pickBeamEpsilon`（321-324、335-338）→ 每家族一次 accumulate（301-347）；末尾按模式缩放（355-379）。
- **Cartesian Cerveny（CC，实测热点家族）**：loop order = segment（`rightIndex=2` 起，以 active prefix 为界，`src/field/cartesian_cerveny_influence.cpp:678`）→ receiver range（719-872）→ receiver depth（758-871）→ images 最内层模板化 1-3（801-804）。per segment：`fortranUpperRangeIndex` O(1) 算术求 `firstUpper/secondUpper`（708-714；实现 90-103）；左端点 active 检查 `if (!points[leftIndex].active) continue;`（700-706，注释明确 terminal-point retention）。per range：`W=(r−rL)/(rR−rL)`（725-726）；插值 position/slowness/实 soundSpeed 为纯几何（727-735）；插值 q/τ/γ 依赖 f（736-744，输入来自 per-(ray,f) 的 `precomputeRayValues` 数组 M/F 与 `RayFrequencyState` F）；`gamma.imag()>0` 早退（745-747）。per (depth,image)：Δz 与 polarity 为纯几何镜像（408-418）；window `−ω·Im(γ)·Δz² < BeamWindow²` **频率依赖**（420-425）；`radiusMax=30·c0/f` + Hermite taper **频率依赖**（649-650、432-433）；`phaseArgument=ω(τ+tzΔz+γΔz²)−φrefl`（440-443）；累加 `pressure[depth*rangeCount+range] += …`（811-822），intensity 为 `|·|²` 且 taper 折入（823-837）。`precomputeRayValues` per (ray,f)：`p=p1+εp2`、`q=q1+εq2`（311-314）、γ 由几何梯度 + p/q 组合（323-329）、kmah 链（336-341）。
- **Ray-Centered Cerveny（RC）**：loop order = depth → image → step → range（`src/field/ray_centered_cerveny_influence.cpp:321-385`）；image normal flip 拓扑沿 step 传播并作为 persistent flip state 维护（317-354），与 per-frequency active prefix 交互；window/taper 频率依赖（399-402、446-447）。
- **GeometricHat**：hat membership **频率无关** —— `beamRadius=|q/q0|`，q 取 `dynamicQ[0]`（无 ε），q0 为几何常量（`src/field/geometric_hat_influence.cpp:190-195、255-273`）；delay/phase/attenuation 频率依赖（295-336 一带）。Cartesian 变体 segment→range→depth。
- **GeometricGaussian**：membership **频率依赖** —— `σ1 = max(geometricSigma, min(nearFieldSigma=0.2·f·Re(τ), wavelengthSigma=π·c/f))`（`src/field/geometric_gaussian_influence.cpp:242、287-292`），window `4·σ1`（293-294），segment 深度预带同样随 f（250-262）。
- **SimpleGaussian**：无 window（源码无任何 BeamWindow 判定），coherent-only；CPA/off-ray/effective 距离为纯几何；phase 随 f。
- 事实更新（相对 [`REPORT_INFLUENCE_FREQUENCY_AUDIT_2026-08-25.md`](REPORT_INFLUENCE_FREQUENCY_AUDIT_2026-08-25.md) 的范围注记）：`solveFrequencyFromSourceCache` 当前已接入全部五个家族（`single_frequency_solver.cpp:215-274`），该报告 L23-26 "TL 入口只接受 coherent CC" 是 FP-2x 批次完成前的旧状态；本修订按当前代码口径陈述。

### A.4 Workspace 所有权与 writer 生命周期

- `FrequencyWorkspace`（complex pressure）与 `IntensityWorkspace`（double intensity）为 frequency-local 产品，per (freq, source) 分配、随 consumer 移交、用后即弃。
- `ShdFrequencyWriter::writeFrequency(index, sourceWorkspaces)`：校验 frequency 与维度、逐 (source, depth) seek-addressed record 写入、按 `written_` 防重，乱序完成频率可以接受，`finalize()` 要求全部频率已写（`src/io/shd_writer.cpp:138-224`）。consumer 侧 `app/main.cpp` 串行/并行均为按频率调用 writer。**writer API 与文件格式与本修订无关，保持不变。**

### A.5 并行所有权（parallel ownership）

- 一个 worker 拥有一个频率跨全部 source（`src/solver/parallel_ray_reuse_solver.cpp:218-265`）；bounded output queue 容量限 1-2（151-158）；consumer 在主线程调用 writer（301-323），worker 不触碰 writer；`RayPathCache` 只读，前后 `contentFingerprint()` 校验（203-211、338-350）；内存预算经 `selectActiveFrequencyLimit` / `estimatedPeakBytes`（87-123）。当前 TL reuse 路径**不存在任何 frequency block/batch 结构**。

---

## B. 关键问题裁决（Q1–Q5）

### Q1 几何重复究竟发生在哪里

调用链（当前 frequency-major）：

```text
SerialRayReuseSolver::solveStreaming                     (serial_ray_reuse_solver.cpp:29-99)
  └─ traceAllSourceFans（一次，频率无关）                  (44-45)
  └─ for frequency:                                      (66-83)
      └─ for source: solveFrequencyFromSourceCache       (73-77 → single_frequency_solver.cpp:181-393)
          └─ for ray:                                    (280)
              ├─ beam-pattern amplitude / Lloyd          (283-292)
              ├─ projector.project(path, f, A)           (298-299)
              ├─ pickBeamEpsilon(ray, f)                 (321-324 / 335-338)
              └─ Influence::accumulate                    (326-347)
                  └─ CC: for segment (678) → for range (719) → for depth (758) → images (801-804)
```

以 CC 为例，下列计算今天**每个频率完整重算一遍**（Nf 次）：

| 重复计算项 | 类别 | 源码 |
|---|---|---|
| segment→receiver-range crossing 拓扑（`firstUpper/secondUpper`）、退化段过滤、越界早退 | A/E（频率无关） | `cartesian_cerveny_influence.cpp:678-714` |
| 插值权重 W、插值 position/slowness/实 soundSpeed | A | 725-735 |
| 每 (depth,image) 的 Δz 镜像与 polarity | A | 408-418 |
| `precomputeRayValues` 中的纯几何部分（tangent/normal、声速平方、梯度投影） | A/B | 311-321 |
| segment/range/depth/image 的遍历与寻址本身（循环控制、索引、workspace 地址计算） | E | 678-872 |
| window/taper 筛选触发的载入与判定 | E（判定频率依赖，但被筛对象 Δz²、Δz 几何不变） | 420-439 |
| per-(ray,f) 组合 p/q/γ/kmah、ε、q/τ/γ 插值、principal、phase、exp、累加 | C/D（本质频率局部，**不是**重复浪费） | 311-341、736-744、749-754、440-446、811-837 |

结论：重复的主体是 **receiver-side geometry 遍历及其触发的筛选**，而不是频率局部物理本身。实测：2 频即产生 `4.97M` range / `999.6M` depth / `3.0B` image evaluations，`86.43%` 在 window/taper 被拒绝，hot loop `18.44 s` vs precompute `0.14 s`（[`../archive/benchmarks/REPORT_F1_BASELINE_96F23F8_2026-07-30.md`](../archive/benchmarks/REPORT_F1_BASELINE_96F23F8_2026-07-30.md)；[`REPORT_INFLUENCE_FREQUENCY_AUDIT_2026-08-25.md`](REPORT_INFLUENCE_FREQUENCY_AUDIT_2026-08-25.md) L27-35、L561-574）。16 频 Munk：Trace/Project/Influence = `0.405/0.509/279.211 s`，Influence 占 `99.62%`（C77FF60 L75）；64 频 Influence `621.719 s` 占 `99.19%`（[`../archive/benchmarks/REPORT_F2_64_FREQUENCY_MATRIX_FDAAF56_2026-07-31.md`](../archive/benchmarks/REPORT_F2_64_FREQUENCY_MATRIX_FDAAF56_2026-07-31.md) L31-32）。

### Q2 各家族逐量分类表

分类定义（本修订口径；对应旧报告 G/M/F/T/O）：**A** 纯几何（频率无关）；**B** 动态射线基（frozen 存储、按频组合）；**C** 频率局部波束状态；**D** 频率局部贡献/累加；**E** 拓扑/隶属状态（须标注是否随频率）。

| 家族 | 量 | 分类 | 频率依赖 | 源码证据 |
|---|---|---|---|---|
| GeoHat G | crossing 拓扑、W、插值 position/tangent、normalOffset、q0、`beamRadius=\|q1/q0\|`、hat 三角权、`W_simple` 类纯几何常量 | A（拓扑为 E，频率无关） | 否 | `geometric_hat_influence.cpp:190-195、255-284` |
| GeoHat G | delay、attenuation、phase、贡献累加 | C/D | 是 | 295-336 |
| GeoGaussian B | normalOffset、投影几何 | A | 否 | `geometric_gaussian_influence.cpp:260-286` |
| GeoGaussian B | `σ1 = max(σg, min(0.2·f·Re τ, π·c/f))`、window `4σ1`、segment 深度预带 | C + E | **是（不得误分类为频率无关）** | 242、250-262、287-294 |
| GeoGaussian B | delay/attenuation/phase、累加 | C/D | 是 | 295-310 |
| SimpleGaussian S | CPA、d_eff、θ、角权重核 | A | 否（无 window，E 不存在） | `simple_gaussian_influence.cpp`（全文无 BeamWindow 判定） |
| SimpleGaussian S | phase、复指数、累加 | D | 是 | 同上 |
| Cerveny Cartesian C | crossing 拓扑、W、插值 position/slowness/c、Δz、polarity | A（拓扑 E，频率无关） | 否 | `cartesian_cerveny_influence.cpp:678-714、725-735、408-418` |
| Cerveny Cartesian C | frozen `p1/p2/q1/q2`、几何梯度投影 | B | 否（存储频率无关） | 311-321；`ray_path.hpp:16-17` |
| Cerveny Cartesian C | ε(f)、`pVB(f)=p1+εp2`、`qVB(f)`、γ(f)、kmah(f)、per-range q/τ/γ 插值、principal | C | 是 | 321-324（solver）、311-341、736-744、749-754 |
| Cerveny Cartesian C | 有损复 τ（**实部与虚部均随 f**） | C | 是 | `frequency_projector.cpp:177-191` |
| Cerveny Cartesian C | window `−ω·Im(γ)·Δz²<BeamWindow²`、taper `30c0/f` | E + C | **是（频率依赖 membership）** | 420-439、649-650 |
| Cerveny Cartesian C | active/terminal prefix | C（对遍历的作用为 per-frequency E） | 是 | `frequency_projector.cpp:16、60-65、109-113、155、192` |
| Cerveny Cartesian C | phaseArgument、exp、pressure/intensity 累加 | D | 是 | 440-446、811-837 |
| Cerveny RC R | 投影几何、插值 W/q/γ 的几何输入 | A/B | 否 | `ray_centered_cerveny_influence.cpp:321-395` |
| Cerveny RC R | image normal flip 拓扑（沿 step 传播） | E | **是（与 per-frequency active prefix 交互；不可持久化跨频冻结，M7）** | 317-354 |
| Cerveny RC R | window `−0.5ω·Im(γ)n²`、taper | E + C | 是 | 399-402、446-447 |
| Cerveny RC R | 相位、贡献、累加 | D | 是 | 404-447 |

正确性要点（冻结）：GeoHat hat membership **频率无关**；GeoGaussian σ1 membership 与 Cerveny window/taper membership **频率依赖**，任何"跨频共享 membership 判定"的设计都是错的；有损复 τ 实部与虚部均为频率局部。

### Q3 各家族最小融合粒度

融合 = 把 frequency 循环移到何处，使"一次几何求值被 Nf 个频率局部求值立即消费"。

| 家族 | frequency 循环插入点 | 说明 |
|---|---|---|
| **CC（IGR-1 目标）** | **(segment, range, depth) receiver-geometry 单元**：segment 拓扑与 W/position/slowness/c 每 (segment, range) 算一次；每 (depth,image) 的 Δz/polarity 算一次；随后对块内每个 f：用共享 W 插值 q(f)/τ(f)/γ(f)，做 window/taper/phase/exp 并累加到 `workspace[f]` | per-range 的 q/τ/γ 插值在频率层用共享 W 完成；per-(ray,f) 的 ε 与 precompute 数组在 ray 层一次备齐 |
| GeoHat（Cartesian 变体） | 同 CC 结构；membership 频率无关，可一次判定、Nf 次只做 delay/phase | 未列入 IGR-1 |
| GeoGaussian | (segment, range, depth)：normalOffset 等几何共享，σ1/window/深度预带按 f | 未列入 IGR-1 |
| SimpleGaussian | (segment, range, depth)：纯几何全共享，仅 phase 按 f | 未列入 IGR-1 |
| RC | depth→image→step→range 顺序下同样可做 transient 融合（per-frequency flip 状态以 transient per-f state 保存），但结构与 CC 不同且非实测热点 | **Deferred**；M7 只否决"持久化跨频冻结 flip 拓扑"，不否决 transient 融合 |

IGR-1 先做当前实测热点 CC，不追求全家族一次统一。

### Q4 内存增长真相（不许写"一切 ×Nf"）

| 状态 | 生命周期 | 增长 |
|---|---|---|
| `RayFrequencyState` ×Nf | per-ray temporary，ray 结束即释放 | O(Nf × points(ray))，单 ray 界 |
| per-(ray,f) precompute（p/q/γ/kmah、ε） | per-ray temporary | O(Nf × points(ray))，单 ray 界 |
| `FrequencyWorkspace` / `IntensityWorkspace` | **必须跨全部 ray 存活至 consumer** | **O(Bf × depthCount × rangeCount)**，真正的长期增长项 |
| 多 source 场 | 同上按 source 累加 | O(Nsource × Bf)（多 source 的真实成本，single-source-first 的动机） |
| writer 缓冲 | 不变 | seek-addressed records，`shd_writer.cpp:138-224` |
| parallel queue | IGR-1 不触碰 | 有界 1-2，`parallel_ray_reuse_solver.cpp:151-158` |
| frozen `RayPathCache` | 不变（D8） | 不随 Nf 增长 |

修订内存模型（D6）：

```text
M_total ≈ M_frozen_ray_cache + Bf × M_frequency_field (+ ×Nsource)
        + per-ray/per-frequency temporaries（单 ray 界）
        + 少量 orchestration 开销；v1 中 Bf = Nf
```

Munk 16F 量级：`201 × 501 × 16 B ≈ 1.6 MB/频`，Bf=Nf=16 约 `26 MB` —— 相对 workspace 之外的开销可忽略；64F 约 `103 MB`（见 §I）。

### Q5 与并行模式的关系

当前 parallel 是 **frequency-level ownership**：一个 worker 独占一个频率跨全部 source（`parallel_ray_reuse_solver.cpp:218-265`），该结构无法直接表达 IGR（融合遍历在 ray 内部跨频）。裁决（D9）：IGR-1 先建正确的 **fused serial/reference path**；并行应放在 ray/source/frequency-block/其他哪一层，延后依据 fused 路径的实测特征决定；现在不做 nested-parallelism 设计。STATUS_PROGRESS L68-70 已声明"当前结构避免 nested parallelism，但不规定未来永远由 frequency 层拥有并行"，与此一致。

---

## C. 冻结决策 D1–D9

- **D1 主方案 Transient Cross-Frequency Geometry Reuse。** 目标遍历结构：`source → ray → segment → receiver range → receiver geometry → frequency`。凡几何量（range membership、插值权重 W、插值几何 position/tangent/slowness/实 soundSpeed、receiver 相对偏移 n/Δz、image 相对几何）在单次遍历中被全部频率**立即消费**，不长期存储。这是 loop restructuring，不是 cache 产品。
- **D2 Persistent geometry cache 降级。** segment→receiver-range stencil、投影 receiver geometry cache、receiver-depth interval/index、sparse geometry cache 一律仅为**未来候选**，不是 IGR-1 核心。理由：(i) 投影算术本身便宜（O(1) index + lerp，`cartesian_cerveny_influence.cpp:708-714、725-735`）；(ii) ray/segment 计数巨大，持久 cache 把算术问题换成内存流量问题——小容量本地 cache（endpoint/depth-scalar/depth-tile）实测全部回退（−3.69%/−1.35%/−1.95%，[`../archive/benchmarks/REPORT_F2_LOOP_INVARIANTS_7CE9C7D_2026-07-31.md`](../archive/benchmarks/REPORT_F2_LOOP_INVARIANTS_7CE9C7D_2026-07-31.md) L94-106）；(iii) 真正要消除的是**跨频重复遍历**，transient reuse 恰好利用 register/L1/L2 局部性；(iv) 是否最终仍需持久 cache，留待 IGR-1 profiling 裁决。**Full receiver-depth/image pair 物化继续明确 REJECTED**（尺寸依据不变：约 `7.45 GiB` depth / `22.3 GiB` image 表，[`REPORT_INFLUENCE_FREQUENCY_AUDIT_2026-08-25.md`](REPORT_INFLUENCE_FREQUENCY_AUDIT_2026-08-25.md) L536-545）。
- **D3 Frequency-local physics 边界。** 以下永远频率局部、**绝不写回 frozen `RayPathCache`**：complex travel time；attenuation；reflection amplitude；reflection phase；active/terminal prefix；beam epsilon（频率依赖模式）；Cerveny p/q/gamma 的频率局部组合；GeoGaussian 的频率依赖宽度 σ1；Gaussian/Cerveny beam window membership；最终复相位/复指数；pressure/intensity 累加状态。
- **D4 Dynamic-ray 基继续来自 frozen `RayPath`。** x、slowness/tangent、实 soundSpeed、实走时、p1/p2、q1/q2、reflection geometry/events、quadrature 频率无关复用；`pVB(f)=p1+ε(f)p2`、`qVB(f)=q1+ε(f)q2`、γ(f) 在频率局部层构造，frozen cache 不动（现状即如此：`cartesian_cerveny_influence.cpp:311-329`）。
- **D5 Per-frequency active-prefix 语义。** 不同频率可有不同 active prefix（reflection/attenuation/amplitude cutoff 各异）。融合遍历**不得**用某一参考频率的 prefix 作为全体频率的几何终止。语义：几何遍历在"至少还有一个频率可能贡献"时继续（= 各频 prefix 的**并集** / 最大可达 segment）；每个频率独立保持现行 Origin/F2CPP/RayReuse 语义：左端点 active 检查、terminal point 保留、inactive 后缀抑制、`<0.005F` active cutoff 不变（`frequency_projector.cpp:16、60-65`；`cartesian_cerveny_influence.cpp:700-706`）。
- **D6 默认理想模式：全频融合，Bf = Nf。** 额外长期内存 ≈ `Nf ×` 频率输出 workspace（**不是** receiver geometry cache）。内存模型见 Q4。
- **D7 Frequency blocking 是同一算法的内存权衡策略，不是第二算法。** `Bf=Nf` → 最大复用/最大 field 内存；`Bf=1` → 退化为接近现行 frequency-major reuse；`1<Bf<Nf` → 时间-内存折中（"对每个频率块：块内融合几何遍历"）。IGR-0 Revision 只冻结此设计，**现在不实现**自动 blocking。
- **D8 Frozen `RayPathCache` 契约不变。** immutable、frequency-independent；以 [`../status/STATUS_PROGRESS.md`](../status/STATUS_PROGRESS.md) L58-70 为准。
- **D9 并行延后。** 见 Q5。先 fused serial/reference path；并行层次后定；不做 nested-parallelism 设计。

---

## D. 被取代的 IGR-0 结论（逐项表）

**继续有效的部分（明示，不被取代）：** G/M/F/T/O 分类体系与全部 threshold 语义纠正——`0.005F` 累计振幅 active cutoff；`<1e-5F` 仅为声学半空间单次 raw 反射抑制；有损复走时实部与虚部均随频率；terminal-point retention；M7（RC flip 拓扑不可跨频持久冻结）；full pair 物化拒绝（`~7.45 GiB` depth / `~22.3 GiB` image）。这些结论是融合方案的分类学基础，本次修订完全继承。

| # | 旧结论（IGR-0 / 相关文档） | 修订结论 | 修订理由 |
|---|---|---|---|
| 1 | Candidate 1B segment-range stencil cache（16B 记录，~39.8 MB/source）为 IGR-1 首个 prototype（§4.1/§5） | 降级为**未来候选**；IGR-1 核心改为 cross-frequency fusion（D1/D2） | 跨频遍历重复才是热点本体；持久 cache 把算术换成内存流量，小 cache 已实测回退 |
| 2 | Candidate 1A（64B 双精度几何变体）为可选实验变体 | 同上降级 | 同上 |
| 3 | Candidate 6（per-point geometry/gradient bundle，~107.8 MB/source）为协同备选 | 降级为未来候选；其**内容**（tangent/normal/梯度投影等纯几何量）被吸收进融合路径的 per-ray 预计算拆分（G 部分每 ray 一次，f 组合每 f 一次） | 融合天然把这些量变成每 ray 一次，无需持久结构 |
| 4 | Candidate 2 depth set DEFERRED / QUESTIONABLE | 维持未来候选（不升级） | 深度窗口随频率与束宽参数，无解析无漏检证明前不实施 |
| 5 | Candidate 4/5 INVALID | 维持 INVALID；补充澄清：M7 否决的是 RC flip 拓扑的**持久化跨频冻结**，不否决 RC 的 transient per-frequency 融合（仍 deferred） | 语义精确化 |
| 6 | Full pair cache REJECTED（尺寸过大） | 仍 REJECTED；理由更新：融合方案根本不需要 replay 表，物化动机消失；尺寸结论不变 | D2 |
| 7 | 内存模型 = MB/source cache 估算（39.8/107.8/159.1 MB） | 内存模型 = `M_frozen_ray_cache + Bf × M_frequency_field (+×Nsource) + per-ray temporaries`（D6/Q4） | 无几何 cache 存在 |
| 8 | 集成点位于现有 `solveFrequencyFromCache()` 内（audit §15.3，L998-1010） | **被取代**：融合横跨频率重构循环，无法住在单频入口内；需要在 serial solver 层新建 multi-frequency fused 入口，既有 per-frequency 路径原样保留为 reference/fallback | 融合改变的是 `(freq → ray)` 嵌套方向，单频函数签名表达不了 |
| 9 | §5.2 measurement-driven 决策流（probe crossing/lerp 开销 → 1B vs 6） | 取消该选型流；measurement 移到 GATE 层（fused vs 现行 baseline 的 net-gain 实测） | 原型对象已换 |
| 10 | §6 cache invalidation/并发契约（围绕持久 cache 写成） | 随 cache 降级一并搁置；v1 无几何 cache → 无 invalidation 面；frozen cache 完整性校验沿用 fingerprint before/after | D2/D8 |
| 11 | 13 项 IGR1-GATE（§9.2，为 cache build/replay 写成） | 改写为 **gate set v2**（下表逐项映射） | 同上 |

### 13 项旧门禁 → gate set v2 映射

| 旧门禁 | 领域 | v2 处置 | v2 要点 |
|---|---|---|---|
| IGR1-GATE-01 | Lossy/Lossless 复走时 | **CARRIED** | 有损 τ 逐频精确积分，实虚部均不从冻结几何污染；融合路径按 (ray,f) 调 projector，语义不变 |
| IGR1-GATE-02 | Source/Reflection threshold | **CARRIED** | source 初始 active；`<0.005F` 累计 cutoff；`<1e-5F` 仅半空间单次抑制；语义逐条复验 |
| IGR1-GATE-03 | First inactive terminal segment | **AMENDED** | per-frequency terminal retention 不变；新增 D5：遍历上界 = 各频 prefix 并集，且每频保留自身左端点检查 |
| IGR1-GATE-04 | Regular grid crossing 一致性 | **CARRIED** | crossing 集一次计算即天然一致；仍须实测验证 |
| IGR1-GATE-05 | Irregular layout & fallback | **AMENDED** | fused v1 仅覆盖 rectilinear/uniform-range CC coherent；其余（irregular、RC、Geo 族、SimpleGaussian）一律显式走既有路径，禁止静默进入 |
| IGR1-GATE-06 | C/I/S 模式 | **AMENDED（收窄）** | v1 coherent pressure only；I/S 经既有路径（输出不变由 construction 保证并抽验）；C+I 统一列为后续候选（见 §H） |
| IGR1-GATE-07 | Multi-source isolation | **DEFERRED** | single-source-first；多 source 的 `Nsource × Bf` workspace 与 writer "每频率需全部 source"约束（`shd_writer.cpp:150-153`）是推迟原因；未来选项见 §I |
| IGR1-GATE-08 | Serial/Reuse/Parallel 一致 | **AMENDED（收窄）** | v1 = fused serial vs 既有 serial 逐字节一致；parallel 路径不触碰、不回归；无共享可变几何状态 → 无锁议题 |
| IGR1-GATE-09 | Fallback path parity | **CARRIED** | execution-policy 开关：fused-off 即既有路径；开关两侧输出一致需 parity 验证 |
| IGR1-GATE-10 | Byte-identical parity | **CARRIED（基线重锚）** | 对照基线改为 IGR-1 启动时的 accepted HEAD；SHA-256 对齐；见 §F 论证 |
| IGR1-GATE-11 | Peak & memory budget | **REWRITTEN** | 新内存模型：实测 `Nf ×` field bytes + per-ray temporaries + peak RSS；**无几何 cache 存在**；不得按结构体理论大小宣称 PASS |
| IGR1-GATE-12 | Origin/F2CPP oracle | **CARRIED** | 全部标准算例继续通过 |
| IGR1-GATE-13 | Wall-time / viability | **EXTENDED** | 计量扩展：wall、Influence、Project、peak RSS、range/depth/image evaluations，**新增 frequency-kernel evaluations 计数**；协议（预热排除、≥5 次、中位数+分散度、交替运行、噪声以上净收益、`NOT_VIABLE` 逃生门）全部继承 |

---

## E. 修订架构图

```text
                ┌────────────────────────────────────────────────┐
                │ Trace once（频率无关；每 source 一次）            │
                │ SerialRayReuseSolver::solveStreaming            │
                │ serial_ray_reuse_solver.cpp:44-45               │
                └───────────────────┬────────────────────────────┘
                                    v
                ┌────────────────────────────────────────────────┐
                │ Frozen RayPathCache（不变，D8）                  │
                │ x / slowness / c / τ_real / p1,p2 / q1,q2 /     │
                │ reflection events / quadrature                  │
                └───────────────────┬────────────────────────────┘
                                    │  per ray（一次取 ray）
                ┌───────────────────┴────────────────────────────┐
                │ Nf 份频率局部 per-ray temporary（D3/D4/D5）       │
                │  - Nf × RayFrequencyState（projector 语义不变）  │
                │  - Nf × ε(f)（pickBeamEpsilon）                 │
                │  - Nf × precompute p/q/γ/kmah（G 部分每 ray 一次）│
                │  - 遍历上界 = 各频 active prefix 的并集           │
                └───────────────────┬────────────────────────────┘
                                    v
                ┌────────────────────────────────────────────────┐
                │ 一次 segment 遍历（rightIndex = 2 .. unionPrefix）│
                │  - crossing 拓扑 firstUpper/secondUpper：一次    │
                │  - per range：W、插值 position/slowness/c：一次  │
                │  - per (depth,image)：Δz、polarity：一次          │
                └───────────────────┬────────────────────────────┘
                                    v
                ┌────────────────────────────────────────────────┐
                │ frequency-local 波束求值（innermost，per f）      │
                │  共享 W → 插值 q(f)/τ(f)/γ(f)；ε(f)；principal；  │
                │  window −ω·Im(γ)·Δz²；taper 30c0/f；phase；exp   │
                └───────────────────┬────────────────────────────┘
                                    v
                ┌────────────────────────────────────────────────┐
                │ workspace[f] 累加（每频独立 FrequencyWorkspace，  │
                │ 按 (ray,segment,range,depth) 升序，见 §F）        │
                └───────────────────┬────────────────────────────┘
                                    v
                SHD 输出：遍历完成后按频率顺序 writeFrequency(f)
                （writer API / 文件格式不变，seek-addressed records）

  ── 未来策略层（同一算法，仅 policy，D7）──────────────────────────
   内存受限时：Bf < Nf 频率分块（"对每个频率块：块内融合遍历"）
   Bf = Nf 全频融合（v1） ； Bf = 1 → 退化为现行 frequency-major reuse
```

---

## F. 累加顺序 / 逐字节一致性分析（必读）

把 frequency 循环移到 receiver-geometry 单元内部，**只有**在"每个频率的 per-ray 贡献累加顺序保持不变"时才是合法变换。

**论证。** 今天 frequency-major 下，固定 f，`workspace[f]` 的累加顺序为：

```text
ray(0..N-1) → segment(2 .. prefix_f) → range(firstUpper+1 .. secondUpper)
→ depth(0 .. D-1) → image(True → Surface → Bottom)
```

（`single_frequency_solver.cpp:280`、`cartesian_cerveny_influence.cpp:678、719、758`、`evaluateImageContributions` 467-487。）

融合结构中 frequency 位于 receiver-geometry 单元最内层。对每个固定 f：

1. ray 仍按升序遍历一次（多 ray 对同一 cell 的累加保持升序 ray 序，因为每 ray 只经过一次）；
2. segment 仍升序；对超出该频自身 prefix 的 segment，该频以**自身的**左端点 active 检查（D5）跳过 —— 与今天 loop bound 在 `prefix_f` 截断等价：这些 segment 对 `workspace[f]` 的贡献为零且不产生加法；
3. range、depth、image 顺序逐层不变；
4. 每个频率的算术表达式序列不变：共享几何量（W、position、slowness、c、Δz、polarity）以相同输入、相同运算**计算一次**得到相同 double，下游 per-f 算术（插值 q/τ/γ、principal、window、taper、phase、exp、累加）逐表达式与现行路径一致。

因此 **byte-identical 输出是设计目标（designed to preserve bitwise parity），必须由 V2-GATE（承 IGR1-GATE-10，基线重锚）强制验证**，不得仅凭本论证宣称达成。比较必须在同一二进制、同一编译/FP 环境下进行（见 §I）。任何改变 per-frequency 累加顺序的后续优化（如重排 image/depth 序、向量化改结合律）都必须走显式数值设计评审，不得搭车。

补充两点边界：`gamma.imag()>0` 早退（745-747）与 window/taper 判定依赖 per-f 的 γ/ω/radiusMax，必须保留在频率层内部，不得提升为共享判定；segment 级越界早退（689-695）与退化段过滤（696-699）是纯几何，可安全提升为共享判定。

---

## G. 性能目标框架

**目标不是 "Influence 加速 = Nf"，而是"消除跨频重复的几何遍历"。** 以下计算本质上属于每个频率、融合后**合法保留** per-frequency：beam width（σ1/radiusMax）、γ(f)、window 判定、taper、attenuation、reflection、复指数、pressure 累加。

IGR-1 性能门禁必须计量（对照现行 reuse baseline）：

| 指标 | 说明 |
|---|---|
| end-to-end wall time | 净收益判据用 |
| Influence time | 热点相位 |
| Project time | 应基本不变（projector 仍 per (ray,f)） |
| peak RSS | 新内存模型（V2-GATE） |
| receiver-range evaluations | 现有 `--profile-influence` 计数，融合后应 ≈ 1/Nf |
| receiver-depth evaluations | 同上 |
| image evaluations | 同上；筛选比例应大体保持 |
| **frequency-kernel evaluations（新增）** | 每 (range, f) 频率局部核求值次数；与 range 计数分离，用于证明"几何一次、频率 Nf 次" |

Benchmark 协议继承旧 IGR1-GATE-13：固定机器/环境、排除预热、`≥5` 次重复、报告中位数与分散度、条件允许交替运行、相同构建/线程/输入设置；PASS 条件 = 不降低数值正确性前提下，代表性 16 频负载表现出**超出运行噪声的可重复正向 end-to-end wall-time 净收益**；若净收益不成立 → `NOT_VIABLE`，不得因代码已写而强行合入。

---

## H. IGR-1 拟议范围（"Cross-Frequency Cartesian Cerveny Influence Fusion"）

### H.1 MVP 边界评估（对照源码事实）

| 提议边界 | 评估 | 依据 |
|---|---|---|
| TL only | 合适 | IGR 只作用于 TL field 路径；A/E/RAY 产品不受影响 |
| Cartesian Cerveny only | 合适 | CC 是唯一实测热点家族（99.6% Influence 占比即 CC Munk 测得）；RC 结构不同且 deferred（Q3） |
| coherent pressure first | 合适（推荐 v1 保持 coherent-only） | coherent 复声压是主路径；intensity 累加（823-837）共享同一融合结构与几何，是同一 skeleton 上的自然后续，但若并入 v1 增加验收面，收益有限——列为 fast-follow 候选，不阻塞 |
| rectilinear receivers first | 合适 | 与旧 GATE-05 的 native domain 一致；irregular 走 fallback |
| single source first | 合适 | 多 source 真实成本是 `Nsource × Bf` workspace 常驻，且 `writeFrequency` 要求每次提供全部 source（`shd_writer.cpp:150-153`）；single-source 先把算法做对 |
| shared launch fan（单一 frozen trace fan，无 per-frequency fan） | 合适（且是唯一选项） | trace 今天就是频率无关的（`serial_ray_reuse_solver.cpp:44-45`），不存在 per-frequency fan |
| `RayPathCache` 不变 | 合适 | D8 |
| `FrequencyProjector` 语义不变（仍 per ray per frequency，只是每 ray 一次做 Nf 份） | 合适 | projector 是频率局部物理，0.5 s/16F，非热点；Semi-coherent Lloyd 因子在 projector 之前的 per-(ray,f) 标量层（`single_frequency_solver.cpp:287-292`），天然随 Nf 份状态融合，无需额外设计 |
| all-frequency fusion（Bf=Nf） | 合适 | Munk 16F workspace ≈ 26 MB，可忽略；64F ≈ 103 MB 仍可行，blocking 留作策略（D7） |
| 无持久几何 cache / 无自动 blocking / 无并行 / 无频域插值 | 合适 | D2/D7/D9；frequency interpolation 一直是明令禁区 |

结论：**该 MVP 是覆盖真实热点的最小切片**，各项边界均有源码与实测依据，不做调整。唯一建议的显式备注：C+I 统一与 Lloyd semi-coherent 融合列为 fast-follow 候选（见上表），不进 v1 验收面。

### H.2 OUT OF SCOPE（逐字冻结）

RayPath/RayPathCache schema changes; frequency interpolation/reconstruction; rolling FrequencyProjector rewrite; persistent full geometry cache; receiver-depth/image materialization; SIMD; GPU; nested parallelism; automatic memory blocking; all-family unification; Arrival/Eigenray IGR; unrelated refactor.

### H.3 IGR-1 设计层注记

- **新 fused serial 入口与既有路径并存**：在 serial solver 层新增 multi-frequency fused 入口（D 集成点结论），既有 per-frequency 路径原样保留为 reference/fallback；以 execution policy 开关选择，fallback parity 为门禁（承 IGR1-GATE-09）。
- **per-frequency timing attribution 变为 block-level**：融合路径中 per-frequency 的 phase 时间无法逐频剥离（遍历共享）；计时按块（traversal 共享段 + frequency-kernel 段）报告并文档化，不得伪造逐频精度。
- **SHD consumer 顺序调用**：遍历完成后按频率顺序调用 `writeFrequency(f)`；writer API 不变。注意峰值内存语义：全部 Bf 个 workspace 常驻至 consumer 逐频取走（single-source 16F ≈ 26 MB，可接受）。
- 任务分解、gate set v2、go/no-go 详见 [`../worklists/IGR-1_CC_FUSION_DESIGN_DRAFT.md`](../worklists/IGR-1_CC_FUSION_DESIGN_DRAFT.md)。

---

## I. 开放风险

| # | 风险 | 现状与缓解 |
|---|---|---|
| 1 | 多 source 内存与 writer API：`Nsource × Nf` workspace 常驻；`writeFrequency` 每频率需全部 source（`shd_writer.cpp:150-153`） | single-source-first 规避；未来选项：per-source writer 记录（writer 扩展）或 frequency blocking（D7），IGR-1 不决定 |
| 2 | RC / GeoGaussian 家族统一 deferred | 结构不同（Q3）；M7 只封持久化，不封 transient；不阻塞 CC |
| 3 | byte-parity 依赖编译器 codegen/FP 环境 | 比较必须在同一二进制内进行（fused 与 fallback 同 build）；任何 FP 收缩/重排差异都会在 V2 parity 门禁暴露 |
| 4 | 大 Nf（64F+）workspace 增长 | 64F ≈ 103 MB 尚可；blocking 策略已冻结设计但未实现（D7），触发条件由 V2 内存计量决定 |
| 5 | per-frequency profile 计数需扩展 | frequency-kernel evaluations 为新计数器；现有 `--profile-influence` 计数器需在融合路径下保持定义一致（R01） |

---

## J. 本轮零 Production 改动声明

本轮 IGR-0-REV **未修改任何 production code**（`Bellhop_RayReuse/src`、`Bellhop_RayReuse/include`、`Bellhop_F2CPP/**`、`Bellhop_origin/**`、tests、cases 均零改动，未运行 benchmark）。本轮创建/修改的文件（全部为文档）：

| 文件 | 动作 |
|---|---|
| `Bellhop_RayReuse/doc/reports/REPORT_IGR0_REVISION_CROSS_FREQUENCY_FUSION_2026-09-01.md` | 新建（本报告） |
| `Bellhop_RayReuse/doc/worklists/IGR-1_CC_FUSION_DESIGN_DRAFT.md` | 新建（IGR-1 设计草案） |
| `Bellhop_RayReuse/doc/reports/REPORT_IGR0_INFLUENCE_GEOMETRY_REUSE_AUDIT.md` | 顶部追加 SUPERSEDED (PARTIAL) banner |
| `Bellhop_RayReuse/doc/worklists/IGR-0_INFLUENCE_GEOMETRY_REUSE_AUDIT_WORKLIST.md` | 顶部追加同款 banner |
| `doc/reference/REFERENCE_INFLUENCE_GEOMETRY_REUSE.md` | 顶部追加实现策略 superseded banner |
| `doc/plans/PLAN_CURRENT_WORK.md` | 状态行更新 |

```text
================================================================================
              IGR-0 REVISION STATUS: ACCEPTED (final review 2026-09-01)
================================================================================
 1. 范围与约束遵从性：
    - [x] 零 production 修改（src/include/F2CPP/Origin/tests/cases）
    - [x] 未运行 benchmark，未做 frequency interpolation
    - [x] 未进入 IGR-1 施工（仅 DESIGN DRAFT，NOT APPROVED）
 2. 冻结输出：
    - [x] D1-D9 决策与理由（§C）
    - [x] 旧结论逐项取代表 + 13 门禁 → gate set v2 映射（§D）
    - [x] 累加顺序 / byte-parity 论证（§F，designed-for，须门禁验证）
    - [x] IGR-1 MVP 边界评估与 OUT OF SCOPE（§H）
 3. 状态合规：独立 final-review 验收 ACCEPTED（2 项 MINOR findings 已闭环：
    F1 引用文件名已补全，F2 历史 review 文件按 history-preserved 原则维持原状）
================================================================================
```
