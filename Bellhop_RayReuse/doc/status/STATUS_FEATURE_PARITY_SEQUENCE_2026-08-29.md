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
| FP-2H | Attenuation Closure | **全流程完成：`ACCEPTED / CLOSED`** |

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

## 3. FP-2G 完成情况（已关闭）

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

## 4. FP-2H 完成情况（已关闭）

### 4.1 DESIGN（已冻结）

- 目标：全面闭环 ATT-01～ATT-05，覆盖 N/F/M/W/Q/L 单位、W 频率响应、Thorp 保护、FG、Biological（0–200 层重叠）、五大 SSP 节点优先转换、边界材料衰减接入及三模式逐字节一致性。
- Worklist：`doc/worklists/FP-2H_ATTENUATION_CLOSURE_WORKLIST.md`。

### 4.2 CONSTRUCT 进度与审查

| Task | 等级 | 状态 | Reviewer | 关键证据 |
|---|---|---|---|---|
| H00 冻结前置基线 | STANDARD | DONE | N/A | 捕获 pre-FP-2H SHA-256、Thorp 单频/smoke/16频回归基线、W 基线、Munk spline 指纹 `1526667602348633172` |
| H01 不可变衰减所有权 | ADVANCED | DONE | PASS (H01-R) | `Environment` 增加 `VolumeAttenuation`，FG 值所有权与 Biological 共享层，单元测试全覆盖 |
| H02 ENV 解析与 PRT 报告 | ADVANCED | DONE | PASS (H02-R) | 3–6 字符选项、FG 4 参数、Biological 0–200 层；PRT 生成 Thorp/FG/Biological 标记 |
| H03 衰减内核与兼容性裁决 | ADVANCED | DONE | PASS (H03-R) | `-fno-builtin-pow` 编译选项；FG 与 Biological 内核；§3.2 矩阵（防双重衰减）；单元测试覆盖 5 锚点与 16 组合 |
| H04 五频域 SSP 后端接入 | ADVANCED | DONE | PASS (H04-R) | C/N/P/S/Q 五后端节点优先转换；Q 衰减用参考节点实声速；组件测试验证生物层深度与基线不变 |
| H05 投影器与边界衰减接入 | ADVANCED | DONE | PASS (H05-R) | 边界声学函数接收 `VolumeAttenuation`；纵/横波衰减；长格式 `1.0e20` legacy 深度；粒度隔离；`FrequencyProjector` 接入 |
| H06 冻结缓存与并发不变性 | ADVANCED | DONE | PASS (H06-R) | 证明受保护文件零改动；单频/串行/并行复用指纹前后不变；参数变化压强不同但几何与指纹完全相同；Munk spline 锚点不变 |
| H07 ATT-01 / ATT-02 闭环 | STANDARD | DONE | N/A | 6 个 `attenuation_unit_*` case.toml 增加 rayreuse；`validate_i4_attenuation_units.py` 执行 54 组配对比较（42 组 gating 全部 PASS，12 组非 gating 记录 F2CPP 单频自规划差异）；5 kHz 跨单位逐位相同；4 kHz 频率缩放通过 |
| H08 ATT-03 / 04 / 05 闭环 | STANDARD | DONE | N/A | FG 与 Biological case.toml 增加 rayreuse；`validate_i4_volume_attenuation.py` 执行 75 组配对比较（39 组 gating 全部 PASS，36 组非 gating 记录 F2CPP 单频自规划差异）；Thorp SHA-256 与 H00 逐位一致；9 个非 no-op guard 全过 |
| H09 模式/追踪/缓存证据矩阵 | STANDARD | DONE | N/A | 10 个宽带 profile 在 nonreuse/reuse/parallel 下 SHD 逐字节一致；追踪次数 2/1/1 与 16/1/1；`--verify-cache` before==after 全量通过 |
| H10 文档发布与批次报告 | SIMPLE | DONE | N/A | 发布矩阵、进度快照、批次报告，Worklist 与批次状态转为 `ACCEPTED / CLOSED` |

### 4.3 审查与验收状态

H01-R～H06-R 六个 ADVANCED reviewer checkpoint 审查全获 `PASS`。Batch Acceptance 亲验通过（41/41 CTest, 187 pytest, 172 test-unit, ATT-01/02 54 组比较, ATT-03/04/05 75 组比较, 10 宽带三模式 byte-identical 与 cache fingerprint 守恒）。Final Review 结论为 **`ACCEPTED`**。FP-2H 已 **`CLOSED`**。

## 5. 全局注意事项

- 每批启动条件 = 前批 ACCEPTED（不是 COMMITTED）；FP-2F 已同时满足两者。
- 未获授权不 push；每 ACCEPTED 批次推荐独立 commit。
- 三批 Worklist 相互独立，禁止合并验收。
