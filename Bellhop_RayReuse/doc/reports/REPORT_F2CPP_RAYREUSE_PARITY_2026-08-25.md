# Bellhop_F2CPP → Bellhop_RayReuse Production Feature Parity Audit

审计日期：2026-08-25；FP-2B 实现验证更新：2026-08-27；FP-2C 实现验证更新：2026-08-28；FP-2D 实现验证更新：2026-08-28；FP-2E 实现验证更新：2026-08-28；FP-2F 实现验证更新：2026-08-29；FP-2G 实现验证更新：2026-08-29；FP-2H 实现验证更新：2026-08-29；FP-2I 最终封板更新：2026-08-29

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

FP-2G 实施前基线：`763c585c5c16053725bcfd6541f99c2776c5b966`

FP-2H 实施前基线：`c40a4ee01ef8a790a5d6d1814b4b24795ea0f083`

FP-2I 实施前基线：`099a2b1602ae03d9203da855f77894a4ae9936cf`

本报告在原始只读审计之后，按 FP-1A～FP-2I 的实际实现与验证结果全面闭环。当前总体输入范围覆盖点声源（Point Source）与线声源（Line Source，`X`）、单源与多源深度（Multisource，`NSz ≥ 1`）、规则网格（Rectilinear）与 Cartesian 配对不规则网格（Irregular，`I`，`NRz == NRr`）。C-linear / PCHIP / N²-linear / cubic spline / quadrilateral `Q` 五大 SSP、基础与体积衰减模型（ATT-01～ATT-05，含 N/F/M/W/Q/L 单位、Thorp、Francois–Garrison 与 Biological）、全边界类型（平坦边界、折线边界 LS/LL、规范曲线边界 `C` short format、平坦普通弹性半空间 P/S、声学与弹性 LL）、全部 TL 波束族（Cartesian Cerveny `CC/IC/SC`、Ray-centered Cerveny `CR/IR/SR`、Cartesian GeoHat `CG/IG/SG`、Ray-centered GeoHat `Cg/Ig/Sg`、Cartesian GeoGaussian `CB/IB/SB`、Simple Gaussian `CS`）以及全部输出产品（SHD、单频 R、ASCII A、Binary a、Eigenray E）均已在 RayReuse 的 `nonreuse`、`reuse`、`parallel` 路径内闭环。随着 FP-2I 闭环线声源（SRC-02 / PRD-08），**F2CPP → RayReuse Production Feature Parity 现已完全达成，真实 GAP 数量归零（GAP = 0）**。

## 1. 结论摘要

RayReuse 目前**已完全达到** F2CPP 二维 production feature surface 的完整 parity。已经闭环的是：
- Cartesian 与 ray-centered Cerveny `C/I/S + P/V/H + {F,M,W}{D,S,Z}`；
- Cartesian 与 ray-centered GeoHat `C/I/S` 以及 Cartesian GeoGaussian `C/I/S`；
- Cartesian Simple Gaussian coherent TL `CS`；
- 点声源（Point Source）与线声源（Line Source，RunType 4th `'X'`）及其在场压强与到达结构上的精确扩散缩放；
- 单点源与多源深度（`NSz ≥ 1`，按 depth `stable_sort`，每源独立 frozen fan cache）；
- 规则网格（Rectilinear）与 Cartesian 配对不规则接收网格（`NRz == NRr`，Cerveny 恒取 `Rz(1)` legacy 语义，GeoHat/GeoGaussian paired 寻址，SHD `PlotType='irregular '`）；
- 五大声速剖面：C-linear、PCHIP `P`、N²-linear `N`、Cubic Spline `S`、Quadrilateral `Q`/`.ssp`；
- 全衰减单位与模型：N/F/M/W/Q/L 衰减单位、Thorp 保护、Francois–Garrison 体积衰减、Biological 多层重叠体积衰减、边界声学衰减穿透；
- 全边界与材料类型：平坦边界 V/R/A/G/F、折线边界 LS/LL、规范曲线短格式 `C` 边界、平坦普通弹性半空间 P/S、声学/弹性 LL；
- 全输出产品：单频/多频 SHD、单频 Origin 兼容 `.ray`（R）、ASCII 到达结构 `.arr`（A）、Binary 到达结构 `.arr`（a）、声线特征路径 `.ray`（E）；
- 宽带三模式（`nonreuse` / `reuse` / `parallel`）在所有支持案例上生成产品逐字节一致（`cmp` 返回 0）；
- frozen `RayPathCache` 契约严格守恒，`--verify-cache` before == after 语义指纹完全不变。

3D、N×2D、Beam Shift 与 Ray-centered Geometric Gaussian 不属 F2CPP production scope，明确归类为 `F2CPP_OUT_OF_SCOPE`。RayReuse 自身的远期特性（HDF5 输出、SIMD 优化、频率插值、BARR 到达算法等）明确归类为 `RAYREUSE_DEFERRED / EXTENSION`，不构成 parity 缺口。

## 2. 判定方法与状态语义

> **组合范围说明：** 本报告按 feature axis 判定 production parity，不把分别
> 支持的 source、receiver、SSP、beam 与 product 自动展开为全部已验证组合。
> Quadrilateral `Q` / `.ssp` 只声明 FP-2E accepted slice：二维、single point
> source、single source depth、rectilinear receivers；TL Cartesian Cerveny
> `CC`、单频 R，以及 Cartesian GeoHat `G` 的 A/a/E。其他 Q cross-product
> 组合即使机制可达，也未建立独立 oracle，不在本报告中声明为已验证 parity。

每个 `PARITY` 必须同时具备：

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
- `F2CPP_OUT_OF_SCOPE`：F2CPP 自己也没有正式 production support。
- `RAYREUSE_DEFERRED / EXTENSION`：不是 F2CPP parity 的必需功能，RayReuse 当前未做或未来可选扩展。

## 3. 主要代码与测试证据

| 标签 | 真实证据 |
| --- | --- |
| `F-PARSER` | `Bellhop_F2CPP/src/io/environment_parser.cpp`：run type/beam/source/receiver、C/P/N/S/Q SSP、LS/LL/C boundary、attenuation dispatch |
| `R-PARSER` | `Bellhop_RayReuse/src/io/environment_parser.cpp`：TL 接受 Cartesian Cerveny `CC/IC/SC` 与 ray-centered Cerveny `CR/IR/SR` 的 `{F,M,W}{D,S,Z} + P/V/H`、Cartesian GeoHat `CG/IG/SG`、ray-centered GeoHat `Cg/Ig/Sg`、Cartesian GeoGaussian `CB/IB/SB`、Cartesian Simple Gaussian `CS`；产品接受 Cartesian `AG/aG/EG`、`AB/aB/EB` 与 ray-centered GeoHat `Ag/ag/Eg`；SSP 接受 C/P/N/S/Q；source depth count 接受 `≥1`；run type 第 4 位接受 `'X'`（Line source）与 `'R'`/blank（Point source）；run type 第 5 位接受 `'I'`（Irregular receiver）；边界接受 `LS`/`LL` 与 canonical curvilinear short format `C` |
| `F-MODEL` | `Bellhop_F2CPP/include/bellhop/model/simulation_case.hpp` 与 boundary/SSP model：source vector、receiver layout、source geometry、coherence、coordinate/beam families、curvilinear geometry |
| `R-MODEL` | `Bellhop_RayReuse/include/rayreuse/model/simulation_case.hpp`、`environment.hpp`、`boundary_geometry.hpp`、`sound_speed_evaluator.hpp` 与 `src/model/simulation_case.cpp`：按 depth `stable_sort` 的 `vector<Source>` 与 `ReceiverGrid`（Rectilinear / Irregular）；`SourceGeometry::{Point, Line}`；`VolumeAttenuation` 不可变参数模型；`BoundaryGeometry` 支持 Flat、Piecewise linear、Curvilinear；`SoundSpeedProfile` 支持 C/P/N/Spline/Quadrilateral 五后端 |
| `F-TL` | `Bellhop_F2CPP/src/solver/single_frequency_solver.cpp` 及 `src/influence/`：按 coherence、beam family、coordinate、source geometry dispatch |
| `R-TL` | `Bellhop_RayReuse/src/solver/single_frequency_solver.cpp`、`frequency_projector.cpp`、各 production Influence 与 `pressure_scaling.cpp`：source sampling、trace、Cartesian/Ray-centered Cerveny、Geometric Hat、Geometric Gaussian、Simple Gaussian 逐频 projection 与 ratio/scaling 计算；C 使用 complex pressure，I/S 使用 intensity；线声源扩散因子 `-4*sqrt(pi)*beamScale` 作用于全部接收距离 |
| `F-GEOM` | `Bellhop_F2CPP/include/bellhop/ray/geometry_tracer.hpp`、`src/ray/flat_boundary_reflection.cpp` 与 boundary geometry：通用 SSP evaluator、canonical curvilinear frame/curvature；D/S/Z 在 reflection 时对完整 `RN` 分别乘 2、保留、置零 |
| `R-GEOM` | `Bellhop_RayReuse/include/rayreuse/model/sound_speed_evaluator.hpp`、`model/boundary_geometry.hpp`、`ray/geometry_tracer.hpp`、`src/ray/ray_stepper.cpp` 与 boundary geometry：value-owned C/P/N²/Spline/Quadrilateral geometry evaluator；`BoundaryGeometry` 支持 `BoundaryInterpolationKind::Curvilinear`；C/N² node jump、P/Spline 连续梯度、Q cell 边界 jump；D/S/Z 写入冻结 real dynamic-ray bases |
| `R-PRODUCT` | `Bellhop_RayReuse/src/io/`：`shd_writer.cpp`、`arrival_writer.cpp`（线声源幅值 `4*sqrt(pi)` 缩放）、`ray_writer.cpp`、`eigenray_writer.cpp`、`ray_prefix_writer.cpp` |
| `R-CLI` | `Bellhop_RayReuse/src/io/command_line.cpp` 与 `app/main.cpp`：PRT 报告、`nonreuse`/`reuse`/`parallel` 执行模式、`--verify-cache` |
| `FP2H-ORACLE` | FP-2H 衰减闭环证据：ATT-01～ATT-05 单元测试与 6 衰减单位 + 2 体积衰减标准算例三方比较全部 PASS，10 宽带 profile 三模式 byte-identical |
| `FP2I-ORACLE` | FP-2I 线声源闭环证据：`source_geometry_line` 与 `arrival_line_directional_multisource` 三方 oracle 闭环（F2CPP 0 diff / 0 ULP），9/9 到达结构三方验证全部 PASS，宽带三模式 byte-identical |

## 4. 全量 Feature Parity 状态表

### 4.1 TL / Influence

| Feature | F2CPP | RayReuse | Status | Evidence |
| --- | --- | --- | --- | --- |
| TL-01 — coherence `C` 跨 F2CPP production TL beam/coordinate family | 全部正式 TL family 按其约束支持 | Cartesian/ray-centered Cerveny、Cartesian/ray-centered GeoHat、Cartesian GeoGaussian 与 Cartesian Simple Gaussian 在 point/line/single/multisource/rectilinear/irregular 与五大 SSP slice 内闭环 | `PARITY` | `F-PARSER`, `R-PARSER`, `F-TL`, `R-TL`, `TEST`, `FP2B-ORACLE`～`FP2I-ORACLE` |
| TL-02 — Cartesian Cerveny，C/I/S、`{F,M,W}{D,S,Z}` beam options、P/V/H component、五大 SSP | 支持 | 支持 | `PARITY` | `F-TL`, `R-TL`, `R-GEOM`, `STD`, `TEST`, `FP1A-ORACLE`, `FP1E-ORACLE`, `FP1F-ORACLE`, `FP1G-ORACLE`, `FP2B-ORACLE`～`FP2I-ORACLE` |
| TL-03 — incoherent `I` | 支持 | parser/runtime/Influence/output/oracle 闭环 | `PARITY` | Cartesian Cerveny 为 image coherent sum 后 ABS²；ray-centered Cerveny 为每个 image 独立 `Hermite × ABS²(contribution)`；Cartesian/ray-centered GeoHat 与 GeoGaussian 各保持自身 linear weight law；均在总 intensity 后 sqrt |
| TL-04 — semicoherent `S` | 支持 | parser/runtime/Influence/output/oracle 闭环 | `PARITY` | 各 family 与其 I 模式共用 contribution；S 的差异仅为 Project 前逐频 Lloyd/source amplitude |
| TL-05 — Cartesian Cerveny production beam options | 支持 F/M/W width × D/S/Z curvature | F/M/W × D/S/Z 全部 parser/runtime/epsilon/KMAH/SHD/oracle 闭环 | `PARITY` | `F-PARSER`, `R-PARSER`, `F-GEOM`, `R-GEOM`, `R-TL`, `FP1F-ORACLE`, `FP1G-ORACLE` |
| TL-06 — ray-centered Cerveny，C/I/S、`{F,M,W}{D,S,Z}`、P/V/H | 支持；要求至少两个等间距 receiver ranges | parser/model/runtime/Influence/SHD/oracle 闭环 | `PARITY` | `F-PARSER`, `R-PARSER`, `F-TL`, `R-TL`, `R-MODEL`, `STD`, `TEST`, `FP1H-ORACLE`, `FP2B-ORACLE`～`FP2I-ORACLE` |
| TL-07 — Cartesian GeoHat，C/I/S | 支持 | parser/runtime/Influence/geometric scaling/SHD/oracle 闭环 | `PARITY` | `R-PARSER`, `R-TL`, `STD`, `TEST`, `FP1B-ORACLE`, `FP2B-ORACLE`～`FP2I-ORACLE` |
| TL-08 — ray-centered GeoHat C/I/S | 支持；至少两个等间距 receiver ranges | parser/model/runtime/Influence/geometric scaling/SHD/oracle 闭环 | `PARITY` | `F-PARSER`, `R-PARSER`, `F-TL`, `R-TL`, `R-MODEL`, `STD`, `TEST`, `FP1I-ORACLE`, `FP2A-ORACLE`～`FP2I-ORACLE` |
| TL-09 — Cartesian GeoGaussian，C/I/S | 支持 | parser/runtime/逐频 width 与 membership/Influence/geometric scaling/SHD/oracle 闭环 | `PARITY` | `R-PARSER`, `R-TL`, `STD`, `TEST`, `FP1C-ORACLE`, `FP2B-ORACLE`～`FP2I-ORACLE` |
| TL-10 — ray-centered GeoGaussian | F2CPP 未正式支持 | 未支持 | `F2CPP_OUT_OF_SCOPE` | `MATRIX`, F2CPP parser/solver 均不提供该组合 |
| TL-11 — Cartesian Simple Gaussian coherent TL | 支持；不提供 I/S accumulator | parser/runtime/Influence/geometric scaling/SHD/oracle 闭环；`IS/SS` 明确拒绝 | `PARITY` | `R-PARSER`, `R-TL`, `STD`, `TEST`, `FP1D-ORACLE`, `FP2B-ORACLE`～`FP2I-ORACLE` |
| TL-12 — directional `.sbp` 对当前 TL 生效 | 支持且有 shared Origin/F2CPP case | 支持；所有已支持 TL family 共用逐 ray、逐频、Project 前 source pattern 路径 | `PARITY` | `F-TL`, `R-PARSER`, `R-TL`, `STD`, `FP1A-ORACLE`, `FP1B-ORACLE`, `FP1C-ORACLE`, `FP1D-ORACLE`, `FP1I-ORACLE` |
| TL-13 — Cartesian Cerveny 1～3 images 与 beam window | 支持 | 支持 | `PARITY` | 两边 parser 均校验 image count ≤ 3 并传入 Cartesian Cerveny Influence；component/shared TL regression 闭环 |
| TL-14 — Cartesian Cerveny `P/V/H` legacy component selector | 支持 | 支持；保持相同 component-independent Cartesian contribution | `PARITY` | `F-PARSER`, `F-TL`, `R-PARSER`, `R-MODEL`, `R-TL`, `STD`, `TEST`, `FP1E-ORACLE` |

### 4.2 Source

| Feature | F2CPP | RayReuse | Status | Evidence |
| --- | --- | --- | --- | --- |
| SRC-01 — point source、single source depth | 支持 | 支持 | `PARITY` | `F-PARSER`, `R-PARSER`, `R-MODEL`, `STD`, `TEST` |
| SRC-02 — line source | 支持（RunType 4th `'X'`） | parser/model/Cerveny/GeoHat/GeoGaussian ratio/PressureScaling/ArrivalWriter/oracle 闭环 | `PARITY` | `F-PARSER`, `F-TL`, `R-PARSER`, `R-MODEL`, `R-TL`, `R-PRODUCT`, `source_geometry_line`, `arrival_line_directional_multisource`, `FP2I-ORACLE` |
| SRC-03 — directional `.sbp`：TL/R/A/a/E | 支持 | 在各自已支持的 product slice 内支持 | `PARITY` | TL 见 TL-12；directional R shared oracle；Arrival component regression 验证逐频 amplitude 改变；Eigenray 产品不跨频共享 writer |
| SRC-04 — multisource / multiple source depths | 支持，source vector 贯穿 solver 与 writers | parser/model 接受 `NSz ≥ 1`（depth 升序 `stable_sort`）；solver 每 source 独立 frozen `RayPathCache`，逐频产品按 `(frequency, source)` 序列生成；writer per-source sequencing/header 与 F2CPP/Origin 一致；三执行模式一致 | `PARITY` | `F-MODEL`, `R-MODEL`, `R-PARSER`, `R-PRODUCT`, `R-CLI`, `STD`, `TEST`, `FP2F-ORACLE`, `FP2I-ORACLE` |

### 4.3 Receiver

| Feature | F2CPP | RayReuse | Status | Evidence |
| --- | --- | --- | --- | --- |
| REC-01 — rectilinear receiver | 支持 | 支持 | `PARITY` | `F-MODEL`, `R-MODEL`, `STD`, `TEST` |
| REC-02 — Cartesian TL irregular receiver | 支持 legacy irregular layout | run type 第 5 位 `I`、paired `NRz == NRr` 的 irregular layout 已接入：Cartesian Cerveny `CC/IC/SC`、Cartesian GeoHat `CG/IG/SG`、Cartesian GeoGaussian `CB/IB/SB` TL；SHD `PlotType` 写 `irregular ` | `PARITY` | `F-PARSER`, `F-MODEL`, `R-PARSER`, `R-MODEL`, `R-PRODUCT`, `STD`, `TEST`, `FP2F-ORACLE` |
| REC-03 — paired irregular A/a/E | 支持 paired receiver identity/sequence | Cartesian `G/B` 的 A/a/E traversal 与 writers 按 `receiversPerRange() × rangeCount` paired cell 编址 | `PARITY` | `R-MODEL`, `R-PRODUCT`, `STD`, `TEST`, `FP2F-ORACLE` |
| REC-04 — ray-centered receiver 约束（regular/equal-range） | 在 ray-centered family 下支持并校验 | 在 TL-06 Cerveny、TL-08 GeoHat 与 PRD-05 Ag/ag/Eg slice 内支持并校验至少两个等间距 ranges；depths 保持严格递增规则轴 | `PARITY` | `F-PARSER`, `F-MODEL`, `F-TL`, `R-PARSER`, `R-MODEL`, `R-TL`, `R-PRODUCT`, `FP1H-ORACLE`, `FP1I-ORACLE`, `FP2A-ORACLE` |
| REC-05 — ray-centered irregular receiver | F2CPP 未正式支持 | 未支持 | `F2CPP_OUT_OF_SCOPE` | `MATRIX` |

### 4.4 SSP

| Feature | F2CPP | RayReuse | Status | Evidence |
| --- | --- | --- | --- | --- |
| SSP-01 — C-linear | 支持 | 支持 | `PARITY` | `F-PARSER`, `R-PARSER`, `R-TL`, `R-GEOM`, `STD`, `TEST`, `FP2B-ORACLE` |
| SSP-02 — PCHIP `P` | 支持 | parser/model、real geometry、dynamic ray、frequency-local projection、TL/R/A/a/E 与三执行模式闭环 | `PARITY` | `F-PARSER`, `F-GEOM`, `R-PARSER`, `R-MODEL`, `R-GEOM`, `R-TL`, `STD`, `TEST`, `FP2B-ORACLE` |
| SSP-03 — N2-linear | 支持 | parser/model、real geometry（`c=1/sqrt(N²)` 逐段线性 N²、节点梯度不连续并共用 C-linear node jump 规则、段内非零 N² Hessian 进入 dynamic ray）、frequency-local complex N² projection、TL/R/A/a/E 与三执行模式闭环 | `PARITY` | `F-PARSER`, `F-GEOM`, `R-PARSER`, `R-MODEL`, `R-GEOM`, `R-TL`, `STD`, `TEST`, `FP2C-ORACLE` |
| SSP-04 — spline `S` | 支持 | parser/model、exact not-a-knot coefficient kernel（含 2/3/4+ node 分支、binary32 `1.0F/6.0F`）、real geometry（value/一阶/二阶导数、节点连续梯度且无 node jump、非零 Hessian 进入 dynamic ray、edge cubic extrapolation）、frequency-local complex spline projection（节点 attenuation 先转换、再每频独立构造复系数）、TL/R/A/a/E 与三执行模式闭环 | `PARITY` | `F-PARSER`, `F-GEOM`, `R-PARSER`, `R-MODEL`, `R-GEOM`, `R-TL`, `STD`, `TEST`, `FP2D-ORACLE` |
| SSP-05 — Q + `.ssp` range-dependent tabulation | 支持 | parser/model、二维 quadrilateral grid、real geometry（cell 内 bilinear 与 `cr/cz/crz`、`crr=czz=0`、density depth 插值；depth/range cell 边界 gradient jump、depth 优先 corner 单次 jump、depth/range grid line landing、`minimumStep=1e-3×nominal` 下限、越出 `.ssp` 网格显式失败）、frequency-local projection（real `c(r,z)` 只在 trace 阶段决定轨迹；imaginary attenuation 由 `.env` reference depth profile 逐频转换后仅沿 depth 插值）；TL Cartesian Cerveny `CC`、单频 R、Cartesian GeoHat `G` A/a/E 已闭环 | `PARITY` | `F-PARSER`, `F-MODEL`, `F-GEOM`, `R-PARSER`, `R-GEOM`, `R-MODEL`, `STD`, `TEST`, `FP2E-ORACLE` |

### 4.5 Boundary / material

| Feature | F2CPP | RayReuse | Status | Evidence |
| --- | --- | --- | --- | --- |
| BND-01 — flat boundary geometry | 支持 | 支持 | `PARITY` | parser/model/component tests 与共享 vacuum/rigid/acoustic cases |
| BND-02 — piecewise-linear `LS` | 支持 | 支持 | `PARITY` | `F-PARSER`, `R-PARSER`, shared `i3_piecewise_linear` |
| BND-03 — piecewise-linear `LL` | 支持 | 支持 | `PARITY` | `F-PARSER`, `R-PARSER`, shared elastic LL case |
| BND-04 — canonical curvilinear `C` | 支持，保存 boundary frame/curvature | parser 接受 `.ati/.bty` 精确 token `C`（canonical curvilinear short format）；`BoundaryGeometry` 支持 curvilinear kind；tracer seam 保持 chord collision + interpolated reflection frame + two-consecutive-outside 终止；`i3_curvilinear_oracle` 459-angle probe oracle 与 SHD 三方 closure、两频三模式 byte-identical；`CS`/`CL` 显式拒绝 | `PARITY` | `F-GEOM`, `R-PARSER`, `R-GEOM`, `R-MODEL`, `STD`, `TEST`, `FP2G-ORACLE` |
| BND-05 — boundary type V/R | 支持 | 支持 | `PARITY` | shared vacuum/rigid cases，parser/component regressions |
| BND-06 — grain `G` | 支持 | 支持 | `PARITY` | shared grain/control cases；raw reflection material 保持 frozen、结果逐频 |
| BND-07 — table `F` + `.trc/.brc` | 支持 | 支持 | `PARITY` | shared top/bottom table/control cases |
| BND-08 — `A` + `.ati/.bty` + LL elastic P/S | 支持 | 支持 | `PARITY` | shared `elastic_ll_top_bottom`，parser/component/oracle 闭环 |
| BND-09 — flat `A` elastic halfspace P/S | 支持 | parser/model、ordinary elastic halfspace P/S 复反射系数逐频求值、`elastic_halfspace_flat` 与 `elastic_halfspace_fluid_control` 共享 case 三方 SHD 比较与 `MINIMUM_SHEAR_EFFECT` shear guard 全部 PASS、两频 `nonreuse/reuse/parallel` byte-identical 与逐频独立性确认 | `PARITY` | `F-PARSER`, `R-PARSER`, `R-MODEL`, `STD`, `TEST`, `FP2G-ORACLE` |
| BND-10 — `G/F` 与 LL 组合 | F2CPP 正式矩阵也不支持 | RayReuse 未支持 | `F2CPP_OUT_OF_SCOPE` | `MATRIX` |

### 4.6 Attenuation

| Feature | F2CPP | RayReuse | Status | Evidence |
| --- | --- | --- | --- | --- |
| ATT-01 — attenuation units N/F/M/Q/L | 支持并有 executable standard cases | parser/转换函数、单元测试及 6 个 `attenuation_unit_*` 共享 case 三方比较全部 PASS，5 kHz 跨单位压强逐位相同 | `PARITY` | `F-PARSER`, `R-PARSER`, `F-TL`, `R-TL`, `STD`, `TEST`, `FP2H-ORACLE` |
| ATT-02 — attenuation unit W | 支持 | 支持 | `PARITY` | 多个共享 RayReuse environment/material case + component/unit regression，4 kHz 频率缩放保护 |
| ATT-03 — Thorp water-column/boundary attenuation | 支持 | 支持 | `PARITY` | shared `constant_speed_thorp` + unit/component regression，单频/smoke/16频回归 SHA-256 逐位不变 |
| ATT-04 — Francois–Garrison | 支持 | 支持，`Environment` 值所有权，20°C 粘滞分支与 FMA 对齐，5 频率锚点与 shared case 三方全 PASS | `PARITY` | `F-PARSER`, `R-PARSER`, `F-TL`, `R-TL`, `STD`, `TEST`, `FP2H-ORACLE` |
| ATT-05 — biological attenuation | 支持 | 支持，`Environment` 不可变共享层所有权，0–200 层支持重叠与端点闭区间，shared case 三方全 PASS | `PARITY` | `F-PARSER`, `R-PARSER`, `F-TL`, `R-TL`, `STD`, `TEST`, `FP2H-ORACLE` |
| ATT-06 — elastic boundary P/S attenuation in current W/LL slice | 支持 | 支持，保持逐频 complex reflection result | `PARITY` | shared elastic LL case、raw projection/component tests |

### 4.7 Products

| Feature | F2CPP | RayReuse | Status | Evidence |
| --- | --- | --- | --- | --- |
| PRD-01 — R：generalized R、directional `.sbp`、active/terminal prefix、explicit Nalpha=1、Origin-compatible writer | 支持 | 在 point/multisource、rectilinear、单频范围支持；多频明确拒绝，irregular receiver 不适用（`R...I` 拒绝） | `PARITY` | `R-PARSER`, `R-PRODUCT`, `R-CLI`, shared `ray_trace_directional_tabulated`, writer/component tests, `FP2F-ORACLE` |
| PRD-02 — A ASCII：Cartesian G/B | 支持 | 在 single/multisource 与 rectilinear/paired-irregular 范围支持；多频逐频独立发布 | `PARITY` | shared hat ASCII/zero、Arrival component/writer tests、`TEST`, `FP2F-ORACLE`, `FP2I-ORACLE` |
| PRD-03 — a Binary：Cartesian G/B | 支持 | 在 single/multisource 与 rectilinear/paired-irregular 范围支持；多频逐频独立发布 | `PARITY` | shared hat Binary/zero、Arrival component/writer tests、`TEST`, `FP2F-ORACLE`, `FP2I-ORACLE` |
| PRD-04 — E Eigenray：Cartesian G/B | 支持 | 在 single/multisource 与 rectilinear/paired-irregular 范围支持；多频逐频独立发布 | `PARITY` | shared Gaussian/zero、Eigenray component/writer tests、`TEST`, `FP2F-ORACLE` |
| PRD-05 — A/a/E ray-centered `g` | 支持；要求至少两个等间距 receiver ranges | parser/model/runtime/Arrival/Eigenray/writer/oracle 闭环；多频逐频独立发布 | `PARITY` | `F-PARSER`, `R-PARSER`, `R-MODEL`, `R-PRODUCT`, `R-CLI`, `STD`, `TEST`, `FP2A-ORACLE`, `FP2B-ORACLE`, `FP2D-ORACLE` |
| PRD-06 — TL/A/a/E irregular receiver product semantics | 支持到 REC-02/REC-03 所述范围；R 本身只使用单 receiver range | SHD `PlotType='irregular '`、paired record 布局与 `receiversPerRange()` workspace 维度、A/a/E paired traversal 已接入 | `PARITY` | `F-MODEL`, `R-MODEL`, `R-PRODUCT`, `R-CLI`, `STD`, `TEST`, `FP2F-ORACLE` |
| PRD-07 — R/A/a/E multisource sequencing/headers | 支持 | SHD header `NSz`+`Sz` 向量与 source-major 寻址；ARR header source count + depths 与 per-source 块（ASCII/binary）；E/R header `1 1 NSz` 与 per-source 段落（source depth 升序）；solver lifecycle per-source（reuse 复用单位 = `(source, frozen fan)`） | `PARITY` | `F-MODEL`, `R-MODEL`, `R-PRODUCT`, `R-CLI`, `STD`, `TEST`, `FP2F-ORACLE` |
| PRD-08 — line-source product scaling | 支持 | 场压强扩散缩放（`-4*sqrt(pi)*beamScale` 全距离）与到达结构幅值缩放（`4*sqrt(pi)`）完全闭环 | `PARITY` | `F-PARSER`, `R-PARSER`, `F-TL`, `R-TL`, `R-PRODUCT`, `source_geometry_line`, `arrival_line_directional_multisource`, `FP2I-ORACLE` |

### 4.8 Support matrix / 声明一致性

| Feature | F2CPP | RayReuse | Status | Evidence |
| --- | --- | --- | --- | --- |
| DOC-01 — production support matrix 与真实 executable surface 同步 | 当前矩阵与 parser/solver/tests 完全一致 | 全量 Feature Parity（FP-1A～FP-2I）支持面已完整同步，支持矩阵与对齐报告保持 0 GAP 一致性 | `PARITY` | `MATRIX`、`TEST`、`FP2B-ORACLE`～`FP2I-ORACLE` 与本报告逐项代码证据对照 |

## 5. RayReuse 当前 execution 范围

| Execution | 当前 production-supported 范围 | 当前明确边界 | 验证事实 |
| --- | --- | --- | --- |
| `nonreuse` | TL-01～TL-14；SRC-01～SRC-04（含 Point/Line 与 Multisource）；REC-01～REC-04（含 Regular 与 Irregular）；SSP-01～SSP-05；BND-01～BND-09；ATT-01～ATT-06；PRD-01～PRD-08 | R 不接受显式 execution mode；3D/N×2D 保持排除 | shared broadband 支持案例通过；每频每源独立 trace + solve/product（nonreuse trace passes = Nfreq×NSz） |
| `reuse` | 与 nonreuse 相同；一次 trace 全部 source fans（每 source 一个 frozen cache）→ 跨频复用 → per-(frequency, source) acoustic/product state | R 拒绝；3D/N×2D 保持排除 | validated 产品与 nonreuse 逐字节相同；cache fingerprint before==after 严格守恒 |
| `parallel` | 与 reuse 相同；frequency workers 只读 const per-source cache vector + serial ordered consumer publish | R 拒绝；3D/N×2D 保持排除 | validated 产品与 nonreuse/reuse 逐字节相同；cache fingerprint before==after 严格守恒 |

## 6. 最终审计结论

### A. 当前完整 GAP 列表

**当前 GAP 数量：0**。
所有 F2CPP production-supported 特性均已在 RayReuse 中实现并具备完整的 parser → model → runtime → product → oracle 证据链。

### B. 按优先级分组

- **P0 — 主功能 / 后续架构**：已清空（0 个）。
- **P1 — 重要 parity gap**：已清空（0 个）。
- **P2 — 外围或证据闭环 gap**：已清空（0 个）。

### C. 边界分类

1. **PARITY（完全对齐）**：
   - 2D Point & Line source geometry（`X`）；
   - Single & Multiple source depths（`NSz ≥ 1`）；
   - Rectilinear & Cartesian paired-irregular receivers（`I`）；
   - Coordinate systems：Cartesian & Ray-centered；
   - Beam families：Cerveny、Geometric Hat、Geometric Gaussian、Simple Gaussian；
   - Beam options：F/M/W width modes、D/S/Z curvature modes、P/V/H component modes；
   - Coherence modes：Coherent `C`、Incoherent `I`、Semi-coherent `S`；
   - SSP backends：C-linear、PCHIP `P`、N²-linear `N`、Cubic Spline `S`、Quadrilateral `Q`/`.ssp`；
   - Attenuation：N/F/M/W/Q/L units、None/Thorp/Francois–Garrison/Biological volume attenuation；
   - Boundaries：Flat V/R/A/G/F、Tabulated `.trc/.brc`、Piecewise-linear LS/LL、Canonical Curvilinear short format `C`、Flat ordinary elastic halfspace P/S；
   - Products：SHD（TL）、R（单频射线轨迹）、A（ASCII 到达）、a（Binary 到达）、E（特征射线）。
2. **F2CPP_OUT_OF_SCOPE（超出 F2CPP 生产范围）**：
   - 3D / Bellhop3D / N×2D；
   - Beam shift；
   - Ray-centered geometric Gaussian；
   - Analytic continuous SSP formulas。
3. **RAYREUSE_DEFERRED / EXTENSION（RayReuse 远期扩展）**：
   - HDF5 容器格式；
   - 多频 R 合并容器；
   - SIMD 向量化加速；
   - Influence Geometry Reuse 与频率插值；
   - BARR 到达结构算法。

### D. 最终封板结论

**Bellhop_F2CPP → Bellhop_RayReuse Production Feature Parity COMPLETE（GAP = 0）**。
