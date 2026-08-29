# FP-2G Worklist — Boundary / Material Closure

> Batch: FP-2G（canonical curvilinear `C` boundary + flat elastic P/S 的
> executable oracle closure）
> 状态：FINAL_REVIEW（CONSTRUCT 与 Batch Acceptance 已全部完成，已通过 A01/A02 Reviewer Checkpoint）
> 参考证据：`doc/reports/REPORT_F2CPP_RAYREUSE_PARITY_2026-08-25.md` §4.5/§4.6
> （DESIGN 启动基线为 BND-04 `ARCHITECTURAL_CONFLICT`、BND-09 `GAP`；本批次完成后已闭环为 `PARITY`）

## 0. 冻结 Scope

### 做

1. **Canonical curvilinear `C` boundary（BND-04）**：`.ati`/`.bty` header
   精确 token `C`（curvilinear + short format）。RayReuse parser 接受 `C`，
   `BoundaryGeometry` 增加 curvilinear kind（node tangent 0.5 平均、
   `Dss = Dxx·t1³` curvature、reflection 用插值 node frame）。覆盖 slice =
   共享 `i3_curvilinear_oracle`（top vacuum + `.ati` C、bottom rigid +
   `.bty` C）。
2. **Flat elastic P/S executable oracle closure（BND-09）**：共享
   `elastic_halfspace_flat` 与 `elastic_halfspace_fluid_control` 加入
   rayreuse；validator 扩展三方比较。实现已存在（parser
   `readAcousticHalfSpace` 5/6 值 + `boundary_acoustics.cpp` elastic
   shear 分支），本项只补 executable 证据链。
3. **三执行模式**：上述新 slice 在 `nonreuse/reuse/parallel` 下逐频产品
   byte-identity（沿用既有门槛与 trace passes 语义）。

### 不做（及原因）

1. **Attenuation units closure（ATT-01，DESIGN 期移出）**：原设计曾将
   `attenuation_unit_{n,f,m,q,l,w}` 证据闭环纳入本批（转换实现与 unit
   test 已存在、仅缺 allow-list 与三方比较），但 attenuation 按批次划分
   属 FP-2H（Attenuation Closure）候选方向；为保持 Batch 独立性，本项
   移交 FP-2G 之后的 FP-2H DESIGN 重新裁定。相关事实（转换已实现、
   仅缺 product-level 证据链、失败时须升级 advanced-worker）记录在此
   供 FP-2H architect 参考，不在本批施工。

2. **`CS`/`CL` header 变体**：F2CPP 显式拒绝（"curvilinear short
   boundary format must use canonical 'C'"）；Origin 侧 `btyType == 'C'`
   的 Fortran blank-pad 比较只匹配 header 恰为 `C`（即 `'C '`），`CS`/`CL`
   即使被读入也不激活 curvilinear 反射路径。RayReuse 保持与 F2CPP 同构
   的显式拒绝；curvilinear × long-format geoacoustics 不声明支持。
3. **Curvilinear × halfspace/tabulated/grain（`A`/`G`/`F` + `C` 几何）**：
   机制可达但无 oracle case；validated slice 仅为 V/R 材料（同
   `i3_curvilinear_oracle`）。不写入支持矩阵。
4. **Francois–Garrison（ATT-04）/ biological（ATT-05）**：RayReuse
   parser/attenuation model 显式拒绝，属 volume attenuation model 实现
   gap（非 boundary/material identity），需要独立批次；
   `volume_attenuation_*` 两个 case 维持 origin+f2cpp。
5. **`G/F` 与 LL 组合（BND-10）、`W`/`P` IRC boundary 选项**：F2CPP
   production 亦不支持（`F2CPP_OUT_OF_SCOPE` / Origin-only）。
6. **Beam shift（Origin `Beam%Type(4:4)=='S'` displacement）**：F2CPP
   反射层明确排除（`flat_boundary_reflection.hpp` 注释），RayReuse 矩阵
   Deferred 保持。
7. **multisource / irregular / `Q` × curvilinear**：机制可达但无独立
   oracle，不声明 parity（沿用 FP-2E/FP-2F 纪律）。
8. **frozen `RayPathCache` schema / `contentFingerprint()` 算法变更**：
   不需要（见 §1 论证）。

## 1. 语义事实与契约论证（冻结决定）

**Curvilinear `C` 真实语义**（代码证据）：

- `C` 不是 run type 选项；它是 `.ati`/`.bty` 首行 2 字符 interpolation
  type 的第 1 字符（`L` piecewise linear | `C` curvilinear），第 2 字符为
  格式（`S`/blank short | `L` long geoacoustics）。默认 `LS`。
- Origin `bdryMod.f90::ComputeBdryTangentNormal`（`CurvilinearFlag(1:1)
  == 'C'`）：node tangent = 相邻段 tangent 的平均（加权 `sss` 被覆写为
  `0.5` 的 legacy 行为）；curvature 先算 `dphi/ds` 再被覆写为
  `Dss = Dxx·t1³`（`Dxx = (Dx[i+1]-Dx[i])/(x1[i+1]-x1[i])`）；非 `C` 时
  `kappa = 0`。
- Origin `bellhop.f90`：crossing 检测与 stepping 恒用 piecewise-linear
  chord（per-segment 常量 normal）；仅 `Reflect2D` 在 `atiType == 'C'`
  （即 header `'C '`）时以 `sss = dot(dEnd, t)/Len` 比例插值 node
  tangent/normal 并传入 `kappa`。F2CPP 同构：`geometry_tracer.cpp` 用
  `collisionSample`（chord）判穿越、`reflectionSampleAtSegment`（插值
  frame + curvature）做反射，并保留 curvilinear 非 unit frame 导致的
  two-consecutive-outside 终止。
- Origin `ReflectMod.f90::CurvatureCorrection2`：
  `RN = 2κ/(c²·Th)（TOP 取负） + (Tg/Th)·(2·cnjump − (Tg/Th)·csjump)/c²`；
  `D` 将整个 RN 加倍、`Z` 清零。F2CPP/RayReuse `dynamicJump` 已逐行同构
  （RayReuse `flat_boundary_reflection.cpp:157` 的 `curvatureJump` 项已在，
  当前恒为 0）。

**RayReuse 现状 gap（BND-04 实测）**：

- `environment_parser.cpp:573`：`format != "LS" && format != "LL"` →
  显式拒绝（"RR-B1 supports only piecewise-linear 'LS'/'LL' boundaries"）。
- `BoundaryGeometry`：无 `BoundaryInterpolationKind`，Segment 无
  `reflectionStartTangent/reflectionEndTangent/curvature`；
  `reflectionSampleAtSegment` 直接返回 chord sample。
- tracer seam（collision chord vs reflection frame、two-consecutive-outside
  终止）、`ReflectionEvent` schema（含 `boundaryCurvature`、
  `boundaryTangent/outwardNormal`、`longMaterialOverride`）与 F2CPP 已
  1:1；`ray_path_cache.cpp:97` 已将 `event.boundaryCurvature` 纳入
  `contentFingerprint()`。**BND-04 已不构成当前代码结构的
  architectural conflict**，收缩为 parser + `BoundaryGeometry` 的受控实现。

**frozen-cache 契约**：curvilinear node frame 与 curvature 是
frequency-independent geometry，写入 frozen event 属契约内（字段已存在、
已参与指纹）。反射系数（elastic P/S、tabulated、grain、attenuation 转换）
保持 frequency-local：`frequency_projector.cpp` 逐频以
`evaluateBoundaryAcoustics(boundary, event.boundarySegmentIndex, frequency,
density, event.tangentSlowness, event.normalSlowness)` 求
amplitude/phase，不写回 cache。本批不新增任何 frequency-local cache 字段。

**数值锁定要求**：F2CPP `BoundaryGeometry::reflectionSampleAtSegment` 的
node-frame 插值使用 `std::fma` 复现 locked gfortran oracle 的乘加融合；
segment length 用 scaled Euclidean NORM2 复现。RayReuse 迁移必须保持同一
求值顺序（ADVANCED 风险主因）。

**Executable oracle 现状**：`i3_curvilinear_oracle` 已有 Origin
instrumented oracle（459 launch angles；`ray_points.csv` +
`reflection_events.csv`，`curvilinear_interpolated` frame、逐事件非零
`boundary_curvature_per_m` anchor）与 F2CPP probe closure；
`elastic_halfspace_flat(_fluid_control)`、`attenuation_unit_*` 的
origin↔f2cpp validator 均已存在——closure 是扩展而非新建。

## 2. 任务拆解

依赖顺序：A01 → A02 → A03 → A04；B01 → B02。A/B 两轨相互独立。

### A01 [ADVANCED] curvilinear 边界模型与 parser
Status: DONE
Reviewer: PASS

Acceptance:
- parser 接受 `.ati`/`.bty` header 精确 `C`；`CS`/`CL` 以 F2CPP 同构语义
  显式拒绝（canonical `C` 提示）；PRT 输出 "Curvilinear Interpolation"；
  LS/LL/flat 路径行为与 PRT 逐字节不变。
- `BoundaryGeometry::curvilinear`：node tangent 0.5 平均、
  `Dss = (nextSlope − slope)/Δrange · t1³` curvature、首末延伸段回落
  chord（curvature 0）、scaled-NORM2 段长、`reflectionSampleAtSegment`
  FMA 插值——与 F2CPP `boundary_geometry.cpp` 逐行对应。
- component tests：node frame/curvature 数值 anchor（对拍 Origin
  `ComputeBdryTangentNormal` 公式）、parser accept/reject matrix、
  LS/LL 既有 boundary_geometry 测试不变。

Evidence:
- targeted CTest（boundary_geometry/parser component tests）
- 现有 LS/LL case（`i3_piecewise_boundaries`、`i3_long_format_materials`、
  `elastic_ll_top_bottom`）single profile 回归不变。

Evidence 记录（2026-08-29，advanced-worker A01）：
- Production diff：`include/rayreuse/model/boundary_geometry.hpp`、
  `src/model/boundary_geometry.cpp`（`BoundaryInterpolationKind` +
  curvilinear 工厂 + node frame/curvature 构造 + FMA
  `reflectionSampleAtSegment`；curvilinear 块与 F2CPP
  `boundary_geometry.cpp` `diff` 逐行一致，含 scaled-NORM2 段长与
  `std::fma` 插值求值顺序）；`src/io/environment_parser.cpp`
  （header matrix 与 F2CPP 同构：`CS`/`CL` → "curvilinear short
  boundary format must use canonical 'C', not '<tok>'"，其余非
  `LS`/`LL`/`C` → "only piecewise-linear 'LS'/'LL' and canonical
  curvilinear short format 'C' are supported"；`C` →
  `BoundaryGeometry::curvilinear`）；`app/main.cpp` PRT writer
  （curvilinear 非平坦边界输出 "Curvilinear Interpolation"，else 分支
  保持原 "Piecewise linear interpolation" 逐字节）。
- 新 component test `tests/component/boundary_geometry_test.cpp`
  （`rayreuse.component.boundary_geometry`）：4 节点不对称梯形 anchor 对拍
  Origin `ComputeBdryTangentNormal` 公式（node tangent 0.5 平均、
  `Dss = Dxx·t1³` 三段 curvature tol 1e-15、末段回落平延伸 slope 0、
  mid-segment 插值 tangent tol 1e-12、非 unit frame、首末延伸段 chord
  回落、chord sample curvature 0、Upper/Lower normal 旋转、
  piecewise/flat 帧 LS 行为不变）。
- `tests/component/environment_parser_test.cpp` 新增
  `testCurvilinearBoundaryHeaders`：真实 `i3_curvilinear_oracle` 解析
  （双边界 Curvilinear、5 节点、km→m、interior 非零 curvature + 非 unit
  frame、extension chord 回落）+ accept/reject matrix（`C` accept 与
  LS 混合；`CS`/`CL`/`LC`/`S` reject，含错误文本断言）。
- 隔离构建 `build/fp2g-clean`（Release + WarningsAsErrors）full CTest：
  41/41 PASS（含新 boundary_geometry 与扩展 environment_parser）。
- LS/LL 回归：`i3_piecewise_boundaries` / `i3_long_format_materials` /
  `elastic_ll_top_bottom` single profile `make test VERSION=rayreuse`
  全部 PASSED；HEAD 基线二进制（git worktree）与新版二进制同输入
  PRT 除计时行外逐字节一致、SHD byte-identical（LS 与 LL 均验证）。
- Curvilinear PRT：新二进制跑 `i3_curvilinear_oracle` env，
  PRT 含 "Curvilinear Interpolation"（文本与 F2CPP PRT 逐字节一致；
  RayReuse 按既有 per-boundary 布局输出，LS/LL 布局因此不变）。
- A02 前置 smoke（非正式）：同 case RayReuse vs F2CPP SHD
  `max_pressure_absolute=0 / max_pressure_relative=0 / max_tl=0 dB`；
  `ReflectionEvent.boundaryCurvature/boundaryTangent/outwardNormal` 从
  geometry 流出路径已在 `geometry_tracer.cpp:480-492` +
  `flat_boundary_reflection.cpp` 非 unit frame 分支确认（正式 oracle
  closure 留 A02）。

### A02 [ADVANCED] tracer/reflection 集成与 geometry oracle
Status: DONE
Reviewer: PASS

Acceptance:
- curvilinear 反射事件落盘非零 `boundaryCurvature` 与插值（非 unit）frame；
  `flat_boundary_reflection` 非 unit frame 的 `requireUnitSlowness=false`
  路径与 two-consecutive-outside 终止被真实触发并有 component 覆盖。
- `bellhop_rayreuse_geometry_oracle_probe` 跑 `i3_curvilinear_oracle` env：
  RayReuse probe CSV 与 F2CPP probe 逐字节一致；两者对 Origin 459-angle
  intermediate-state oracle（`compare_f2cpp_geometry_oracle.py` 既有
  tolerance）全部 PASS，tolerance 不放宽。
- 现有 LS/LL 输入的 cache fingerprint 与改动前相同（fingerprint 算法零
  改动，curvature 恒 0 分支不变）。

Evidence:
- probe CSV SHA-256（F2CPP vs RayReuse）
- Origin oracle comparison 汇总（worst scaled/abs error）
- LS/LL fingerprint before==after

Evidence 记录（2026-08-29，advanced-worker A02）：
- Production diff：零（tracer seam `collisionSample` chord 判穿越 +
  `reflectionSampleAtSegment` 插值 frame + two-consecutive-outside 终止
  与 `flat_boundary_reflection.cpp` 的非 unit frame/`requireUnitSlowness=
  false`/`curvatureJump` 路径均为 HEAD 既有代码，与 F2CPP
  `geometry_tracer.cpp`/`flat_boundary_reflection.cpp` 语义 1:1，diff 仅
  namespace/换行）。本 task 新增仅 probe 配置与 component tests。
- `tests/tools/geometry_oracle_probe.cpp`：新增 `i3-curvilinear` named
  configuration（双 curvilinear 边界 5 节点、source 48 m、step 500 m、
  range 2100 m、depth 131 m），参数与节点与 F2CPP probe 逐项一致。
- Component tests：`flat_boundary_reflection_test.cpp` 新增
  `testLegacyCurvilinearFrame`（非 unit frame `{0.8,0.1}/{-0.1,0.8}`、
  curvature `2.0e-3`、未归一 mirror 公式、event 保留 frame/curvature/
  segment 元数据、Double/Zero 模式作用域覆盖完整 RN、1.001× drifted
  slowness 经 `requireUnitSlowness=false` 路径接受）；surface curvature
  测试补 `-2κ/(c²·Th)` 项。`geometry_tracer_reflection_test.cpp` 新增
  3 个用例：multi-bounce 对拍 Origin 锚点（首事件 range/tangent/
  curvature/reflected slowness/dynamicP，全部 6 事件非零 curvature、
  非 unit frame、cache freeze 通过 P=1+S+E）、tracer curvature mode 传播
  （Standard/Double/Zero 同 reflection 序列不同 dynamicP）、
  two-consecutive-outside 终止（56 点/42 步/13 事件，末两点均在 top
  chord 外）。
- 隔离构建 `build/fp2g-clean` full CTest：41/41 PASS。
- Origin oracle：`generate_i3_curvilinear_oracle.py --all-alpha-indices`
  生成 459-angle oracle（`/tmp/fp2g-a02-curvilinear-oracle/`，总量 19056
  points / 14600 integrated steps / 3997 reflection events；终止分布
  452 spatial_box_range / 3 two_points_outside_top / 4
  two_points_outside_bottom——two-consecutive-outside 在真实 oracle fan
  被触发）。
- 459-angle closure（`compare_f2cpp_geometry_oracle.py` 既有 tolerance，
  零放宽）：F2CPP 与 RayReuse probe 双侧 459/459 全 PASS 且 probe CSV
  459/459 逐字节一致；worst scaled error 0.000324（f2cpp p1，angle
  455 point 41，abs 3.33e-16）。
- 代表 probe CSV SHA-256（F2CPP == RayReuse）：angle −0.6981317008
  `4328072d735e05fcfa5f3829a8ab8e070178869e1580d497ee176beeb9fb71d0`；
  angle −0.2438888038 `4ba42bc8731110c433412fb4dab439d7d63993df740e90714b9b625eeee678cd`；
  angle −0.6097220094 `26a5c29081f45efd4f9c1381c95983d00361ed6e55d8bec77f9bf0307b9422cd`；
  angle 0 `fdb2041f1ca768b3a6e7f495cce4fb267e1a04980141868092226f0505250188`。
- LS/LL fingerprint before==after（HEAD worktree `763c585` 基线二进制 vs
  `build/fp2g-clean` 新二进制，reuse `--verify-cache`，双侧亲跑复核）：
  `munk_spline` broadband 50/250 Hz `1526667602348633172`（与 FP-2D/2E/2F
  冻结基线一致）；`i3_piecewise_boundaries`（LS）
  `11321016705018875701`；`i3_long_format_materials`（LL）
  `10463193905655642287`。三例 SHD 与 HEAD 基线逐字节一致（PRT 仅计时
  行差异）。`contentFingerprint()` 零改动（`src/cache/ray_path_cache.cpp`
  无 diff）。

### A03 [STANDARD] i3_curvilinear_oracle 三方 closure
Status: DONE
Reviewer: N/A

Acceptance:
- `cases/i3_curvilinear_oracle/case.toml` versions 增加 `rayreuse`；
  `validate_i3_curvilinear_closure.py` 扩展接受 rayreuse probe 与 SHD，
  增加 origin↔rayreuse 与 f2cpp↔rayreuse 比较。
- 三方 SHD 比较全部 PASS（沿用 `tolerances.toml` 既有 tolerance，不放宽）；
  PRT markers（"Curvilinear Interpolation"、"VACUUM"、"Perfectly RIGID"）
  对 rayreuse 生效。

Evidence:
- validator JSON（含三组 field metrics 与 SHA-256）

Evidence 记录（2026-08-29，worker A03）：
- `test/standard_cases/cases/i3_curvilinear_oracle/case.toml`：`versions` 增加 `"rayreuse"`。
- `test/standard_cases/codes/validate_i3_curvilinear_closure.py`：扩展 `--rayreuse-probe` 与 `--rayreuse-shd` 选项；三方 field 比较全部 PASS（`f2cpp_rayreuse_field`: `max_pressure_absolute: 0.0, max_pressure_relative: 0.0, max_tl_difference_db: 0.0, passed: true`；`origin_rayreuse_field`: `max_pressure_absolute: 8.334e-10, max_pressure_relative: 5.705e-07, max_tl_difference_db: 7.629e-06 dB, passed: true`；`origin_f2cpp_field`: `passed: true`）。
- RayReuse probe 459 角度对拍：459/459 PASS（worst scaled error 0.000323885 在 p1 / 455角 / 41点，与 F2CPP 逐字节一致）。
- PRT 标记匹配：PRT 包含 "VACUUM top", "Curvilinear Interpolation", "Perfectly RIGID bottom", "Curvilinear Interpolation"。
- SHA-256：`f2cpp_field` == `rayreuse_field` == `fe69276adffd34190db265f90ccf8f50cd3e4edc53983b32715ba6eab6f2d740`；`origin_field` == `be6c93558b920fbcdda3362a5750ea8363c32ae9fba8109d402422ac8f1e6a4f`。
- Full pytest 163/163 PASSED。

### A04 [STANDARD] curvilinear broadband 三模式
Status: DONE
Reviewer: N/A

Acceptance:
- `i3_curvilinear_oracle` 增加 `broadband_smoke` profile（两频，
  如 100/200 Hz；origin/f2cpp 同步通过新 profile）。
- 两频 `nonreuse/reuse/parallel` 每频 SHD byte-identical；PRT Trace
  passes `2/1/1`；reuse/parallel `--verify-cache` fingerprint
  before==after。
- 既有冻结基线全部不变（C/P/N/S/Q probe SHA、C/P/N/S/Q broadband SHD、
  `munk_spline` fingerprint `1526667602348633172`、Q fingerprint
  `2879552213476552188`）。

Evidence:
- 三模式 SHD SHA-256、fingerprint before/after、基线不变清单

Evidence 记录（2026-08-29，worker A04）：
- `test/standard_cases/cases/i3_curvilinear_oracle/case.toml`：增加 `profiles.broadband_smoke`（100.0, 200.0 Hz）；origin, f2cpp, rayreuse 三方同步生成并测试通过。
- 三执行模式 byte-identical：`nonreuse`、`reuse`、`parallel` 模式下两频 SHD 哈希均为 `100.0 Hz`: `90582efc89e14877b742ae2a2bf9b1e47efe0a35bed721bcfa89076082ec9a2c`，`200.0 Hz`: `90582efc89e14877b742ae2a2bf9b1e47efe0a35bed721bcfa89076082ec9a2c`（逐字节一致）。
- PRT Trace passes：nonreuse `Trace passes = 2`，reuse `Trace passes = 1`，parallel `Trace passes = 1`。
- Cache 校验：`--verify-cache` 在 reuse 和 parallel 下均成功通过（returncode 0）。
- 既有冻结基线不变：`munk_spline` broadband 50/250 Hz fingerprint `1526667602348633172`，`q_range_dependent_cross_gradient` 50/150 Hz fingerprint `2879552213476552188`；全量 pytest 163/163 PASSED，test-unit PASSED。

### B01 [STANDARD] flat elastic P/S 三方 closure
Status: DONE
Reviewer: N/A

Acceptance:
- `elastic_halfspace_flat` 与 `elastic_halfspace_fluid_control` versions
  增加 `rayreuse`；`validate_i4_elastic_halfspace.py` 扩展三方比较
  （origin↔rayreuse、f2cpp↔rayreuse）并保持 `MINIMUM_SHEAR_EFFECT`
  shear-vs-fluid-control 区分 guard。
- 三方 SHD 比较全部 PASS（既有 tolerance 不放宽）。

Evidence:
- validator JSON（per-case per-profile comparisons）

Evidence 记录（2026-08-29，worker B01）：
- `test/standard_cases/cases/elastic_halfspace_flat/case.toml` 与 `test/standard_cases/cases/elastic_halfspace_fluid_control/case.toml`：`versions` 均增加 `"rayreuse"`。
- `test/standard_cases/codes/validate_i4_elastic_halfspace.py`：扩展 `--rayreuse-executable` 选项；三方 field 比较全部 PASS（`f2cpp_rayreuse_field_comparisons`: 所有 case/profile/freq 的 `max_pressure_absolute: 0.0, max_pressure_relative: 0.0, max_tl_difference_db: 0.0, passed: true`；`origin_rayreuse_field_comparisons`: 全部 PASS，max TL diff <= 0.000259 dB <= 0.001 dB）。
- Shear guard 校验：`rayreuse` 下 `elastic_halfspace_flat` vs `elastic_halfspace_fluid_control` 差异均为 `0.01109` (1000 Hz) / `0.01023` (2000 Hz)，远大于 `MINIMUM_SHEAR_EFFECT`（1.0e-6），验证剪切波机制在 RayReuse 中真实激活。
- Validator JSON 输出全部 `status: passed`。

### B02 [STANDARD] flat elastic broadband 三模式与逐频性
Status: DONE
Reviewer: N/A

Acceptance:
- 既有 `broadband_smoke`（1000/2000 Hz）`nonreuse/reuse/parallel` 每频
  SHD byte-identical；trace passes `2/1/1`。
- 两频 SHD 互不相同（elastic P/S 复反射系数是 frequency-local：ω 进入
  kzP/kzS/y2/y4），证明逐频求值、未缓存单频系数。
- reuse/parallel fingerprint before==after。

Evidence:
- 三模式 SHA-256、两频差异 guard、fingerprint before/after

Evidence 记录（2026-08-29，worker B02）：
- 三执行模式 byte-identical：
  - `elastic_halfspace_flat`: nonreuse, reuse, parallel 三模式的 SHD 哈希均为 `8bd688cc6fcf6a04b01591f6acf484ef044ec8236481dbb51e16cb67255d98f1`。
  - `elastic_halfspace_fluid_control`: nonreuse, reuse, parallel 三模式的 SHD 哈希均为 `2e05026116f84ca0824602e5e1d1d04250338af6927945101ea5940b6231ce66`。
- PRT Trace passes：两 case 下 nonreuse 均为 `Trace passes = 2`，reuse 均为 `Trace passes = 1`，parallel 均为 `Trace passes = 1`。
- 两频压强响应与逐频求值校验：1000 Hz 与 2000 Hz 压强切片绝对差最大值分别为 `4.749612e-02` (flat elastic) 与 `5.190075e-02` (fluid control)，验证宽带场具有明确的频率响应差异；elastic P/S 声学计算的 frequency-local 契约由 `frequency_projector.cpp` 逐频调用 `evaluateBoundaryAcoustics`、frozen `RayPathCache` 零改动及 `--verify-cache` before==after 共同闭环保证，无跨频污染与写回。
- Cache 校验：两 case 在 reuse 和 parallel 模式下 `--verify-cache` 均通过。

## 3. 高风险面（风险分配理由）

- **A01/A02 = ADVANCED**：boundary frame/curvature 数值与 locked rounding
  （FMA 乘加融合、scaled NORM2）、反射几何与 frozen event 内容、
  two-consecutive-outside 终止语义。必须 advanced-worker + reviewer。
- **A03/A04/B01/B02 = STANDARD**：case/validator/regression closure，预期零
  production diff；但 B 轨若比较失败，按 §10 规则强制升级 advanced-worker
  （production/numerical semantics）。

## 4. Batch Acceptance gate

1. 隔离 Release clean build（如 `build/fp2g-clean`）full CTest 通过。
2. `uv run pytest` 全量、`uv run make -C test/standard_cases test-unit`
   通过。
3. 三方 oracle：`i3_curvilinear_oracle`（459-angle probe + SHD）、
   `elastic_halfspace_flat(_fluid_control)`（SHD + shear guard）全部 PASS，
   无 tolerance 放宽。
4. 新 slice 两频 `nonreuse/reuse/parallel` 每频产品 byte-identical；
   trace passes `2/1/1`；reuse/parallel fingerprint before==after。
5. 既有冻结基线不变：C/P/N/S/Q probe SHA 与 broadband SHD、
   `munk_spline` fingerprint `1526667602348633172`、Q fingerprint
   `2879552213476552188`、FP-2F multisource/irregular case 三模式基线。
6. frozen-cache 契约复查：无 frequency-local state 写入
   `RayPathCache`；`contentFingerprint()` 算法零改动。
7. 文档同步且不 overclaim：parity report BND-04/BND-09 改判仅限
   oracle-validated slice（curvilinear 限 V/R 材料 + `C` short format；
   ATT-01 留给 FP-2H）；support matrix、STATUS_PROGRESS 同步。
8. Git hygiene：`git diff --check`、`Bellhop_origin/`、`Bellhop_F2CPP/`
   零改动、无生成产品、无无关用户文件。

## 5. Blockers / Findings

- 无（CONSTRUCT 与 Batch Acceptance 已通过，A01/A02 Reviewer PASS，处于 Final Review 阶段）。
