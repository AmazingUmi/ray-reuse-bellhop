# IGR-1 — Cross-Frequency Cartesian Cerveny Influence Fusion — DESIGN DRAFT

> **状态：** **`DESIGN DRAFT — NOT APPROVED, NOT IN CONSTRUCTION`**
> 本文件是 IGR-1 的 DESIGN 级草案，等待用户批准后才进入 IGR-1 DESIGN/CONSTRUCT。
> 在获批并冻结为正式 `IGR-1_WORKLIST` 之前，本文件不代表任何已授权施工。
>
> **设计来源：** [`../reports/REPORT_IGR0_REVISION_CROSS_FREQUENCY_FUSION_2026-09-01.md`](../reports/REPORT_IGR0_REVISION_CROSS_FREQUENCY_FUSION_2026-09-01.md)（IGR-0-REV，`READY_FOR_REVIEW`）
> **基线：** `main @ ba653229560aac53eb61eeaa5fdb2c2fd3254338`；IGR-1 启动时 byte-parity 基线重锚至当时 accepted HEAD
> **日期：** 2026-09-01
> **风险等级：** 全部任务 `[ADVANCED]`（数值算法 / 多频状态 / ownership / 高风险性能路径）

---

## 1. Scope（DESIGN 级）

在 `Bellhop_RayReuse` TL 生产路径中，为 **Cartesian Cerveny coherent pressure** 建立 **cross-frequency fused influence traversal**：

```text
source → ray → segment → receiver range → receiver geometry → frequency
```

一次 receiver-geometry 求值（crossing 拓扑、W、插值 position/slowness/c、Δz、polarity）被块内全部频率的频率局部求值（q/τ/γ 插值、ε、principal、window、taper、phase、exp、累加）**立即消费**；不引入任何持久几何 cache。

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
5. **D5** per-frequency active prefix：遍历上界 = 各频 prefix 并集；每频独立保留左端点 active 检查、terminal retention、`<0.005F` cutoff。
6. **D6** Bf = Nf；额外长期内存 ≈ `Nf ×` frequency field workspace。
7. **D7** frequency blocking 是同一算法的内存策略，v1 不实现。
8. **D8** frozen `RayPathCache` 契约不变（STATUS_PROGRESS L58-70）。
9. **D9** 并行延后；先 fused serial/reference path。

IGR-1 特定冻结：

10. **集成点**：serial solver 层新增 multi-frequency fused 入口；既有 per-frequency 路径原样保留为 reference/fallback；execution-policy 开关选择，禁止删除或改写既有路径语义。
11. **计时语义**：fused 路径 per-frequency 时间不可逐频剥离；按 block-level 报告（共享 traversal 段 + frequency-kernel 段），文档明示，不伪造逐频精度。
12. **byte-parity 目标**：designed to preserve bitwise parity（设计来源报告 §F 论证）；同一二进制内 fused vs fallback 比较；由 V2-GATE-07 强制验证，不得凭设计宣称达成。

## 3. Task Decomposition（coarse，全部 [ADVANCED]）

### R01 [ADVANCED] Profile-counter extension 与基线测量
Status: TODO
Reviewer: N/A

Acceptance:
- 现有 `--profile-influence` 计数器（receiver-range/depth/image evaluations、window/taper rejections）在融合路径下定义保持一致；
- 新增 **frequency-kernel evaluations** 计数器（每 (range, f) 频率局部核求值）；
- 在 accepted HEAD 上采集现行 reuse baseline（16F Munk：wall/Influence/Project/peak RSS/各计数器），作为 R06 对照基线。

Evidence:
- 计数器定义与 baseline JSON/报告存档；无 production 之外文件改动。

### R02 [ADVANCED] Fused CC coherent pressure 路径（execution-policy 开关之后）
Status: TODO
Reviewer: N/A

Acceptance:
- 新 fused serial 入口：per ray 一次取 ray → Nf 份 `RayFrequencyState` + Nf 份 ε + Nf 份 precompute（G 部分每 ray 一次）→ 一次 segment/range/depth 遍历 → per-f 频率局部求值 → `workspace[f]` 累加；
- 共享判定只提升纯几何项（crossing 拓扑、W、插值 position/slowness/c、Δz、polarity、越界/退化段过滤）；`gamma.imag()>0` 早退、window/taper、phase、exp 保留在频率层；
- 开关关闭 = 现行路径逐指令不变；fallback 永远可达。

Evidence:
- 针对性 component tests + V2-GATE-07 byte-parity 初验。

### R03 [ADVANCED] Active-prefix union 语义
Status: TODO
Reviewer: N/A

Acceptance:
- 遍历上界 = 各频 active prefix 的并集；每频以自身左端点 active 检查跳过自身 prefix 外 segment；
- per-frequency terminal-point retention、inactive 后缀抑制、`<0.005F` cutoff 与现行路径语义逐条等价；
- 构造多频不同 cutoff 的针对性测试（不同反射/衰减使各频 prefix 不同）。

Evidence:
- 针对性 prefix 差异测试 + oracle 比对。

### R04 [ADVANCED] Workspace / consumer / writer 生命周期
Status: TODO
Reviewer: N/A

Acceptance:
- Bf = Nf 个 `FrequencyWorkspace` 跨全部 ray 常驻至 consumer；遍历完成后按频率顺序调用 `writeFrequency(f)`；writer API 与 SHD 格式零改动；
- per-ray temporaries（Nf × RayFrequencyState、Nf × precompute）ray 结束即释放；
- `--verify-cache` fingerprint before/after 在 fused 路径下保持 PASS（frozen cache 零写回）。

Evidence:
- writer 输出与既有路径一致；fingerprint 校验记录。

### R05 [ADVANCED] Parity harness：byte-identical SHD
Status: TODO
Reviewer: N/A

Acceptance:
- fused vs 现行路径（同一二进制）在全部适用 CC coherent SHD 标准用例上 SHA-256 逐字节一致；
- fallback（开关关闭）与 nonreuse 模式输出不受影响；
- 覆盖：lossless 与 lossy 介质、含反射边界、不同 ε 模式、terminal-prefix 用例。

Evidence:
- parity 矩阵（用例 × 模式 × 开关）结果存档。

### R06 [ADVANCED] Benchmark 与内存测量
Status: TODO
Reviewer: N/A

Acceptance:
- 16F Munk 代表性负载：wall / Influence / Project / peak RSS / range / depth / image / frequency-kernel evaluations，对照 R01 baseline；
- 协议：预热排除、≥5 次重复、中位数+分散度、交替运行、相同构建/线程/输入；
- 内存实测：`Nf ×` field bytes、per-ray temporaries 峰值、peak RSS（禁止按结构体理论大小宣称）。

Evidence:
- benchmark 报告 + 内存表；`NOT_VIABLE` 判定路径明示。

## 4. Correctness Gates（gate set v2；映射见设计来源报告 §D）

| 门禁 | 内容 | 承自 |
|---|---|---|
| V2-GATE-01 | 有损复走时逐频精确积分，实虚部均不受冻结几何污染；lossless 直接取实走时 | IGR1-GATE-01 |
| V2-GATE-02 | threshold 语义：source 初始 active；`<0.005F` 累计 cutoff；`<1e-5F` 仅半空间单次抑制 | IGR1-GATE-02 |
| V2-GATE-03 | per-frequency terminal retention + **union 遍历上界**（D5） | IGR1-GATE-03（修订） |
| V2-GATE-04 | regular grid crossing 集一致性（一次计算，天然一致，仍须验证） | IGR1-GATE-04 |
| V2-GATE-05 | domain routing：fused 仅 rectilinear/uniform-range CC coherent；其余显式走既有路径，禁止静默进入 | IGR1-GATE-05（修订） |
| V2-GATE-06 | 模式覆盖：coherent 走 fused；I/S 走既有路径且输出不变（抽验） | IGR1-GATE-06（收窄） |
| V2-GATE-07 | **byte-identical SHD parity**：fused vs 现行路径，同一二进制，SHA-256 对齐；基线重锚至 IGR-1 启动时 accepted HEAD | IGR1-GATE-10 |
| V2-GATE-08 | fallback / execution-policy parity：开关两侧输出一致；fused-off 即既有路径；nonreuse（cache-off）模式不变 | IGR1-GATE-09 |
| V2-GATE-09 | frozen-cache integrity：fingerprint before/after；`RayPathCache` 零写回（D8） | IGR1-GATE-08/09 cache 部分 |
| V2-GATE-10 | 内存模型实测：`Nf ×` field bytes + per-ray temporaries + peak RSS；无几何 cache 存在 | IGR1-GATE-11（重写） |
| V2-GATE-11 | Origin / F2CPP oracle 套件全部继续通过 | IGR1-GATE-12 |
| V2-GATE-12 | 性能净收益 + 扩展计数器（含 frequency-kernel evaluations）；协议与 `NOT_VIABLE` 逃生门继承 | IGR1-GATE-13（扩展） |
| V2-GATE-D1（DEFERRED） | multi-source isolation（`Nsource × Bf` + writer 全 source 约束）；延后至 IGR-2/后续批次 | IGR1-GATE-07 |

## 5. Performance Gates

- 计量清单：end-to-end wall、Influence time、Project time、peak RSS、receiver-range/depth/image evaluations、**frequency-kernel evaluations（新）**；
- 对照：R01 采集的现行 reuse baseline（同机、同构建、同输入）；
- PASS 判据：数值正确性不降低前提下，16F 代表性负载出现**超出运行噪声的可重复正向 end-to-end wall-time 净收益**；
- `NOT_VIABLE` 逃生门：净收益不成立即判定 `NOT_VIABLE`，不得因代码已写而合入 production 路径。

## 6. Required Memory Measurements

- `Bf × M_frequency_field`（Nf × depthCount × rangeCount × 16 B，Munk 16F ≈ 26 MB 量级验证）；
- per-ray temporaries 峰值（Nf × RayFrequencyState + Nf × precompute，单 ray 界）；
- peak RSS 实测（对照既有 reuse 模式）；
- frozen cache bytes 不变的确认（不随 Nf 增长）。

## 7. Go / No-Go Criteria

- **GO（进入 CONSTRUCT）**：用户批准本草案；byte-parity 论证（报告 §F）经评审无异议；V2 gate set 冻结。
- **NO-GO / 阻塞**：发现融合导致任何 per-frequency 累加顺序改变且无法恢复；发现 CC 融合需要触碰 frozen cache schema；parity 基线无法重锚。
- **NOT_VIABLE（施工后退出）**：性能净收益不成立（§5）。

## 8. OUT OF SCOPE（逐字冻结，同设计来源报告 §H.2）

RayPath/RayPathCache schema changes; frequency interpolation/reconstruction; rolling FrequencyProjector rewrite; persistent full geometry cache; receiver-depth/image materialization; SIMD; GPU; nested parallelism; automatic memory blocking; all-family unification; Arrival/Eigenray IGR; unrelated refactor.

---

> **再次声明：本文件为 DESIGN DRAFT — NOT APPROVED, NOT IN CONSTRUCTION。**
> 获批后由 coordinator 冻结为正式 IGR-1 Worklist 并按 AGENTS.md 状态机执行。
