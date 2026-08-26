# Bellhop_F2CPP → Bellhop_RayReuse Production Feature Parity Audit

审计日期：2026-08-25

原始审计基线：`39c0407d634b387a6b3c81d6fdf94c330cdd1bb2`（`feat/i8-arrivals-eigenray`）

FP-1A 实施前基线：`f7b05e4a300cbd68c27099e64f5768ed62b58265`

FP-1B 实施前基线：`bbc24fbbded037d1f6b4cd0b4825748ad2335d3c`

FP-1C 实施前基线：`9f7d64eb9076802541cce16326f0909dcde1ecc7`

FP-1D 实施前基线：`fa66c6120090038fcdaa072d4734c694b7a00374`

FP-1E 实施前基线：`7b6ff32659c070a64585ff95784294138ca8c407`

FP-1F 实施前基线：`094ffe326b2ca975b45720bbcdcf5cd4fd4fe436`

FP-1G 实施前基线：`16449e0ef1ce2f5e43b0f78001d7db51c7ca9326`

本报告在原始只读审计之后，按 FP-1A～FP-1G 的实际实现与验证结果增量更新。新增
范围严格限定为 `point + single source depth + rectilinear receiver + C-linear SSP`
下的 Cartesian Cerveny、Cartesian GeoHat 与 Cartesian GeoGaussian C/I/S TL，以及
Cartesian Simple Gaussian coherent TL、Cartesian Cerveny `P/V/H` legacy component
selector、`D/S/Z` boundary curvature condition、`F/M/W` beam width 和共用的
directional `.sbp` source weighting；
其他原始 GAP 不因本次更新而放宽。

## 1. 结论摘要

RayReuse 目前**尚未达到** F2CPP 二维 production feature surface 的完整 parity。
已经闭环的是：当前受限的 Cartesian Cerveny、Cartesian GeoHat 与 Cartesian
GeoGaussian C/I/S TL、Cartesian Simple Gaussian coherent TL、Cartesian Cerveny
`P/V/H + {F,M,W}{D,S,Z}`、单点单源规则接收网格、C-linear SSP、RR-B1 边界子集、单频
R，以及 Cartesian G/B 的
A/a/E；这些已可在 RayReuse 的 `nonreuse`、`reuse`、`parallel` 路径内按其适用
范围使用。

主要差距不是旧 RayReuse `Deferred` 列表，而是以下真实代码边界：

- Cartesian Cerveny 的 `C/I/S + {F,M,W}{D,S,Z} + P/V/H`、Cartesian GeoHat 与 Cartesian
  GeoGaussian 的 `C/I/S` 及 Cartesian Simple Gaussian 的 coherent production
  contribution 已接入；其他 F2CPP TL beam/coordinate family 尚未接入。
- Origin/F2CPP 的 Cartesian Cerveny `P/V/H` 是被 parser 保存并写入 PRT、但不被
  Cartesian Influence 数值分支使用的 legacy selector；RayReuse 保持相同 observable
  contract。ray-centered Cerveny 才有的 V/H derivative 不属于本 slice。
- TL directional `.sbp` 的旧 silent-ignore correctness gap 已关闭：source pattern
  现在在逐频 Project 前作用于每条 ray 的 source amplitude，C/I/S 共用该语义。
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
| `R-PARSER` | `Bellhop_RayReuse/src/io/environment_parser.cpp`：TL 接受 Cartesian Cerveny `CC/IC/SC + {F,M,W}{D,S,Z} + P/V/H`、Cartesian GeoHat `CG/IG/SG`（含 `^`/blank alias）、Cartesian GeoGaussian `CB/IB/SB` 与 coherent Cartesian Simple Gaussian `CS`；`IS/SS`、无效 width/curvature/component 和非 Cerveny family 的 Cerveny tail 明确拒绝；继续显式拒绝 line、irregular、非 C SSP、FG/biological、canonical C boundary；source count 必须为 1 |
| `F-MODEL` | `Bellhop_F2CPP/include/bellhop/model/simulation_case.hpp` 与 boundary/SSP model：source vector、receiver layout、coherence、coordinate/beam families、curvilinear geometry |
| `R-MODEL` | `Bellhop_RayReuse/include/rayreuse/model/simulation_case.hpp`、`beam_width.hpp`、`beam_curvature.hpp`、`src/model/simulation_case.cpp`：单 `Source`、Cartesian-product `ReceiverGrid`；显式 C/I/S coherence、`P/V/H` field component、`F/M/W` beam width、`D/S/Z` reflection curvature 与 complex-pressure/intensity workspace 选择；非 Cerveny family 只允许 pressure/minimum-width/standard-curvature，Simple Gaussian 模型只允许 coherent；仍无 line/multisource/irregular/ray-centered；构造 C-linear SSP |
| `F-TL` | `Bellhop_F2CPP/src/solver/single_frequency_solver.cpp` 及 `src/influence/`：按 coherence、beam family、coordinate、source geometry dispatch；Cartesian Cerveny constructor 不接收 component，只有 ray-centered Cerveny 接收并应用 V/H derivative |
| `R-TL` | `Bellhop_RayReuse/src/solver/single_frequency_solver.cpp`、`beam_epsilon.cpp`、`cartesian_cerveny_influence.cpp`、`geometric_hat_influence.cpp`、`geometric_gaussian_influence.cpp`、`simple_gaussian_influence.cpp` 与 `pressure_scaling.cpp`：固定 `CLinearSsp`；按 Cerveny/GeoHat/GeoGaussian/Simple Gaussian dispatch；C 使用 complex pressure，前三个 family 的 I/S 使用逐频 intensity；Cartesian Cerveny 每频每 ray 构造 F/M/W epsilon，并按 width dispatch KMAH；`P/V/H` 按 Origin/F2CPP legacy contract 不改变 contribution；G/B/S 使用 geometric point normalization；`.sbp` 与适用的 S Lloyd factor 在逐频 Project 前形成 source amplitude |
| `F-GEOM` | `Bellhop_F2CPP/include/bellhop/ray/geometry_tracer.hpp`、`src/ray/flat_boundary_reflection.cpp` 与 boundary geometry：通用 SSP evaluator、canonical curvilinear frame/curvature；D/S/Z 在 reflection 时对完整 `RN` 分别乘 2、保留、置零 |
| `R-GEOM` | `Bellhop_RayReuse/include/rayreuse/ray/geometry_tracer.hpp`、`src/ray/geometry_tracer.cpp`、`flat_boundary_reflection.cpp` 与 boundary geometry：绑定 `CLinearSsp`，仅 flat/piecewise-linear boundary；D/S/Z 使用同一 frequency-independent reflection jump 公式并写入冻结 real dynamic-ray bases |
| `R-PRODUCT` | RayReuse `arrival_solver.cpp`、`eigenray_solver.cpp`、`ray_writer.cpp`、`arrival_writer.cpp`、`eigenray_writer.cpp`：R/A/a/E frequency-local 产品已接入，但 writer headers/layout 固定一个 source 与规则网格 |
| `R-CLI` | `Bellhop_RayReuse/app/main.cpp`：R/A/a/E/TL 正式 dispatch；R 只允许单频；A/a/E/TL 支持 nonreuse/reuse/parallel；逐频 serial consumer 发布 |
| `MATRIX` | `Bellhop_F2CPP/doc/reference/REFERENCE_FEATURE_SUPPORT_MATRIX.md` 与 `Bellhop_RayReuse/doc/reference/REFERENCE_FEATURE_SUPPORT_MATRIX.md`；RayReuse 矩阵已同步 FP-1A～FP-1G 实际支持面与剩余限制 |
| `STD` | `test/standard_cases/coverage.toml`、各 case `case.toml`、`codes/standard_cases.py`：共享 adapter/oracle 与版本 allow-list |
| `TEST` | RayReuse isolated clean CTest 31/31；仓库全量 Python/pytest 168/168；standard-case unittest 153/153；RayReuse single 42 个支持案例通过；broadband 三模式各 34 个支持案例、39 个产品通过且跨模式逐字节一致 |
| `FP1A-ORACLE` | 共享 `constant_speed_direct`、`incoherent_direct`、`semicoherent_direct`、directional/omni `.sbp` 输入：F2CPP 与 RayReuse pressure/TL 全部 0 差异；四个 FP-1A broadband case 的 nonreuse/reuse/parallel SHD 逐字节一致；directional 与 omni 最大 pressure 差 `2.1412473171949387e-2` |
| `FP1B-ORACLE` | 共享 `geometric_hat_cartesian`、safe control、`geometric_hat_incoherent`、`geometric_hat_semicoherent`、`geometric_hat_directional`：F2CPP 与 RayReuse pressure/TL 全部 0 差异；Origin/F2CPP 最大 pressure absolute `4.16500123e-9`、最大 TL 差 `7.62939453e-6 dB`；GeoHat C/I/S 两频 SHD 三模式逐字节一致 |
| `FP1C-ORACLE` | 共享 `geometric_gaussian_cartesian`、`geometric_gaussian_incoherent`、`geometric_gaussian_semicoherent`、`geometric_gaussian_directional`：F2CPP 与 RayReuse pressure/TL 全部 0 差异；Origin/F2CPP 最大 pressure absolute `1.3038516e-8`、最大 TL 差 `2.28881836e-5 dB`；GeoGaussian C/I/S 两频 SHD 三模式逐字节一致 |
| `FP1D-ORACLE` | 共享 `simple_gaussian_cartesian` 与最小 directional case：F2CPP 与 RayReuse pressure/TL 全部 0 差异；Origin/F2CPP 最大 pressure absolute `6.71586253e-9`、最大 TL 差 `2.28881836e-5 dB`；directional 与 omni 最大 pressure 差 `2.50503421e-2`；Simple Gaussian 两频 SHD 三模式逐字节一致 |
| `FP1E-ORACLE` | 共享 `cartesian_component_pressure/vertical/horizontal`：Origin 与 F2CPP 各自 P/V/H SHD 逐字节相同；F2CPP/RayReuse 三组件 pressure/TL 全部 0 差异；Origin/F2CPP 最大 pressure absolute `1.49468509e-8`、最大 TL 差 `7.62939453e-6 dB`；两频 P/V/H 的 nonreuse/reuse/parallel 9 个 SHD 共用 SHA-256 `0d534d63df8e7d13c9b11f60cb4e7d0d12c0bfeb1d96d08482f7cbf4ddec82e2` |
| `FP1F-ORACLE` | 共享 flat-gradient `MD/MZ`：F2CPP/RayReuse pressure/TL 全部 0 差异；Origin/F2CPP 最大 pressure absolute `1.50819641e-8`、最大 TL 差 `1.14440918e-5 dB`；既有五选项 validator 的 `MD/MS/MZ` effect guards 与 Origin/F2CPP comparison 全通过；两频 D/Z 的 nonreuse/reuse/parallel SHD 分别逐字节一致，reuse/parallel cache fingerprint 前后相同 |
| `FP1G-ORACLE` | 共享 flat-gradient `FS/WS`：F2CPP/RayReuse pressure/TL 全部 0 差异；Origin/F2CPP 最大 pressure absolute `3.35405446e-8`、最大 TL 差 `3.05175781e-5 dB`；既有五选项 validator 的 F/M/W epsilon anchors、12 个 option-effect guards 与 5 个 Origin/F2CPP comparisons 全通过；两频 F/W 的 nonreuse/reuse/parallel SHD 分别逐字节一致，parallel cache fingerprint 前后相同且 F/W cache fingerprint 相同 |

## 4. Production feature parity 表

### 4.1 TL / Influence

| Feature | F2CPP | RayReuse | Status | Evidence |
| --- | --- | --- | --- | --- |
| TL-01 — coherence `C` 跨 F2CPP production TL family | 全部正式 TL family 按其约束支持 | Cartesian Cerveny、Cartesian GeoHat、Cartesian GeoGaussian 与 Cartesian Simple Gaussian 的受限 slice 闭环；其他 family 未闭环 | `GAP` | `F-PARSER`, `R-PARSER`, `F-TL`, `R-TL`, `TEST`；不得把四个 Cartesian 子集外推为全部 TL family parity |
| TL-02 — Cartesian Cerveny，C/I/S、`{F,M,W}{D,S,Z}` beam options、P/V/H component、point/single/rectilinear/C-SSP | 支持 | 支持 | `PARITY` | `F-TL`, `R-TL`, `R-GEOM`, `STD`, `TEST`, `FP1A-ORACLE`, `FP1E-ORACLE`, `FP1F-ORACLE`, `FP1G-ORACLE`；C 原路径保持，I/S 使用独立逐频 intensity workspace，P/V/H 保持 legacy identity |
| TL-03 — incoherent `I`（当前 Cartesian Cerveny/GeoHat/GeoGaussian slice） | 支持 | 三个 family 的 parser/runtime/Influence/output/oracle 闭环 | `PARITY` | Cerveny 为 image coherent sum 后 ABS²；GeoHat 为 attenuated real constant 平方后 `W_hat` 只乘一次；GeoGaussian 为 `sqrt(2π) × power × W_gaussian` 且权重只乘一次；均在总 intensity 后 sqrt；`FP1A-ORACLE`, `FP1B-ORACLE`, `FP1C-ORACLE` |
| TL-04 — semicoherent `S`（当前 Cartesian Cerveny/GeoHat/GeoGaussian slice） | 支持 | 三个 family 的 parser/runtime/Influence/output/oracle 闭环 | `PARITY` | 各 family 与其 I 模式共用 contribution；S 的差异仅为 Project 前逐频 Lloyd/source amplitude；`F-TL`, `R-TL`, `FP1A-ORACLE`, `FP1B-ORACLE`, `FP1C-ORACLE` |
| TL-05 — Cartesian Cerveny production beam options | 支持 F/M/W width × D/S/Z curvature | F/M/W × D/S/Z 全部 parser/runtime/epsilon/KMAH/SHD/oracle 闭环 | `PARITY` | `F-PARSER`, `R-PARSER`, `F-GEOM`, `R-GEOM`, `R-TL`, `FP1F-ORACLE`, `FP1G-ORACLE`；F/M 使用 positive-imaginary epsilon 与 complex-q branch，W 使用 real epsilon 与 real-q crossing |
| TL-06 — ray-centered Cerveny | 支持 | 无 parser/Influence/runtime dispatch | `GAP` | `F-PARSER`, `F-TL`；RayReuse CMake/solver 仅 Cartesian Cerveny |
| TL-07 — Cartesian GeoHat，C/I/S、point/single/rectilinear/C-SSP | 支持 | parser/runtime/Influence/geometric scaling/SHD/oracle 闭环 | `PARITY` | `R-PARSER`, `R-TL`, `STD`, `TEST`, `FP1B-ORACLE`；不外推到 ray-centered/line/multisource/irregular/non-C SSP |
| TL-08 — ray-centered GeoHat | 支持 | 无 parser/Influence/runtime dispatch | `GAP` | `F-PARSER`, `F-TL`, `R-PARSER`, `R-TL` |
| TL-09 — Cartesian GeoGaussian，C/I/S、point/single/rectilinear/C-SSP | 支持 | parser/runtime/逐频 width 与 membership/Influence/geometric scaling/SHD/oracle 闭环 | `PARITY` | `R-PARSER`, `R-TL`, `STD`, `TEST`, `FP1C-ORACLE`；`sigma_nf`、`sigma_lambda`、`sigma_1`、membership 与 Gaussian kernel 全部逐频计算 |
| TL-10 — ray-centered GeoGaussian | F2CPP 未正式支持 | 未支持 | `F2CPP_OUT_OF_SCOPE` | `MATRIX`, F2CPP parser/solver 均不提供该组合 |
| TL-11 — Cartesian Simple Gaussian coherent TL，point/single/rectilinear/C-SSP | 支持；不提供 I/S accumulator | parser/runtime/Influence/geometric scaling/SHD/oracle 闭环；`IS/SS` 明确拒绝 | `PARITY` | `R-PARSER`, `R-TL`, `STD`, `TEST`, `FP1D-ORACLE`；保留 `0.98F` 混合精度、legacy SINT、严格 q crossing 与 right-endpoint acoustic state |
| TL-12 — directional `.sbp` 对当前 Cartesian Cerveny/GeoHat/GeoGaussian C/I/S 及 Simple Gaussian C TL 生效 | 支持且有 shared Origin/F2CPP case | 支持；四个 family 共用逐 ray、逐频、Project 前 source pattern 路径 | `PARITY` | `F-TL`, `R-PARSER`, `R-TL`, `STD`, `FP1A-ORACLE`, `FP1B-ORACLE`, `FP1C-ORACLE`, `FP1D-ORACLE`；Simple Gaussian directional F2CPP/RayReuse 0 差异，R/A/a/E 既有路径未改 |
| TL-13 — Cartesian Cerveny 1～3 images 与 beam window | 支持 | 在 TL-02 子集内支持 | `PARITY` | 两边 parser 均校验 image count ≤ 3 并传入 Cartesian Cerveny Influence；component/shared TL regression 闭环 |
| TL-14 — Cartesian Cerveny `P/V/H` legacy component selector | parser/model/PRT 支持三者；Cartesian Influence 不读取 selector，故三者数值相同 | parser/model/PRT 支持三者；保持相同 component-independent Cartesian contribution；非 Cerveny V/H 明确拒绝 | `PARITY` | `F-PARSER`, `F-TL`, `R-PARSER`, `R-MODEL`, `R-TL`, `STD`, `TEST`, `FP1E-ORACLE`；不外推到 ray-centered V/H derivative |

### 4.2 Source

| Feature | F2CPP | RayReuse | Status | Evidence |
| --- | --- | --- | --- | --- |
| SRC-01 — point source、single source depth | 支持 | 支持 | `PARITY` | `F-PARSER`, `R-PARSER`, `R-MODEL`, `STD`, `TEST` |
| SRC-02 — line source | 支持 | parser 显式拒绝；无 line-source scaling dispatch | `GAP` | `F-PARSER`, `F-TL`, `R-PARSER`, `R-TL` |
| SRC-03 — directional `.sbp`：TL/R/A/a/E | 支持 | 在各自已支持的 product slice 内支持 | `PARITY` | TL 见 TL-12；directional R shared oracle；Arrival component regression 验证逐频 amplitude 改变；Eigenray 产品不跨频共享 writer |
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
| DOC-01 — production support matrix 与真实 executable surface 同步 | 当前矩阵与 parser/solver/tests 基本一致 | FP-1A coherence、FP-1B Cartesian GeoHat、FP-1C Cartesian GeoGaussian、FP-1D Cartesian Simple Gaussian、FP-1E Cartesian Cerveny P/V/H legacy selector、FP-1F D/S/Z curvature、FP-1G F/M/W width、workspace、`.sbp` 与 execution 范围已同步；其他限制保留 | `PARITY` | `MATRIX` 与本报告逐项代码证据对照 |

## 5. RayReuse 当前 execution 范围

以下只记录当前已证明范围，不把执行模式通过外推到尚未支持的 feature：

| Execution | 当前 production-supported 范围 | 当前明确边界 | 验证事实 |
| --- | --- | --- | --- |
| `nonreuse` | TL-02/03/04/05/07/09/11/12/14（含 {F,M,W}{D,S,Z}）；PRD-02/03/04；当前 BND/ATT parity 子集 | R 不接受显式 execution mode；所有 SRC/REC/SSP/其他 TL gap 保持不支持 | shared broadband 34 个支持案例通过；每频独立 trace + solve |
| `reuse` | 与 nonreuse 相同；trace once → frozen cache → per-frequency acoustic/product state | 不为 gap feature 提供隐式 fallback；R 拒绝 | 全部支持案例通过；39 个产品与 nonreuse 逐字节相同；D/Z/F/W 显式 cache fingerprint 前后相同 |
| `parallel` | 与 reuse 相同；frequency workers + serial ordered consumer publish | 没有新增 source/receiver owner；不代表 gap feature 已支持 | 全部支持案例通过；39 个产品与 nonreuse/reuse 逐字节相同；D/Z/F/W 显式 cache fingerprint 前后相同 |

现有 evidence 继续满足关键所有权边界：trajectory/geometry/reflection raw material 在
frozen `RayPathCache`；amplitude、phase、complex travel time、active prefix、reflection
result、ArrivalWorkspace 和 Eigenray hits 为 per-frequency。审计未发现逐频状态写回
frozen cache 的证据。这个结论只覆盖当前已 dispatch 的 feature slice。

## 6. FP-1A～FP-1G 验证记录

- RayReuse 使用 `/tmp/rayreuse-fp1a-build` 隔离 Release clean build：configure/build 成功，
  CTest 28/28 通过。
- `test/standard_cases` unittest：148/148 通过；仓库全量 `pytest`：163/163 通过。
- 共享 standard cases 中当前允许 RayReuse 的 single profile：24 个支持案例通过；其他
  feature case 按 manifest 明确 skip，不能作为支持证据。
- RayReuse broadband `nonreuse` / `reuse` / `parallel`：每模式 20 个支持案例通过；
  三模式的 25 个 `.shd/.arr/.ray` 产品逐字节一致。
- `constant_speed_direct`、`incoherent_direct`、`semicoherent_direct`、directional 与 omni
  `.sbp` 五个单频案例中，F2CPP 与 RayReuse 的 pressure absolute/relative difference 和
  TL difference 均为 `0`。
- FP-1A 的 I/S/directional/omni 四个 broadband case，三种 execution mode 的 SHD 均
  逐字节相同；C/I/S component test 同时验证 frozen cache fingerprint 前后不变。
- directional 与 omni RayReuse pressure array 不相同，最大绝对差为
  `2.1412473171949387e-2`，证明旧 `.sbp` silent-ignore 已关闭。
- Origin 与 RayReuse 仍使用项目既有 tolerance：I/S 最大 TL 差
  `1.52587891e-5 dB`，directional 最大 TL 差 `2.28881836e-5 dB`；RayReuse 与 F2CPP
  为 0 差异，因此没有为 FP-1A 放宽容差。
- FP-1B 使用 `/tmp/rayreuse-fp1b-clean-build` 隔离 Release configure/build，CTest
  29/29；standard-case unittest 149/149；仓库全量 `pytest` 164/164。
- `geometric_hat_cartesian`、safe control、IG、SG、directional 五个共享单频 case 的
  F2CPP/RayReuse pressure absolute/relative difference 与 TL difference 均为 `0`。
  Origin/F2CPP 沿用既有读写/数值容差，最大 pressure absolute difference 为
  `4.16500123e-9`，最大 TL difference 为 `7.62939453e-6 dB`，未放宽标准。
- GeoHat C/I/S 两频 case 的 nonreuse/reuse/parallel SHD SHA-256 分别逐模式一致；
  全量 broadband 回归中每模式 23 个 shared cases、28 个产品均通过，跨模式逐字节
  一致。reuse/parallel 的显式 cache fingerprint before/after 均为
  `10925417565703232468`。
- 新增 component anchors 绑定 Cartesian `q/q0`、normal offset、linear hat `W`、
  q-zero `pi/2` caustic phase、inactive terminal prefix，以及 I/S 的 attenuation 后
  real constant 平方再单次乘 `W`。GeoHat `.sbp` regression 同时覆盖 C/I/S，现有
  R/A/a/E 与 FP-1A CC/IC/SC 由 full CTest 和 shared single/broadband matrix 保护。
- FP-1C 使用 `/tmp/rayreuse-fp1c-clean-build` 隔离 Release configure/build，CTest
  30/30；standard-case unittest 150/150；仓库全量 `pytest` 165/165。
- `geometric_gaussian_cartesian`、IB、SB、directional 四个共享单频 case 的
  F2CPP/RayReuse pressure absolute/relative difference 与 TL difference 均为 `0`。
  Origin/F2CPP 沿用既有容差，最大 pressure absolute difference 为
  `1.3038516e-8`，最大 TL difference 为 `2.28881836e-5 dB`，未放宽标准。
- GeoGaussian C/I/S 两频 case 的 nonreuse/reuse/parallel SHD 逐字节一致；全量
  RayReuse single 33 个支持案例通过，broadband 每模式 26 个支持案例、31 个产品
  通过且跨模式逐字节一致。reuse/parallel component test 显式验证 frozen cache
  fingerprint 前后不变。
- 新增 component anchors 绑定 `0.2F` near-field 常量、geometric/near-field/
  wavelength-cap 三个 width branch、segment depth gate、normal membership、Gaussian
  kernel、q-zero caustic 与 inactive terminal prefix；I/S anchor 明确验证
  `sqrt(2π) × power × W`，排除 `W²`。现有 AB/aB/EB 方法体未改，并由 full CTest
  与 shared single/broadband matrix 保护。
- FP-1D 使用隔离 Release build，CTest 31/31；standard-case unittest 151/151；仓库
  全量 `pytest` 166/166。RayReuse single 35 个支持案例通过；broadband 每模式 27 个
  支持案例、32 个产品通过且跨模式逐字节一致。
- `simple_gaussian_cartesian` 与 `simple_gaussian_directional` 的 F2CPP/RayReuse
  pressure absolute/relative difference 与 TL difference 均为 `0`。Origin/F2CPP
  沿用既有容差，最大 pressure absolute difference 为 `6.71586253e-9`、最大 TL
  difference 为 `2.28881836e-5 dB`。directional/omni 最大 pressure difference
  为 `2.50503421e-2`。
- Simple Gaussian component anchor 固定 `beta=double(0.98F)`、
  `A=-4 log(beta)/dalpha²`、legacy SINT、CPA/DS/SX1、严格 q-zero caustic、
  interpolated complex delay、right-endpoint amplitude/reflection phase 和 coherent
  contribution；静态接口检查确认没有 I/S accumulator。frozen cache 仍只读，逐频
  amplitude/phase/complex travel time/active prefix 未写回 cache。
- FP-1E 使用隔离 Release build，CTest 31/31；standard-case unittest 151/151；仓库
  全量 `pytest` 166/166。RayReuse single 38 个支持案例通过；broadband 每模式 30 个
  支持案例、35 个产品通过且跨模式逐字节一致。
- `cartesian_component_pressure/vertical/horizontal` 三个共享单频 case 的
  F2CPP/RayReuse pressure absolute/relative difference 与 TL difference 均为 `0`。
  Origin/F2CPP 沿用既有容差，最大 pressure absolute difference 为
  `1.49468509e-8`、最大 TL difference 为 `7.62939453e-6 dB`；Origin 与 F2CPP
  各自的 P/V/H SHD 均逐字节相同。
- parser/model/PRT 与 9 组 `C/I/S × P/V/H` component tests 固定 legacy selector
  contract；非 Cerveny family 的 V/H 明确拒绝。所有 component 共享现有 Cartesian
  image order、surface polarity、KMAH、attenuation/reflection、C/I/S accumulation
  与 final scaling，不增加 ray-centered derivative。两频 nonreuse/reuse/parallel
  workspace 逐字节相同，reuse/parallel cache fingerprint 前后不变。
- FP-1F 使用 `/tmp/rayreuse-fp1f-final-build` 隔离 clean Release configure/build，CTest 31/31；
  standard-case unittest 152/152；仓库全量 `pytest` 167/167。RayReuse single 40 个
  支持案例通过；broadband 每模式 32 个支持案例、37 个产品通过且跨模式逐字节一致。
- F2CPP/Origin source 与既有五选项 validator 证明 `PickEpsilon` 只读取 width letter：
  `MD/MS/MZ` 全部保持 minimum-width `epsilon=(0, 1500000.0000000002)` anchor；D/S/Z
  只在 `CurvatureCorrection2` 完成 `RN` 计算后分别倍增、保留、清零。12 个 width/
  curvature option-effect guards 与 5 个 Origin/F2CPP oracle comparisons 全通过。
- M width 的 observable order 保持为 `omega=2*pi*f`，
  `HalfWidth=sqrt(2*c*RLoopMeters/omega)`，先对已舍入的 `HalfWidth` 自乘，再计算
  `epsilonOpt=(0, (0.5*omega)*(HalfWidth*HalfWidth))`，最后左乘
  `EpsMultiplier`；source gradient、launch angle 与 `Dalpha` 不参与 M 公式。F2CPP 在
  ray loop 中调用，RayReuse 保持既有的每目标频率一次计算；由于 M 对 ray 不变，两个
  shared executable cases 仍与 F2CPP 0 差异，且未做跨频缓存或公式化简。
- 新增共享 flat-gradient `MD/MZ` executable cases：F2CPP/RayReuse complex pressure 与
  TL 全部 0 差异；Origin/F2CPP 最大 pressure absolute difference
  `1.50819641e-8`，最大 TL difference `1.14440918e-5 dB`，沿用既有容差。两频 D/Z
  nonreuse/reuse/parallel SHD 分别逐字节相同；reuse/parallel 的 cache fingerprint
  before/after 相同。
- component tests 覆盖全部 `C/I/S × D/S/Z × P/V/H`：D/S/Z 保持相同中心轨迹、
  reflection topology 与 `dynamicQ`，只改变反射后 real `dynamicP` jump；逐频
  epsilon/pVB/qVB/gamma/KMAH/window 仍在现有投影/Influence 顺序精确计算，不将
  combined complex state 写回 frozen cache。
- FP-1G 使用独立 Release build，CTest 31/31；standard-case unittest 153/153；仓库
  全量 `pytest` 168/168。RayReuse single 42 个支持案例通过；broadband 每模式 34 个
  支持案例、39 个产品通过且跨模式逐字节一致。
- F width 保持 Origin/F2CPP 公式 `HalfWidth=2/((omega/c0)*Dalpha)`，再按已舍入
  HalfWidth 自乘并形成 positive-imaginary epsilon；M 继续保持 FP-1F 已冻结的
  sqrt→square 顺序；W 使用 `HUGE` half width，零梯度时 real `1e10`，否则保持
  `(-sin(alpha)/cos(alpha*alpha))*c0*c0/cz` 的 legacy 顺序。三者最后才乘
  `EpsMultiplier`。
- W 使用 real-q zero crossing 更新 KMAH；F/M 保持 complex-q branch-cut 的
  `q.real()<0` 与 imaginary crossing 语义。pVB/qVB/gamma、receiver window、Hermite
  membership 和 transverse phase 均由每频每 ray epsilon 逐次形成，不进入 frozen
  cache。F/M/W 三个 SimulationCase 的 trace cache fingerprint 完全相同。
- 新增共享 flat-gradient `FS/WS` executable cases：F2CPP/RayReuse complex pressure
  与 TL 全部 0 差异；Origin/F2CPP 最大 pressure absolute difference
  `3.35405446e-8`、最大 TL difference `3.05175781e-5 dB`，沿用既有容差。两频
  F/W nonreuse/reuse/parallel SHD 分别逐字节相同；parallel `--verify-cache` 的
  before/after 均为 `925351105865613188`。
- parser 与 solver component matrix 覆盖全部
  `C/I/S × F/M/W × D/S/Z × P/V/H`，并继续拒绝非 Cerveny family 的 option tail；
  GeoHat、GeoGaussian、Simple Gaussian、R/A/a/E 方法体未改，由 full CTest 与共享
  single/broadband regression 保护。

## 7. 审计结论

### A. 当前完整 GAP 列表

1. `TL-01`：coherent `C` 尚未覆盖 F2CPP 全部 production TL family；当前只有 Cartesian Cerveny、Cartesian GeoHat、Cartesian GeoGaussian 与 Cartesian Simple Gaussian slice parity。
2. `TL-06`：ray-centered Cerveny。
3. `TL-08`：ray-centered GeoHat TL。
4. `SRC-02` / `PRD-08`：line source 及其产品 scaling。
5. `SRC-04` / `PRD-07`：multisource / multiple source depths、产品 sequencing/header（architectural conflict）。
6. `REC-02` / `PRD-06`：Cartesian TL irregular receiver（architectural conflict）。
7. `REC-03` / `PRD-06`：paired irregular A/a/E（architectural conflict）。
8. `REC-04`：ray-centered regular/equal-range receiver 路径。
9. `SSP-02`：PCHIP SSP（architectural conflict）。
10. `SSP-03`：N2-linear SSP（architectural conflict）。
11. `SSP-04`：spline SSP（architectural conflict）。
12. `SSP-05`：Q + `.ssp` range-dependent SSP（architectural conflict）。
13. `BND-04`：canonical curvilinear `C` boundary（architectural conflict）。
14. `BND-09`：flat `A` elastic P/S 缺 RayReuse executable oracle 闭环。
15. `ATT-01`：attenuation units N/F/M/Q/L 缺 RayReuse product-level oracle 闭环。
16. `ATT-04`：Francois–Garrison attenuation。
17. `ATT-05`：biological attenuation。
18. `PRD-05`：A/a/E ray-centered `g`。

`TL-10`、`REC-05`、`BND-10` 不在 GAP 列表，因为它们是
`F2CPP_OUT_OF_SCOPE`，而不是把 RayReuse 旧 Deferred 误当作 out of scope。

### B. 按优先级分组

- **P0 — 主功能 / 后续 Influence 架构**：TL-01、TL-06、TL-08；
  SSP-02～SSP-05；SRC-04、REC-02、REC-03 所暴露的 source/receiver ownership 与
  product dimension 冲突。
- **P1 — 重要 parity gap**：SRC-02/PRD-08、REC-04、BND-04、ATT-04、ATT-05、
  PRD-05。
- **P2 — 外围或证据闭环 gap**：BND-09、ATT-01。

### C. 推荐下一步先补哪一组

只推荐一个下一阶段：**ray-centered Cerveny TL parity**。

### D. 建议把下一阶段控制在什么范围

仅同步 ray-centered Cerveny 的 parser/runtime、receiver constraint、P/V/H derivative
与 Influence/output oracle；不同时带入 ray-centered GeoHat、SSP parity、irregular
receiver、Influence Geometry Reuse 或频率插值。

### E. FP-1A～FP-1G 更新状态

FP-1B 只修改 RayReuse 的 Cartesian GeoHat TL dispatch/Influence/geometric scaling、对应
测试、共享 case allow-list 与 RayReuse 文档；FP-1A source weighting 被复用而未另建
`.sbp` 路径。Bellhop_F2CPP 与 Origin production code 均未修改。提交身份以本轮 Git
history 为准。FP-1C 只增加 RayReuse Cartesian GeoGaussian TL field sink、B family
parser/runtime dispatch、共享案例与文档；现有 AB/aB/EB traversal 未重构。Bellhop_F2CPP
与 Origin production code 均未修改。FP-1D 只增加 coherent Cartesian Simple Gaussian
TL field sink、`CS` parser/runtime dispatch、共享 direct/directional cases 与文档；没有
增加 I/S API，也没有修改 Bellhop_F2CPP 或 Origin production code。FP-1E 只增加
RayReuse Cartesian Cerveny P/V/H selector 的 parser/model/PRT 生命周期、共享 case
allow-list、component/execution regression 与文档；Cartesian Influence 数值路径未改，
Bellhop_F2CPP 与 Origin production code 未改。FP-1F 只增加 RayReuse Cartesian
Cerveny D/S/Z curvature configuration、GeometryTracer reflection dispatch、共享
flat-gradient cases 与回归；沿用既有 flat-boundary reflection production 公式，未改
Influence contribution、epsilon 实现、Bellhop_F2CPP 或 Origin production code。
FP-1G 只增加 RayReuse Cartesian Cerveny F/M/W width model、逐频逐 ray epsilon、
width-aware KMAH、两个最小共享 flat-gradient cases 与回归；未改变 frozen trajectory、
其他 beam family、Bellhop_F2CPP 或 Origin production code。
