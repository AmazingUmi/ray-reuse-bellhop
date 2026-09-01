# IGR-1 — Cross-Frequency Cartesian Cerveny Influence Fusion — DESIGN DRAFT

> **状态：** **`DESIGN DRAFT — NOT APPROVED, NOT IN CONSTRUCTION`**
> 本文件是 IGR-1 的 DESIGN 级草案，等待用户批准后才进入 IGR-1 DESIGN/CONSTRUCT。
> 在获批并冻结为正式 `IGR-1_WORKLIST` 之前，本文件不代表任何已授权施工。
>
> **设计来源：** [`../reports/REPORT_IGR0_REVISION_CROSS_FREQUENCY_FUSION_2026-09-01.md`](../reports/REPORT_IGR0_REVISION_CROSS_FREQUENCY_FUSION_2026-09-01.md)（IGR-0 Final Remediation，`ACCEPTED`）
> **基线：** `main @ ba653229560aac53eb61eeaa5fdb2c2fd3254338`；IGR-1 启动时 byte-parity 基线重锚至当时 accepted HEAD
> **日期：** 2026-09-01
> **风险等级：** 全部任务 `[ADVANCED]`（数值算法 / 多频状态 / ownership / 高风险性能路径）

---

## 1. Scope（DESIGN 级）

在 `Bellhop_RayReuse` TL 生产路径中，为 **Cartesian Cerveny coherent pressure** 建立 **cross-frequency fused influence traversal**：

```text
source → ray → segment → receiver range
       → per-frequency range-state preparation
       → receiver depth → image geometry → frequency-local image kernel
```

一次 receiver-geometry 求值（crossing 拓扑、W、插值 position/slowness/c、Δz、Δz²、polarity）被块内全部频率立即消费；不引入任何持久几何 cache。q/τ/γ/principal 是 range-level frequency state，先在共享 range geometry 后逐频准备；image kernel 的 frequency loop 位于 image geometry 内。segment 左端点产生 `activeMask[f]`；每个 crossed range 从该 mask 重新初始化 `rangeEligible[f]`，并在该频率 `gamma.imag()>0` 时清零。只有 range-eligible frequency 才初始化/使用 `corrected[f]`、进入 depth/image kernel 与写入 workspace。每频保持独立 `imageSum[f]`，按 True → Surface → Bottom 累加后，才执行一次 `workspace[f] += corrected[f] * imageSum[f]`。

**v1 边界（MVP）：**

- TL only；Cartesian Cerveny only；coherent pressure only（C+I 统一与 Lloyd semi-coherent 融合为 fast-follow 候选，不进 v1 验收面）；
- rectilinear / uniform receiver ranges only（其余一律显式走既有路径）；
- single source only（多 source 延后，见风险 1）；
- shared launch fan：单一 frozen trace fan（trace 本就频率无关）；
- `RayPathCache` 不变（D8）；`FrequencyProjector` 语义不变（仍 per ray per frequency，每 ray 一次产出 Nf 份）；
- all-frequency fusion：Bf = Nf；无自动 blocking（D7 仅冻结设计）；无并行（D9）；无频域插值（禁区）。

## 2. Frozen Decisions

继承 IGR-0-REV D1–D9（见设计来源报告 §C），要点重述：

1. **D1** transient cross-frequency geometry reuse；几何量单次遍历立即消费，不长期存储。
2. **D2** 无持久几何 cache；full pair 物化 REJECTED。
3. **D3** 频率局部物理边界（complex τ、attenuation、reflection amplitude/phase、active prefix、ε(f)、p/q/γ 频率局部组合、σ1、window membership、复相位/指数、pressure/intensity 累加）绝不写回 frozen cache。
4. **D4** dynamic-ray 基（p1/p2、q1/q2、几何、events、quadrature）继续来自 frozen `RayPath`；`pVB(f)=p1+ε(f)p2` 等在频率局部层构造。
5. **D5** per-frequency active prefix：差异来自 frequency-local source/reflection amplitude evolution 与累计 cutoff，不直接来自 volume attenuation；遍历上界 = 各频 prefix 并集；每频独立保留左端点 active 检查、terminal retention、`<0.005F` cutoff。
6. **D6** Bf = Nf；额外长期内存 ≈ `Nf ×` frequency field workspace。
7. **D7** frequency blocking 是同一算法的内存策略，v1 不实现。
8. **D8** frozen `RayPathCache` 契约不变（STATUS_PROGRESS L58-70）。
9. **D9** 并行延后；先 fused serial/reference path。

IGR-1 特定冻结：

10. **集成点**：serial solver 层新增 multi-frequency fused 入口；既有 per-frequency 路径原样保留为 reference/fallback；execution-policy 开关选择，禁止删除或改写既有路径语义。
11. **计时语义**：fused 路径 per-frequency Influence 时间不可逐频剥离；按 block-level 报告共享 traversal 与 frequency-kernel，并单独报告 Project、Influence、Scale、wall，不伪造逐频计时精度。
12. **byte-parity 目标**：Conditions designed to preserve bitwise parity；同一二进制内 fused vs fallback 依次验证 cache fingerprint、raw workspace、scaled workspace 与 SHD SHA-256，不得凭设计宣称达成。
13. **image addition shape**：每频 `imageSum[f]` 保持 True → Surface → Bottom，完整 imageSum 后只向 workspace 加一次；禁止逐 image 写 workspace。
14. **并行与重结合禁止**：v1 不引入 tree/ray-local/thread-local reduction、SIMD reassociation 或 depth/image reorder。

## 3. Task Decomposition（coarse，全部 [ADVANCED]）

### R01 [ADVANCED] Profile-counter extension 与基线测量
Status: TODO
Reviewer: N/A

Acceptance:
- 将统计明确拆为 geometry-side：`geometrySegmentEvaluations`、`geometryRangeEvaluations`、`geometryDepthEvaluations`、`geometryImageGeometryEvaluations`；
- frequency-kernel：`frequencyRangeKernelEvaluations`、`frequencyImageKernelEvaluations`、`windowRejections`、`taperRejections`、`nonzeroImageContributions`；
- 旧 `imageEvaluations` 只能作为 frequency-image-kernel 口径迁移，不能继续兼任纯几何计数；
- 在 accepted HEAD 上采集现行 reuse baseline（16F Munk：wall/Project/Influence/Scale/peak RSS/各计数器），作为 R06 对照基线。

Evidence:
- 计数器定义与 baseline JSON/报告存档；无 production 之外文件改动。

### R02 [ADVANCED] Fused CC coherent pressure 路径（execution-policy 开关之后）
Status: TODO
Reviewer: N/A

Acceptance:
- 新 fused serial 入口：per ray 一次取 ray → Nf 份 `RayFrequencyState` + Nf 份 ε + Nf 份 precompute → 一次 segment/range traversal → range-level per-f q/τ/γ/principal preparation → depth → image shared geometry → per-f image kernel；
- 共享判定只提升纯几何项（crossing 拓扑、W、插值 position/slowness/c、Δz、Δz²、polarity、越界/退化段过滤）；`gamma.imag()>0`、window/taper、phase、exp 保留在频率层；
- segment 级 `activeMask[f]` 来自左端点 active；每个 crossed range 初始化 `rangeEligible[f]=activeMask[f]`，只对 active frequency 计算 q/τ/γ，且在 `gamma[f].imag()>0` 时立即置 `rangeEligible[f]=false`；若全 false 则跳过该 range；
- 只对 range-eligible frequency 初始化/使用 principal/corrected、执行 depth/image kernel 和最终 workspace add；每频维护瞬时 `imageSum[f]`，严格按 True → Surface → Bottom 累加，之后只执行一次 `workspace[f] += corrected[f] * imageSum[f]`；
- 开关关闭 = 现行路径逐指令不变；fallback 永远可达。

Evidence:
- 针对性 component tests + V2-GATE-07 byte-parity 初验。

### R03 [ADVANCED] Active-prefix union 语义
Status: TODO
Reviewer: N/A

Acceptance:
- 遍历上界 = 各频 active prefix 的并集；每频以自身左端点 active 检查跳过自身 prefix 外 segment；
- per-frequency terminal-point retention、inactive 后缀抑制、`<0.005F` cutoff 与现行路径语义逐条等价；
- 构造多频不同 cutoff 的针对性测试（frequency-local source/reflection amplitude evolution 使各频 prefix 不同；不得把 volume attenuation 写成 active 的直接原因）。

Evidence:
- 针对性 prefix 差异测试 + oracle 比对。

### R04 [ADVANCED] Workspace / consumer / writer 生命周期
Status: TODO
Reviewer: N/A

Acceptance:
- Bf = Nf 个 raw `FrequencyWorkspace` 跨全部 ray 常驻；遍历完成后逐频执行 `scaleCoherentCartesianPressure(workspace[f])`，再交 consumer / `writeFrequency(f)`；writer API 与 SHD 格式零改动；
- per-ray temporaries（Nf × RayFrequencyState、Nf × precompute、Nf × epsilon 与 O(Nf) imageSum/eligibility scratch）ray 结束即释放或复用；
- `--verify-cache` fingerprint before/after 在 fused 路径下保持 PASS（frozen cache 零写回）。

Evidence:
- writer 输出与既有路径一致；fingerprint 校验记录。

### R05 [ADVANCED] Parity harness：Level A-D
Status: TODO
Reviewer: N/A

Acceptance:
- Level A：frozen cache fingerprint before == after；
- Level B：同一二进制/compiler/FP 环境下，每频 raw fused workspace 与 raw current-reuse workspace bitwise identical；
- Level C：逐频 scaling 后，每频 scaled workspace bitwise identical；
- Level D：全部适用 CC coherent SHD 标准用例上 fused/reference SHA-256 一致；
- 若现有返回接口因在 return 前已 scaling 而无法观察 raw workspace，必须建立窄内部/test seam 或记录 blocker 并返回 design review；不得跳过 Level B/C；
- fallback（开关关闭）与 nonreuse 模式输出不受影响；
- 覆盖：lossless 与 lossy 介质、含反射边界、不同 ε 模式、terminal-prefix 用例。

Evidence:
- parity 矩阵（用例 × 模式 × 开关）结果存档。

### R06 [ADVANCED] Benchmark 与内存测量
Status: TODO
Reviewer: N/A

Acceptance:
- 16F Munk 代表性负载：wall / Project / Influence / Scale / peak RSS / 全部 geometry 与 frequency-kernel counters，对照 R01 baseline；
- 协议：预热排除、≥5 次重复、中位数+分散度、交替运行、相同构建/线程/输入；
- 内存实测：`Nf ×` field bytes、per-ray temporaries 峰值、peak RSS（禁止按结构体理论大小宣称）。

Evidence:
- benchmark 报告 + 内存表；`NOT_VIABLE` 判定路径明示。

## 4. Correctness Gates（gate set v2；映射见设计来源报告 §§C–H）

| 门禁 | 内容 | 承自 |
|---|---|---|
| V2-GATE-01 | 有损复走时逐频精确积分，实虚部均不受冻结几何污染；lossless 直接取实走时 | IGR1-GATE-01 |
| V2-GATE-02 | threshold 语义：source 初始 active；`<0.005F` 累计 cutoff；`<1e-5F` 仅半空间单次抑制 | IGR1-GATE-02 |
| V2-GATE-03 | per-frequency terminal retention + **union 遍历上界**（D5） | IGR1-GATE-03（修订） |
| V2-GATE-04 | regular grid crossing 集一致性（一次计算，天然一致，仍须验证） | IGR1-GATE-04 |
| V2-GATE-05 | domain routing：fused 仅 rectilinear/uniform-range CC coherent；其余显式走既有路径，禁止静默进入 | IGR1-GATE-05（修订） |
| V2-GATE-06 | 模式覆盖：coherent 走 fused；I/S 走既有路径且输出不变（抽验） | IGR1-GATE-06（收窄） |
| V2-GATE-07 | **Level A-D parity**：cache fingerprint；raw workspace bitwise；scaled workspace bitwise；SHD SHA-256；基线重锚至 IGR-1 启动时 accepted HEAD | IGR1-GATE-10（扩展） |
| V2-GATE-08 | fallback / execution-policy parity：开关两侧输出一致；fused-off 即既有路径；nonreuse（cache-off）模式不变 | IGR1-GATE-09 |
| V2-GATE-09 | frozen-cache integrity：fingerprint before/after；`RayPathCache` 零写回（D8） | IGR1-GATE-08/09 cache 部分 |
| V2-GATE-10 | 内存模型实测：`Nf ×` field bytes + per-ray temporaries + peak RSS；无几何 cache 存在 | IGR1-GATE-11（重写） |
| V2-GATE-11 | Origin / F2CPP oracle 套件全部继续通过 | IGR1-GATE-12 |
| V2-GATE-12 | 性能净收益 + geometry/frequency-kernel 分组计数器 + Scale seconds；协议与 `NOT_VIABLE` 逃生门继承 | IGR1-GATE-13（扩展） |
| V2-GATE-D1（DEFERRED） | multi-source isolation（`Nsource × Bf` + writer 全 source 约束）；延后至 IGR-2/后续批次 | IGR1-GATE-07 |

## 5. Performance Gates

- 计量清单：end-to-end wall、Project、Influence、Scale、peak RSS；geometry segment/range/depth/image-geometry counters；frequency range/image kernel counters 与 rejection/contribution counters；
- 理想相同-prefix `Bf=Nf` 下仅 geometry counters 接近 baseline/Nf；frequency-kernel counters 通常不降至 1/Nf；
- 对照：R01 采集的现行 reuse baseline（同机、同构建、同输入）；
- PASS 判据：数值正确性不降低前提下，16F 代表性负载出现**超出运行噪声的可重复正向 end-to-end wall-time 净收益**；
- `NOT_VIABLE` 逃生门：净收益不成立即判定 `NOT_VIABLE`，不得因代码已写而合入 production 路径。

## 6. Required Memory Measurements

- `Bf × M_frequency_field`（Nf × depthCount × rangeCount × 16 B，Munk 16F ≈ 26 MB 量级验证）；
- per-ray temporaries 峰值（Nf × RayFrequencyState + Nf × precompute + Nf × epsilon + O(Nf) scratch，单 ray 界）；
- peak RSS 实测（对照既有 reuse 模式）；
- frozen cache bytes 不变的确认（不随 Nf 增长）。

## 7. Go / No-Go Criteria

- **GO（进入 CONSTRUCT）**：用户批准本草案；byte-parity 论证（报告 §E）经评审无异议；V2 gate set 冻结。
- **NO-GO / 阻塞**：发现融合导致任何 per-frequency 累加顺序改变且无法恢复；发现 CC 融合需要触碰 frozen cache schema；parity 基线无法重锚。
- **NOT_VIABLE（施工后退出）**：性能净收益不成立（§5）。

## 8. OUT OF SCOPE（同设计来源报告 §H）

RayPath/RayPathCache schema changes; frequency interpolation/reconstruction; rolling FrequencyProjector rewrite; persistent full geometry cache; receiver-depth/image materialization; SIMD; GPU; nested parallelism; automatic memory blocking; all-family unification; Arrival/Eigenray IGR; unrelated refactor.

---

> **再次声明：本文件为 DESIGN DRAFT — NOT APPROVED, NOT IN CONSTRUCTION。**
> 获批后由 coordinator 冻结为正式 IGR-1 Worklist 并按 AGENTS.md 状态机执行。
