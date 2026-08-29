# Bellhop_F2CPP → Bellhop_RayReuse Production Feature Parity Audit

审计日期：2026-08-25；FP-2B 实现验证更新：2026-08-27；FP-2C 实现验证更新：2026-08-28；FP-2D 实现验证更新：2026-08-28；FP-2E 实现验证更新：2026-08-28；FP-2F 实现验证更新：2026-08-29

原始审计基线：`39c0407d634b387a6b3c81d6fdf94c330cdd1bb2`（`feat/i8-arrivals-eigenray`）

FP-1A 实施前基线：`f7b05e4a300cbd68c27099e64f5768ed62b58265`

FP-1B 实施前基线：`bbc24fbbded037d1f6b4cd0b4825748ad2335d3c`

FP-1C 实施前基线：`9f7d64eb9076802541cce16326f0909dcde1ecc7`

FP-1D 实施前基线：`fa66c6120090038fcdaa072d4734c694b7a00374`

FP-1E 实施前基线：`7b6ff32659c070a64585ff95784294138ca8c407`

FP-1F 实施前基线：`094ffe326b2ca975b45720bbcdcf5cd4fd4fe436`

FP-1G 实施前基线：`16449e0ef1ce2f5e43b0f78001d7db51c7ca9326`

FP-1H 实施前基线：`24193aa1b60ef2b6a2d5aaba1e8d099dee615e04`

FP-1I 实施前基线：`79ca4fdb46b8fb046383605ae1b6a044456ae0b8`

FP-2A 实施前基线：`796634502a0ce004f889a07e5cf6f4a1eaa6d3ac`

FP-2B 实施前基线：`a0678e6063463e370bbfc27ebef0ca41f870be24`

FP-2C 实施前基线：`3130e60509c87334cdf4f6fd2ee0360b04db7fe`

FP-2D 实施前基线：`9efe8bebfbd6f50fb818392fe27252d8ecee7856`

FP-2E 实施前基线：`6b3428c1adc145fe9659bc6504f9eaf3b545bd7f`

FP-2F 实施前基线：`5a71221d2a10ee68b8ea7666b9c11a65e718fe7f`

本报告在原始只读审计之后，按 FP-1A～FP-2F 的实际实现与验证结果增量更新。当前
总体输入范围为二维 point source（含 multisource source depths）与 rectilinear /
Cartesian paired-irregular receiver。C-linear/PCHIP/N²-linear/cubic-spline SSP 下的
产品范围覆盖后文逐项列出的 Cartesian/ray-centered Cerveny、GeoHat、GeoGaussian、
Simple Gaussian 与 R/A/a/E 子集。Quadrilateral `Q`/`.ssp` 仍按 FP-2E oracle 独立
限界：只声明 TL Cartesian Cerveny `CC`、R、Cartesian GeoHat `G` A/a/E，以及既有
single/broadband Q profiles 上的 `nonreuse/reuse/parallel` 验证；其他 Q beam
family/option 组合即使机制上可 reach/dispatch，也不声明 parity，`Q` 与
multisource/irregular receiver 的组合同样不声明。FP-2F 按 oracle 关闭 multisource
（SRC-04/PRD-07）与 Cartesian paired irregular receiver（REC-02/REC-03/PRD-06）
的已验证 slice（见 §4 对应条目）；line source、3D 与 N×2D 仍为独立 GAP，其他
原始 GAP 不因本次更新而放宽。

## 1. 结论摘要

RayReuse 目前**尚未达到** F2CPP 二维 production feature surface 的完整 parity，
但在本报告定义的 TL beam/coordinate family 维度已经闭环。已经闭环的是：当前
受限的 Cartesian Cerveny、Cartesian/ray-centered GeoHat 与 Cartesian
GeoGaussian C/I/S TL、Cartesian Simple Gaussian coherent TL、Cartesian Cerveny
`P/V/H + {F,M,W}{D,S,Z}`、ray-centered Cerveny
`C/I/S + P/V/H + {F,M,W}{D,S,Z}`、ray-centered GeoHat `C/I/S`、单点与多源
（`NSz ≥ 1`）source depths、规则与 Cartesian paired-irregular 接收网格
（FP-2F oracle slice）、C-linear、PCHIP `P`、N²-linear `N` 与 cubic spline `S` SSP、
RR-B1 边界子集、单频 R，以及 Cartesian G/B 与 ray-centered GeoHat g 的 A/a/E；
这些已可在 RayReuse 的 `nonreuse`、`reuse`、`parallel` 路径内按其适用范围使用。
Quadrilateral `Q` 只在二维 point/single/rectilinear 下关闭 FP-2E 已验证产品 slice：
TL Cartesian Cerveny `CC`、R、Cartesian GeoHat `G` A/a/E。不得将这项 closure
外推为 Q 与其他 beam/option family 的组合 parity。FP-2F 已在 oracle-validated
范围内关闭 multisource（SRC-04/PRD-07）与 Cartesian paired irregular receiver
（REC-02/REC-03/PRD-06）；3D、N×2D 与 line source 仍是独立 GAP。

主要差距不是旧 RayReuse `Deferred` 列表，而是以下真实代码边界：

- Cartesian 与 ray-centered Cerveny 的
  `C/I/S + {F,M,W}{D,S,Z} + P/V/H`、Cartesian/ray-centered GeoHat 与 Cartesian
  GeoGaussian 的 `C/I/S` 及 Cartesian Simple Gaussian coherent production
  contribution 已接入；F2CPP production-supported 的二维 TL beam/coordinate
  family 在本报告的 point/single/rectilinear 与 C-linear/PCHIP/N²-linear/cubic-spline
  SSP slice 内已闭环。
- Origin/F2CPP 的 Cartesian Cerveny `P/V/H` 是被 parser 保存并写入 PRT、但不被
  Cartesian Influence 数值分支使用的 legacy selector；RayReuse 保持相同 observable
  contract；ray-centered Cerveny 的 V/H 则已按 F2CPP/Origin derivative 公式接入，
  没有反向改变 Cartesian identity 行为。
- TL directional `.sbp` 的旧 silent-ignore correctness gap 已关闭：source pattern
  现在在逐频 Project 前作用于每条 ray 的 source amplitude，C/I/S 共用该语义。
- `SimulationCase` 已持有按 depth `stable_sort` 的 source vector（`NSz ≥ 1`）与
  `ReceiverGridLayout{Rectilinear, Irregular}` 接收网格（FP-2F）；ray-centered
  Cerveny/GeoHat 在规则网格内仍要求至少两个等间距 ranges，ray-centered family +
  irregular 被 parser/model 拒绝，multisource × ray-centered 与
  multisource/irregular × `Q` 未建立 oracle，不声明 parity。
- geometry tracer/stepper 已通过最小 `GeometrySspEvaluator<CLinear,Pchip,N2Linear,CubicSpline,Quadrilateral>`
  解耦，frequency projection 使用独立的
  `FrequencySspEvaluator<CLinear,Pchip,N2Linear,CubicSpline,Quadrilateral>`；PCHIP 与 cubic spline
  的连续梯度进入 dynamic ray（spline 节点无 jump），N²-linear 的不连续梯度 node jump 与
  段内非零 N² Hessian 同样进入 dynamic ray，PCHIP/N²-linear/cubic spline 的非零
  `d²c/dz²` 均真实消耗于 dynamic-ray equations；quadrilateral `Q` 在 depth/range
  两个方向存在 cell 边界梯度 jump（depth 优先、corner 单次 jump），其 transient
  `rangeSegmentIndex` 只存在于 sample/step/limit 与 tracer 局部状态，不进入
  `RayState`/`RayPath`/`RayPathCache`。
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
| `R-PARSER` | `Bellhop_RayReuse/src/io/environment_parser.cpp`：TL 接受 Cartesian Cerveny `CC/IC/SC` 与 ray-centered Cerveny `CR/IR/SR` 的 `{F,M,W}{D,S,Z} + P/V/H`、Cartesian GeoHat `CG/IG/SG`（含 `^`/blank alias）、ray-centered GeoHat `Cg/Ig/Sg`、Cartesian GeoGaussian `CB/IB/SB` 与 coherent Cartesian Simple Gaussian `CS`；产品接受 Cartesian `AG/aG/EG`、`AB/aB/EB` 与 ray-centered GeoHat `Ag/ag/Eg`；SSP 接受 C-linear、PCHIP `P`、N²-linear `N`、cubic spline `S` 与 quadrilateral `Q`（同根 `.ssp` sidecar，缺失/维度不匹配显式失败），明确拒绝未知 kind；两个 ray-centered family 都要求至少两个等间距 ranges；source depth count 接受 `≥1`（multisource，F2CPP 同构上限与读取顺序）；run type 第 5 位 `I` 接受并施加 F2CPP 同构约束（ray-centered 家族与 `CS` 拒绝 `I`、`NRz != NRr` 拒绝、`R` 的第 5 位仅 ` `/`R`）；`IS/SS`、line（`X`）、FG/biological、canonical C boundary 和其他未实现组合继续明确拒绝 |
| `F-MODEL` | `Bellhop_F2CPP/include/bellhop/model/simulation_case.hpp` 与 boundary/SSP model：source vector、receiver layout、coherence、coordinate/beam families、curvilinear geometry |
| `R-MODEL` | `Bellhop_RayReuse/include/rayreuse/model/simulation_case.hpp`、`environment.hpp`、`sound_speed_evaluator.hpp` 与 `src/model/simulation_case.cpp`：按 depth `stable_sort` 的 `vector<Source>`（`sourceCount()/sources()`，每 source 独立 amplitude）与 `ReceiverGridLayout{Rectilinear, Irregular}` `ReceiverGrid`（`receiversPerRange()`/`depthAt(depthIndex, rangeIndex)`/`isIrregular()`，irregular 要求 `NRz == NRr`）；`SoundSpeedProfile` 保存 C-linear/PCHIP/N²-linear/cubic-spline/quadrilateral kind（quadrilateral grid 由 profile 以 immutable shared ownership 持有，非 Q profile 不分配 grid heap storage），geometry 与 frequency-local evaluator 均为含 C/P/N²/Spline/Quadrilateral 的 value-owned variant；显式 C/I/S coherence、Cartesian/ray-centered Cerveny coordinate、`P/V/H` field component、`F/M/W` beam width、`D/S/Z` reflection curvature 与 complex-pressure/intensity workspace 选择；仍无 line source 表示 |
| `F-TL` | `Bellhop_F2CPP/src/solver/single_frequency_solver.cpp` 及 `src/influence/`：按 coherence、beam family、coordinate、source geometry dispatch；Cartesian Cerveny constructor 不接收 component，只有 ray-centered Cerveny 接收并应用 V/H derivative |
| `R-TL` | `Bellhop_RayReuse/src/solver/single_frequency_solver.cpp`、`frequency_projector.cpp`、各 production Influence 与 `pressure_scaling.cpp`：source sampling、trace、Cartesian Cerveny local sampling 与逐频 projection 共用 C/P/N²/Spline/Quadrilateral evaluator dispatch；C 使用 complex pressure，各 production-supported family 的 I/S 使用逐频 intensity；Cartesian/ray-centered Cerveny 共用每频每 ray F/M/W epsilon；ray-centered GeoHat 保持独立 traversal；G/B/S 使用 geometric point normalization；`.sbp` 与适用的 S Lloyd factor 在逐频 Project 前形成 source amplitude；multisource 经 `traceAllSourceFans`/`solveFrequencyFromSourceCache` 以 `(sourceIndex, cache)` 配对消费（F2CPP `for sourceIndex` 外层循环同构），per-source `sourceSoundSpeed`/Lloyd/epsilon/pattern 全部取当前 source。Quadrilateral evaluator 的共享 dispatch 只说明机制可达，不构成未列入 `FP2E-ORACLE` 的 Q beam/option parity 证据 |
| `F-GEOM` | `Bellhop_F2CPP/include/bellhop/ray/geometry_tracer.hpp`、`src/ray/flat_boundary_reflection.cpp` 与 boundary geometry：通用 SSP evaluator、canonical curvilinear frame/curvature；D/S/Z 在 reflection 时对完整 `RN` 分别乘 2、保留、置零 |
| `R-GEOM` | `Bellhop_RayReuse/include/rayreuse/model/sound_speed_evaluator.hpp`、`ray/geometry_tracer.hpp`、`src/ray/ray_stepper.cpp` 与 boundary geometry：value-owned C/P/N²/Spline/Quadrilateral geometry evaluator；C-linear 与 N²-linear 保留 node gradient jump（共用同一 reduced-step 规则），PCHIP 与 cubic spline 使用连续梯度且不执行 node jump，C-linear 梯度为零、PCHIP/N²-linear/cubic spline 的非零 `d²c/dz²` 进入 dynamic ray；quadrilateral `Q` 的 depth/range cell 边界梯度 jump 与两级 grid line landing 按 F2CPP/Origin `Quad`/ReduceStep2D 语义迁移（depth 优先 corner 单次 jump、`minimumStep=1e-3×nominal` 下限、越出 `.ssp` 网格显式失败）；仅 flat/piecewise-linear boundary；D/S/Z 使用同一 frequency-independent reflection jump 公式并写入冻结 real dynamic-ray bases |
| `R-PRODUCT` | RayReuse `arrival_solver.cpp`、`eigenray_solver.cpp`、`geometric_hat_influence.cpp`、`ray_writer.cpp`、`arrival_writer.cpp`、`eigenray_writer.cpp`：R、Cartesian G/B A/a/E 与 ray-centered GeoHat g A/a/E frequency-local 产品已接入；writer headers/record layout 支持 per-source sequencing（SHD header `NSz`/`Sz` 向量与 source-major 寻址、ARR header source count + depths 与 per-source 块、E/R header `1 1 NSz` 与 per-source 段落，source depth 升序）与 irregular `PlotType`/paired cell 编址；ray-centered `g` 产品仍限于 single-source/rectilinear 范围 |
| `R-CLI` | `Bellhop_RayReuse/app/main.cpp`：R/A/a/E/TL 正式 dispatch；R 只允许单频；A/a/E/TL 支持 nonreuse/reuse/parallel；逐频 serial consumer 按 per-source workspace/hit 序列发布；PRT 输出多源 `source depths` 行、irregular `Irregular grid` marker 与冻结语义 Trace passes（`Nfreq×NSz / NSz`） |
| `MATRIX` | `Bellhop_F2CPP/doc/reference/REFERENCE_FEATURE_SUPPORT_MATRIX.md` 与 `Bellhop_RayReuse/doc/reference/REFERENCE_FEATURE_SUPPORT_MATRIX.md`；RayReuse 矩阵已同步 FP-1A～FP-2F 实际支持面与剩余限制 |
| `STD` | `test/standard_cases/coverage.toml`、各 case `case.toml`、`codes/standard_cases.py`：共享 adapter/oracle 与版本 allow-list |
| `TEST` | FP-2F 隔离 Release clean build（`build/fp2f-clean`）CTest 40/40；仓库全量 Python/pytest 178 passed（含 subtests）；standard-case unittest 163/163；`multi_source_depths`/`irregular_receiver_pairs`/`arrival_multi_source(_binary)`/`eigenray_irregular_pairs`/`ray_trace_vacuum_rigid`/`eigenray_geometric_hat` 等 multisource/irregular case single 与 broadband 三模式通过；既有 FP-2A full product matrix 继续由相关回归保护 |
| `FP1A-ORACLE` | 共享 `constant_speed_direct`、`incoherent_direct`、`semicoherent_direct`、directional/omni `.sbp` 输入：F2CPP 与 RayReuse pressure/TL 全部 0 差异；四个 FP-1A broadband case 的 nonreuse/reuse/parallel SHD 逐字节一致；directional 与 omni 最大 pressure 差 `2.1412473171949387e-2` |
| `FP1B-ORACLE` | 共享 `geometric_hat_cartesian`、safe control、`geometric_hat_incoherent`、`geometric_hat_semicoherent`、`geometric_hat_directional`：F2CPP 与 RayReuse pressure/TL 全部 0 差异；Origin/F2CPP 最大 pressure absolute `4.16500123e-9`、最大 TL 差 `7.62939453e-6 dB`；GeoHat C/I/S 两频 SHD 三模式逐字节一致 |
| `FP1C-ORACLE` | 共享 `geometric_gaussian_cartesian`、`geometric_gaussian_incoherent`、`geometric_gaussian_semicoherent`、`geometric_gaussian_directional`：F2CPP 与 RayReuse pressure/TL 全部 0 差异；Origin/F2CPP 最大 pressure absolute `1.3038516e-8`、最大 TL 差 `2.28881836e-5 dB`；GeoGaussian C/I/S 两频 SHD 三模式逐字节一致 |
| `FP1D-ORACLE` | 共享 `simple_gaussian_cartesian` 与最小 directional case：F2CPP 与 RayReuse pressure/TL 全部 0 差异；Origin/F2CPP 最大 pressure absolute `6.71586253e-9`、最大 TL 差 `2.28881836e-5 dB`；directional 与 omni 最大 pressure 差 `2.50503421e-2`；Simple Gaussian 两频 SHD 三模式逐字节一致 |
| `FP1E-ORACLE` | 共享 `cartesian_component_pressure/vertical/horizontal`：Origin 与 F2CPP 各自 P/V/H SHD 逐字节相同；F2CPP/RayReuse 三组件 pressure/TL 全部 0 差异；Origin/F2CPP 最大 pressure absolute `1.49468509e-8`、最大 TL 差 `7.62939453e-6 dB`；两频 P/V/H 的 nonreuse/reuse/parallel 9 个 SHD 共用 SHA-256 `0d534d63df8e7d13c9b11f60cb4e7d0d12c0bfeb1d96d08482f7cbf4ddec82e2` |
| `FP1F-ORACLE` | 共享 flat-gradient `MD/MZ`：F2CPP/RayReuse pressure/TL 全部 0 差异；Origin/F2CPP 最大 pressure absolute `1.50819641e-8`、最大 TL 差 `1.14440918e-5 dB`；既有五选项 validator 的 `MD/MS/MZ` effect guards 与 Origin/F2CPP comparison 全通过；两频 D/Z 的 nonreuse/reuse/parallel SHD 分别逐字节一致，reuse/parallel cache fingerprint 前后相同 |
| `FP1G-ORACLE` | 共享 flat-gradient `FS/WS`：F2CPP/RayReuse pressure/TL 全部 0 差异；Origin/F2CPP 最大 pressure absolute `3.35405446e-8`、最大 TL 差 `3.05175781e-5 dB`；既有五选项 validator 的 F/M/W epsilon anchors、12 个 option-effect guards 与 5 个 Origin/F2CPP comparisons 全通过；两频 F/W 的 nonreuse/reuse/parallel SHD 分别逐字节一致，parallel cache fingerprint 前后相同且 F/W cache fingerprint 相同 |
| `FP1H-ORACLE` | 共享 `cartesian_component_pressure` 与 `ray_centered_component_pressure/vertical/horizontal`：F2CPP/RayReuse 四个 pressure/TL comparison 全部 0 差异，两边的 four-case aggregate SHA-256 同为 `861f8fc8ade098fcd11d5a985bd61d034369bc64710147648d85f62fe5814a9e`；Origin/F2CPP 最大 pressure absolute `4.48584387e-8`、最大 TL 差 `6.10351563e-5 dB`，沿用既有 tolerance；ray-centered 两频 nonreuse/reuse/parallel SHD 逐字节一致，reuse/parallel cache fingerprint 前后均为 `10925417565703232468` |
| `FP1I-ORACLE` | 共享 `geometric_hat_cartesian` 与 `geometric_hat_ray_centered`：F2CPP/RayReuse 的 G/g pressure/TL 全部 0 差异；Origin/RayReuse 最大 pressure absolute `9.38570333e-10`、最大 TL 差 `0 dB`，沿用既有 tolerance；ray-centered GeoHat 两频 nonreuse/reuse/parallel SHD 共用 SHA-256 `fef67deae6f74627d385782a2464adb456d8aec75f0be411dab1e4836a71c6eb`，reuse/parallel cache fingerprint 前后均为 `11321016705018875701`；component/solver tests 覆盖 C/I/S、`.sbp`、same-index/reverse traversal、linear hat、attenuation 与两级 q caustic |
| `FP2A-ORACLE` | 共享 `arrival_geometric_hat_ray_centered`、最小 binary companion 与 `eigenray_geometric_hat_ray_centered`：Ag/ag 各 352 条 records，Origin/F2CPP/RayReuse 所有 Arrival 字段均 0 ULP，A/a encoding 语义 0 ULP；Eg 466 个 blocks，F2CPP/RayReuse 与 Origin/RayReuse 坐标最大差均为 0 m；两频 Ag/ag/Eg 的 nonreuse/reuse/parallel 对应产品 SHA-256 一致，cache fingerprint before/after 均为 `5762074209948553069` |
| `FP2B-ORACLE` | 共享 `munk_pchip`：PCHIP geometry/frequency-local projection oracle 通过，F2CPP/RayReuse geometry probe、50 Hz TL SHD 与代表性 R 产品逐字节一致；SSP=`P` 在当前合法的 ray-centered `Ag/ag/Eg` 路径中闭环，A ASCII ARR、a binary ARR 与 E RAY 均逐字节一致；Origin intermediate-state 370 points，worst absolute error `5.82e-11`；两频 nonreuse/reuse/parallel SHD 逐字节一致，reuse/parallel cache fingerprint 前后不变；C-linear probe 与基线 SHA-256 `29c483a2f843ee1f48267b41c21df45a247b774a67fb98919b66e2539b50bd0b` 一致；N/S/Q 仍 deferred/unsupported |
| `FP2C-ORACLE` | 共享 `munk_n2`：N² geometry/frequency-local projection oracle 通过，F2CPP/RayReuse munk-n2 geometry probe CSV 逐字节一致（SHA-256 `360dda437550e396b531ed9a4692a006ebe8e5e29ddcaddde40fe8ddbcc00be8`；233 points/232 steps/0 events @0.0125 rad）；SSP=`N` 在当前合法产品范围闭环，两频 TL SHD 与 R/A/a/E 产品 F2CPP=RayReuse 逐字节一致（SHD `1dcec8713169f7c7862b1649c536b56ea14b5940f348625c52f14a213b583ba8`、R `81dd81ab9ebf7565ee48336f93c9ed37b6c5cae1a372a8daecdd512467844815`、A `1322fda04a950b14b9fabf93fd9af7f08d3936f198a26a6cd473688eee202406`、a `41ddac86242c04683ac5ecf5af3c6912da73e15dd3164ec2f0835e5d3f854b51`、E `78b3ba5e2b4805a457591860bc89ad631c8531783aa6be85390bb5fb380a77b6`）；ARR 552,440 arrivals/100,701 cells/0 nonfinite；Origin intermediate-state matrix 366 points/363 steps/2 events，worst error q2@158 abs `1.81e-14`/scaled `1.21e-3`（既有预算 `3e-9`，未放宽）；两频 nonreuse/reuse/parallel SHD 逐字节一致（SHA-256 `18817c6788b6e7a4c0c7cbd73cb5b8de78c4c92ea90e06a059badf80c27d29c4`），PRT Trace passes nonreuse=2、reuse=1、parallel=1；C/P probe 与 C/P broadband SHD 基线 SHA 均不变；N 结果区别于 C/P（point_index=2 起分歧：N vs C p1 `+1.34e-6`、N vs P p1 `-2.22e-4`）；S/Q 仍 deferred/unsupported |
| `FP2D-ORACLE` | 共享 `munk_spline`：cubic-spline geometry/frequency-local projection oracle 通过，F2CPP/RayReuse munk-spline geometry probe CSV 逐字节一致（SHA-256 `1fd0e4f84391aa24ec5e9876fae5d582b2ffdeb30422533113b407eba7faad63`；237 points/236 steps/0 events @0.0125 rad）；SSP=`S` 在当前合法产品范围闭环，两频 TL SHD 与 R/A/a/E 产品 F2CPP=RayReuse 逐字节一致（SHD `ce216646d078190320420c339248ee7062279f50792d9df169be184f7bd4a36d`、R `ddd94952eec067628d5ad771f2d9b7c53167eb542cddb1c8dee19d2b4a2df04b`、A `30042a8403c1bba1e426333c3243d4e3203a8cd29391a3ce311df14726584ac0`（100,701 cells/536,601 arrivals/max 19 per cell）、a `837e1e4f10592ec31b08be8165e25930370440fe28600e667debdb9d427a013c`、E `c921296bc51a8583ef88898efef7076114a0ec3f022904cc5310171e42376202`，884,091 records/236,420,945 points）；R 为 5000 rays/1,689,310 points、top/bottom bounces 3778/3577；Origin intermediate-state matrix 370 points/367 steps/2 top reflections，worst error h_m@369 abs `1.45e-11`/scaled `5.54e-4`（既有 tolerance，未放宽）；两频 nonreuse/reuse/parallel SHD 逐字节一致（SHA-256 `74028065178ff80d43755ef2ba70ba5ba3e4947574a37a4154a7ecc52eef1596`），PRT Trace passes nonreuse=2、reuse=1、parallel=1，reuse/parallel cache fingerprint before/after 均为 `1526667602348633172`；C/P/N probe 与 C/P/N broadband SHD 基线 SHA 均不变；S 结果区别于 C/P/N（point_index=2 起 |Δp1| S vs C/P/N 为 `2.24e-4`/`2.22e-4`/`2.12e-9`，终点深度差 12.4/13.1/18.9 m，probe 点数 237 vs 234/234/233）；`Q`/`.ssp` 仍 deferred/unsupported |
| `FP2E-ORACLE` | 共享 `q_range_dependent_cross_gradient` 与 `q_range_independent_control`：F2CPP/RayReuse `i5-quadrilateral` geometry probe CSV 逐字节一致（SHA-256 `4e22fd057eeca5dcabca171aeeb9129fba09e7616c0d8fdb5621f26c6029d32f`；715 points/714 steps/0 events @`-0.002626749710359303` rad）；Origin intermediate-state oracle（`generate_i5_q_oracle.py` ALPHA_INDEX=150）PASS，worst scaled `1.06e-9`（t_z@131）；Origin final-field 走既有 `validate_i5_quadrilateral_ssp.py` policy：12 个 field comparison（6 Origin→F2CPP + 6 Origin→RayReuse，两 case × single/broadband × 1000/2000 Hz）全部 PASS，range-dependent worst TL `1.53e-05 dB`（tolerance `0.001 dB`）、control worst `0.0113 dB`（既有 `tolerances_i5_q_control.toml` `0.02 dB`），tolerance 未动；SSP=`Q` 产品在两频 1000/2000 Hz 下 TL（`CC` SHD）/R/A/a/E 共 8 对文件 F2CPP=RayReuse byte-identical；两频 nonreuse/reuse/parallel SHD 逐字节一致（SHA-256 `b53c02cba0a1372ac13123937643106579ddaed5bb77db7515d2440cc263ed2f`），PRT Trace passes 2/1/1，reuse/parallel cache fingerprint before/after 均为 `2879552213476552188`；C/P/N/S probe 四 SHA 与 C/P/N/S broadband SHD 冻结值、`munk_spline` fingerprint `1526667602348633172` 均不变 |
| `FP2F-ORACLE` | 共享 multisource/irregular 八 case（validator 二进制 = `build/fp2f-clean/bellhop_rayreuse`）：`multi_source_depths`（TL `CC` NSz=3）origin↔rayreuse max \|Δp\| `2.049e-08`、max TL diff `3.81e-05 dB`（与 origin↔f2cpp 同 metric 同 tolerance，未放宽），f2cpp↔rayreuse decoded payload exact，source depths 向量 (20,50,80) 与 SHD dims `[1,1,1,1,3,11,51]` exact；`irregular_receiver_pairs`（`CC RI` paired）f2cpp↔rayreuse payload exact、origin↔rayreuse max \|Δp\| `3.79e-09`、max TL diff `3.81e-06 dB`、irregular header axes/record shape exact；`ray_trace_vacuum_rigid`（R 双源）f2cpp↔rayreuse 与 origin↔rayreuse max coordinate error 均 `0.0 m`；`arrival_multi_source`/`arrival_multi_source_binary`（A ASCII/a binary 双源，各 162 records）与 `arrival_geometric_gaussian_irregular`（A paired，335 records）origin↔rayreuse 与 f2cpp↔rayreuse 全字段 0 ULP；`eigenray_irregular_pairs`（E paired）max coordinate error `0.0 m`、双源 header count guard 通过；六个 broadband case（`multi_source_depths`/`irregular_receiver_pairs`/`arrival_multi_source`/`arrival_multi_source_binary`/`eigenray_irregular_pairs`/`eigenray_geometric_hat`）`nonreuse/reuse/parallel` 每频产品逐字节一致，trace passes 两频双源 `4/2/2`、两频三源 `6/3/3`、两频单源 `2/1/1`，reuse/parallel per-source cache fingerprint before==after；R 保持单频（双源 `ray_trace_vacuum_rigid` 通过，reuse/parallel 对 R 显式拒绝）；C/P/N/S probe SHA 与 broadband SHD 基线、`munk_spline` fingerprint `1526667602348633172`、Q fingerprint `2879552213476552188` 全部不变 |

## 4. Production feature parity 表

### 4.1 TL / Influence

| Feature | F2CPP | RayReuse | Status | Evidence |
| --- | --- | --- | --- | --- |
| TL-01 — coherence `C` 跨 F2CPP production TL beam/coordinate family | 全部正式 TL family 按其约束支持 | Cartesian/ray-centered Cerveny、Cartesian/ray-centered GeoHat、Cartesian GeoGaussian 与 Cartesian Simple Gaussian 在 point/single/rectilinear 与 C-linear/PCHIP/N²-linear/cubic-spline SSP slice 内闭环；quadrilateral `Q` 的 TL oracle 覆盖 Cartesian Cerveny `CC` slice | `PARITY` | `F-PARSER`, `R-PARSER`, `F-TL`, `R-TL`, `TEST`, `FP2B-ORACLE`, `FP2C-ORACLE`, `FP2D-ORACLE`, `FP2E-ORACLE`；line source 仍由 SRC-02 跟踪为 `GAP`；multisource 与 Cartesian irregular receiver 的关闭范围见 SRC-04/REC-02/REC-03 |
| TL-02 — Cartesian Cerveny，C/I/S、`{F,M,W}{D,S,Z}` beam options、P/V/H component、point/single/rectilinear/C/P/N²/Spline SSP | 支持 | 支持 | `PARITY` | `F-TL`, `R-TL`, `R-GEOM`, `STD`, `TEST`, `FP1A-ORACLE`, `FP1E-ORACLE`, `FP1F-ORACLE`, `FP1G-ORACLE`, `FP2B-ORACLE`, `FP2C-ORACLE`, `FP2D-ORACLE`；C 原路径保持，I/S 使用独立逐频 intensity workspace，P/V/H 保持 legacy identity。此行不包含 Q；Q 的 TL parity 仅为 TL-01/SSP-05 所列 Cartesian Cerveny `CC` slice |
| TL-03 — incoherent `I`（当前 Cerveny coordinate 与 production GeoHat/GeoGaussian slice） | 支持 | parser/runtime/Influence/output/oracle 闭环 | `PARITY` | Cartesian Cerveny 为 image coherent sum 后 ABS²；ray-centered Cerveny 为每个 image 独立 `Hermite × ABS²(contribution)`；Cartesian/ray-centered GeoHat 与 GeoGaussian 各保持自身 linear weight law；均在总 intensity 后 sqrt；`FP1A-ORACLE`, `FP1B-ORACLE`, `FP1C-ORACLE`, `FP1H-ORACLE`, `FP1I-ORACLE` |
| TL-04 — semicoherent `S`（当前 Cerveny coordinate 与 production GeoHat/GeoGaussian slice） | 支持 | parser/runtime/Influence/output/oracle 闭环 | `PARITY` | 各 family 与其 I 模式共用 contribution；S 的差异仅为 Project 前逐频 Lloyd/source amplitude；`F-TL`, `R-TL`, `FP1A-ORACLE`, `FP1B-ORACLE`, `FP1C-ORACLE`, `FP1H-ORACLE`, `FP1I-ORACLE` |
| TL-05 — Cartesian Cerveny production beam options | 支持 F/M/W width × D/S/Z curvature | F/M/W × D/S/Z 全部 parser/runtime/epsilon/KMAH/SHD/oracle 闭环 | `PARITY` | `F-PARSER`, `R-PARSER`, `F-GEOM`, `R-GEOM`, `R-TL`, `FP1F-ORACLE`, `FP1G-ORACLE`；F/M 使用 positive-imaginary epsilon 与 complex-q branch，W 使用 real epsilon 与 real-q crossing |
| TL-06 — ray-centered Cerveny，C/I/S、`{F,M,W}{D,S,Z}`、P/V/H、point/single/rectilinear/C/P/N²/Spline SSP | 支持；要求至少两个等间距 receiver ranges | parser/model/runtime/Influence/SHD/oracle 闭环；保持 F2CPP projection/traversal、persistent image-normal flip、P/V/H derivative、逐 image I/S 与 receiver-level KMAH | `PARITY` | `F-PARSER`, `R-PARSER`, `F-TL`, `R-TL`, `R-MODEL`, `STD`, `TEST`, `FP1H-ORACLE`, `FP2B-ORACLE`, `FP2C-ORACLE`, `FP2D-ORACLE`；不外推到 irregular receiver；`Q` 的 TL oracle 未覆盖本 family |
| TL-07 — Cartesian GeoHat，C/I/S、point/single/rectilinear/C/P/N²/Spline SSP | 支持 | parser/runtime/Influence/geometric scaling/SHD/oracle 闭环 | `PARITY` | `R-PARSER`, `R-TL`, `STD`, `TEST`, `FP1B-ORACLE`, `FP2B-ORACLE`, `FP2C-ORACLE`, `FP2D-ORACLE`；不外推到 line source；multisource 与 Cartesian paired irregular 的适用范围见 SRC-04/REC-02；`Q` 的 TL oracle 未覆盖本 family（`Q` 的 Cartesian GeoHat 证据在 A/a/E 产品，见 PRD-05） |
| TL-08 — ray-centered GeoHat C/I/S、point/single/rectilinear/C/P/N²/Spline SSP | 支持；至少两个等间距 receiver ranges | parser/model/runtime/Influence/geometric scaling/SHD/oracle 闭环；保持 F2CPP depth projection、range-index walker、same-index skip、right-endpoint acoustic state、linear hat 与 q-caustic order | `PARITY` | `F-PARSER`, `R-PARSER`, `F-TL`, `R-TL`, `R-MODEL`, `STD`, `TEST`, `FP1I-ORACLE`, `FP2B-ORACLE`, `FP2C-ORACLE`, `FP2D-ORACLE`；没有 Cerveny image loop/persistent flip/KMAH/window；相同 geometry primitive 的产品闭环由 PRD-05 跟踪 |
| TL-09 — Cartesian GeoGaussian，C/I/S、point/single/rectilinear/C/P/N²/Spline SSP | 支持 | parser/runtime/逐频 width 与 membership/Influence/geometric scaling/SHD/oracle 闭环 | `PARITY` | `R-PARSER`, `R-TL`, `STD`, `TEST`, `FP1C-ORACLE`, `FP2B-ORACLE`, `FP2C-ORACLE`, `FP2D-ORACLE`；`sigma_nf`、`sigma_lambda`、`sigma_1`、membership 与 Gaussian kernel 全部逐频计算 |
| TL-10 — ray-centered GeoGaussian | F2CPP 未正式支持 | 未支持 | `F2CPP_OUT_OF_SCOPE` | `MATRIX`, F2CPP parser/solver 均不提供该组合 |
| TL-11 — Cartesian Simple Gaussian coherent TL，point/single/rectilinear/C/P/N²/Spline SSP | 支持；不提供 I/S accumulator | parser/runtime/Influence/geometric scaling/SHD/oracle 闭环；`IS/SS` 明确拒绝 | `PARITY` | `R-PARSER`, `R-TL`, `STD`, `TEST`, `FP1D-ORACLE`, `FP2B-ORACLE`, `FP2C-ORACLE`, `FP2D-ORACLE`；保留 `0.98F` 混合精度、legacy SINT、严格 q crossing 与 right-endpoint acoustic state |
| TL-12 — directional `.sbp` 对当前 Cartesian/ray-centered Cerveny、Cartesian/ray-centered GeoHat、GeoGaussian C/I/S 及 Simple Gaussian C TL 生效 | 支持且有 shared Origin/F2CPP case | 支持；所有已支持 TL family 共用逐 ray、逐频、Project 前 source pattern 路径 | `PARITY` | `F-TL`, `R-PARSER`, `R-TL`, `STD`, `FP1A-ORACLE`, `FP1B-ORACLE`, `FP1C-ORACLE`, `FP1D-ORACLE`, `FP1I-ORACLE`；ray-centered GeoHat targeted solver test 覆盖 C/I/S，FP-2A 复用同一逐频 projector 而未建立产品专用 source path |
| TL-13 — Cartesian Cerveny 1～3 images 与 beam window | 支持 | 在 TL-02 子集内支持 | `PARITY` | 两边 parser 均校验 image count ≤ 3 并传入 Cartesian Cerveny Influence；component/shared TL regression 闭环 |
| TL-14 — Cartesian Cerveny `P/V/H` legacy component selector | parser/model/PRT 支持三者；Cartesian Influence 不读取 selector，故三者数值相同 | parser/model/PRT 支持三者；保持相同 component-independent Cartesian contribution；非 Cerveny V/H 明确拒绝 | `PARITY` | `F-PARSER`, `F-TL`, `R-PARSER`, `R-MODEL`, `R-TL`, `STD`, `TEST`, `FP1E-ORACLE`；不外推到 ray-centered V/H derivative |

### 4.2 Source

| Feature | F2CPP | RayReuse | Status | Evidence |
| --- | --- | --- | --- | --- |
| SRC-01 — point source、single source depth | 支持 | 支持 | `PARITY` | `F-PARSER`, `R-PARSER`, `R-MODEL`, `STD`, `TEST` |
| SRC-02 — line source | 支持 | parser 显式拒绝；无 line-source scaling dispatch | `GAP` | `F-PARSER`, `F-TL`, `R-PARSER`, `R-TL` |
| SRC-03 — directional `.sbp`：TL/R/A/a/E | 支持 | 在各自已支持的 product slice 内支持 | `PARITY` | TL 见 TL-12；directional R shared oracle；Arrival component regression 验证逐频 amplitude 改变；Eigenray 产品不跨频共享 writer |
| SRC-04 — multisource / multiple source depths | 支持，source vector 贯穿 solver 与 writers | parser/model 接受 `NSz ≥ 1` point-source source depths（depth 升序 `stable_sort`）；solver 每 source 独立 frozen `RayPathCache`（共享 launch fan，cache schema 与 fingerprint 算法零改动），逐频产品按 `(frequency, source)` 序列生成；writer per-source sequencing/header 与 F2CPP/Origin 一致；三执行模式一致 | `PARITY` | `F-MODEL`, `R-MODEL`, `R-PARSER`, `R-PRODUCT`, `R-CLI`, `STD`, `TEST`, `FP2F-ORACLE`；关闭范围 = 已支持 TL beam family、R（单频）、A/a/E 的 multisource 组合；multisource × `Q`、multisource × ray-centered family 未 oracle 验证，不声明 parity；line source 仍为 SRC-02 `GAP` |

### 4.3 Receiver

| Feature | F2CPP | RayReuse | Status | Evidence |
| --- | --- | --- | --- | --- |
| REC-01 — rectilinear receiver | 支持 | 在当前 TL/R/A/a/E 子集内支持 | `PARITY` | `F-MODEL`, `R-MODEL`, `STD`, `TEST` |
| REC-02 — Cartesian TL irregular receiver | 支持 legacy irregular layout | run type 第 5 位 `I`、paired `NRz == NRr` 的 irregular layout 已接入：Cartesian Cerveny `CC/IC/SC`、Cartesian GeoHat `CG/IG/SG`、Cartesian GeoGaussian `CB/IB/SB` TL；SHD `PlotType` 写 `irregular ` | `PARITY` | `F-PARSER`, `F-MODEL`, `R-PARSER`, `R-MODEL`, `R-PRODUCT`, `STD`, `TEST`, `FP2F-ORACLE`；Cartesian Cerveny 在 irregular 下按 Origin/F2CPP legacy 语义恒取首深度 `Rz(1)`（非 paired `Rz(ir)`），RayReuse 逐字节同构；GeoHat/GeoGaussian Cartesian 按 paired `depthAt(depthIndex, rangeIndex)`；ray-centered 家族与 `CS` 拒绝 irregular；irregular × `Q` 不声明 parity |
| REC-03 — paired irregular A/a/E | 支持 paired receiver identity/sequence | Cartesian `G/B` 的 A/a/E traversal 与 writers 按 `receiversPerRange() × rangeCount` paired cell 编址 | `PARITY` | `R-MODEL`, `R-PRODUCT`, `STD`, `TEST`, `FP2F-ORACLE`（`arrival_geometric_gaussian_irregular`、`eigenray_irregular_pairs`）；不外推到 ray-centered family（irregular 被拒绝） |
| REC-04 — ray-centered receiver 约束（regular/equal-range） | 在 ray-centered family 下支持并校验 | 在 TL-06 Cerveny、TL-08 GeoHat 与 PRD-05 Ag/ag/Eg slice 内支持并校验至少两个等间距 ranges；depths 保持严格递增规则轴 | `PARITY` | `F-PARSER`, `F-MODEL`, `F-TL`, `R-PARSER`, `R-MODEL`, `R-TL`, `R-PRODUCT`, `FP1H-ORACLE`, `FP1I-ORACLE`, `FP2A-ORACLE` |
| REC-05 — ray-centered irregular receiver | F2CPP 未正式支持 | 未支持 | `F2CPP_OUT_OF_SCOPE` | `MATRIX` |

### 4.4 SSP

| Feature | F2CPP | RayReuse | Status | Evidence |
| --- | --- | --- | --- | --- |
| SSP-01 — C-linear | 支持 | 支持 | `PARITY` | `F-PARSER`, `R-PARSER`, `R-TL`, `R-GEOM`, `STD`, `TEST`, `FP2B-ORACLE` |
| SSP-02 — PCHIP `P` | 支持 | parser/model、real geometry、dynamic ray、frequency-local projection、TL/R/A/a/E 与三执行模式闭环 | `PARITY` | `F-PARSER`, `F-GEOM`, `R-PARSER`, `R-MODEL`, `R-GEOM`, `R-TL`, `STD`, `TEST`, `FP2B-ORACLE` |
| SSP-03 — N2-linear | 支持 | parser/model、real geometry（`c=1/sqrt(N²)` 逐段线性 N²、节点梯度不连续并共用 C-linear node jump 规则、段内非零 N² Hessian 进入 dynamic ray）、frequency-local complex N² projection、TL/R/A/a/E 与三执行模式闭环 | `PARITY` | `F-PARSER`, `F-GEOM`, `R-PARSER`, `R-MODEL`, `R-GEOM`, `R-TL`, `STD`, `TEST`, `FP2C-ORACLE` |
| SSP-04 — spline `S` | 支持 | parser/model、exact not-a-knot coefficient kernel（含 2/3/4+ node 分支、binary32 `1.0F/6.0F`）、real geometry（value/一阶/二阶导数、节点连续梯度且无 node jump、非零 Hessian 进入 dynamic ray、edge cubic extrapolation）、frequency-local complex spline projection（节点 attenuation 先转换、再每频独立构造复系数）、TL/R/A/a/E 与三执行模式闭环 | `PARITY` | `F-PARSER`, `F-GEOM`, `R-PARSER`, `R-MODEL`, `R-GEOM`, `R-TL`, `STD`, `TEST`, `FP2D-ORACLE` |
| SSP-05 — Q + `.ssp` range-dependent tabulation | 支持 | parser/model、二维 quadrilateral grid（`.ssp` reader 严格验证 range/depth 维度与数值，immutable shared ownership，非 Q profile 不分配 grid storage）、real geometry（cell 内 bilinear 与 `cr/cz/crz`、`crr=czz=0`、density depth 插值；depth/range cell 边界 gradient jump、depth 优先 corner 单次 jump、depth/range grid line landing、`minimumStep=1e-3×nominal` 下限、越出 `.ssp` 网格显式失败）、frequency-local projection（real `c(r,z)` 只在 trace 阶段决定轨迹；imaginary attenuation 由 `.env` reference depth profile 逐频转换后仅沿 depth 插值）。产品 parity 严格限于 TL Cartesian Cerveny `CC`、单频 R、Cartesian GeoHat `G` A/a/E；TL/A/a/E 已验证 `nonreuse/reuse/parallel`，R 保持单频产品语义；transient `rangeSegmentIndex` 不进入 frozen path/cache | `PARITY` | `F-PARSER`, `F-MODEL`, `F-GEOM`, `R-PARSER`, `R-GEOM`, `R-MODEL`, `STD`, `TEST`, `FP2E-ORACLE`；其他 Q beam/option 组合仅机制可达、未独立 oracle 验证，不声明 parity；不外推到 3D/N×2D/line/multisource/irregular |

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
| PRD-01 — R：generalized R、directional `.sbp`、active/terminal prefix、explicit Nalpha=1、Origin-compatible writer | 支持 | 在 point/multisource、rectilinear、单频范围支持；多频明确拒绝，irregular receiver 不适用（`R...I` 拒绝） | `PARITY` | `R-PARSER`, `R-PRODUCT`, `R-CLI`, shared `ray_trace_directional_tabulated`, writer/component tests, `FP2F-ORACLE` |
| PRD-02 — A ASCII：Cartesian G/B | 支持 | 在 single/multisource 与 rectilinear/paired-irregular 范围支持；多频逐频独立发布 | `PARITY` | shared hat ASCII/zero、Arrival component/writer tests、`TEST`、`FP2F-ORACLE` |
| PRD-03 — a Binary：Cartesian G/B | 支持 | 在 single/multisource 与 rectilinear/paired-irregular 范围支持；多频逐频独立发布 | `PARITY` | shared hat Binary/zero、Arrival component/writer tests、`TEST`、`FP2F-ORACLE` |
| PRD-04 — E Eigenray：Cartesian G/B | 支持 | 在 single/multisource 与 rectilinear/paired-irregular 范围支持；多频逐频独立发布 | `PARITY` | shared Gaussian/zero、Eigenray component/writer tests、`TEST`、`FP2F-ORACLE` |
| PRD-05 — A/a/E ray-centered `g` | 支持；要求至少两个等间距 receiver ranges | parser/model/runtime/Arrival/Eigenray/writer/oracle 闭环；多频逐频独立发布，沿用既有命名与 cleanup lifecycle | `PARITY` | `F-PARSER`, `R-PARSER`, `R-MODEL`, `R-PRODUCT`, `R-CLI`, `STD`, `TEST`, `FP2A-ORACLE`, `FP2B-ORACLE`, `FP2D-ORACLE`；不外推到 irregular/line/multisource；`Q` 的 A/a/E oracle 覆盖 Cartesian `G`（见 SSP-05），未覆盖本 family |
| PRD-06 — TL/A/a/E irregular receiver product semantics | 支持到 REC-02/REC-03 所述范围；R 本身只使用单 receiver range | SHD `PlotType='irregular '`、paired record 布局与 `receiversPerRange()` workspace 维度、A/a/E paired traversal 已接入；范围同 REC-02/REC-03 | `PARITY` | `F-MODEL`, `R-MODEL`, `R-PRODUCT`, `R-CLI`, `STD`, `TEST`, `FP2F-ORACLE`；CC/IC/SC 的 `Rz(1)` legacy 语义见 REC-02；irregular × `Q` 不声明 parity |
| PRD-07 — R/A/a/E multisource sequencing/headers | 支持 | SHD header `NSz`+`Sz` 向量与 source-major 寻址；ARR header source count + depths 与 per-source 块（ASCII/binary）；E/R header `1 1 NSz` 与 per-source 段落（source depth 升序）；solver lifecycle per-source（reuse 复用单位 = `(source, frozen fan)`） | `PARITY` | `F-MODEL`, `R-MODEL`, `R-PRODUCT`, `R-CLI`, `STD`, `TEST`, `FP2F-ORACLE`；R 保持单频产品（多频 R 仍拒绝）；`NSz==1` 且 rectilinear 时输出 byte-identical 冻结回归 |
| PRD-08 — line-source product scaling | 支持 | parser/runtime 不支持 line source | `GAP` | SRC-02 的全链路证据 |

### 4.8 Support matrix / 声明一致性

| Feature | F2CPP | RayReuse | Status | Evidence |
| --- | --- | --- | --- | --- |
| DOC-01 — production support matrix 与真实 executable surface 同步 | 当前矩阵与 parser/solver/tests 基本一致 | FP-1A～FP-2A 已支持面，以及 FP-2B C-linear/PCHIP、FP-2C N²-linear、FP-2D cubic-spline 已同步；FP-2E 只同步 quadrilateral `Q`/`.ssp` 的 evaluator/geometry 与 oracle-supported 产品 slice（TL Cartesian Cerveny `CC`、R、Cartesian GeoHat `G` A/a/E）；FP-2F 同步 multisource（SRC-04/PRD-07）与 Cartesian paired irregular（REC-02/REC-03/PRD-06）的已验证 slice，不把机制可达组合（multisource × Q/ray-centered、irregular × Q）写成 parity；其他限制保留 | `PARITY` | `MATRIX`、`TEST`、`FP2B-ORACLE`、`FP2C-ORACLE`、`FP2D-ORACLE`、`FP2E-ORACLE`、`FP2F-ORACLE` 与本报告逐项代码证据对照 |

## 5. RayReuse 当前 execution 范围

以下只记录当前已证明范围，不把执行模式通过外推到尚未支持的 feature：

| Execution | 当前 production-supported 范围 | 当前明确边界 | 验证事实 |
| --- | --- | --- | --- |
| `nonreuse` | TL-02/03/04/05/06/07/09/11/12/14（含 {F,M,W}{D,S,Z}）；PRD-02/03/04/05；C-linear/PCHIP/N²-linear/cubic-spline SSP；Q 仅限 TL Cartesian Cerveny `CC` 与 Cartesian GeoHat `G` A/a/E 的 FP-2E validated slice；FP-2F multisource/irregular validated slice（SRC-04/REC-02/REC-03）；当前 BND/ATT parity 子集 | R 不接受显式 execution mode；Q 的其他 beam/option 组合、multisource × Q/ray-centered、irregular × Q 及 line source 保持不声明 parity | shared broadband 支持案例通过；N²-linear/cubic-spline 每频独立 trace + solve/product；Q validated slice 每频独立 trace + solve/product；FP-2F validated slice 每频每源独立 trace + solve/product（nonreuse trace passes = Nfreq×NSz） |
| `reuse` | 与 nonreuse 相同；一次 trace 全部 source fans（每 source 一个 frozen cache）→ 跨频复用 → per-(frequency, source) acoustic/product state | 不为 gap feature、未验证 Q 组合或 multisource × ray-centered 提供隐式 parity；R 拒绝 | validated 产品与 nonreuse 逐字节相同；D/Z/F/W/ray-centered 既有案例与 cubic-spline `munk_spline` 的 cache fingerprint 前后相同；Q 证据仅为 `q_range_dependent_cross_gradient` validated slice，fingerprint before/after=`2879552213476552188`；FP-2F 六个 multisource/irregular case 三模式逐字节一致，per-source fingerprint before==after |
| `parallel` | 与 reuse 相同；frequency workers 只读 const per-source cache vector + serial ordered consumer publish | 没有新增 source/receiver owner；不代表 gap feature、其他 Q 组合或 multisource × ray-centered 已支持 | validated 产品与 nonreuse/reuse 逐字节相同；Q 证据仅为 `q_range_dependent_cross_gradient` validated slice，fingerprint before/after=`2879552213476552188`；FP-2F 六个 multisource/irregular case 三模式逐字节一致，per-source fingerprint before==after |

现有 evidence 继续满足关键所有权边界：trajectory/geometry/reflection raw material 在
per-source frozen `RayPathCache`（每 source 一个 fan，schema 与 fingerprint 算法不变）；
amplitude、phase、complex travel time、active prefix、reflection result、
ArrivalWorkspace 和 Eigenray hits 为 per-(frequency, source) 临时状态。审计未发现
逐频状态写回 frozen cache 的证据。这个结论只覆盖当前已 dispatch 的 feature slice。

## 6. FP-1A～FP-2F 验证记录

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
- FP-1H 使用 `Bellhop_RayReuse/build/fp1h` 隔离 Release build，CTest 32/32；
  standard-case unittest 153/153；仓库全量 `pytest` 168/168（另含 348 subtests）。
  RayReuse single 45 个支持案例通过；broadband nonreuse/reuse/parallel 每模式 35 个
  支持案例、40 个产品通过，三模式产品逐字节一致。
- 共享 `cartesian_component_pressure` 与
  `ray_centered_component_pressure/vertical/horizontal` 四个 executable case 的
  F2CPP/RayReuse complex pressure 与 TL 全部 0 差异，四 case aggregate SHA-256 同为
  `861f8fc8ade098fcd11d5a985bd61d034369bc64710147648d85f62fe5814a9e`。
  Origin/F2CPP 最大 pressure absolute difference 为 `4.48584387e-8`，最大 TL
  difference 为 `6.10351563e-5 dB`，沿用既有 tolerance。
- ray-centered traversal 固定为 receiver depth → true/surface/bottom image → ray
  segment → receiver range；`t=c*s`，normal 为 `(t.depth,-t.range)`，
  `n=(z_receiver-z_image)/normal.depth`，projected range 为
  `r_ray+n*normal.range`。surface/bottom image 在 segment loop 内执行并跨 image/depth
  持续保留的 normal-range flip；near-horizontal skip 发生在 flip 之前，保持 Origin
  legacy observable order。
- 基础 contribution 使用 right-endpoint amplitude/reflection phase、left-endpoint
  sound speed、插值 complex travel time/q/gamma，并在 receiver q 上再次更新 KMAH。
  V 使用 Fortran complex `DOT_PRODUCT` 对 derivative 的共轭语义，H 保持 Origin
  handwritten non-conjugated expression；component correction 在 KMAH/image polarity 与
  C/I/S sink 之前发生。C 对每个 image 直接加 `Hermite × contribution`；I/S 对每个
  image 独立加 `Hermite × ABS²(contribution)`，不做 Cartesian image coherent sum。
- parser 与 execution component matrix 覆盖全部
  `C/I/S × F/M/W × D/S/Z × P/V/H` 共 81 个 ray-centered 组合；nonreuse/reuse/
  parallel workspace 逐字节相同。两频 shared case 三模式 SHD SHA-256 均为
  `2b827187a4fbaba51f6910b2366972cc6dd27ff1ca287f47ecd3f8553f4e2d27`；reuse 与
  parallel 的 cache fingerprint before/after 均为 `10925417565703232468`。
- FP-1I 使用 `Bellhop_RayReuse/build/fp1i` 隔离 Release build，CTest 32/32；
  standard-case unittest 153/153；仓库全量 `pytest` 168/168（另含 349 subtests）。
  RayReuse single 46 个支持案例通过；broadband nonreuse/reuse/parallel 每模式 36 个
  支持案例、41 个产品通过，三模式产品逐字节一致。
- FP-1I 在同一 `GeometricHatInfluence` 中保留 Cartesian traversal，并新增明确的
  ray-centered TL field branch。normal 逐 ray point 取
  `(c*slowness.depth, -c*slowness.range)`；外层顺序为 receiver depth → active ray
  point/segment → projected receiver range，并保留 Origin 的 initial same-index skip、
  near-horizontal skip、duplicate-point skip 和正反向 range-index walker。该路径没有
  true/surface/bottom image loop 或 persistent normal flip；反射幅相来自逐频投影状态。
- ray-centered GeoHat 固定 `q0=c_source/Dalpha`，normal offset 与 q/complex delay
  线性插值，sound speed/amplitude 取右端点、reflection phase 取左端点；严格
  `abs(n)<abs(q)/q0`，C 乘一次 linear hat weight 后加 complex pressure，I/S 对
  attenuated real constant 平方后只乘一次 hat weight。segment-left 与 receiver-q
  crossing 分别按 F2CPP 顺序增加 `pi/2`。
- 共享 `geometric_hat_cartesian`/`geometric_hat_ray_centered` 三方 validator 通过：
  F2CPP/RayReuse G/g pressure 与 TL 为 0 差异；Origin/RayReuse 最大 pressure absolute
  difference `9.38570333e-10`、最大 TL difference `0 dB`，未放宽 tolerance。两频
  ray-centered GeoHat 的 nonreuse/reuse/parallel SHD SHA-256 均为
  `fef67deae6f74627d385782a2464adb456d8aec75f0be411dab1e4836a71c6eb`；reuse/parallel
  cache fingerprint before/after 均为 `11321016705018875701`。
- parser/model tests 覆盖 `Cg/Ig/Sg`、`Ag/ag/Eg` 与等间距 range 限制；
  solver tests 对 Cartesian/ray-centered GeoHat C/I/S 共用 `.sbp` source weighting。
  frozen `RayPathCache` 未增加字段，projection、q、phase、workspace 均保持逐频或
  Influence invocation 局部状态。
- FP-2A 沿用同一 `GeometricHatInfluence`，产品侧的 Ag/ag/Eg 共用一个明确的
  ray-centered traversal helper；FP-1I TL 的既有循环与 accumulation 顺序保持不动。
  产品 traversal 为 active ray point/segment → receiver-depth projection → 正/反向
  range-index walker；保持 initial same-index、near-horizontal、duplicate-point skip，
  endpoint、linear interpolation、严格 hat membership 与两级 q-caustic 次序和 F2CPP
  相同。
- Ag/ag Arrival 使用 interpolated complex delay、right-endpoint amplitude/sound speed、
  left-endpoint reflection phase、right-prefix bounce count/declination，并进入既有
  frequency-local `ArrivalWorkspace`/`AddArr`；A 与 a 只在 writer encoding 不同。
  Eg 使用独立 direct-hit publication：inactive terminal right endpoint 不发布，命中后
  写出 `rightIndex + 1` 的 active ray prefix，不从 ArrivalCandidate 转换。
- 三方 product validator 通过：Ag/ag 各 352 条 records，Origin/F2CPP/RayReuse 的
  amplitude、phase、delay、angles、bounce/index 字段均为 0 ULP；Eg 466 blocks，
  F2CPP/RayReuse 与 Origin/RayReuse 的最大 coordinate difference 均为 0 m。
  两频 Ag/ag/Eg 的 nonreuse/reuse/parallel 逐频产品 SHA-256 各模式一致，cache
  fingerprint before/after 均为 `5762074209948553069`；parallel worker 不持有 writer，
  仍由 serial consumer 按 frequency index 发布。
- FP-2A 使用 `Bellhop_RayReuse/build/fp2a` 隔离 Release clean build，CTest 32/32；
  standard-case unittest 153/153；仓库全量 `pytest` 168/168。RayReuse single 49 个
  支持案例通过；broadband nonreuse/reuse/parallel 每模式 39 个支持案例、47 个产品
  通过，跨模式逐字节一致。
- FP-2B 引入最小 C/P real 与 frequency-local evaluator。PCHIP 直接迁移 F2CPP
  production coefficient/limiter/Horner 顺序；`d²c/dz²` 进入 dynamic ray，P 节点不执行
  C-linear gradient jump。F2CPP/RayReuse 的 PCHIP geometry probe、TL SHD 与代表性
  R/A/a/E 产品逐字节一致；Origin intermediate-state matrix 的 `munk_pchip` 为 370 points，
  worst absolute error `5.82e-11`，未修改 tolerance。
- `munk_pchip` 两频 nonreuse/reuse/parallel SHD SHA-256 均为
  `fd5b2e2cf77a524ec4972e8563c19efe0e33de48c87677a193c2d20c80d85cde`；reuse/parallel
  均只 trace once 且 cache fingerprint 前后不变。C-linear Munk probe 保持基线 SHA-256
  `29c483a2f843ee1f48267b41c21df45a247b774a67fb98919b66e2539b50bd0b`。
- FP-2B 隔离 Release clean build 与 CTest 34/34、仓库 `pytest` 168/168、standard-case
  unittest 153/153、intermediate-state matrix 4/4 均通过。N/S/Q 未实现且继续明确拒绝。
- FP-2C 引入 N²-linear `N` 的 real geometry backend 与 frequency-local complex N²
  evaluator。real 路径按逐段线性 N² 插值（`c=1/sqrt(N²)`），节点梯度不连续并共用
  C-linear 的 reduced-step node jump，段内非零 N² Hessian 进入 dynamic ray；
  frequency-local 路径先把节点声速按目标频率转为复数，再形成复 N² 系数，gradient 与
  curvature 保持 F2CPP 的 real-part observable。
- F2CPP/RayReuse `munk_n2` geometry probe CSV 逐字节一致（SHA-256
  `360dda437550e396b531ed9a4692a006ebe8e5e29ddcaddde40fe8ddbcc00be8`；233 points/
  232 steps/0 events @0.0125 rad）；两频 TL SHD 与 R/A/a/E 产品 F2CPP=RayReuse
  逐字节一致，ARR 552,440 arrivals/100,701 cells/0 nonfinite。
- Origin intermediate-state matrix 的 `munk_n2` 为 366 points/363 steps/2 events，
  worst error q2@158 abs `1.81e-14`/scaled `1.21e-3`（既有预算 `3e-9`，未修改 tolerance）。
- `munk_n2` 两频 nonreuse/reuse/parallel SHD SHA-256 均为
  `18817c6788b6e7a4c0c7cbd73cb5b8de78c4c92ea90e06a059badf80c27d29c4`，PRT Trace passes
  分别为 2/1/1。C-linear Munk probe 保持基线 SHA-256
  `809b126d4b2657b8c54100e9f0e867c69bd26633963a58a883b2e985b98492e2`、PCHIP probe 保持
  `eb51ced19656a7724594e0dc7e0c2c5977daa8447962d76b9ca8112c55c646a0`，C/P broadband
  SHD 基线不变。
- FP-2C 隔离 Release clean build 与 CTest 35/35、release build CTest 35/35、仓库
  `pytest` 168/168、standard-case unittest 153/153 均通过。S/Q 未实现且继续明确拒绝；
  N 结果区别于 C/P（point_index=2 起分歧：N vs C p1 `+1.34e-6`、N vs P p1 `-2.22e-4`），
  不能从 C/P/N² closure 外推为整个 SSP family parity。
- FP-2D 引入 cubic spline `S` 的 exact coefficient kernel、real geometry backend 与
  frequency-local complex spline evaluator。coefficient 构造逐行迁移 F2CPP production
  （not-a-knot endpoint、2/3/4+ node 分支、forward elimination/back substitution 顺序、
  complex arithmetic），保留 legacy binary32
  `kFortranSixth = static_cast<double>(1.0F/6.0F)`；real spline 梯度节点连续
  （`ContinuousAtNodes`，不执行 C/N² 的 reduced-step node jump），非零 Hessian 经
  production `soundSpeedNormalSecondDerivativeOverSquaredSpeed` 进入 dynamic-ray
  equations；frequency-local 路径每频先按目标频率转换节点 attenuation、再构造复系数，
  每频各持一份 coefficients，spline interior imaginary 只做 finite 校验。
- F2CPP/RayReuse `munk-spline` geometry probe CSV 逐字节一致（SHA-256
  `1fd0e4f84391aa24ec5e9876fae5d582b2ffdeb30422533113b407eba7faad63`；237 points/
  236 steps/0 events @0.0125 rad）；两频 TL SHD 与 R/A/a/E 产品 F2CPP=RayReuse
  逐字节一致（SHD `ce216646…`、R `ddd94952…`、A `30042a84…`、a `837e1e4f…`、
  E `c921296b…`），R 为 5000 rays/1,689,310 points、top/bottom bounces 3778/3577，
  A 为 100,701 cells/536,601 arrivals/max 19 per cell，E 为 884,091 records/
  236,420,945 points。
- Origin intermediate-state matrix 的 `munk_spline` 为 370 points/367 steps/
  2 top reflections，worst error h_m@369 abs `1.45e-11`/scaled `5.54e-4`，
  未修改 tolerance。
- `munk_spline` 两频 nonreuse/reuse/parallel SHD SHA-256 均为
  `74028065178ff80d43755ef2ba70ba5ba3e4947574a37a4154a7ecc52eef1596`，PRT Trace passes
  分别为 2/1/1，reuse/parallel cache fingerprint before/after 均为
  `1526667602348633172`。C-linear/PCHIP/N² Munk probe 保持基线 SHA-256
  `809b126d…`/`eb51ced1…`/`360dda43…`，C/P/N broadband SHD 基线
  `cf1f9711…`/`fd5b2e2c…`/`18817c67…` 均不变。
- S 结果区别于 C/P/N（point_index=2 起 |Δp1| S vs C/P/N 为 `2.24e-4`/`2.22e-4`/
  `2.12e-9`，终点深度差 12.4/13.1/18.9 m，probe 点数 237 vs 234/234/233），排除
  silent fallback；不能把 S 与 C/P/N 数值混同。
- FP-2D 隔离 Release clean build（`build/fp2d-clean`）与 CTest 36/36、release build
  CTest 36/36、仓库 `pytest` 168/168、standard-case unittest 153/153 均通过；clean
  build 的 `bellhop_rayreuse` 与 geometry probe 可执行文件同 release build 逐字节相同
  （SHA-256 `f1f87158…`/`4979b2e0…`），clean probe 的 C-linear Munk 输出与基线
  SHA-256 `809b126d…` 一致。`Q`/`.ssp` 未实现且继续明确拒绝。
- FP-2E 引入 quadrilateral `Q`/`.ssp` SSP：`.ssp` reader 与二维 grid（immutable
  shared ownership，非 Q profile 不分配 grid heap storage）、real geometry evaluator
  （cell 内 bilinear、`cr/cz/crz`、`crr=czz=0`、density depth 插值、depth/range
  locator）与 frequency-local evaluator（real `c(r,z)` 来自 `.ssp` 2D matrix；
  imaginary attenuation 由 `.env` reference depth profile 逐节点按目标频率转换后仅沿
  depth 插值，不从 `.ssp` 生成二维衰减）。`RayStepper` 双 hint 采样与 depth/range
  cell 边界 gradient jump（depth 优先、corner 单次 jump）逐行迁移 F2CPP production；
  `GeometryStepLimiter` 接入 SSP range segment 边界（越界 trial step 缩减到 grid
  line）并保留 `minimumStep = 1e-3 × nominalStepLength` 下限；transient
  `rangeSegmentIndex` 只存在于 `SoundSpeedSample`/`StepLimitRequest`/`RayStepResult`
  与 tracer 局部变量，`RayState`/`RayPath`/`RayPathCache` 的 fields/layout 与
  `contentFingerprint` 算法不变。
- F2CPP/RayReuse `i5-quadrilateral` geometry probe CSV 逐字节一致（SHA-256
  `4e22fd057eeca5dcabca171aeeb9129fba09e7616c0d8fdb5621f26c6029d32f`；715 points/
  714 steps/0 events @`-0.002626749710359303` rad）；Origin intermediate-state
  oracle（`generate_i5_q_oracle.py` ALPHA_INDEX=150 角度）PASS，worst scaled
  `1.06e-9`（t_z@131）；Origin final-field 沿用既有 `validate_i5_quadrilateral_ssp.py`
  policy，12 个 field comparison（6 Origin→F2CPP + 6 Origin→RayReuse）全部 PASS：
  range-dependent worst TL `1.52587890625e-05 dB`（tolerance `0.001 dB`）、control
  worst `0.01125335693359375 dB`（既有 `tolerances_i5_q_control.toml` `0.02 dB`），
  tolerance 未放宽、未修改。
- SSP=`Q` 两频（1000/2000 Hz）TL（`CC` SHD）/R/A/a/E 共 8 对文件 F2CPP=RayReuse
  逐字节一致（TL `132d6af2…`；R `00e11586…`；A `19ef39a8…`/`0dd74fb1…`；
  a `3c9acd39…`/`a7f2ccee…`；E `deae13ce…`/`e5324d32…`）。
- `q_range_dependent_cross_gradient` 两频 nonreuse/reuse/parallel SHD SHA-256 均为
  `b53c02cba0a1372ac13123937643106579ddaed5bb77db7515d2440cc263ed2f`，PRT Trace
  passes 分别为 2/1/1，reuse/parallel cache fingerprint before/after 均为
  `2879552213476552188`。C/P/N/S Munk probe 保持基线 SHA-256
  `809b126d…`/`eb51ced1…`/`360dda43…`/`1fd0e4f8…`，C/P/N/S broadband SHD 基线
  `cf1f9711…`/`fd5b2e2c…`/`18817c67…`/`74028065…` 均不变，`munk_spline`
  reuse/parallel fingerprint 仍为 `1526667602348633172`。
- FP-2E 隔离 Release clean build（`build/fp2e-clean`）与 CTest 37/37、仓库 `pytest`
  173/173、standard-case unittest 158/158 均通过。line source、multisource、
  irregular receiver、3D/N×2D 不属本批次；`Q` 的产品 oracle 限于 TL `CC` 与 R、
  Cartesian GeoHat `G` A/a/E，不外推到其他 family。
- FP-2F 引入 source/receiver generalization：`SimulationCase` 持有按 depth
  `stable_sort` 的 source vector（`NSz ≥ 1` point source），`ReceiverGrid` 增加
  `ReceiverGridLayout{Rectilinear, Irregular}` 与 `receiversPerRange()`/
  `depthAt(depthIndex, rangeIndex)`；multisource = 每 source 独立 frozen
  `RayPathCache`（共享 launch fan，`RayPath`/`RayPathCache`/`RayState` schema 与
  `contentFingerprint()` 算法零改动），reuse 复用单位细化为 `(source, frozen fan)`；
  逐频产品状态变为 per-(frequency, source) workspace 序列，`ArrivalWorkspace`/
  Eigenray hits 保持 frequency-local；statistics 冻结语义 trace passes =
  `Nfreq×NSz / NSz / NSz`。
- Cartesian irregular 按 Origin/F2CPP legacy 语义施工：Cerveny `CC/IC/SC` 恒取首
  深度 `Rz(1)`（F2CPP `cartesian_cerveny_influence.cpp` 同构，非 paired
  `Rz(ir)`），GeoHat/GeoGaussian Cartesian 按 paired `depthAt(depthIndex,
  rangeIndex)`；SHD header 写 `NSz`/`Sz` 向量与 `PlotType = 'irregular '`；ARR/E/R
  writer per-source 块与 header 对齐 F2CPP/Origin；`NSz==1` 且 rectilinear 时全部
  输出与改动前 byte-identical（munk_spline 冻结基线 `74028065…`、fingerprint
  `1526667602348633172` 不变）。
- 三方 validator（`build/fp2f-clean` 二进制）：`multi_source_depths`（TL `CC`
  NSz=3）origin↔rayreuse max \|Δp\| `2.049e-08`、max TL diff `3.81e-05 dB`（同
  tolerance 未放宽），f2cpp↔rayreuse decoded payload exact；`irregular_receiver_pairs`
  （`CC RI`）f2cpp↔rayreuse payload exact、origin↔rayreuse max \|Δp\| `3.79e-09`、
  max TL diff `3.81e-06 dB`；`ray_trace_vacuum_rigid`（R 双源）两腿 max coordinate
  error `0.0 m`；`arrival_multi_source`/`arrival_multi_source_binary`/
  `arrival_geometric_gaussian_irregular` A/a 全字段 0 ULP；`eigenray_irregular_pairs`
  max coordinate error `0.0 m`。
- 六个 multisource/irregular case 的 broadband `nonreuse/reuse/parallel` 每频产品
  逐字节一致（10 个产品文件）；两频双源 trace passes `4/2/2`、两频三源 `6/3/3`、
  两频单源 `2/1/1`；reuse/parallel `--verify-cache` per-source fingerprint
  before==after；R 保持单频（双源 `ray_trace_vacuum_rigid` 通过，reuse/parallel 对
  R 显式拒绝）；C/P/N/S probe 与 broadband SHD 基线、`munk_spline` fingerprint
  `1526667602348633172`、Q fingerprint `2879552213476552188` 全部不变。
- FP-2F 隔离 Release clean build（`build/fp2f-clean`）CTest 40/40、仓库 `pytest`
  178 passed（含 subtests）、standard-case unittest 163/163 均通过。line source
  （`X`）继续显式拒绝；multisource × `Q`、multisource × ray-centered、
  irregular × `Q` 等机制可达但未 oracle 验证的组合不声明 parity。

## 7. 审计结论

### A. 当前完整 GAP 列表

1. `SRC-02` / `PRD-08`：line source 及其产品 scaling。
2. `BND-04`：canonical curvilinear `C` boundary（architectural conflict）。
3. `BND-09`：flat `A` elastic P/S 缺 RayReuse executable oracle 闭环。
4. `ATT-01`：attenuation units N/F/M/Q/L 缺 RayReuse product-level oracle 闭环。
5. `ATT-04`：Francois–Garrison attenuation。
6. `ATT-05`：biological attenuation。

`SSP-03`（N2-linear）已由 FP-2C 关闭、`SSP-04`（spline `S`）已由 FP-2D 关闭、
`SSP-05` 的 evaluator/geometry 与已验证产品 slice（quadrilateral `Q`/`.ssp`，
二维 point/single/rectilinear 下 TL Cartesian Cerveny `CC`、R、Cartesian GeoHat
`G` A/a/E）已由 FP-2E 关闭；`SRC-04`/`PRD-07`（multisource）与
`REC-02`/`REC-03`/`PRD-06`（Cartesian paired irregular）已由 FP-2F 在
oracle-validated 范围内关闭——均不再列于 GAP；未验证 Q beam/option 组合与
multisource × ray-centered 等机制可达组合不因此获得 parity。`TL-10`、`REC-05`、
`BND-10` 不在
GAP 列表，因为它们是
`F2CPP_OUT_OF_SCOPE`，而不是把 RayReuse 旧 Deferred 误当作 out of scope。

### B. 按优先级分组

- **P0 — 主功能 / 后续架构**：已清空——FP-2F 在 oracle-validated 范围内关闭了
  SRC-04、REC-02、REC-03 所暴露的 source/receiver ownership 与 product dimension
  冲突。
- **P1 — 重要 parity gap**：SRC-02/PRD-08、BND-04、ATT-04、ATT-05。
- **P2 — 外围或证据闭环 gap**：BND-09、ATT-01。

### C. 后续阶段状态

FP-2B 已关闭 PCHIP `P` parity；FP-2C 已关闭 N²-linear `N` parity；FP-2D 已关闭
cubic spline `S` parity；FP-2E 已关闭 quadrilateral `Q`/`.ssp` 的已验证产品 slice
（二维 point/single/rectilinear 下 TL Cartesian Cerveny `CC`、R、Cartesian GeoHat
`G` A/a/E）；FP-2F 已关闭 multisource（SRC-04/PRD-07）与 Cartesian paired
irregular receiver（REC-02/REC-03/PRD-06）的 oracle-validated slice。其他 Q
beam/option 组合不据此获得 parity。本报告不指定下一
feature batch。line source、canonical curvilinear boundary、Influence Geometry Reuse
和频率插值仍为独立后续范围，不能从既有 abstraction 推断已支持。

### D. FP-1A～FP-2E 更新状态

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
其他 beam family、Bellhop_F2CPP 或 Origin production code。FP-1H 只增加 RayReuse
ray-centered Cerveny coordinate model、规则 range 校验、独立 Influence、现有三个共享
ray-centered component case 的 allow-list 与三方 validator；复用 FP-1G epsilon 和既有
逐频 workspace，未修改 frozen trajectory、Cartesian Influence、Bellhop_F2CPP 或
Origin production code。FP-1I 只增加 RayReuse ray-centered GeoHat TL parser/model/
Influence dispatch、现有共享 `geometric_hat_ray_centered` case 的 allow-list/profile 与
三方 validator；Cartesian GeoHat 与 A/a/E G/B 方法体保持不变。FP-2A 只增加
RayReuse `Ag/ag/Eg` parser/model/product dispatch、frequency-local Arrival/Eigenray
consume、一个最小共享 binary companion case 与三方 validator；复用既有 writer、
多频命名、ordered publish 和 cleanup lifecycle，未修改 Cartesian `AG/aG/EG`、
`AB/aB/EB` 的 observable semantics，也未修改 Bellhop_F2CPP 或 Origin production code。
FP-2B 只增加 C/P interpolation kind、PCHIP real/frequency-local evaluator、共享 geometry/
projection 接入、`munk_pchip` oracle 与相应文档；没有实现 N/S/Q、改变 frozen cache
ownership、增加 frequency interpolation，或修改 Bellhop_F2CPP/Origin production code。
FP-2C 只增加 N²-linear interpolation kind、N² real/frequency-local evaluator、共享
geometry/projection 接入、`munk_n2` 三方 oracle 与相应文档，并把 `applyCLinearGradientJump`
机械重命名为 `applyGradientJump` 以服务 C/N² 共用 node jump；没有实现 S/Q、改变
frozen cache ownership、增加 frequency interpolation，或修改 Bellhop_F2CPP/Origin
production code。FP-2D 只增加 cubic-spline interpolation kind、exact coefficient
kernel 与 real/frequency-local spline evaluator、generic dispatch 接入、`munk_spline`
三方 oracle 与相应文档；coefficient 构造逐行迁移 F2CPP production 并保留 legacy
binary32 `1.0F/6.0F`，spline 节点连续梯度不执行 node jump、非零 Hessian 进入 dynamic
ray；没有实现 Q/`.ssp`、改变 frozen cache ownership、增加 frequency interpolation，
或修改 Bellhop_F2CPP/Origin production code。FP-2E 只增加 quadrilateral
interpolation kind、`.ssp` reader 与二维 grid、real/frequency-local quadrilateral
evaluator、stepper/limiter/tracer 的 transient range-segment 状态（不进入
`RayState`/`RayPath`/`RayPathCache`）、共享
`q_range_dependent_cross_gradient`/`q_range_independent_control` 三方 oracle 与相应
文档；没有实现 3D/N×2D/line/multisource/irregular、改变 frozen cache ownership、
增加 frequency interpolation，或修改 Bellhop_F2CPP/Origin production code。FP-2F
只增加 RayReuse multisource parser/model/solver/writer lifecycle、
`ReceiverGridLayout{Rectilinear, Irregular}` irregular layout 与 paired/legacy-`Rz(1)`
receiver addressing、per-source 产品 sequencing/headers、共享 multisource/irregular
case allow-list 与新增 case、以及相应文档；`RayPathCache`/`RayPath`/`RayState`
schema 与 `contentFingerprint()` 算法零改动，frozen cache 语义收紧为 one cache =
one source fan（frequency-independent source depth 在契约内）；line source 继续显式
拒绝；没有修改 Bellhop_F2CPP 或 Origin production code。
