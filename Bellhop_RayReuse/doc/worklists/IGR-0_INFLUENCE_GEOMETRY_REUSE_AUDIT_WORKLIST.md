# IGR-0 — Influence Geometry Reuse Audit Worklist

> **SUPERSEDED (PARTIAL) — 2026-09-01。** IGR 主方案已由用户重新决策为
> **Cross-Frequency Influence Geometry Fusion（transient reuse via loop restructuring）**。
> 本 Worklist 冻结的原型方向约束（Candidate 1B 首选原型、persistent cache 契约、
> 13 项 IGR1-GATE）由 IGR-0 Revision 取代；分类与 threshold 语义结论继续有效。
> 见 [`../reports/REPORT_IGR0_REVISION_CROSS_FREQUENCY_FUSION_2026-09-01.md`](../reports/REPORT_IGR0_REVISION_CROSS_FREQUENCY_FUSION_2026-09-01.md)。
> 本文件为历史记录，按原样保留（IGR-0 批次本身的验收状态仍以其时点为准）。

> **Batch:** IGR-0
> **Feature / Task:** Influence Geometry Reuse (IGR) Architectural Audit & Baseline Verification
> **Status:** **`READY_FOR_REVIEW`**
> **Review Baseline:** `main @ ba653229560aac53eb61eeaa5fdb2c2fd3254338`（Audit branch: `feat/igr-influence-geometry-reuse`）
> **Working Tree Policy:** Read-only inspection on production code; no production edits; no IGR-1 implementation; no frequency interpolation; no large benchmarks.
> **Coordinator:** Gemini 3.7 Flash
> **Review Input:** `Bellhop_RayReuse/doc/reviews/IGR0_CODEX_REVIEW_CHANGES_REQUIRED.md`
> **Report Target:** `Bellhop_RayReuse/doc/reports/REPORT_IGR0_INFLUENCE_GEOMETRY_REUSE_AUDIT.md`

---

## 1. Remediation Scope & Guidelines

本 Worklist 用于跟踪 IGR-0 审计报告针对 Codex CHANGES_REQUIRED 与 Re-Final Review 评审意见的整改闭环与基准冻结。

### 核心约束：
1. **零生产代码修改：** 禁止修改 `Bellhop_RayReuse/src/` 或 `include/` 中的生产代码；
2. **禁止提前进入 IGR-1：** 本轮只做架构审计、契约设计与失效分析，禁止任何 IGR-1 prototype 施工；
3. **禁止频域插值（Frequency Interpolation）：** 本审计专注无损几何复用（Influence Geometry Reuse），严禁引入对快速震荡相位或场量的插值近似；
4. **禁止未经实现的绝对声明：** 严禁使用 "100% Bitwise Parity"、"100% byte-identical"、"zero numerical risk" 等未经实施验证的绝对用语，统一使用严谨工程表述（"designed to preserve bitwise parity"、"requires IGR-1 byte-parity verification"）；
5. **内存预算核算标准：** 明确所有缓存估算均为单声源（per-source 5k-ray shared-cache basis）基准估算，多声源线性累加，并发工作区另计，且必须受 implementation memory-budget gate 约束；未建立完整 schema/population 的候选方案一律标记 `MEMORY_NOT_FROZEN`；
6. **候选范围收缩：** 仅冻结 Candidate 1B 为首选 IGR-1 原型（Candidate 1A 为可选实验变体），Candidate 2 标记为 DEFERRED/QUESTIONABLE，Candidate 4/5 标记为 INVALID，Candidate 3/7/8/9 统一降级为 `EXPLORATORY / DEFERRED`；
7. **最终状态锁定：** 报告与 Worklist 最终状态为 `READY_FOR_REVIEW`，不得自行标记 `ACCEPTED`。

---

## 2. Codex Findings Closure Matrix

| Finding ID | 严重级别 | 缺陷描述 | 整改动作与证据 | 状态 |
|---|---|---|---|---|
| **C1** | **Critical** | Candidate 1/2 使用 `uint16` 索引（仓库支持 2,000,000 点）并将数据降为 `float`，破坏 bitwise parity，内存/收益计算无效 | 废除 `uint16`/`float`，Schema 升级为 `uint32_t` 与 IEEE-754 `double`；以单声源 5k 射线基准重新核算内存（1B 拓扑 ~39.8 MB/source，1A 几何 ~159.1 MB/source）；删除未经实现的绝对 parity 声明。 | **CLOSED** |
| **M1** | **Major** | `complexTravelTime` 分类错误（有损介质下实部亦随频率变化） | 纠正分类：无损介质下实部为 G；有损介质下实部与虚部均为 F（因 $\frac{1}{c+ic_i} = \frac{c-ic_i}{c^2+c_i^2}$ 包含 $c_i(f)$）。 | **CLOSED** |
| **M2** | **Major** | `active/terminal prefix` 机制描述不准，混淆了衰减与阈值 | 纠正描述：source point 初始 active；$<0.005\text{F}$ 是 transition 后累计 projected amplitude 的 active cutoff；衰减不改 active；$<10^{-5}\text{F}$ 仅为声学半空间分支中单次 raw reflection coefficient 的 suppression threshold；保留首个 terminal inactive 点的进入段贡献。 | **CLOSED** |
| **M3** | **Major** | 热点与收益结论缺少证据（`fortranUpperRangeIndex` 为 $O(1)$ 非二分查找；3–6x / 5–10x 缺少实测依据） | 纠正 `fortranUpperRangeIndex` 为 $O(1)$ 算术截断；移除所有未经实测的 3–6x/5–10x 加速比与 L3 缓存命中率断言；引用 F1 基准实测数据作为热点依据；移除固定 $\ge 10\%$ 门槛，改为 measurement-driven 选型。 | **CLOSED** |
| **M4** | **Major** | Munk 维度写反（实际为 201 depths, 501 ranges），Candidate 2 深度收益推导无效 | 纠正 Munk 维度：201 深度，501 距离；废除基于 501 深度推导的 Candidate 2 收益；将 Candidate 2 标记为 DEFERRED/QUESTIONABLE。 | **CLOSED** |
| **M5** | **Major** | Candidate 1 跨波束族适用性被夸大（CC 是水平跨越，GeoHat/GeoGauss 是 2D 点法向投影） | 严格收缩 Candidate 1 为 **CC-only per-source Segment–Range Crossing Topology Cache**；明确仅覆盖规则均匀接收距离网格，非均匀/不规则网格走 existing path / fallback。 | **CLOSED** |
| **M6** | **Major** | Cache key、ownership、invalidation 生命周期不完整（未指明 per-source，未绑定完整布局） | 补充完整契约：Geometry cache 必须为 per-source 结构，由求解器拥有，生命周期与 `RayPathCache` 绑定；纳入 `selectActiveFrequencyLimit()` 与 `estimatedPeakMemoryBytes` 预算。 | **CLOSED** |
| **M7** | **Major** | Candidate 4（Ray-Centered Crossing/Flip）拓扑不能跨频共享（active prefix 影响 flip parity） | 将 Candidate 4 方案标记为 **INVALID**，明确射线中心法向反转状态机不能跨频冻结。 | **CLOSED** |
| **M8** | **Major** | 遗漏 4 个关键几何复用候选并缺乏收缩 | 增补 Candidate 6（CC 点几何与梯度投影束，实测 ~107.8 MB/source）；将未建立完整契约的 Candidate 3/7/8/9 统一降级为 `EXPLORATORY / DEFERRED` 并标记 `MEMORY_NOT_FROZEN`。 | **CLOSED** |
| **M9** | **Major** | 缺少 IGR-0 Worklist 与 Batch Acceptance Gates；自封 ACCEPTED | 建立本 Worklist 与第 4 节可执行的 IGR-0 与 IGR-1 冻结门禁；将状态保持为 `READY_FOR_REVIEW`。 | **CLOSED** |
| **m1** | **Minor** | 行号与当前源码漂移 | 对照当前分支源码全面核准并更新行号与签名。 | **CLOSED** |
| **m2** | **Minor** | 8 处 git diff trailing whitespace | 清理全部行尾空白字符，确保 `git diff --check` 干净。 | **CLOSED** |
| **m3** | **Minor** | `rayOffsets_` 索引上限保护 | 声明全局偏移必须支持累积总条目数（使用 `std::size_t` 或 `uint64_t`）。 | **CLOSED** |

---

## 3. Ordered Task List

### T01 [ADVANCED] — 状态分类与阈值语义精确纠偏
- **Status:** DONE
- **Acceptance:**
  1. `complexTravelTime` 明确区分为 lossless（实部 G，虚部 0）与 lossy（实部 F，虚部 F）；
  2. 修正阈值语义：source point 初始 active；$<0.005\text{F}$ 为累计振幅 active cutoff；介质吸收衰减不改 active；$<10^{-5}\text{F}$ 仅为单次声学半空间反射抑制阈值；首个 terminal inactive 点进入段仍有效；
  3. `workspace` 与 `receiver contributions` 明确分类为 frequency-local（O）。

### T02 [ADVANCED] — 候选方案重审与范围收缩
- **Status:** DONE
- **Acceptance:**
  1. Candidate 1 严格收缩为 CC-only per-source Segment–Range Crossing Topology Cache（冻结 1B 拓扑为首选，1A 几何为可选变体）；
  2. Candidate 6（CC 点几何与梯度投影束）作为有效协同备选项（~107.8 MB/source）；
  3. Candidate 2（Bounded Depth Set）标记为 `DEFERRED / QUESTIONABLE`（标记 `MEMORY_NOT_FROZEN`）；
  4. Candidate 4（Ray-Centered Flip Stencil）与 Candidate 5（Universal Multi-Family Cache）正式标记为 `INVALID`；
  5. Candidate 3、7、8、9 统一降级为 `EXPLORATORY / DEFERRED`（标记 `MEMORY_NOT_FROZEN`，删除未经证明的适用性与内存声明）。

### T03 [ADVANCED] — 严密数据结构与 F1 Population 内存重新核算
- **Status:** DONE
- **Acceptance:**
  1. 废除 `uint16` 和 `float`；点索引采用 `uint32_t`，浮点几何量采用 IEEE-754 `double`；
  2. 修正 F1 统计基数：2 频 10,000 射线 / 3.37M 点对应单声源 5,000 射线 / 1.68M 点；
  3. Candidate 6 修正为 ~107.8 MB/source；Candidate 1B 修正为 ~39.8 MB/source（1A 为 ~159.1 MB/source）；
  4. 声明全局 offsets 使用 `std::size_t` / `uint64_t`。

### T04 [STANDARD] — 性能、热点与 Benchmark 协议冻结
- **Status:** DONE
- **Acceptance:**
  1. 纠正 `fortranUpperRangeIndex` 为 $O(1)$ 算术计算；修正 Munk 维度（201 depths, 501 ranges）；
  2. 删除所有虚假 3–6x 加速比与固定 $\ge 10\%$ 选型阈值，改为 measurement-driven；
  3. 冻结 GATE-13 的 Benchmark Protocol（预热排除、$\ge 5$ 次重复、中位数+分散度、包含构建耗时、净加速判定）。

### T05 [ADVANCED] — 缓存生命周期、所有权、Key 与失效契约设计
- **Status:** DONE
- **Acceptance:**
  1. 明确 Geometry Cache 为 per-source 结构，由求解器拥有，生命周期与 `RayPathCache` 绑定；
  2. 明确 Cache Key 必须包含：Source Cache Fingerprint、完整 Receiver Range 布局、CC/Coordinate/Traversal Semantic Version 及 Checked Offsets；
  3. 纳入 `selectActiveFrequencyLimit()` 与 `estimatedPeakMemoryBytes` 动态预算控制。

### T06 [GENERAL] — 报告全面修订与 Batch Acceptance 设立
- **Status:** DONE
- **Acceptance:**
  1. 报告与 Worklist 全面更新，清理 trailing whitespaces，通过 `git diff --check`；
  2. 状态保持为 `READY_FOR_REVIEW`；
  3. 设立 8 项 IGR-0 审计门禁与 13 项冻结的 IGR-1 实施门禁。

---

## 4. Acceptance Contract（验收门禁契约）

### 4.1 IGR-0 阶段审计门禁（Audit Gates — 本阶段验证）

| 门禁 ID | 门禁名称 | 验收判定准则 | 状态 |
|---|---|---|---|
| **GATE-1** | **无生产代码修改门禁** | `git diff -- Bellhop_RayReuse/src Bellhop_RayReuse/include` 为空。 | **PASS** |
| **GATE-2** | **无频域插值门禁** | 报告与方案明确禁止在 IGR 中引入任何频域插值或快速相位近似。 | **PASS** |
| **GATE-3** | **状态分类准确门禁** | Lossy/Lossless 走时、Active 前缀、Threshold 语义严格符合源码。 | **PASS** |
| **GATE-4** | **Schema 精度与范围门禁** | 索引支持 2,000,000 点（`uint32_t`），几何浮点量保留 `double`。 | **PASS** |
| **GATE-5** | **Cache Key 与所有权门禁** | 明确 per-source 生命周期、求解器持有模型、完整的 Cache Invalidation 条件。 | **PASS** |
| **GATE-6** | **无效候选剔除门禁** | Candidate 4 与 Candidate 5 正式标记为 INVALID，Candidate 3/7/8/9 降为 EXPLORATORY。 | **PASS** |
| **GATE-7** | **证据与热点真实性门禁** | 修正 $O(1)$ 查找、Munk 维度与 5k-ray population accounting，引用 F1 实测数据。 | **PASS** |
| **GATE-8** | **报告状态合规门禁** | 报告与 Worklist 状态统一标记为 `READY_FOR_REVIEW`，附 Codex 闭环矩阵。 | **PASS** |

---

### 4.2 IGR-1 原型实施门禁契约（Frozen IGR-1 Prototype Acceptance Gates — 冻结待实现）

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
