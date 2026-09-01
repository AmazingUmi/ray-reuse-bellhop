# IGR-0 Revision — Final Review Record（独立最终验收记录）

> **Batch：** IGR-0-REV（Cross-Frequency Influence Geometry Fusion rebaseline）
> **验收日期：** 2026-09-01
> **验收对象：** [`REPORT_IGR0_REVISION_CROSS_FREQUENCY_FUSION_2026-09-01.md`](../reports/REPORT_IGR0_REVISION_CROSS_FREQUENCY_FUSION_2026-09-01.md) 及其 5 份配套修订文件（见下）
> **验收角色：** final-reviewer（独立、只读；非 coordinator/architect 自验）
> **验收基线：** `feat/igr-influence-geometry-reuse @ 95a4fef`（batch diff = 全部工作树改动）

---

## VERDICT

```text
ACCEPTED
```

无 CRITICAL / MAJOR finding。2 项 MINOR 非阻断 finding，闭环处置见文末 §Remediation。

---

## FILES REVIEWED

- `Bellhop_RayReuse/doc/reports/REPORT_IGR0_REVISION_CROSS_FREQUENCY_FUSION_2026-09-01.md`（新建，379 行）
- `Bellhop_RayReuse/doc/worklists/IGR-1_CC_FUSION_DESIGN_DRAFT.md`（新建，173 行）
- `Bellhop_RayReuse/doc/reports/REPORT_IGR0_INFLUENCE_GEOMETRY_REUSE_AUDIT.md`（diff 仅 +8 行 banner）
- `Bellhop_RayReuse/doc/worklists/IGR-0_INFLUENCE_GEOMETRY_REUSE_AUDIT_WORKLIST.md`（diff 仅 +7 行 banner）
- `doc/reference/REFERENCE_INFLUENCE_GEOMETRY_REUSE.md`（diff 仅 +7 行 banner）
- `doc/plans/PLAN_CURRENT_WORK.md`（状态更新）
- 源码只读抽验：`serial_ray_reuse_solver.cpp`、`single_frequency_solver.cpp`、`cartesian_cerveny_influence.cpp`、`ray_centered_cerveny_influence.cpp`、`geometric_hat_influence.cpp`、`geometric_gaussian_influence.cpp`、`simple_gaussian_influence.cpp`、`frequency_projector.cpp`、`shd_writer.cpp`、`parallel_ray_reuse_solver.cpp`、`ray_path.hpp`、`frequency_workspace.hpp`、`ray_path_cache.cpp`、`app/main.cpp`、相关 benchmark/status 文档与 `test/standard_cases/cases/munk_cerveny_cc/origin.env.in`。

## CURRENT ARCHITECTURE（验收确认）

§A 全部 load-bearing 事实经逐行核对成立：trace-once 先于 frequency loop（`serial_ray_reuse_solver.cpp:44-45`）；frequency-major，per (freq, source) 调 `solveFrequencyFromSourceCache`（66-83、73-77），逐频 consumer 交接（82）；per-(freq,source) workspace/projector/influence 对象与逐 ray `pickBeamEpsilon`（`single_frequency_solver.cpp:227-274、298-299、321-338`）；CC 循环序 segment(自 2、active-prefix 界) → range → depth → images，含 left-endpoint active check 与 terminal retention（`cartesian_cerveny_influence.cpp:678、700-706、719、758、801-804`）；O(1) `fortranUpperRangeIndex`（90-102、708-714）；W 公式（725-726）；纯几何插值（727-735）；频率依赖 q/τ/γ（736-744）；γ.imag>0 早退（745-747）；频率依赖 window −ω·Im(γ)Δz²（420-425）与 radiusMax=30·c0/f（649-650）；`p=p1+εp2 / q=q1+εq2`（311-314）；累加（811-837）。GeoHat membership 频率无关（`beamRadius=|q/q0|` 出自 `dynamicQ[0U]`，无 ε——未被误分类）；GeoGaussian σ1 频率依赖（`kNearFieldFactor=0.2F`、`kBeamWindow=4.0` 核实）；projector 0.005F 单调 AND、首点播种、lossy 复 τ 虚实部均频率依赖（`frequency_projector.cpp:16、60-65、109-113、155、169-191`）；SHD seek-addressed 记录、乱序完成允许、每调用全 source、finalize 要求全部写入（`shd_writer.cpp:138-224、150-153`）；parallel 为 frequency-ownership、队列 1-2、主线程 consumer、前后 fingerprint 校验；`RayState` 无频率字段、`contentFingerprint()` 仅哈希几何。报告对 2026-08-25 audit "CC-only TL" 范围注记的纠正是准确的（当前五个家族均接入，`single_frequency_solver.cpp:215-274`）。所引 benchmark 数字（0.405/0.509/279.211 s、99.62%；621.719 s、99.19%；4,972,960/999,564,960/2,998,694,880；18.438 s/0.143 s；86.43%；−3.69/−1.35/−1.95%）与 archive 报告一致。

## SUPERSEDED DECISIONS（确认）

§D 覆盖全部必备项：Candidate 1B/1A stencil cache 与 Candidate 6 降为未来候选；Candidate 2 维持 deferred；Candidate 4/5 维持 INVALID 并保留 M7 的精确表述（持久冻结被否，transient fusion 不在被否范围）；full pair cache 维持 REJECTED 且尺寸依据不变；内存模型由 MB/source cache 估算改写为 `M_frozen + Bf×M_field + per-ray temporaries`；integration point 由 `solveFrequencyFromCache()` 内（旧主张见 influence-audit L996-1010）移至新的 fused 多频入口；13 项 IGR1-GATE（audit L366-378）映射为 gate set v2，GATE-10 重锚定、GATE-11 重写、GATE-13 扩展 frequency-kernel 计数器。remains-valid 清单明确且正确。

## FROZEN IGR DECISIONS（确认 D1–D9）

D1 transient fusion 主方案；D2 persistent cache 降级（四条理由，其中 arithmetic→memory-traffic 转化有实测 loop-invariant 回退证据支撑）；D3 frequency-local 物理边界清单；D4 冻结基底 + f-local pVB/qVB/γ；D5 union prefix 语义，<0.005F cutoff、terminal retention、left-endpoint suppression 全部不变；D6 Bf=Nf 及所述内存模型；D7 blocking 仅作为 policy 冻结；D8 frozen RayPathCache 契约（与 STATUS_PROGRESS L58-70 一致）；D9 parallel 延后、fused serial reference 先行。全部与用户重决策一致。

## IGR-1 SCOPE（确认）

TL only / CC only / coherent first / rectilinear / single source / shared fan / cache 与 projector 语义不变 / Bf=Nf / 无 persistent cache、auto-blocking、parallel、frequency interpolation——§H.1 每项均有独立核实的源码事实支撑（非照抄；如 `shd_writer.cpp:150-153` 每 writeFrequency 全 source 支撑 single-source-first；trace 本就频率无关支撑 shared fan）。OUT OF SCOPE 清单在 §H.2 与 worklist §8 两处齐备，覆盖全部 12 项排除项。Worklist 顶部与底部均标 `DESIGN DRAFT — NOT APPROVED, NOT IN CONSTRUCTION`，R01–R06 全部 TODO，go/no-go 判据（§7）在位。§F 累加顺序论证成立——rightIndex == prefix_f 落在 inactive 左端点的等价边界情形已核实：left-endpoint check 精确重现当前 loop-bound 截断，零新增贡献。

## OPEN RISKS

报告列出：(1) multi-source 内存与 writer 全-source 约束；(2) RC/GeoGaussian 家族统一延后；(3) byte-parity 对 codegen/FP 环境的依赖（以同二进制比对缓解）；(4) 大 Nf workspace 增长（64F ≈ 103 MB，blocking 已冻结未实施）；(5) profile 计数器需扩展。补充（非阻断）：旧 IGR-0 batch 本身停在 `READY_FOR_REVIEW`（supersession 前未被正式 ACCEPTED）——banner 已诚实披露，修订报告未作他称。

## NO PRODUCTION CODE CHANGED（确认）

`git status --short` 恰为申报文件集；`git diff --check` 干净；`git diff -- Bellhop_RayReuse/src Bellhop_RayReuse/include Bellhop_F2CPP Bellhop_origin test` 为空（0 行）。报告状态未自封 ACCEPTED（验收前）；byte-parity 表述为 "designed to preserve, requires gate verification"；无绝对化声明；新改文档内链全部可解析、fence 平衡、表格规整。

---

## Remediation（MINOR findings 闭环）

| Finding | 严重级 | 内容 | 处置 | 状态 |
|---|---|---|---|---|
| F1 | MINOR | 修订报告两处 "audit" 引用未注明文件名（D2 的 "audit L536-545" 与 §A.3 的 "audit L23-26"，实指 `REPORT_INFLUENCE_FREQUENCY_AUDIT_2026-08-25.md`） | 已由 coordinator 补全文件名链接（[SIMPLE] 文档修正，无 production 影响） | **CLOSED**（final-reviewer 复验） |
| F2 | MINOR | `IGR0_CODEX_REVIEW_CHANGES_REQUIRED.md`（95a4fef 已提交的历史第三方 review）无 supersession banner | 维持原状：属验收申报范围外的历史 review 记录，按 history-preserved 原则不改动（final-reviewer 认定无需动作） | **CLOSED（no action）** |

状态更新随 F1 修复一并完成：修订报告 header 与文末 verdict block 更新为 `ACCEPTED`（引用本记录）；`PLAN_CURRENT_WORK.md` IGR 行同步为 final-review ACCEPTED。
