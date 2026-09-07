# FP-2E Quadrilateral SSP Batch Report

> 批次：FP-2E — Quadrilateral range-dependent SSP（`Q`/`.ssp`）parity
> 施工基线：`6b3428c1adc145fe9659bc6504f9eaf3b545bd7f`（施工起点工作树干净，
> `git status --short` 为空，见 worklist §1.1）
> 合同：`doc/worklists/FP-2E_QUADRILATERAL_SSP_WORKLIST.md`
> （架构合同 FP-2E-A 已完成外部 Contract Final Review，结论 `FP-2E-A ACCEPTED`，
> reviewer 为 Gemini 3.7 Flash；本 worklist 记录获批条款）
> 报告状态：施工与批次验证完成，待独立 Final Review。本报告不声明
> `FP-2E ACCEPTED`。

## A. Completed Tasks

| Task | 角色 | 状态 | 验收证据 |
|---|---|---|---|
| A01 [ADVANCED] — Q grid、`.ssp` reader 与双 evaluator | advanced-worker | 完成，reviewer mechanical-diff review PASS | grid validation/exact-node/outside rejection/bilinear/`cr/cz/crz`/density/frequency-local imaginary/dispatch 组件测试；Release CTest 37/37；`A01 CHECKPOINT: PASS`（2026-08-28） |
| A02 [ADVANCED] — stepper/limiter/tracer 二维集成 | advanced-worker | 完成，reviewer mechanical compare PASS | 双 hint `stepRay` 与 F2CPP 逐字节一致；gradient jump depth-priority/corner single-jump/singular 算术一致；`ray_path.hpp`/`ray_path_cache.*`/field/solver diff 0 字节；Release CTest 37/37；`A02 CHECKPOINT: PASS`（2026-08-28） |
| G01 [GENERAL] — Q geometry/CLI/oracle closure | worker | 完成，reviewer 亲验 PASS | probe 与冻结基线 `4e22fd05…` byte-identical（715 points/714 steps/ExitedDomain）；Origin intermediate oracle PASS（worst scaled `1.06e-9`）；C/P/N/S probe SHA match；缺 `.ssp` EXIT=1；`G01 CHECKPOINT: PASS`（2026-08-28） |
| G02 [GENERAL] — broadband/products/reuse | worker | 完成 | 三模式 Trace passes 2/1/1、fingerprint `2879552213476552188` 前后不变、三模式 SHD 逐字节一致；五产品 8 对文件 f2cpp=rayreuse byte-identical；Origin final-field 12 comparison 全 PASS、tolerance 未动；C/P/N/S 冻结值全 match；新增 `testQuadrilateralFrozenPathProjection`（2026-08-28） |
| G03 [GENERAL] — 文档收口、full validation、Batch Report | worker (ZCode subagent) | 完成（本报告） | 3 个文档更新 + 隔离 clean build full validation 全部通过（J 节）+ git checks（M 节）+ 本报告 |

## B. Architecture Contract

- 合同状态：`FP-2E-A ACCEPTED`（外部 Gemini 3.7 Flash Contract Final Review，
  于批次开工前完成；本批次按获批条款施工，未重设架构）。
- `RayPath` unchanged：A02 reviewer 确认 `ray_path.hpp` diff 为 0 字节。
- `RayPathCache` unchanged：A02 reviewer 确认 `ray_path_cache.*` diff 为 0 字节；
  `contentFingerprint` 算法与 freeze contract 未改；G02 三模式 `--verify-cache`
  before==after 为直接证据。
- `rangeSegmentIndex` transient only，仅出现在合同允许的四处：
  1. `SoundSpeedSample::rangeSegmentIndex`（transient sample，含 freeze-contract 注释）；
  2. `StepLimitRequest::initialRangeSegmentIndex`；
  3. `RayStepResult::rangeSegmentIndex`；
  4. `GeometryTracer` local variable。
  未加入 `RayState`、`StepQuadrature`、`ReflectionEvent`、`RayPath`、`RayPathCache`。
- 非 Q profile 不分配 quadrilateral grid heap storage（`SoundSpeedProfile` 的 grid
  为 `std::shared_ptr<const QuadrilateralSspGrid>`，默认空）。
- `.ssp` 缺失、维度不匹配 → explicit failure，无 fallback；本批次 clean executable
  实测缺 `.ssp` EXIT=1（诊断 `unable to open quadrilateral SSP file`）。
- 无 `ARCHITECTURE_BLOCKER`。

## C. F2CPP Migration

机械迁移范围（F2CPP production → RayReuse，逐文件）：

- `model/quadrilateral_ssp.*`：`QuadrilateralSspGrid`/`SharedQuadrilateralSspGrid`
  shared ownership、depth slope precomputation、bilinear、`cr/cz/crz`、`crr=czz=0`、
  density depth 插值、range/depth locator（hinted、internal exact node 选右 cell、
  最终 range cell 闭区间）。
- `model/quadrilateral_frequency_ssp.*`：real `c(r,z)` 来自 2D matrix；imaginary 由
  `.env` reference depth profile 逐节点按目标频率转换后仅沿 depth 插值；按 F2CPP
  顺序逐频 conversion；imaginary finite 且 non-negative 校验；两频独立、重复
  evaluation bit-stable。
- `model/environment.*`：grid struct/构造签名/`quadrilateralRealSoundSpeedAt` 与
  F2CPP 相同 surface；`validateQuadrilateralGrid` 严格验证（range count ≥2、
  strictly increasing、finite、positive real sound speed、shape、overflow guard、
  km→m 转换）。
- `io/environment_parser.cpp`：`readQuadrilateralSspGrid` 语义迁移，`Q` + 同根
  `.ssp` dispatch；原 RayReuse 对 `Q` 的显式拒绝诊断改为接通。
- `ray/ray_stepper.cpp`：双 hint `stepRay`（predictor/midpoint/endpoint 传递
  depth+range hint）、`crossedDepthSegment`/`crossedRangeSegment`（depth
  `tan(alpha)=tr/tz`、range `tan(alpha)=-tz/tr`、depth 优先 corner 单次 jump、
  singular 算术无 epsilon/clamp）。
- `ray/geometry_tracer.cpp`：`initialRangeSegment`（source 经
  `locateRangeSegment(sourcePosition.range, 0)`）、每步消费
  `RayStepResult::rangeSegmentIndex`（tracer local）；source speed 按 F2CPP 从
  `Vec2{0, sourceDepth}` 真实二维采样。
- `GeometryStepLimiter`：有效 horizontal segment =
  `max(top.min, bottom.min, SSP.minimumRangeForSegment)` /
  `min(top.max, bottom.max, SSP.maximumRangeForSegment)`；trial 越界
  `reduceAtRange` 落到 grid line；`minimumStep = 1.0e-3 × nominalStepLength`
  逐字保留。

有意差异（全部为 RayReuse 侧既定接口适配，非数值语义差异）：

- A01 侧 4+1 项：
  1. `convertAttenuation` 使用 RayReuse 3 参签名（F2CPP 为 4 参 +
     `VolumeAttenuation`；RayReuse `RawAttenuation` 自带 volume model）；
  2. `QuadrilateralFrequencySsp` 不提供 `VolumeAttenuation` 构造重载
     （RayReuse 无该类型）；
  3. namespace/include 路径（`bellhop::`→`rayreuse::`）；
  4. 行宽/换行格式（RayReuse 80-col wrap）；
  5. （+1）头文件迁移溯源注释（"migrated line by line from the Bellhop F2CPP
     production implementation" 与 attenuation conversion 说明）。
- A02 侧：保留既有单 depth-hint `stepRay` overload（range hint 恒为 0）服务
  C/P/N/S 调用方；`GeometrySspEvaluator`/`FrequencySspEvaluator` 的 range API 对
  非 Q backend 显式拒绝非零 range index 并返回 ±∞ range bound（无 silent
  fallback）。
- F2CPP 数值语义零改动：本批次 `git diff -- Bellhop_F2CPP Bellhop_origin` 为空。

## D. Origin Oracle

- Geometry/intermediate（`generate_i5_q_oracle.py` ALPHA_INDEX=150 对应角度
  `-0.002626749710359303` rad；G03 于 clean executable 重跑
  `intermediate_state_matrix.py --case q_range_dependent_cross_gradient`）：
  PASSED；Origin ray 715 points/714 steps/0 reflection events/termination
  `spatial_box_range`；worst comparison `t_z_s_per_m`@point 131，absolute
  `1.0587911840678754e-22`、scaled `1.0583956015444314e-09`；C++ 两侧 probe
  CSV byte-identical（SHA 见 E 节）。
- Final field（既有 `validate_i5_quadrilateral_ssp.py` policy，G02 运行
  `i5_validation_rayreuse.json`）：12 个 field comparison 全 PASS（6 个
  Origin→F2CPP + 6 个 Origin→RayReuse；两 case × single/broadband_smoke ×
  1000/2000 Hz）：
  - `q_range_dependent_cross_gradient`：worst TL `1.52587890625e-05 dB`
    （tolerance `0.001 dB`，`codes/tolerances.toml`）；
  - `q_range_independent_control`：worst TL `0.01125335693359375 dB`
    （既有 `tolerances_i5_q_control.toml` `0.02 dB`，control 为 effect/no-op
    guard，coherent 近零 cell 放大 sub-micropressure 差异）。
  - tolerance 未放宽、未修改；control 宽容差为既有 policy，非本批次引入。
- 本批次未修改任何 `Bellhop_origin` production code。

## E. Q Geometry

- F2CPP/RayReuse `i5-quadrilateral` probe（相同 launch/config：source 50 m、
  depths [0,100]、ranges [0,350,800]、2×3 speeds、step 1.0、range limit 710、
  depth limit 101、vacuum/rigid）CSV byte-identical，SHA-256
  `4e22fd057eeca5dcabca171aeeb9129fba09e7616c0d8fdb5621f26c6029d32f`
  （715 points/714 integrated steps/0 reflections/ExitedDomain）。G01 双侧生成、
  G02 复跑与 G03 clean-build probe（oracle 角度重跑）三度确认同一 SHA。
- 两级 grid line landing：depth grid line 与 range grid line 同时作为 step
  limit；limiter 的 SSP range bound 参与 min/max 收缩，越界 trial step 经
  `reduceAtRange` 精确落到 grid line（`testQCrossRangeBoundaryLandsOnGridLine`、
  `testQHorizontalGridLineProgression`），且 `minimumStep=1e-3×nominal` 下限保留
  （`testQMinimumStepClampRetainedAtDepthNode`），grid line 上无 zero-step 振荡。
- Corner single-jump：`testQCornerCrossingPrefersDepthBranchSingleJump` 证明
  同时 crossing 时 depth 分支优先、只执行一次 jump、不追加第二次 range
  correction；`testQCrossDepthSegmentKeepsDepthJumpSemantics` 证明 depth jump
  语义与 F2CPP 一致。
- `crz` consumption：cross-gradient case 的非零 `crz` 进入二维几何演化；
  `testQRangeJumpActuallyChangesDynamicP` 证明 range jump 真实改变 dynamic p，
  非 no-op；range-dependent 与 range-independent control 的 Origin field 差异
  由 validator 的 effect guards 保护。
- 非 Q 路径不受 range API 影响：`testNonQRangeHintZeroIsBitIdentical` 证明
  C/P/N/S 下新旧调用 bit-identical。

## F. Frequency Projection

- `QuadrilateralFrequencySsp`：real `c(r,z)` 来自 `.ssp` 2D matrix，只在 geometry
  trace 阶段决定轨迹；imaginary attenuation 由 `.env` reference depth profile
  逐节点按目标频率转换后仅沿 depth 插值（1D），不从 `.ssp` 生成 2D 衰减。
- Frozen path 不变证据：`testQuadrilateralFrozenPathProjection`（真实 Q
  `RayPath` 两频投影 frozen path 逐字段不变 + 分频结果独立 + 重复投影
  bit-stable）；reuse 模式 trace once（Trace passes = 1）服务两频投影，三模式
  SHD 逐字节一致（G 节）；projection 不把 `rangeSegmentIndex` 写回 cache。
- Attenuation 转换沿用共享 `convertAttenuation` 3 参路径，与其他 frequency
  evaluator 相同。

## G. Execution Parity

`q_range_dependent_cross_gradient` broadband_smoke（1000/2000 Hz；G02 于
release build 运行，G03 于 clean executable 重跑复核，两者一致）：

| Mode | Trace passes | SHD SHA-256 | cache fingerprint |
|---|---|---|---|
| `nonreuse` | 2 | `b53c02cba0a1372ac13123937643106579ddaed5bb77db7515d2440cc263ed2f` | — |
| `reuse` | 1 | 同上（逐字节一致） | before==after==`2879552213476552188` |
| `parallel` | 1 | 同上（逐字节一致） | before==after==`2879552213476552188` |

三模式输出写入隔离目录（G02 `/tmp/fp2e_g02/modes/{nonreuse,reuse,parallel}`；
G03 `/tmp/fp2e_g03/modes/`），未互相覆盖。R 不接受显式 execution mode（既有
行为不变）。

## H. Product Validation

同一 Q 环境（`'QVW'`，2×3 cross-gradient `.ssp`，300 rays，`'MS' 1.0 0.5`）下，
五产品 F2CPP=RayReuse 逐字节一致（G02 生成；G03 以 clean executable 复算 TL 与
多频 A 确认可复现）：

| 产品 | 频率 | SHA-256（两侧相同） |
|---|---|---|
| TL（SHD，Cartesian Cerveny `CC`） | 1000 Hz | `132d6af2335df5b3ce73715a07c08702fa7063d6cfbd4303887aa170db0b1bbd` |
| R（RAY，`'R'`） | 1000 Hz | `00e115863e90bfe1b99bf16b8e137c1325c0e40004762e345c1dd2c39c364831` |
| A（ASCII ARR，`'AG'`） | 1000 Hz | `19ef39a8927708773c48e9af53296c7c49753347c6bcd0d017283198b56ffe3b` |
| A | 2000 Hz | `0dd74fb175e6c7b60fb38fe4369a20e2e2508a2ff7840ebecbc502298a5f3a28` |
| a（binary ARR，`'aG'`） | 1000 Hz | `3c9acd3962d8c45c8da201ba7f5cb14ba818d9dc02ccb988efb6f21cc942684e` |
| a | 2000 Hz | `a7f2ccee35aadb26da83e1d2ed20f296b5ed9bb5417bd94736ae58a5d46daa28` |
| E（RAY eigenray，`'EG'`） | 1000 Hz | `deae13ce798cb513a938a1ea73879395906c77c3362648ef736f47d93fa89998` |
| E | 2000 Hz | `e5324d3233b9c076ba82cc1e0423fdff83fcc48016bf8d51a9110cb99e48c6eb` |

说明：Q 的产品 oracle 覆盖 TL `CC` 与 R、Cartesian GeoHat `G` A/a/E；A/a/E 多频
按既有逐频独立文件命名发布。Q 与其他 beam family（ray-centered
Cerveny/GeoHat、GeoGaussian、Simple Gaussian、ray-centered `g` 产品）的组合未
建立专门 oracle，不在本批次声明范围内。

## I. C/P/N/S Zero Regression

| 项目 | 冻结基线 | 本批次复测 | 结论 |
|---|---|---|---|
| C-linear probe SHA | `809b126d4b2657b8c54100e9f0e867c69bd26633963a58a883b2e985b98492e2` | 一致（G01/G02；G03 clean-build probe 复算同 SHA） | match |
| PCHIP probe SHA | `eb51ced19656a7724594e0dc7e0c2c5977daa8447962d76b9ca8112c55c646a0` | 一致（同上） | match |
| N² probe SHA | `360dda437550e396b531ed9a4692a006ebe8e5e29ddcaddde40fe8ddbcc00be8` | 一致（同上） | match |
| Spline probe SHA | `1fd0e4f84391aa24ec5e9876fae5d582b2ffdeb30422533113b407eba7faad63` | 一致（同上） | match |
| C broadband SHD | `cf1f9711aefcab087bd766c395a03b935c1c9cf13980335a368035515fd126bc` | 一致（G02 `/tmp/fp2e_g02/regress/` 复算） | match |
| P broadband SHD | `fd5b2e2cf77a524ec4972e8563c19efe0e33de48c87677a193c2d20c80d85cde` | 一致（同上） | match |
| N broadband SHD | `18817c6788b6e7a4c0c7cbd73cb5b8de78c4c92ea90e06a059badf80c27d29c4` | 一致（同上） | match |
| S broadband SHD | `74028065178ff80d43755ef2ba70ba5ba3e4947574a37a4154a7ecc52eef1596` | 一致（同上） | match |
| `munk_spline` reuse/parallel fingerprint | `1526667602348633172` | before==after 同值（G02 `/tmp/fp2e_g02/regress_fp/`） | match |
| RayReuse CTest | 基线 36/36 | 37/37（新增 `bellhop_rayreuse_quadrilateral_ssp_tests`，既有全部通过） | pass |
| standard-case test-unit | 基线 158 | 158 OK | pass |
| 仓库 pytest | 基线 173 | 173 passed | pass |

## J. Tests

批次级 full validation（G03 于最终工作树状态亲手运行，zsh + uv 环境，
`build/fp2e-clean` 隔离 clean build）：

| 命令 | 结果 |
|---|---|
| `uv run cmake -S Bellhop_RayReuse -B Bellhop_RayReuse/build/fp2e-clean -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON` | configure 成功 |
| `uv run cmake --build Bellhop_RayReuse/build/fp2e-clean --parallel 8` | build 成功（`100% Built target`，含全部测试 target 与 probe） |
| `uv run ctest --test-dir Bellhop_RayReuse/build/fp2e-clean --output-on-failure` | `100% tests passed out of 37` |
| `uv run pytest` | `173 passed` |
| `uv run make -C test/standard_cases test-unit` | `Ran 158 tests ... OK` |
| `make test VERSION=rayreuse CASE=q_range_dependent_cross_gradient PROFILE=single` | `test: PASSED` |
| `make test VERSION=rayreuse CASE=q_range_independent_control PROFILE=single` | `test: PASSED` |
| `make test VERSION=rayreuse CASE=q_range_dependent_cross_gradient PROFILE=broadband_smoke` | `test: PASSED` |
| clean probe `i5-quadrilateral` @ `-0.002626749710359303` rad | SHA `4e22fd05…`（= 冻结基线）；715/714/ExitedDomain |
| `intermediate_state_matrix.py --case q_range_dependent_cross_gradient`（clean probes） | `INTERMEDIATE GEOMETRY MATRIX PASSED`；worst scaled `1.0583956e-09` |
| clean executable TL 复算（`q_tl`，1000 Hz `CC`） | SHA `132d6af2…`（= 冻结值），EXIT=0 |
| clean executable 多频 A 复算（`q_a`，1000/2000 Hz） | 两 `.arr` SHA = 冻结值，EXIT=0 |
| clean executable 三模式（`--frequencies-hz 1000,2000` + `--verify-cache`） | 2/1/1、fingerprint before==after `2879552213476552188`、三 SHD 均 `b53c02cb…` |
| clean probe C/P/N/S（0.0125 rad） | 四 SHA = 冻结基线 |
| 缺 `.ssp`（clean executable） | EXIT=1，诊断 `unable to open quadrilateral SSP file` |
| `git diff --check` | 通过（无输出） |
| `git diff -- Bellhop_F2CPP Bellhop_origin` | 空 |

分任务测试记录（来自各任务 checkpoint，G03 未重跑的部分如实引用）：A01/A02
Release CTest 37/37 + 组件测试（`quadrilateral_ssp_test.cpp` 10 个：grid
validation、exact range/depth node ownership、outside rejection、bilinear 与
`cr/cz/crz`、density、dispatch、frequency imaginary anchors、两频独立性、parser；
`ray_stepper_test.cpp` 6 个 Q 测试：depth jump 语义、corner depth-priority 单次
jump、exact range node landing 与 hint retention、outside grid 拒绝、range jump
改变 dynamic p、非 Q bit-identical；`geometry_tracer_ssp_interface_test.cpp` 4 个
Q 测试：range boundary 落 grid line、horizontal grid line progression、
minimum-step clamp 保留、back node 越界拒绝）；G01 targeted + 缺 `.ssp` CLI
检查 + freeze contract diff 检查；G02 三模式/五产品/Origin final-field/
`testQuadrilateralFrozenPathProjection`；G03 见上表。

## K. Files Changed

`git status --short`（G03 文档更新完成后、Batch Report 写入前实况；本报告文件
本身亦为 untracked）：

```text
 M Bellhop_RayReuse/CMakeLists.txt                                              [A01]
 M Bellhop_RayReuse/app/main.cpp                                                [G01]
 M Bellhop_RayReuse/include/rayreuse/model/environment.hpp                      [A01]
 M Bellhop_RayReuse/include/rayreuse/model/sound_speed_evaluator.hpp            [A01]
 M Bellhop_RayReuse/include/rayreuse/model/sound_speed_types.hpp                [A01]
 M Bellhop_RayReuse/include/rayreuse/ray/ray_stepper.hpp                        [A02]
 M Bellhop_RayReuse/src/io/environment_parser.cpp                               [A01]
 M Bellhop_RayReuse/src/model/environment.cpp                                   [A01]
 M Bellhop_RayReuse/src/model/sound_speed_evaluator.cpp                         [A01]
 M Bellhop_RayReuse/src/ray/geometry_tracer.cpp                                 [A02]
 M Bellhop_RayReuse/src/ray/ray_stepper.cpp                                     [A02]
 M Bellhop_RayReuse/tests/component/frequency_projector_test.cpp                [G02]
 M Bellhop_RayReuse/tests/component/geometry_tracer_ssp_interface_test.cpp      [A02]
 M Bellhop_RayReuse/tests/component/ray_stepper_test.cpp                        [A02]
 M Bellhop_RayReuse/tests/tools/geometry_oracle_probe.cpp                       [G01]
 M test/standard_cases/cases/q_range_dependent_cross_gradient/case.toml        [G01]
 M test/standard_cases/cases/q_range_independent_control/case.toml             [G01]
 M test/standard_cases/codes/intermediate_state_matrix.py                       [G01]
 M test/standard_cases/codes/tests/test_case_model.py                           [G01]
 M test/standard_cases/codes/validate_i5_quadrilateral_ssp.py                   [G02]
 M Bellhop_RayReuse/doc/reference/REFERENCE_FEATURE_SUPPORT_MATRIX.md           [G03]
 M Bellhop_RayReuse/doc/reports/REPORT_F2CPP_RAYREUSE_PARITY_2026-08-25.md      [G03]
 M Bellhop_RayReuse/doc/status/STATUS_PROGRESS.md                               [G03]
?? Bellhop_RayReuse/doc/worklists/FP-2E_QUADRILATERAL_SSP_WORKLIST.md           [batch]
?? Bellhop_RayReuse/include/rayreuse/model/quadrilateral_frequency_ssp.hpp     [A01]
?? Bellhop_RayReuse/include/rayreuse/model/quadrilateral_ssp.hpp               [A01]
?? Bellhop_RayReuse/src/model/quadrilateral_frequency_ssp.cpp                  [A01]
?? Bellhop_RayReuse/src/model/quadrilateral_ssp.cpp                             [A01]
?? Bellhop_RayReuse/tests/component/quadrilateral_ssp_test.cpp                 [A01/A02]
```

`git diff --stat`（tracked，不含 untracked）：23 files changed,
1631 insertions(+), 120 deletions(-)。新增 production source/header 4 个 +
component test 1 个（合计 1411 行，`wc -l`：real evaluator 273 cpp + 71 hpp、
frequency evaluator 138 cpp + 71 hpp、component test 858 行；F2CPP 对应
277/70/145/70）。

FP-2E agents did not modify `.pi` configuration.

During Final Review, the working tree also contained four user-initiated
project-level `.pi` deletions unrelated to FP-2E. They were identified as
out-of-scope user configuration changes and isolated from the FP-2E working
tree before Re-Final Review. They are not part of the FP-2E diff and must not
be staged with FP-2E.

## L. Remaining GAPs

本批次只关闭二维 point/single/rectilinear slice 内的 SSP `Q`。以下 GAP 与
FP-2D 后状态一致，未因本批次变化：

- line source（SRC-02/PRD-08）、multisource（SRC-04/PRD-07）、irregular receiver
  （REC-02/REC-03/PRD-06）；
- canonical curvilinear boundary（BND-04）、flat elastic oracle 闭环（BND-09）、
  attenuation units N/F/M/Q/L oracle 闭环（ATT-01）、Francois–Garrison（ATT-04）、
  biological（ATT-05）；
- `Q` 的 3D / N×2D 形态不属本批次；
- `Q` 与本批次 oracle 未覆盖的 beam family 组合（ray-centered Cerveny/GeoHat、
  GeoGaussian、Simple Gaussian TL 与 ray-centered `g` A/a/E）保持不外推。

## M. Working Tree

- 施工基线 `6b3428c` 之后无 commit、无 stage、无 push；全部改动保持在工作树/
  untracked 状态（含本报告文件），由用户/coordinator 决定纳入版本控制。
- 生成产物（`.prt/.shd/.ray/.arr`、probe CSV、三模式输出）全部位于 `/tmp/fp2e_g01/`、
  `/tmp/fp2e_g02/`、`/tmp/fp2e_g03/`、`Bellhop_RayReuse/build/fp2e-clean`（隔离
  构建目录）与 `test/standard_cases/results/`（可再生成产物目录，不进入版本
  控制）；无 generated products 进入 git 跟踪（K 节清单可核对）。
- `git diff --check` 通过；`git diff -- Bellhop_F2CPP Bellhop_origin` 为空。
- Final Review 期间出现的四个 `.pi` 删除属于用户主动维护的项目级配置变化，
  与 FP-2E 无关；Re-Final Review 前已从 FP-2E 工作树隔离，`git diff -- .pi` 为空。

## N. Known Limitations

- `Q` 只支持当前矩阵整体适用的二维、单 point source、单一 source depth、
  rectilinear receiver、单一环境范围；同一 `.env`+`.ssp` 上的多频共享一条冻结
  轨迹。
- `.ssp` 必须在 parseFile 阶段与 `.env` 同根可读：缺失、range/depth 维度不匹配、
  非递增 range、非有限或非正实声速均显式失败；无 fallback、无外推、无 clamp。
- range 超出 `.ssp` 网格 `[r_first, r_last]` 显式失败（内部 cell 左闭右开、
  最终 cell 闭区间的 exact-node 归属按 F2CPP hinted locator 语义）。
- attenuation 只沿 depth 插值：imaginary 声速由 `.env` reference depth profile
  逐节点按目标频率转换后 1D 插值；不存在 2D `.ssp` 衰减表示。
- `rangeSegmentIndex` 为 transient 状态（B 节四处合法位置），调用方不能从
  frozen `RayPath`/`RayPathCache` 读回 range cell identity。
- `Q` 的产品 oracle 限于 TL `CC` 与 R、Cartesian GeoHat `G` A/a/E（H 节）；
  其他 beam family 与 `Q` 的组合机制上可 dispatch（parser 接受如 `QVW` 组合），
  但没有专门 oracle 证据，不在支持声明内。
- control case（`q_range_independent_control`）沿用既有较宽的 `0.02 dB`
  tolerance（coherent 近零 cell 放大效应，`tolerances_i5_q_control.toml` 既有
  policy 注记）；本批次未修改。
- STATUS_PROGRESS 中 FP-2C 行保持"完成（待最终验收）"原文：项目历史中未找到
  FP-2C 独立 Final Review `ACCEPTED` 的记录（`FP-2C_N2_LINEAR_SSP_BATCH_REPORT.md`
  止于 `Ready for Final Review: YES` 且明确不自行声明 ACCEPTED；FP-2D/R1 与
  FP-2E worklist 只把 FP-2C 当作已提交基线 `34c88fd` 引用），故不做状态改写；
  若后续找到明确验收记录，可再修正。

## O. Final Review and FP-2E-R1 Remediation

### Original Final Review

原独立 Final Review 结论：

```text
FP-2E CHANGES_REQUIRED
```

阻断项仅有两项：

1. documentation scope overclaim：Support Matrix 与 parity report 将 `Q` 映射到
   未经 FP-2E oracle 验证的完整 TL beam/option 组合；
2. working-tree hygiene：Final Review 当时工作树包含四个用户主动删除的项目级
   `.pi` 配置/agent 文件，污染了 FP-2E 的 Git 边界判断。

### Remediation

- production numerical implementation 未修改；FP-2E-R1 只修改本文档、Support
  Matrix、parity report 与 STATUS_PROGRESS；
- Q 声明已收紧到实际 oracle-supported slice：二维 single point source、single
  source depth、rectilinear receivers；TL Cartesian Cerveny `CC`、R、Cartesian
  GeoHat `G` A/a/E；既有 single/broadband Q profiles；TL/A/a/E 的
  `nonreuse/reuse/parallel`；
- 完整 C/I/S × F/M/W × D/S/Z × P/V/H、ray-centered Cerveny/GeoHat、
  GeoGaussian、Simple Gaussian、ray-centered `g` 产品及 line/multisource/
  irregular/3D/N×2D 均不声明 Q parity；机制上可 dispatch 不作为 oracle 证据；
- 四个 `.pi` 删除确认是用户主动维护且 OUT-OF-SCOPE FOR FP-2E，并已在
  Re-Final Review 前从 FP-2E 工作树隔离；FP-2E 不包含任何 `.pi` diff，也不得将
  这些用户配置变化与 FP-2E 一同 stage；
- `git diff -- Bellhop_F2CPP Bellhop_origin` 与 `git diff -- .pi` 均为空；
- remediation 最小复验：`uv run pytest` 173 passed，standard-case unit 158 OK，
  `git diff --check` 通过。

```text
FP-2E-R1 DOCUMENTATION REVIEW: PASS
```

Ready for Re-Final Review: YES

（本报告不声明 `FP-2E ACCEPTED`；正式结论仍由独立 Re-Final Reviewer 给出。）
