# Feature Parity 串行批次进度快照（FP-2F → FP-2G → FP-2H → FP-2I）

> 快照日期：2026-08-29
> 性质：执行期进度记录（非批次验收文档；各批次权威状态见对应 Worklist 与
> Batch Report）
> 序列授权：用户已批准 FP-2F → FP-2G → FP-2H → FP-2I 串行自动推进，验收
> 全部 ACCEPTED。

## 1. 序列总览

| Batch | 主题 | 状态 |
|---|---|---|
| FP-2E（前置补验收） | Quadrilateral SSP `Q`/`.ssp` | Re-Final Review `ACCEPTED`（2026-08-29） |
| FP-2F | Source/Receiver Generalization | **全流程完成：`ACCEPTED`，commit `763c585`** |
| FP-2G | Boundary/Material Closure | **全流程完成：`ACCEPTED`，commit `eb27045`** |
| FP-2H | Attenuation Closure | **全流程完成：`ACCEPTED`，commit `099a2b1`** |
| FP-2I | Line Source Closure | **全流程完成：`ACCEPTED / CLOSED`** |

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
- **Final Review**：`FP-2F ACCEPTED`（2026-08-29）。
- **Git**：独立 commit `763c585`；reference 零改动。
- 文档：`doc/worklists/FP-2F_SOURCE_RECEIVER_GENERALIZATION_WORKLIST.md`、
  `doc/workreports/FP-2F_SOURCE_RECEIVER_GENERALIZATION_BATCH_REPORT.md`。

## 3. FP-2G 完成情况（已关闭）

- **DESIGN**：BND-04（canonical curvilinear `C` short format）与 BND-09（flat ordinary elastic halfspace P/S）。
- **CONSTRUCT**：A01/A02（ADVANCED，全部 reviewer PASS）、A03/A04/B01/B02（STANDARD）。
- **Batch Acceptance**：CTest 41/41, pytest 178, make unit 163, 459/459 angle probe 逐字节一致，三模式 byte-identical。
- **Final Review**：`FP-2G ACCEPTED`（2026-08-29）。
- **Git**：独立 commit `eb27045`；reference 零改动。
- 文档：`doc/worklists/FP-2G_BOUNDARY_MATERIAL_CLOSURE_WORKLIST.md`、
  `doc/workreports/FP-2G_BOUNDARY_MATERIAL_CLOSURE_BATCH_REPORT.md`。

## 4. FP-2H 完成情况（已关闭）

- **DESIGN**：全面闭环 ATT-01～ATT-05（N/F/M/W/Q/L 单位、Thorp 保护、FG、Biological 0–200 层重叠、五大 SSP 节点优先转换、边界材料衰减接入及三模式一致性）。
- **CONSTRUCT**：H01–H06（ADVANCED，全部 reviewer PASS）、H07–H09（STANDARD）、H10（SIMPLE）。
- **Batch Acceptance**：CTest 41/41, pytest 187, make unit 172, ATT-01/02 54 组比较, ATT-03/04/05 75 组比较, 10 宽带三模式 byte-identical 与 cache fingerprint 守恒。
- **Final Review**：`FP-2H ACCEPTED`（2026-08-29）。
- **Git**：独立 commit `099a2b1`；reference 零改动。
- 文档：`doc/worklists/FP-2H_ATTENUATION_CLOSURE_WORKLIST.md`、
  `doc/workreports/FP-2H_ATTENUATION_CLOSURE_BATCH_REPORT.md`。

## 5. FP-2I 完成情况（已关闭）

- **DESIGN**：全面闭环 SRC-02（Line Source）与 PRD-08（Line Source Product Scaling）。
- **CONSTRUCT**：
  - I01（ADVANCED）：`SourceGeometry` enum 与 `SimulationCase` 不可变所有权；
  - I02（STANDARD）：RunType 第 4 位 `'X'` 解析与 PRT 报告，Simple Gaussian 严格拒绝；
  - I03（ADVANCED）：Cartesian/Ray-centered Cerveny、Geometric Hat、Geometric Gaussian 的线声源 ratio 内核；
  - I04（ADVANCED）：PressureScaling `-4.0F * sqrt(pi)` 线声源全距离缩放；
  - I05（STANDARD）：ArrivalWriter `4.0F * sqrt(pi)` 线声源幅值缩放；
  - I06（ADVANCED）：全求解器贯通与 `--verify-cache` 验证；
  - I07（STANDARD）：`source_geometry_line` 与 `arrival_line_directional_multisource` 三方 oracle 闭环（F2CPP 0 diff / 0 ULP）；
  - I08（STANDARD）：宽带三模式（nonreuse / reuse / parallel）byte-identical（`cmp` 0）；
  - I09（SIMPLE）：文档封板与批次报告。
- **Batch Acceptance**：CTest 41/41, pytest 187, make unit 172, `validate_i8_arrivals.py` 9/9 PASSED，三模式逐字节一致，cache fingerprint 守恒。
- **Final Review**：`FP-2I ACCEPTED`（2026-08-29）。
- 文档：`doc/worklists/FP-2I_WORKLIST.md`、`doc/workreports/FP-2I_LINE_SOURCE_CLOSURE_BATCH_REPORT.md`。

## 6. 最终结论

整个 Feature Parity 序列（FP-1A～FP-2I）已全面完成并 **CLOSED**。
**Bellhop_F2CPP → Bellhop_RayReuse Production Feature Parity COMPLETE（GAP = 0）**。

## 7. Repository-level final acceptance（2026-08-30）

2026-08-30 repository-level final acceptance 已完成 Re-Final Review，结论为
**ACCEPTED**。Accepted production HEAD 为 `0721fb3`；final acceptance
documentation commit 为 `88ba8b7`。最终整体验收结论与 Performance Snapshot 见
[`REPORT_FEATURE_PARITY_FINAL.md`](../reports/REPORT_FEATURE_PARITY_FINAL.md)。

本文仍是 2026-08-29 的 sequence snapshot；以上附记只记录该序列已被后续
repository-level acceptance 正式关闭，不改写各 Batch 当时的数字和证据。
