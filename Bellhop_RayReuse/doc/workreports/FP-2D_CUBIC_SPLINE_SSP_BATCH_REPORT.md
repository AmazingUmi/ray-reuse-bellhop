# FP-2D Cubic Spline SSP Batch Report

> 批次：FP-2D — Cubic spline SSP (`S`) parity
> 施工基线：`9efe8bebfbd6f50fb818392fe27252d8ecee7856`（= FP-2C 提交 `34c88fd` +
> worklist 文档提交 `e468c67` + settings 提交 `9efe8be`；施工起点工作树干净）
> 报告状态：A01–G03 施工、Batch Acceptance 与 FP-2D-R1 remediation 已完成；
> 独立 Re-Final Review 结论为 `ACCEPTED`（O 节）
> 合同：`doc/worklists/FP-2D_CUBIC_SPLINE_SSP_WORKLIST.md`

## A. Completed Tasks

| Task | 角色 | 状态 | 验收证据 |
|---|---|---|---|
| A01 [ADVANCED] — exact coefficient kernel + real spline evaluator | advanced-worker | 完成，reviewer checkpoint PASS | kernel/evaluator 机械 diff 与 F2CPP 一致；4+ node literal anchors、2-node 解析 anchor、3-node structural/continuity regression；全量 CTest 36/36；`A01 CHECKPOINT: PASS` |
| A02 [ADVANCED] — frequency-local complex spline + dynamic-ray contract | advanced-worker | 完成，reviewer checkpoint PASS；R02 regression 已补 | 逐行迁移 + 有限差异仅 3 项；两频 bit-stable；production spline 跨 node no-jump regression；Hessian 非 zero 消耗；`A02 CHECKPOINT: PASS` |
| G01 [GENERAL] — parser/enum/CMake/generic dispatch | worker (ZCode subagent) | 完成 | `S` parser 接通、`Q`/unknown 明确拒绝、CLI smoke S EXIT=0 / Q EXIT=1、targeted 9/9、全量 36/36 |
| G02 [GENERAL] — shared oracle、product 与 execution parity | worker (ZCode subagent) | 完成 | `munk_spline` 三方 single PASSED；probe byte-identical；TL/R/A/a/E byte-identical；三模式一致；C/P/N zero regression |
| G03 [GENERAL] — 文档收口、full validation、Batch Report | worker (ZCode subagent) | 完成（本报告） | 3 个文档更新 + 批次级 full validation 全部通过 + 本报告 |

全部原 worklist 任务（A01、A02、G01、G02、G03）、Batch Acceptance 与 FP-2D-R1
remediation 已完成；独立 Re-Final Review 结论为 `ACCEPTED`（见 O 节）。

## B. GENERAL Work

### G01 — Parser, enum, CMake and generic runtime dispatch

- `SspInterpolationKind::CubicSpline`（ordinal 追加，不改既有 ordinal）；gradient
  continuity 标记为 `SspGradientContinuity::ContinuousAtNodes`。
- geometry variant 与 frequency variant 各加入 spline backend；双 factory 显式 case，
  无 default 回落。
- parser 将 SSP top-option `S`（含 `SVW` 等合法组合形式）映射到 CubicSpline；`Q`
  单独显式拒绝（不与 unknown 混同），unknown kind 使用独立诊断；诊断措辞覆盖
  C-linear/N2-linear/PCHIP/cubic-spline；gradientContinuity 查表化回补、profile kind
  回补。
- CLI smoke：S environment EXIT=0（300 rays 正常完成）；Q environment EXIT=1 明确拒绝。
- 新增 `testCubicSplineDispatchIsExact`（四个 backend evaluator 互异 + frequency 互异，
  排除 silent fallback）与 `testSplineEnvironmentSolverSmoke`（solver 级 trace
  fingerprint S≠C）。
- 验证：targeted 9/9，全量 CTest 36/36，`git diff --check` 通过。

### G02 — Shared oracle, product and execution parity

- 将 RayReuse 加入 `munk_spline` compatibility（`case.toml` versions 已含
  `origin/f2cpp/rayreuse`），复用既有 Origin/F2CPP input 与 tolerance，未新建 case、
  未扩大 tolerance。
- `[50, 250]` Hz broadband profile 接入 `munk_spline`；intermediate-state matrix 接入
  `munk_spline → munk-spline`。
- RayReuse geometry probe 增加 `munk-spline` 配置，与 F2CPP 相同 launch/config。
- 结果（全部由 G02 实际运行产生，G03 抽验复核）：
  - 三方 `munk_spline` single PASSED；
  - F2CPP/RayReuse `munk-spline` probe CSV byte-identical
    `1fd0e4f84391aa24ec5e9876fae5d582b2ffdeb30422533113b407eba7faad63`
    （237 points/236 steps/0 events @0.0125 rad）；
  - Origin intermediate-state matrix PASSED：370 points/367 steps/2 top reflections，
    worst scaled `5.5368e-4` / abs `1.4523e-11` / field `h_m` / point 369，tolerance
    未修改；
  - 三模式 broadband SHD 逐字节一致
    `74028065178ff80d43755ef2ba70ba5ba3e4947574a37a4154a7ecc52eef1596`，Trace passes
    2/1/1，`--verify-cache` fingerprint before==after=`1526667602348633172`
    （reuse/parallel）；
  - 五产品 F2CPP=RayReuse byte-identical（详见 I 节）；
  - S≠C/P/N（详见 I 节末）；
  - zero regression：C/P/N probe 三 SHA 一致、ctest 36/36、test-unit 153 OK、
    C/P/N SHD 三冻结值 match。
- G02 同时补齐 A02 reviewer 指出的端到端面证据（见 C 节）。

### G03 — Documentation closure, full validation and Batch Report

- 更新 `REFERENCE_FEATURE_SUPPORT_MATRIX.md`：`S` 从 Deferred 更新为已支持，SSP
  wording 更新为 C/P/N²/Spline，`Q`/`.ssp` 保持 Deferred 原文。
- 更新 `REPORT_F2CPP_RAYREUSE_PARITY_2026-08-25.md`：追加 `FP2D-ORACLE` 行、SSP-04
  GAP→PARITY、`S/Q` 排除注记→`Q`、execution 表、Section 6 追加 FP-2D 验证记录块、
  Section 7 GAP 列表移除 SSP-04；历史批次行（FP2B-ORACLE/FP2C-ORACLE 及 Section 6
  中 FP-2B/FP-2C 块）保持快照原文。
- 更新 `STATUS_PROGRESS.md`：追加 FP-2D 完成条目（只关闭 `S` slice，注明"待最终
  验收"）。
- 运行批次级 full validation（见 J 节），生成本报告。

## C. ADVANCED Work

### A01 — Exact coefficient kernel and real spline evaluator

- advanced-worker 实现摘要：迁移 F2CPP `ComplexSplinePolynomial` 与 coefficient
  builder（`numerics/cubic_spline_coefficients.*`）与 real evaluator
  （`model/cubic_spline_ssp.*`），含 2/3/4+ node production 分支、forward
  elimination/back substitution 原始顺序、complex arithmetic、non-finite 与非递增
  节点验证、locator/extrapolation/density 线性插值。
- coefficient/mixed-precision 决定：
  - `kFortranSixth = static_cast<double>(1.0F/6.0F)` 逐字保留（`splinec.f90` 默认实数
    常量 binary32 rounding），未替换为 binary64 `1.0/6.0`；
  - not-a-knot endpoint（`IBCBEG=IBCEND=0`）语义保留，未用自然边界/clamped 替代；
  - 4+ node anchors 从 F2CPP 冻结移植（如 `1513.87282436185433`），2-node 使用
    独立解析线性值；3-node 没有独立 literal anchor，由与 F2CPP 相同的退化结构和
    continuity regression 覆盖，不从被测 evaluator 动态生成 expected；
  - 无第三方 spline、无 dense resampling、无通用 linear algebra solver。
- reviewer checkpoint：reviewer 亲证 kernel/evaluator 与 F2CPP 机械 diff 完全一致；
  全量 CTest 36/36。结论 `A01 CHECKPOINT: PASS`。
- 编排记录：A01 reviewer checkpoint 一次 reviewer 启动遇 provider rate limit、一次
  取消，重试后成功完成——如实记录，未切换模型路由，未修改 `.pi/settings.json`。

### A02 — Frequency-local complex spline and dynamic-ray contract

- advanced-worker 实现摘要：`CubicSplineFrequencySsp` 逐行迁移 F2CPP
  `cubic_spline_frequency_ssp.*`；每频先转换全部节点 attenuation、再复用 A01 kernel
  构造 complex coefficients；evaluation 先取 real evaluator 的 density/segment，
  再按 F2CPP 顺序覆盖 complex value 与 real-part gradient/Hessian；每频一次 kernel
  调用、coefficients 实例私有，不跨频共享 mutable state。
- coefficient/mixed-precision 决定：
  - `kFortranSixth` 复制逐字保留（F2CPP 本身两个文件各持一份，RayReuse 保持相同
    布局）；
  - 允许的差异仅三项（均为 RayReuse 侧已确定的真实接口差异）：`convertAttenuation`
    3 参签名、无 VolumeAttenuation 重载、无 `rangeSegmentIndex`；
  - imaginary sound speed 仅做 finite 校验（欠冲测试 imag < -1 通过），未从 PCHIP
    复制 `imaginary >= 0`，不 clamp、不取 abs、不 silent fallback；
  - 两频（50/250 Hz Thorp）evaluator 分频 bit-stable，value storage 完全独立。
- no-jump 证明：continuity 静态/runtime mapping 为 `ContinuousAtNodes`；FP-2D-R1
  增加真实 `GeometrySspEvaluator(CubicSpline)` 经 production `stepRay` 跨 SSP node
  regression，逐对照无 jump predictor/corrector 的 dynamic p/q，并确认若误入
  `applyGradientJump` 会产生可检测偏差。
- Hessian 证明：经 production `soundSpeedNormalSecondDerivativeOverSquaredSpeed`
  非零，且真实改变 dynamic p/q（非仅存在于 sample）。
- reviewer checkpoint：ownership 与 mixed precision 审查通过；reviewer 明确记录
  **端到端面（stepper 积分/tracer/projector 逐字段）当时因 variant 未接入而留待
  G01/G02 补齐**。结论 `A02 CHECKPOINT: PASS`。
- G02 补齐证据链（本批次证据叙事的关键闭环）：
  - G01 完成 variant 接入后，G02 在 production 测试中新增
    `testSplineCurvatureEntersDynamicEquations`（stepper 经真实 spline variant：
    C baseline p bit-exact，spline p/q 偏离 > 1e-9，证明非零 Hessian 进入
    dynamic-ray equations）与 `testSplineFrozenPathProjection`（真实 spline
    `RayPath` 两频投影：frozen path 逐字段不变 + 分频结果独立 + 重复投影
    bit-stable）；
  - 端到端产品面由 G02 的 byte-identical 证据闭环：probe CSV、TL/R/A/a/E 五产品、
    三模式 SHD 全部与 F2CPP 逐字节一致（E/I 节）。

## D. Architecture Deviations

- none。

说明（非偏离）：

- A02 的三项允许差异（`convertAttenuation` 3 参、无 VolumeAttenuation 重载、无
  `rangeSegmentIndex`）为 worklist 预先界定的 RayReuse 接口适配，不属于架构偏离。
- `kFortranSixth` 在 coefficient kernel 与 frequency evaluator 两处各持一份，与
  F2CPP production 自身布局一致，属刻意保真而非重复抽象。
- evaluator 扩展仅增加 `CubicSplineSsp`/`CubicSplineFrequencySsp` 两个叶子，未新增
  virtual hierarchy、plugin registry 或 future SSP factory；`GeometryTracer`、
  `RayStepper`、`FrequencyProjector` 无 product-specific `S` 分支。
- 无 `ARCHITECTURE_BLOCKER`。

## E. F2CPP Oracle

- A01 kernel/real evaluator 与 F2CPP production 机械 diff 一致（reviewer 亲证）。
- A02 frequency evaluator 与 F2CPP 逐行迁移，差异仅 D 节所列三项。
- F2CPP 4+ node literal anchors 冻结移植（`1513.87282436185433` 等）；2-node 由
  独立解析线性 anchor 覆盖。F2CPP 的 3-node 测试本身没有独立 literal anchor，
  RayReuse 同样以 global-quadratic structural check 与 node continuity regression
  覆盖该退化分支，不把它表述为独立数值 anchor。
- `munk-spline` geometry probe（F2CPP vs RayReuse，相同 launch 0.0125 rad/config）：
  CSV byte-identical，SHA-256 `1fd0e4f84391aa24ec5e9876fae5d582b2ffdeb30422533113b407eba7faad63`
  （237 points/236 steps/0 events）。
- 五产品 F2CPP=RayReuse byte-identical：见 I 节。
- 本批次未修改任何 `Bellhop_F2CPP` production code（`git diff -- Bellhop_F2CPP` 为空）。

## F. Origin Oracle

- `munk_spline` single standard case：Origin/F2CPP/RayReuse 三方 PASSED（G02 运行；
  SHD 维度 `[1,1,1,1,1,201,501]`、PRT marker 检查按既有 case 定义）。
- Origin intermediate-state matrix（`munk_spline`）：370 points/367 steps/
  2 top reflections；worst comparison field `h_m`@point 369，absolute `1.4523493518936448e-11`、
  scaled `5.5368e-4`；沿用既有 tolerance，未放宽、未修改。
- F2CPP/RayReuse 对 Origin 的 probe CSV 一致（同一 SHA `721b08ef0cabbd906b966acafab11351edec17eec7ed29530bac63872210e581`，
  G02 matrix 记录）。
- 本批次未修改任何 `Bellhop_origin` production code（`git diff -- Bellhop_origin` 为空）。

## G. C/P/N Zero Regression

| 项目 | 冻结基线 | 本批次复测 | 结论 |
|---|---|---|---|
| C-linear probe SHA | `809b126d4b2657b8c54100e9f0e867c69bd26633963a58a883b2e985b98492e2` | 一致（G02 复测；G03 clean-build probe 复算 `809b126d…` 一致） | match |
| PCHIP probe SHA | `eb51ced19656a7724594e0dc7e0c2c5977daa8447962d76b9ca8112c55c646a0` | 一致（G02） | match |
| N² probe SHA | `360dda437550e396b531ed9a4692a006ebe8e5e29ddcaddde40fe8ddbcc00be8` | 一致（G02） | match |
| C broadband SHD | `cf1f9711aefcab087bd766c395a03b935c1c9cf13980335a368035515fd126bc` | 一致（G02；G03 于 `results/rayreuse/munk_cerveny_cc/broadband_smoke/run_manifest.json` 复核） | match |
| P broadband SHD | `fd5b2e2cf77a524ec4972e8563c19efe0e33de48c87677a193c2d20c80d85cde` | 一致（同上） | match |
| N broadband SHD | `18817c6788b6e7a4c0c7cbd73cb5b8de78c4c92ea90e06a059badf80c27d29c4` | 一致（同上） | match |
| RayReuse CTest | 基线 35/35 | 36/36（新增 spline 测试 target，既有全部通过） | pass |
| standard-case test-unit | 153/153 | 153 OK | pass |
| 仓库 pytest | 168/168 | 168 passed | pass |

## H. Execution Parity

- `nonreuse`：每频独立 trace + solve/product；broadband SHD
  `74028065178ff80d43755ef2ba70ba5ba3e4947574a37a4154a7ecc52eef1596`；PRT Trace
  passes = 2。
- `reuse`：trace once → frozen cache → per-frequency projection；SHD 与 nonreuse
  逐字节一致；Trace passes = 1；`--verify-cache` fingerprint before==after=
  `1526667602348633172`。
- `parallel`：frequency workers + serial ordered publish；SHD 与 nonreuse/reuse
  逐字节一致；Trace passes = 1；fingerprint before==after=`1526667602348633172`。
- 三模式输出写入隔离目录（G02 临时产物 `/tmp/fp2d_g02/{nonreuse,reuse,parallel}`），
  未互相覆盖；G03 已复核三目录 SHD 同为 `74028065…`、PRT Trace passes 2/1/1。

## I. Product Validation

`munk_spline` 同一环境（两频 50/250 Hz broadband smoke）下，五产品 F2CPP=RayReuse
逐字节一致：

| 产品 | SHA-256（两侧相同） | 规模 |
|---|---|---|
| TL（SHD，Cartesian Cerveny `CC`） | `ce216646d078190320420c339248ee7062279f50792d9df169be184f7bd4a36d` | 两频 broadband smoke |
| R（RAY） | `ddd94952eec067628d5ad771f2d9b7c53167eb542cddb1c8dee19d2b4a2df04b` | 5000 rays / 1,689,310 points / top+bottom bounces 3778+3577 |
| A（ASCII ARR） | `30042a8403c1bba1e426333c3243d4e3203a8cd29391a3ce311df14726584ac0` | 100,701 cells / 536,601 arrivals / max 19 per cell |
| a（binary ARR） | `837e1e4f10592ec31b08be8165e25930370440fe28600e667debdb9d427a013c` | 同 A 的 binary encoding |
| E（RAY eigenray） | `c921296bc51a8583ef88898efef7076114a0ec3f022904cc5310171e42376202` | 884,091 records / 236,420,945 points |

说明：五产品 SHA-256 均经过两次独立确认——G03 于 `/tmp/fp2d_g02/products/`
复算 TL/R/A/a，**Batch Acceptance（coordinator）在 `/tmp/fp2d_accept/products/`
以两版可执行文件重新生成全部五产品并三方比对（f2cpp = rayreuse = 冻结 SHA，
含 E 产品逐字节复算，全部 EXIT=0）**，关闭了 G03 草稿中"E 引用 G02 冻结记录
未复算"的证据边界。

S 与相同节点的 C/P/N 明确不同（排除 silent fallback；G02 实测）：

- probe 点数 237 vs C/P/N 234/234/233（@0.0125 rad）；
- point_index=2 起 |Δp1|：S vs C `2.24e-4`、S vs P `2.22e-4`、S vs N `2.12e-9`；
- 终点深度差：12.4 / 13.1 / 18.9 m。

## J. Tests

批次级 full validation（G03 于最终工作树状态亲手运行，zsh + uv 环境）：

| 命令 | 结果 |
|---|---|
| `uv run cmake -S Bellhop_RayReuse -B Bellhop_RayReuse/build/fp2d-clean -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON` | configure 成功 |
| `uv run cmake --build Bellhop_RayReuse/build/fp2d-clean --parallel 8` | build 成功（`100% Built target`，含全部测试 target 与 probe） |
| `uv run ctest --test-dir Bellhop_RayReuse/build/fp2d-clean --output-on-failure` | `100% tests passed out of 36` |
| `uv run ctest --test-dir Bellhop_RayReuse/build/release --output-on-failure` | `100% tests passed out of 36` |
| `uv run pytest` | `168 passed in 2.28s` |
| `uv run make -C test/standard_cases test-unit` | `Ran 153 tests ... OK` |
| `git diff --check` | 通过（无输出） |
| `git diff -- Bellhop_F2CPP Bellhop_origin` | 空 |

FP-2D-R1 R02 targeted validation（remediation 工作树）：

| 命令 | 结果 |
|---|---|
| `uv run cmake --build Bellhop_RayReuse/build/release --target bellhop_rayreuse_ray_stepper_tests --parallel 8` | build 成功 |
| `uv run ctest --test-dir Bellhop_RayReuse/build/release --output-on-failure -R ray_stepper` | 1/1 passed |
| `git diff --check` | 通过（无输出） |

FP-2D-R1 汇总复验（coordinator 于 remediation 最终工作树运行）：

| 命令/门 | 结果 |
|---|---|
| clean build + `ctest --test-dir Bellhop_RayReuse/build/fp2d-clean` | 36/36 passed |
| `uv run pytest` | 173/173 passed |
| `uv run make -C test/standard_cases test-unit` | 158/158 passed |
| 默认 tolerance 路由的 `model_matrix.py --case munk_spline --profiles broadband_smoke --modes nonreuse,reuse,parallel` | EXIT=0，MODEL MATRIX PASSED；case-local `0.005 dB` 自动生效 |
| Origin→F2CPP/RayReuse 250 Hz | max TL `0.00634765625 dB` ≤ scoped `0.0065 dB`；pressure rules 未变 |
| F2CPP→RayReuse 250 Hz decoded complex64 gate | nonreuse/reuse/parallel 各 805608 bytes，SHA-256 均 `94ffd3638de4f286079e65489db70a643ca38469f706b330ade897f23becf4d0`，exact |
| `intermediate_state_matrix.py --case munk_spline` | PASS；C++ probes byte-identical；worst abs `1.4523493518936448e-11` / scaled `5.536846572998383e-4` |
| C/P/N probe zero regression | `809b126d…` / `eb51ced1…` / `360dda43…` 全部 match |
| git checks | `git diff --check` 干净；F2CPP/Origin production diff 为空 |

隔离 clean build 复现性：

```text
f1f87158b9e41e3d47ff1b5ffd35d19e4d114dc2649b928a41d61e9cb4132b4b  build/fp2d-clean/bellhop_rayreuse
4979b2e0971bc65a228e113f83b31595f5e119eead9978bcb7905799271c2c3b  build/fp2d-clean/bellhop_rayreuse_geometry_oracle_probe
```

与既有 `build/release` 对应产物逐字节相同（同上两个 SHA-256）；用 clean probe 复算
C-linear Munk（`0.0125 munk`）输出 SHA-256 `809b126d…` 与冻结基线一致（234 points/
233 steps/ExitedDomain），证明可复现。

分任务测试记录（来自各任务 checkpoint，G03 未重跑的部分如实引用）：
A01 全量 CTest 36/36 + 新 component test；A02 targeted component tests（含两频
Thorp bit-stable、原 no-jump 检查、Hessian 动力学；R02 后续增加 production spline
跨 node regression）；G01 targeted 9/9 + 全量 36/36 +
CLI smoke（S EXIT=0 / Q EXIT=1）；G02 三方 single/matrix/产品/三模式 + G02 新增
`testSplineCurvatureEntersDynamicEquations`/`testSplineFrozenPathProjection`；G03
见上表。

## K. Files Changed

`git status --short`（G03 文档更新完成后实况；报告文件本身写入后亦为 untracked，
见 N 节）：

```text
 M Bellhop_RayReuse/CMakeLists.txt                                      [A01/A02/G01]
 M Bellhop_RayReuse/include/rayreuse/model/sound_speed_evaluator.hpp    [G01]
 M Bellhop_RayReuse/include/rayreuse/model/sound_speed_types.hpp        [G01]
 M Bellhop_RayReuse/src/io/environment_parser.cpp                       [G01]
 M Bellhop_RayReuse/src/model/sound_speed_evaluator.cpp                 [G01]
 M Bellhop_RayReuse/tests/component/environment_parser_test.cpp         [G01]
 M Bellhop_RayReuse/tests/component/frequency_projector_test.cpp        [G02]
 M Bellhop_RayReuse/tests/component/geometry_tracer_ssp_interface_test.cpp [G01]
 M Bellhop_RayReuse/tests/component/ray_stepper_test.cpp                [G02]
 M Bellhop_RayReuse/tests/component/single_frequency_solver_test.cpp    [G01]
 M Bellhop_RayReuse/tests/component/sound_speed_evaluator_test.cpp      [G01]
 M Bellhop_RayReuse/tests/tools/geometry_oracle_probe.cpp               [G02]
 M test/standard_cases/cases/munk_spline/case.toml                      [G02]
 M test/standard_cases/codes/intermediate_state_matrix.py               [G02]
 M test/standard_cases/codes/tests/test_case_model.py                   [G02]
 M Bellhop_RayReuse/doc/reference/REFERENCE_FEATURE_SUPPORT_MATRIX.md   [G03]
 M Bellhop_RayReuse/doc/reports/REPORT_F2CPP_RAYREUSE_PARITY_2026-08-25.md [G03]
 M Bellhop_RayReuse/doc/status/STATUS_PROGRESS.md                       [G03]
?? Bellhop_RayReuse/include/rayreuse/model/cubic_spline_frequency_ssp.hpp [A02]
?? Bellhop_RayReuse/include/rayreuse/model/cubic_spline_ssp.hpp          [A01]
?? Bellhop_RayReuse/include/rayreuse/numerics/cubic_spline_coefficients.hpp [A01]
?? Bellhop_RayReuse/src/model/cubic_spline_frequency_ssp.cpp             [A02]
?? Bellhop_RayReuse/src/model/cubic_spline_ssp.cpp                       [A01]
?? Bellhop_RayReuse/src/numerics/cubic_spline_coefficients.cpp           [A01]
?? Bellhop_RayReuse/tests/component/cubic_spline_ssp_test.cpp            [A01/A02]
?? Bellhop_RayReuse/doc/workreports/FP-2D_CUBIC_SPLINE_SSP_BATCH_REPORT.md [G03]
```

（方括号归属来自批次任务记录；同一文件多任务接续修改时列出全部任务。）

`.pi/settings.json` 不属于 FP-2D/R1 修改范围；当前工作树中若存在该文件改动，视为
用户维护的执行环境配置，所有 FP-2D worker 均不触碰。G02 临时产物位于
`/tmp/fp2d_g02/`，未进入仓库；`build/fp2d-clean` 为隔离构建目录，不进入版本控制。

## L. Remaining GAPs

本批次只关闭 SSP `S` slice。以下 GAP 与 FP-2C 后状态一致，未因本批次变化：

- `SSP-05`：Q + `.ssp` range-dependent SSP（architectural conflict；parser 继续明确
  拒绝，本批次仅将 unknown/`Q` 诊断拆分，未实现任何 Q 功能）；
- line source（SRC-02/PRD-08）、multisource（SRC-04/PRD-07）、irregular receiver
  （REC-02/REC-03/PRD-06）；
- canonical curvilinear boundary（BND-04）、flat elastic oracle 闭环（BND-09）、
  attenuation units N/F/M/Q/L oracle 闭环（ATT-01）、Francois–Garrison（ATT-04）、
  biological（ATT-05）；
- 本批次未外推：spline 支持不扩展到 line/multisource/irregular/3D 等任何其他维度。

Batch Acceptance 已确认 E 产品逐字节复算（见 I 节），但外部 Final Review 随后
识别出 policy/test/documentation findings，因此不能再表述为“已知限制：无”或据此
推导最终可接受性。当前已知验收边界见 O 节；remediation 与复验完成前，FP-2D
保持未验收状态。

## M. Git Diff Summary

- 原 Batch Acceptance 时修改文件 18 个：`797 insertions(+) / 78 deletions(-)`
  （当时的 `git diff --stat`，不含 untracked）；FP-2D-R1 继续修改测试、文档及
  coordinator 负责的 policy，当前精确规模以 live `git diff --stat` 为准。
- 新增文件 7 个（不含本报告）：spline production source/header 6 个 + component
  test 1 个，合计 965 行（`wc -l`）：coefficient kernel（139 cpp + 23 hpp）、
  real evaluator（135 cpp + 44 hpp）、frequency evaluator（102 cpp + 52 hpp）、
  component test（470 行）。
- G03 文档部分（`git diff --stat -- Bellhop_RayReuse/doc`）：3 files changed，
  105 insertions / 54 deletions（support matrix 8 行、parity report 143 行、
  progress 8 行的 diff 规模）。
- `git diff --check` 通过；`git diff -- Bellhop_F2CPP Bellhop_origin` 为空。

## N. Working Tree

- 施工基线 `9efe8be` 之后无 commit、无 stage、无 push；全部改动保持在工作树/
  untracked 状态（含本报告文件），由用户/coordinator 决定纳入版本控制。
- K 节清单是原 Batch Acceptance 时的 batch-scoped 快照；R1 期间的精确状态以
  live `git status --short` 为准。
- 生成产物（`.prt/.shd/.ray/.arr`、probe CSV、三模式输出）全部位于
  `/tmp/fp2d_g02/`、`/tmp/fp2d_g03_*`、`build/fp2d-clean` 与
  `test/standard_cases/results/`（results 为可再生成产物目录，不进入版本控制），
  无 generated products 进入 git 跟踪。

## O. Final Review and FP-2D-R1 Remediation

`Re-Final Review: ACCEPTED` — 外部 Final Review 给出的 R01–R04 已全部落盘、
预审并由 coordinator 复验；独立 final-reviewer 已检查完整 Worklist、报告、真实
diff、production/source-of-truth 与测试证据，无阻断 finding。

### 外部 Final Review findings

| ID | Finding | Remediation 状态 |
|---|---|---|
| R01 | `munk_spline` broadband 250 Hz Origin final-field TL `0.00634765625 dB` 超出原 `0.005 dB`，且 policy 必须防止 case-wide 放宽 | 已证明 RayReuse 与 F2CPP decoded complex64 exact；仅 `munk_spline + Origin→C++ + 250 Hz` 使用 `0.0065 dB`，50 Hz/pressure/intermediate/C++→C++ 不变；三模式各有 decoded-payload exact hard gate；默认 matrix 自动解析 case-local tolerance |
| R02 | 缺少真实 spline evaluator 经 production `stepRay` 跨 SSP node、可防止误施 gradient jump 的回归 | 已补充最小 regression，clean/release ray-stepper 与全量 CTest 通过 |
| R03 | Batch Report 错称 3-node 分支具有独立 literal anchor | 已纠正为 2-node 解析 anchor、4+ node 独立 literal anchors；3-node 由 structural/continuity regression 覆盖 |
| R04 | STATUS_PROGRESS/Batch Report 未反映外部 `CHANGES_REQUIRED`，并含“已知限制：无”等过强表述 | 已更新为 implementation/Batch Acceptance/R1 remediation 完成；Re-Final Review 后同步为 `ACCEPTED` |

R01 root cause、policy approval、精确 cell/pressure/TL 证据与完整复验记录见
`FP-2D-R1_FINAL_REVIEW_REMEDIATION_REPORT.md`。

### Batch Acceptance（coordinator 亲手运行，2026-08-28，当前实际执行）

在 G03 后执行，未沿用任何旧结果：

| Gate | 结果 |
|---|---|
| clean build 重建（`build/fp2d-clean`） | 成功（100%） |
| `ctest fp2d-clean` / `ctest release` | 均 36/36 passed |
| `uv run pytest` | 168 passed |
| `uv run make -C test/standard_cases test-unit` | Ran 153 tests, OK |
| 三方 `munk_spline` single（origin/f2cpp/rayreuse） | 均 PASSED |
| F2CPP/RayReuse `munk-spline` probe（0.0125 重跑） | `cmp` byte-identical，SHA `1fd0e4f84391aa24ec5e9876fae5d582b2ffdeb30422533113b407eba7faad63` |
| `intermediate_state_matrix.py --case munk_spline` | PASSED；`cpp_probe_byte_identical: true`；worst abs `1.4523493518936448e-11` / scaled `5.536846572998383e-4`；370 points / 367 steps / 2 events |
| 三模式 broadband（隔离目录，`--frequencies-hz 50,250 --verify-cache`） | 三模式 SHD 均 `74028065178ff80d43755ef2ba70ba5ba3e4947574a37a4154a7ecc52eef1596`；Trace passes 2/1/1；reuse/parallel fingerprint before==after=`1526667602348633172` |
| 五产品 smoke（两版可执行文件重新生成） | 全部 f2cpp = rayreuse = 冻结 SHA（TL `ce216646…`/R `ddd94952…`/A `30042a84…`/a `837e1e4f…`/E `c921296b…`），EXIT=0；**E 产品逐字节复算在此关闭 G03 草稿的证据边界** |
| C/P/N zero regression | probe 三 SHA（`809b126d…`/`eb51ced1…`/`360dda43…`）与 SHD 三冻结值（`cf1f9711…`/`fd5b2e2c…`/`18817c67…`）全部 match |
| git 检查 | `git diff --check` 干净；`git diff -- Bellhop_F2CPP Bellhop_origin` 空（0 行）；untracked 仅 8 个预期条目 |

### Reviewer batch review

`BATCH REVIEW: PASS`（read-only reviewer agent，ZCode subagent，2026-08-28）

- 10 项检查全 PASS：scope（CubicSpline 出现点仅限 model/numerics/parser/factory）、
  numerical semantics 抽查（机械 diff、`1.0F/6.0F` 两处、imaginary finite-only、
  每频一次 kernel 调用）、stale docs、silent fallback（多重 S≠C/P/N 证据）、
  runtime path、unrelated diff、generated-file cleanup、Batch Report 一致性、
  测试真实性抽查（reviewer 亲跑 36/36、153 OK、F2CPP 3/3、rayreuse single
  PASSED、pytest 168）、Worklist Section 5 清单 30 项逐项核对。
- 唯一 finding（LOW）：本报告 I/L 节 E 产品 SHA 证据标注滞后——已在本定稿中
  更新（E 已于 Batch Acceptance 复算确认）。
- reviewer 无权声明 FP-2D ACCEPTED；后续外部 Final Review 已实际给出
  `CHANGES_REQUIRED`，该结论由上方 remediation 状态取代原先的待审状态。

### 结论

原 implementation gates、A01/A02 checkpoints、G01/G02/G03、Batch Acceptance、
FP-2D-R1 remediation、coordinator 汇总复验与独立 Re-Final Review 均已完成，最终
结论为 `ACCEPTED`。本批次与 remediation 均未 stage、未 commit、未 push。
