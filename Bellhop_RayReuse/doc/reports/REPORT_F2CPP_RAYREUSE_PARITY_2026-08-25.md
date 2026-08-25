# Bellhop_F2CPP → Bellhop_RayReuse Production Feature Parity Audit

审计日期：2026-08-25

审计基线：`39c0407d634b387a6b3c81d6fdf94c330cdd1bb2`（`feat/i8-arrivals-eigenray`）

审计性质：只读代码、测试与可执行路径审计；除本报告外未修改代码、数值路径或测试资产。

## 1. 结论摘要

RayReuse 目前**尚未达到** F2CPP 二维 production feature surface 的完整 parity。
已经闭环的是：当前受限的 coherent Cartesian Cerveny TL、单点单源规则接收网格、
C-linear SSP、RR-B1 边界子集、单频 R，以及 Cartesian G/B 的 A/a/E；这些已可在
RayReuse 的 `nonreuse`、`reuse`、`parallel` 路径内按其适用范围使用。

主要差距不是旧 RayReuse `Deferred` 列表，而是以下真实代码边界：

- TL 只有 `C + Cartesian Cerveny + MS + pressure` 的 runtime/Influence 主链；
  `I/S`、其他 F2CPP TL beam/coordinate family 和完整 Cerveny 选项尚未接入。
- TL directional `.sbp` 存在 silent-ignore correctness gap：parser 接受并报告
  directional pattern，但 TL projection 未乘 source pattern。
- `SimulationCase`、receiver/workspace 与 writer 仍是单 source、Cartesian-product
  receiver 模型，不能表达 F2CPP multisource 和 irregular/paired-irregular 产品语义。
- geometry tracer/stepper 仍绑定 `CLinearSsp`，不能承载 F2CPP 的 P/N/S/Q SSP evaluator。
- boundary geometry 只有 flat/piecewise-linear 表示，没有 canonical curvilinear `C`
  所需的插值、局部 frame 和 curvature 数据。
- 部分 attenuation/material 路径虽有 parser 或组件实现，但没有 RayReuse executable
  product + regression/oracle 闭环，按本审计的严格标准不能标为 production-supported。

本审计没有把 RayReuse 旧矩阵中的 `Deferred` 当作新 scope，也没有因为 class/file
存在就判定支持。

## 2. 判定方法与状态语义

每个 `PARITY` 必须同时找到：

```text
parser accepts
→ runtime dispatch exists
→ implementation exists
→ output path exists
→ regression/oracle evidence exists
```

状态含义：

- `PARITY`：在表中写明的范围内，两边均有完整 production 证据链。
- `GAP`：F2CPP production-supported；RayReuse 缺 parser、dispatch、实现、输出或验证链中的至少一环。
- `ARCHITECTURAL_CONFLICT`：F2CPP production-supported，但 RayReuse 当前核心数据模型/所有权不能表达该语义；不是简单放开 parser 即可支持。
- `F2CPP_OUT_OF_SCOPE`：F2CPP 自己也没有正式 production support。

`ARCHITECTURAL_CONFLICT` 不表示必须建立 shared library，也不表示需要改变 frozen-cache
契约；它只表示需要先扩展 RayReuse 自身的 frequency-independent geometry/schema 或
frequency-local product schema。

## 3. 主要代码与测试证据

为避免表格重复，下列证据标签在后文复用：

| 标签 | 真实证据 |
| --- | --- |
| `F-PARSER` | `Bellhop_F2CPP/src/io/environment_parser.cpp`：run type/beam/source/receiver、C/P/N/S/Q SSP、LS/LL/C boundary、attenuation dispatch |
| `R-PARSER` | `Bellhop_RayReuse/src/io/environment_parser.cpp`：仅 CC/R/A/a/E(G/B)，显式拒绝 line、irregular、非 C SSP、FG/biological、canonical C boundary；source count 必须为 1；TL settings 仅 `MS/P` |
| `F-MODEL` | `Bellhop_F2CPP/include/bellhop/model/simulation_case.hpp` 与 boundary/SSP model：source vector、receiver layout、coherence、coordinate/beam families、curvilinear geometry |
| `R-MODEL` | `Bellhop_RayReuse/include/rayreuse/model/simulation_case.hpp`、`src/model/simulation_case.cpp`：单 `Source`、Cartesian-product `ReceiverGrid`、无 I/S、无 source geometry/coordinates/Simple Gaussian；构造 C-linear SSP |
| `F-TL` | `Bellhop_F2CPP/src/solver/single_frequency_solver.cpp` 及 `src/influence/`：按 coherence、beam family、coordinate、source geometry 和 component dispatch |
| `R-TL` | `Bellhop_RayReuse/src/solver/single_frequency_solver.cpp`：固定 `CLinearSsp`、`CartesianCervenyInfluence`、coherent point-source pressure scaling |
| `F-GEOM` | `Bellhop_F2CPP/include/bellhop/ray/geometry_tracer.hpp` 与 boundary geometry：通用 SSP evaluator、canonical curvilinear frame/curvature |
| `R-GEOM` | `Bellhop_RayReuse/include/rayreuse/ray/geometry_tracer.hpp`、`src/ray/ray_stepper.cpp` 与 boundary geometry：绑定 `CLinearSsp`，仅 flat/piecewise-linear boundary |
| `R-PRODUCT` | RayReuse `arrival_solver.cpp`、`eigenray_solver.cpp`、`ray_writer.cpp`、`arrival_writer.cpp`、`eigenray_writer.cpp`：R/A/a/E frequency-local 产品已接入，但 writer headers/layout 固定一个 source 与规则网格 |
| `R-CLI` | `Bellhop_RayReuse/app/main.cpp`：R/A/a/E/TL 正式 dispatch；R 只允许单频；A/a/E/TL 支持 nonreuse/reuse/parallel；逐频 serial consumer 发布 |
| `MATRIX` | `Bellhop_F2CPP/doc/reference/REFERENCE_FEATURE_SUPPORT_MATRIX.md` 与 `Bellhop_RayReuse/doc/reference/REFERENCE_FEATURE_SUPPORT_MATRIX.md`；后者仍保留旧阶段限制，只作历史证据，不作 scope |
| `STD` | `test/standard_cases/coverage.toml`、各 case `case.toml`、`codes/standard_cases.py`：共享 adapter/oracle 与版本 allow-list |
| `TEST` | F2CPP CTest 37/37；RayReuse isolated clean CTest 28/28；standard-case Python 148/148；RayReuse 支持的 single profile 20/20；broadband 三模式各 16 cases/21 products，逐文件 SHA-256 一致 |
| `SBP-PROBE` | 以共享 `source_beam_pattern_directional` 和 omni control 输入运行 RayReuse，parser/PRT 均接受 directional，但读取 SHD 后两者 pressure array 完全相同（max abs diff 0），与 `R-TL` 未使用 `sourceBeamPattern` 的代码一致 |

## 4. Production feature parity 表

### 4.1 TL / Influence

| Feature | F2CPP | RayReuse | Status | Evidence |
| --- | --- | --- | --- | --- |
| TL-01 — coherence `C` 跨 F2CPP production TL family | 全部正式 TL family 按其约束支持 | 只有 TL-02 的受限组合闭环 | `GAP` | `F-PARSER`, `R-PARSER`, `F-TL`, `R-TL`, `TEST`；当前可声称的 parity 仅为 TL-02 明示子集 |
| TL-02 — Cartesian Cerveny，`MS` curvature、pressure component、point/single/rectilinear/C-SSP | 支持 | 支持 | `PARITY` | `F-TL`, `R-TL`, `STD`, `TEST`；这是当前 RayReuse SHD 主链 |
| TL-03 — incoherent `I` | 支持 | enum/parser/runtime 均无 | `GAP` | `F-PARSER`, `F-MODEL`, `F-TL` 对比 `R-PARSER`, `R-MODEL` |
| TL-04 — semicoherent `S` | 支持 | enum/parser/runtime 均无 | `GAP` | 同 TL-03 |
| TL-05 — Cartesian Cerveny 完整 production 选项（F/M/W width、D/S/Z curvature、P/V/H component） | 支持 | parser 只接受 `MS` 和 `P`，solver 固定单一路径 | `GAP` | `F-PARSER`, `F-TL`, `R-PARSER`, `R-TL` |
| TL-06 — ray-centered Cerveny | 支持 | 无 parser/Influence/runtime dispatch | `GAP` | `F-PARSER`, `F-TL`；RayReuse CMake/solver 仅 Cartesian Cerveny |
| TL-07 — Cartesian GeoHat | 支持 | G 只用于 Arrival/Eigenray traversal，不是 TL Influence | `GAP` | `F-TL`, `R-TL`, `R-PRODUCT` |
| TL-08 — ray-centered GeoHat | 支持 | 无 parser/Influence/runtime dispatch | `GAP` | `F-PARSER`, `F-TL`, `R-PARSER`, `R-TL` |
| TL-09 — Cartesian GeoGaussian | 支持 | B 只用于 Arrival/Eigenray traversal，不是 TL Influence | `GAP` | `F-TL`, `R-TL`, `R-PRODUCT` |
| TL-10 — ray-centered GeoGaussian | F2CPP 未正式支持 | 未支持 | `F2CPP_OUT_OF_SCOPE` | `MATRIX`, F2CPP parser/solver 均不提供该组合 |
| TL-11 — Simple Gaussian | coherent point-source、Cartesian、rectilinear 限制下支持 | 无 enum/parser/Influence/runtime dispatch | `GAP` | `F-PARSER`, `F-TL`, `R-PARSER`, `R-MODEL` |
| TL-12 — directional `.sbp` 对 TL pressure 生效 | 支持且有 shared Origin/F2CPP case | parser 接受并打印 marker，但 TL solver 未应用 pattern | `GAP` | `F-TL`, `R-PARSER`, `R-TL`, `STD`, `SBP-PROBE`；这是 silent-ignore correctness gap |
| TL-13 — Cartesian Cerveny 1～3 images 与 beam window | 支持 | 在 TL-02 子集内支持 | `PARITY` | 两边 parser 均校验 image count ≤ 3 并传入 Cartesian Cerveny Influence；component/shared TL regression 闭环 |

### 4.2 Source

| Feature | F2CPP | RayReuse | Status | Evidence |
| --- | --- | --- | --- | --- |
| SRC-01 — point source、single source depth | 支持 | 支持 | `PARITY` | `F-PARSER`, `R-PARSER`, `R-MODEL`, `STD`, `TEST` |
| SRC-02 — line source | 支持 | parser 显式拒绝；无 line-source scaling dispatch | `GAP` | `F-PARSER`, `F-TL`, `R-PARSER`, `R-TL` |
| SRC-03 — directional `.sbp`：R/A/a/E | 支持 | parser、逐频 acoustic projection 与 writer 路径存在 | `PARITY` | directional R shared oracle；Arrival component regression 验证逐频 amplitude 改变；Eigenray 产品不跨频共享 writer。TL 例外见 TL-12 |
| SRC-04 — multisource / multiple source depths | 支持，source vector 贯穿 solver 与 writers | `SimulationCase` 只有单 `Source`，parser 强制 1，全部 writer header 固定 source count 1 | `ARCHITECTURAL_CONFLICT` | `F-MODEL`, `R-MODEL`, `R-PRODUCT`；会影响 cache ownership、product dimensions 与 sequencing |

### 4.3 Receiver

| Feature | F2CPP | RayReuse | Status | Evidence |
| --- | --- | --- | --- | --- |
| REC-01 — rectilinear receiver | 支持 | 在当前 TL/R/A/a/E 子集内支持 | `PARITY` | `F-MODEL`, `R-MODEL`, `STD`, `TEST` |
| REC-02 — Cartesian TL irregular receiver | 支持 legacy irregular layout | 只有 depth × range Cartesian-product grid；SHD writer 固定 `rectilin` | `ARCHITECTURAL_CONFLICT` | `F-PARSER`, `F-MODEL`, `R-PARSER`, `R-MODEL`, `R-PRODUCT` |
| REC-03 — paired irregular A/a/E | 支持 paired receiver identity/sequence | ArrivalWorkspace/Eigenray hits 与 writers 按 depth × range cell 编址 | `ARCHITECTURAL_CONFLICT` | `F-MODEL`, `R-MODEL`, `R-PRODUCT` |
| REC-04 — ray-centered receiver 约束（regular/equal-range） | 在 ray-centered family 下支持并校验 | RayReuse 无 ray-centered TL/product family | `GAP` | `F-PARSER`, `F-MODEL`, `F-TL`, `R-PARSER` |
| REC-05 — ray-centered irregular receiver | F2CPP 未正式支持 | 未支持 | `F2CPP_OUT_OF_SCOPE` | `MATRIX` |

### 4.4 SSP

| Feature | F2CPP | RayReuse | Status | Evidence |
| --- | --- | --- | --- | --- |
| SSP-01 — C-linear | 支持 | 支持 | `PARITY` | `F-PARSER`, `R-PARSER`, `R-TL`, `STD`, `TEST` |
| SSP-02 — PCHIP `P` | 支持 | parser 拒绝；geometry tracer/stepper 绑定 `CLinearSsp` | `ARCHITECTURAL_CONFLICT` | `F-PARSER`, `F-GEOM`, `R-PARSER`, `R-GEOM` |
| SSP-03 — N2-linear | 支持 | 同 SSP-02 | `ARCHITECTURAL_CONFLICT` | 同 SSP-02 |
| SSP-04 — spline `S` | 支持 | 同 SSP-02 | `ARCHITECTURAL_CONFLICT` | 同 SSP-02 |
| SSP-05 — Q + `.ssp` range-dependent tabulation | 支持 | parser 拒绝；model/tracer 无 range-dependent SSP 表示 | `ARCHITECTURAL_CONFLICT` | `F-PARSER`, `F-MODEL`, `F-GEOM`, `R-PARSER`, `R-GEOM`, `STD` |

### 4.5 Boundary / material

| Feature | F2CPP | RayReuse | Status | Evidence |
| --- | --- | --- | --- | --- |
| BND-01 — flat boundary geometry | 支持 | 支持 | `PARITY` | parser/model/component tests 与共享 vacuum/rigid/acoustic cases |
| BND-02 — piecewise-linear `LS` | 支持 | 支持 | `PARITY` | `F-PARSER`, `R-PARSER`, shared `i3_piecewise_linear` |
| BND-03 — piecewise-linear `LL` | 支持 | 支持 | `PARITY` | `F-PARSER`, `R-PARSER`, shared elastic LL case |
| BND-04 — canonical curvilinear `C` | 支持，保存 boundary frame/curvature | parser 显式拒绝，boundary model 无 frame/curvature 表示 | `ARCHITECTURAL_CONFLICT` | `F-GEOM`, `R-PARSER`, `R-GEOM`, shared case 仅 Origin/F2CPP |
| BND-05 — boundary type V/R | 支持 | 支持 | `PARITY` | shared vacuum/rigid cases，parser/component regressions |
| BND-06 — grain `G` | 支持 | 支持 | `PARITY` | shared grain/control cases；raw reflection material 保持 frozen、结果逐频 |
| BND-07 — table `F` + `.trc/.brc` | 支持 | 支持 | `PARITY` | shared top/bottom table/control cases |
| BND-08 — `A` + `.ati/.bty` + LL elastic P/S | 支持 | 支持 | `PARITY` | shared `elastic_ll_top_bottom`，parser/component/oracle 闭环 |
| BND-09 — flat `A` elastic halfspace P/S | 支持 | parser/elastic coefficient 实现存在，但 shared flat-elastic case 排除 RayReuse，缺 executable oracle 闭环 | `GAP` | `STD` 的 `elastic_halfspace_flat` allow-list；严格 production 证据链未闭合 |
| BND-10 — `G/F` 与 LL 组合 | F2CPP 正式矩阵也不支持 | RayReuse 未支持 | `F2CPP_OUT_OF_SCOPE` | `MATRIX` |

### 4.6 Attenuation

| Feature | F2CPP | RayReuse | Status | Evidence |
| --- | --- | --- | --- | --- |
| ATT-01 — attenuation units N/F/M/Q/L | 支持并有 executable standard cases | parser/转换函数存在，unit tests 存在；相应 per-unit shared executable cases 明确排除 RayReuse | `GAP` | `Bellhop_RayReuse/src/acoustics/attenuation.cpp`, unit tests, `STD` allow-list；缺 product-level oracle 闭环 |
| ATT-02 — attenuation unit W | 支持 | 支持 | `PARITY` | 多个共享 RayReuse environment/material case + component/unit regression |
| ATT-03 — Thorp water-column/boundary attenuation | 支持 | 支持 | `PARITY` | shared `constant_speed_thorp` + unit/component regression |
| ATT-04 — Francois–Garrison | 支持 | parser/attenuation model 显式拒绝 | `GAP` | `F-PARSER`, `R-PARSER`, RayReuse attenuation dispatch throw path, `STD` |
| ATT-05 — biological attenuation | 支持 | parser/attenuation model 显式拒绝 | `GAP` | 同 ATT-04 |
| ATT-06 — elastic boundary P/S attenuation in current W/LL slice | 支持 | 支持，保持逐频 complex reflection result | `PARITY` | shared elastic LL case、raw projection/component tests；其他 unit 受 ATT-01 约束 |

### 4.7 Products

| Feature | F2CPP | RayReuse | Status | Evidence |
| --- | --- | --- | --- | --- |
| PRD-01 — R：generalized R、directional `.sbp`、active/terminal prefix、explicit Nalpha=1、Origin-compatible writer | 支持 | 在单 point/single/rectilinear、单频范围支持；多频明确拒绝 | `PARITY` | `R-PARSER`, `R-PRODUCT`, `R-CLI`, shared `ray_trace_directional_tabulated`, writer/component tests |
| PRD-02 — A ASCII：Cartesian G/B | 支持 | 在 single/rectilinear 范围支持；多频逐频独立发布 | `PARITY` | shared hat ASCII/zero、Arrival component/writer tests、`TEST` |
| PRD-03 — a Binary：Cartesian G/B | 支持 | 在 single/rectilinear 范围支持；多频逐频独立发布 | `PARITY` | shared hat Binary/zero、Arrival component/writer tests、`TEST` |
| PRD-04 — E Eigenray：Cartesian G/B | 支持 | 在 single/rectilinear 范围支持；多频逐频独立发布 | `PARITY` | shared Gaussian/zero、Eigenray component/writer tests、`TEST` |
| PRD-05 — A/a/E ray-centered `g` | 支持 | parser 明确只允许 Cartesian G/B，无 ray-centered traversal | `GAP` | `F-PARSER`, `F-TL`, `R-PARSER`, `R-PRODUCT`, `STD` |
| PRD-06 — TL/A/a/E irregular receiver product semantics | 支持到 REC-02/REC-03 所述范围；R 本身只使用单 receiver range | 当前 SHD/workspace/hit/writer schema 不能表达 | `ARCHITECTURAL_CONFLICT` | `F-MODEL`, `R-MODEL`, `R-PRODUCT` |
| PRD-07 — R/A/a/E multisource sequencing/headers | 支持 | writer headers 与 solver lifecycle 固定一个 source | `ARCHITECTURAL_CONFLICT` | `F-MODEL`, `R-MODEL`, `R-PRODUCT` |
| PRD-08 — line-source product scaling | 支持 | parser/runtime 不支持 line source | `GAP` | SRC-02 的全链路证据 |

### 4.8 Support matrix / 声明一致性

| Feature | F2CPP | RayReuse | Status | Evidence |
| --- | --- | --- | --- | --- |
| DOC-01 — production support matrix 与真实 executable surface 同步 | 当前矩阵与 parser/solver/tests 基本一致 | 仍以旧阶段 `CC-only/single-source/... Deferred` 叙述，既漏掉 RR-B1～B4 已支持项，也不能作为新 parity scope | `GAP` | `MATRIX` 与本报告逐项代码证据对照 |

## 5. RayReuse 当前 execution 范围

以下只记录当前已证明范围，不把执行模式通过外推到尚未支持的 feature：

| Execution | 当前 production-supported 范围 | 当前明确边界 | 验证事实 |
| --- | --- | --- | --- |
| `nonreuse` | TL-02；PRD-02/03/04；当前 BND/ATT parity 子集 | R 不接受显式 execution mode；所有 SRC/REC/SSP/TL gap 保持不支持 | shared broadband 16 cases / 21 products 通过 |
| `reuse` | 与 nonreuse 相同；trace once → frozen cache → per-frequency acoustic/product state | 不为 gap feature 提供隐式 fallback；R 拒绝 | 21 个逐频产品与 nonreuse SHA-256 全同；cache fingerprint tests 通过 |
| `parallel` | 与 reuse 相同；frequency workers + serial ordered consumer publish | 没有新增 source/receiver owner；不代表 gap feature 已支持 | 21 个逐频产品与 nonreuse/reuse SHA-256 全同；publish names/order 稳定 |

现有 evidence 继续满足关键所有权边界：trajectory/geometry/reflection raw material 在
frozen `RayPathCache`；amplitude、phase、complex travel time、active prefix、reflection
result、ArrivalWorkspace 和 Eigenray hits 为 per-frequency。审计未发现逐频状态写回
frozen cache 的证据。这个结论只覆盖当前已 dispatch 的 feature slice。

## 6. 本次验证记录

- `Bellhop_F2CPP/build/release`：CTest 37/37 通过。
- RayReuse 使用 `/tmp` 隔离 Release clean build：configure/build 成功，CTest 28/28 通过。
- `test/standard_cases` Python/tool tests：148/148 通过。
- 共享 standard cases 中当前允许 RayReuse 的 single profile：20/20 通过；其他 feature
  case 按 manifest 明确 skip，不能作为支持证据。
- RayReuse broadband `nonreuse` / `reuse` / `parallel`：每模式 16 个支持案例通过，
  各自产生 21 个逐频产品；relative path 集合和逐文件 SHA-256 三模式完全一致。
- directional TL probe：directional 与 omni SHD pressure array 完全一致，最大绝对差为
  `0.0`；结合 solver 代码确认 TL-12 为真实 silent-ignore gap，不是报告推断。
- 仓库内现有 RayReuse release build metadata 指向另一个绝对工作区，不能作为 clean-build
  证据；隔离构建已排除源代码构建失败。该生成目录不影响 tracked working tree。

## 7. 审计结论

### A. 当前完整 GAP 列表

1. `TL-01`：coherent `C` 尚未覆盖 F2CPP 全部 production TL family；只有 TL-02 子集 parity。
2. `TL-03`：incoherent `I`。
3. `TL-04`：semicoherent `S`。
4. `TL-05`：Cartesian Cerveny 完整 width/curvature/component dispatch。
5. `TL-06`：ray-centered Cerveny。
6. `TL-07`：Cartesian GeoHat TL。
7. `TL-08`：ray-centered GeoHat TL。
8. `TL-09`：Cartesian GeoGaussian TL。
9. `TL-11`：Simple Gaussian TL。
10. `TL-12`：directional `.sbp` 被 TL parser 接受但未作用于 pressure，属于 silent fallback。
11. `SRC-02` / `PRD-08`：line source 及其产品 scaling。
12. `SRC-04` / `PRD-07`：multisource / multiple source depths、产品 sequencing/header（architectural conflict）。
13. `REC-02` / `PRD-06`：Cartesian TL irregular receiver（architectural conflict）。
14. `REC-03` / `PRD-06`：paired irregular A/a/E（architectural conflict）。
15. `REC-04`：ray-centered regular/equal-range receiver 路径。
16. `SSP-02`：PCHIP SSP（architectural conflict）。
17. `SSP-03`：N2-linear SSP（architectural conflict）。
18. `SSP-04`：spline SSP（architectural conflict）。
19. `SSP-05`：Q + `.ssp` range-dependent SSP（architectural conflict）。
20. `BND-04`：canonical curvilinear `C` boundary（architectural conflict）。
21. `BND-09`：flat `A` elastic P/S 缺 RayReuse executable oracle 闭环。
22. `ATT-01`：attenuation units N/F/M/Q/L 缺 RayReuse product-level oracle 闭环。
23. `ATT-04`：Francois–Garrison attenuation。
24. `ATT-05`：biological attenuation。
25. `PRD-05`：A/a/E ray-centered `g`。
26. `DOC-01`：RayReuse production support matrix 仍是旧阶段声明，未与真实 executable surface 同步。

`TL-10`、`REC-05`、`BND-10` 不在 GAP 列表，因为它们是
`F2CPP_OUT_OF_SCOPE`，而不是把 RayReuse 旧 Deferred 误当作 out of scope。

### B. 按优先级分组

- **P0 — 主功能 / 后续 Influence 架构**：TL-01、TL-03～TL-09、TL-11、TL-12；
  SSP-02～SSP-05；SRC-04、REC-02、REC-03 所暴露的 source/receiver ownership 与
  product dimension 冲突。
- **P1 — 重要 parity gap**：SRC-02/PRD-08、REC-04、BND-04、ATT-04、ATT-05、
  PRD-05。
- **P2 — 外围或证据闭环 gap**：BND-09、ATT-01、DOC-01。

### C. 推荐下一步先补哪一组

只推荐一个下一阶段：**TL coherence + Influence dispatch parity**。

### D. 建议把下一阶段控制在什么范围

仅在当前已经稳定的 `point + single source + rectilinear receiver + C-linear SSP +
现有 boundary` 表面内，接入 F2CPP 已支持的 `C/I/S` 与 TL beam/coordinate dispatch，
并修复 TL directional `.sbp` 的逐频 source weighting；同步复用现有 F2CPP/shared
standard cases，在 nonreuse/reuse/parallel 三路径验证。不要同时带入 SSP parity、
multisource、irregular receiver、canonical curvilinear boundary 或 attenuation 扩展。

### E. 当前 git 状态

审计开始时 working tree clean，分支为 `feat/i8-arrivals-eigenray`，HEAD 为
`39c0407d634b387a6b3c81d6fdf94c330cdd1bb2`。生成报告后，当前唯一未提交变更为本文件：
`Bellhop_RayReuse/doc/reports/REPORT_F2CPP_RAYREUSE_PARITY.md`；未修改 F2CPP、RayReuse
代码、测试、算例或既有文档。
