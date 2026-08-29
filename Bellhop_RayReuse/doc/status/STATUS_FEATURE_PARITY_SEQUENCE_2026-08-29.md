# Feature Parity 串行批次进度快照（FP-2F → FP-2G → FP-2H）

> 快照日期：2026-08-29
> 性质：执行期进度记录（非批次验收文档；各批次权威状态见对应 Worklist 与
> Batch Report）
> 序列授权：用户已批准 FP-2F → FP-2G → FP-2H 三批串行自动推进，验收
> 全部 ACCEPTED 后停止，不自动进入后续研究阶段。

## 1. 序列总览

| Batch | 主题 | 状态 |
|---|---|---|
| FP-2E（前置补验收） | Quadrilateral SSP `Q`/`.ssp` | Re-Final Review `ACCEPTED`（2026-08-29），前置补验收仅覆盖文档 scope/工作树两项 R1 修复 |
| FP-2F | Source/Receiver Generalization | **全流程完成：`ACCEPTED`，commit `763c585`** |
| FP-2G | Boundary/Material Closure | **全流程完成：`ACCEPTED`**（Re-Final Review `ACCEPTED` 2026-08-29） |
| FP-2H | Attenuation Closure | **DESIGN 进行中**（architect 规划中） |

## 2. FP-2F 完成情况（已关闭）

- **DESIGN**：architect 冻结 scope = multisource（`NSz ≥ 1` point source）+
  Cartesian paired irregular receiver（run type `I`，`NRz == NRr`）+ 三模式；
  line source、3D/N×2D、ray-centered irregular、cache schema 变更均排除。
- **CONSTRUCT**：F01–F06（ADVANCED，全部 reviewer PASS）、F07–F08
  （STANDARD）、F09（SIMPLE）。两处 reference 语义裁定：CC/IC/SC irregular
  恒取 `Rz(1)`（Origin legacy）；SHD `PlotType` 为 10 字符。
- **Batch Acceptance**（coordinator 亲验）：隔离 build + CTest 40/40；
  pytest 178；test-unit 163；三方 batch 222 组合 PASSED；三模式
  byte-identity（Trace passes `6/3/3`）；冻结基线不变（`munk_spline`
  fingerprint `1526667602348633172`、SHD `74028065…`）。
- **Final Review**：`FP-2F ACCEPTED`（2026-08-29，无 HIGH/BLOCKER；LOW
  备注 Worklist Reviewer 字段已同步为 PASS）。
- **Git**：独立 commit `763c585`；reference 零改动。
- 文档：`doc/worklists/FP-2F_SOURCE_RECEIVER_GENERALIZATION_WORKLIST.md`、
  `doc/workreports/FP-2F_SOURCE_RECEIVER_GENERALIZATION_BATCH_REPORT.md`。

## 3. FP-2G 当前状态（进行中）

### 3.1 DESIGN（已冻结）

- architect 审计结论：BND-04（curvilinear `C`）已不构成 architectural
  conflict——tracer seam、`ReflectionEvent.boundaryCurvature`、fingerprint
  哈希均已就绪，真实 gap 仅 parser 拒绝与 `BoundaryGeometry` curvilinear
  kind；BND-09（flat elastic P/S）实现已存在，缺 executable oracle 证据链。
- **coordinator scope 修正**：architect 原设计含 C 轨（attenuation units
  `attenuation_unit_{n,f,m,q,l,w}` 证据闭环，ATT-01）；因 attenuation 按批次
  划分属 FP-2H 候选方向，已将 C 轨移出 FP-2G（记录于 Worklist"不做"第 1
  条），事实留给 FP-2H DESIGN 裁定。
- 冻结任务：A01/A02 [ADVANCED]、A03/A04/B01/B02 [STANDARD]；
  `CS`/`CL` 拒绝、curvilinear 仅 V/R 材料 + `C` short format、
  curvilinear × multisource/irregular/Q 不声明。
- Worklist：`doc/worklists/FP-2G_BOUNDARY_MATERIAL_CLOSURE_WORKLIST.md`。

### 3.2 CONSTRUCT 进度

| Task | 等级 | 状态 | Reviewer | 关键证据 |
|---|---|---|---|---|
| A01 curvilinear 模型+parser | ADVANCED | DONE | PASS | CTest 41/41；curvilinear 构造/`reflectionSampleAtSegment` 与 F2CPP 逐行一致（FMA/t1³/0.5 平均）；LS/LL SHD byte-identical；PRT `Curvilinear Interpolation` |
| A02 tracer/reflection 集成+geometry oracle | ADVANCED | DONE | PASS | production 零 diff；459/459 angle probe CSV 与 F2CPP 逐字节一致；Origin 459-angle oracle 双侧 PASS（worst scaled 3.24e-4，tolerance 未放宽）；LS/LL fingerprint before==after |
| A03 `i3_curvilinear_oracle` 三方 closure | STANDARD | DONE | N/A | `case.toml` 增加 rayreuse；`validate_i3_curvilinear_closure.py` 三方 SHD 比较通过（f2cpp↔rayreuse max TL 0.0 dB，origin↔rayreuse passed）；459 角度 probe 对拍 459/459 PASS；PRT markers 生效 |
| A04 curvilinear broadband 三模式 | STANDARD | DONE | N/A | `broadband_smoke`（100/200 Hz）三执行模式每频产品 byte-identical；PRT Trace passes 2/1/1；reuse/parallel `--verify-cache` 通过；既有基线不变 |
| B01 flat elastic P/S 三方 closure | STANDARD | DONE | N/A | `elastic_halfspace_flat` 与 `elastic_halfspace_fluid_control` 增加 rayreuse；`validate_i4_elastic_halfspace.py` 三方比较全部 PASS；`MINIMUM_SHEAR_EFFECT` shear guard 全部 PASS |
| B02 flat elastic broadband 三模式+逐频性 | STANDARD | DONE | N/A | `broadband_smoke`（1000/2000 Hz）三执行模式每频产品 byte-identical；PRT Trace passes 2/1/1；1000 Hz 与 2000 Hz 压强切片呈现频率依赖性差异（绝对差 > 4.7e-2）；frequency-local 声学计算由 `frequency_projector.cpp` 逐频调用与 `--verify-cache` before==after 严格闭环 |

### 3.3 审查与验收状态

A01+A02 reviewer checkpoint 审查已获得 `PASS` 结论。A03–B02 全部完成，Batch Acceptance 亲验通过（41/41 CTest, 178 pytest, 163 test-unit, 459/459 probe, 三方 SHD, 三模式 byte-identical 与 cache fingerprint 一致）。独立 Final Review 结论为 **`FP-2G ACCEPTED`**（2026-08-29）。

### 3.4 批次结论

FP-2G 已 CLOSED。按序列授权，自动进入 FP-2H DESIGN 阶段。

## 4. FP-2H 输入事实（未开始）

- 候选方向：N/F/M/Q/L attenuation product oracle closure、
  Francois–Garrison、biological attenuation。
- 已知事实（来自 FP-2G DESIGN 审计，供 FP-2H architect 复核）：
  - attenuation units 转换（N/F/M/W/Q/L）runtime 与 unit tests 已存在，
    六个 `attenuation_unit_*` 共享 case 仅缺 rayreuse allow-list 与三方
    比较（纯证据闭环，原 FP-2G C 轨设计可参考）；
  - `volume_attenuation_francois_garrison` / `volume_attenuation_biological`
    为真实实现 gap（RayReuse parser 显式拒绝），需独立实现设计；
  - W 单位的 ATT-02 parity 目前仅由其他 case 间接佐证。

## 5. 全局注意事项

- 每批启动条件 = 前批 ACCEPTED（不是 COMMITTED）；FP-2F 已同时满足两者。
- 未获授权不 push；每 ACCEPTED 批次推荐独立 commit。
- 三批 Worklist 相互独立，禁止合并验收。
