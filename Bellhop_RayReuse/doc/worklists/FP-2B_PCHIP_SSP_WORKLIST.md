# FP-2B — PCHIP SSP Parity

> Workers may not expand FP-2B scope.
>
> 本清单由 Codex 完成架构设计。OpenCode `general-worker` 负责批次编排：
> `[GENERAL]` 任务由 Gemini 3.7 Flash 执行；遇到 `[ADVANCED]` 任务时，必须通过
> `task(advanced-worker)` 交给 GLM-5.3。施工完成后只能声明
> **Ready for Codex Review**，不得自行声明 **FP-2B accepted**。

## Architecture Decisions

### 1. 最小 Real SSP abstraction

RayReuse 当前将 `CLinearSsp` 直接绑定在 `SimulationCase`、
`GeometryTracer`、`RayStepper`、`CartesianCervenyInfluence` 和
`FrequencyProjector` 等路径。FP-2B 采用与 F2CPP production 设计一致、但只覆盖
本阶段范围的 value-owned evaluator：

```text
SoundSpeedProfile
    interpolationKind = CLinear | Pchip
              │
              ├── GeometrySspEvaluator
              │       ├── CLinearSsp
              │       └── PchipSsp
              │
              └── FrequencySspEvaluator(f)
                      ├── CLinearFrequencySsp
                      └── PchipFrequencySsp
```

- evaluator 应不可变或只读使用，并由使用方按值拥有；禁止全局 current SSP 或
  current frequency。
- `SoundSpeedSample`、Hessian 和 gradient-continuity 等公共类型从具体
  `CLinearSsp` 后端中最小解耦。
- 本轮接口只需要 depth segment，不引入 Q SSP 所需的 range-segment、二维网格或
  插件接口。
- 不为 N/S/Q 预建 backend、factory 分支或占位类。
- C-linear 公式、segment hint、外推和 evaluation order 保持原样；wrapper 只做
  backend dispatch，不重写已冻结的 C-linear 数值实现。

### 2. PCHIP 是真实 evaluator

PCHIP 必须直接迁移 F2CPP 当前 production 语义，source of truth 为：

- `Bellhop_F2CPP/src/numerics/pchip_coefficients.cpp`
- `Bellhop_F2CPP/src/model/pchip_ssp.cpp`
- `Bellhop_F2CPP/src/model/pchip_frequency_ssp.cpp`
- `Bellhop_F2CPP/src/model/sound_speed_evaluator.cpp`
- `Bellhop_F2CPP/tests/component/pchip_ssp_test.cpp`
- `Bellhop_F2CPP/tests/component/sound_speed_evaluator_test.cpp`

禁止以下捷径：

```text
PCHIP → dense resampling → C-linear
```

也禁止使用 SciPy、Boost 或第三方 PCHIP library 代替 production 算法。必须保留：

1. depth interval 和 complex secant 的生成顺序；
2. endpoint slope estimate 与 endpoint limiter；
3. Origin `splinec.f90::CSPLINE` 风格的 clamped-spline elimination/back-substitution
   顺序；
4. interior monotonicity limiter；
5. real/imaginary 分量分别 limiter；
6. cubic Hermite coefficient 的 binary64 evaluation order；
7. Horner 顺序的 `c(z)`、`dc/dz` 和 `d²c/dz²`；
8. exact-node hinted arrival-side segment、首末段 cubic extrapolation；
9. density 仍在节点之间线性插值。

两节点 profile 必须精确退化为直线；不能使用一般三次流程制造舍入差异。

### 3. Gradient continuity 与 dynamic ray

增加最小 continuity 语义：

```text
C-linear = DiscontinuousAtNodes
PCHIP    = ContinuousAtNodes
```

- 现有 SSP depth-node step limiter 对 C 和 P 都保留，保持 F2CPP 的节点到达和最小
  forward-step 行为。
- `applyCLinearGradientJump` 只允许在 discontinuous backend 上执行；PCHIP 到达节点
  时不得应用 C-linear 的 gradient jump。
- PCHIP 的非零 `d²c/dz²` 必须进入现有 dynamic-ray equation，真实改变 dynamic
  `p/q`；只实现 `c` 与 `dc/dz` 不算完成。
- boundary collision 继续按 F2CPP 的到达端 SSP sample/gradient 时序计算，不另建
  boundary SSP 近似。

### 4. Frozen trajectory contract

PCHIP 是 real geometry environment。reuse/parallel 必须保持：

```text
real PCHIP environment
        ↓
trace geometry once
        ↓
Frozen RayPathCache
        ↓
per-frequency projection
        ↓
Influence / R / A / a / E product
```

Frozen cache 可以保存由 PCHIP real evaluator 生成的 trajectory、real slowness、
real sound speed/travel time、real dynamic `p/q`、reflection topology 和 geometry
identity。PCHIP coefficients 属于 immutable evaluator derived state，不属于
`RayPathCache`。

禁止 reuse/parallel 为每个频率重新 trace，也禁止把 complex travel time、
attenuation、reflection amplitude/phase、active prefix、ArrivalWorkspace 或
Eigenray hits 写回 frozen cache。

### 5. Geometry SSP 与 frequency-local SSP 分离

Geometry 使用 real PCHIP：

- `c(z)`、`dc/dz`、`d²c/dz²`；
- launch planning、trajectory、dynamic ray；
- boundary reflection geometry；
- Influence 中依赖 real SSP 的 local sampling。

每个目标频率独立构造 frequency evaluator：

- 先按当前 RayReuse 已支持的 attenuation 语义转换每个 SSP node；
- 用 complex node sound speed 构造同一套 PCHIP coefficients；
- real/imaginary 分量分别 limiter；
- complex travel time、reflection coefficient、amplitude/phase prefix 保持逐频。

lossless fast path 可以继续使用 frozen real travel time，但必须由
`PchipFrequencySsp::isLossless()` 的 production 语义驱动；不能用 C-linear
imaginary interpolation 代替 complex PCHIP。

### 6. Product integration boundary

P SSP 通过共享 evaluator 接入当前已支持的 TL、R、A/a/E 产品，不建立
product-specific PCHIP solver：

```text
parser/model
  → shared real trace
  → shared per-frequency projection
  → existing beam/product implementation
  → existing writer/lifecycle
```

产品格式、命名、publish order、stale cleanup 和 failure cleanup 不变。R 使用 PCHIP
real geometry；A/a/E 继续消费逐频 acoustic state。当前 source、receiver、beam、
boundary 和 attenuation 限制保持不变。

### 7. Oracle 与 zero-regression strategy

- 直接复用 `test/standard_cases/cases/munk_pchip/`，不建立第二个 PCHIP 算例库。
- 复用现有 Origin trajectory oracle、F2CPP geometry probe、SHD comparator、
  standard-case adapter 和 cache fingerprint 机制。
- C-linear 结果应尽可能 byte-for-byte/bitwise 不变；若任何 C-linear observable
  改变，必须先定位原因，禁止先放宽 tolerance。
- PCHIP 必须证明 geometry/dynamic ray 与同节点 C-linear 不同，同时与 F2CPP
  production evaluator 和 Origin oracle 一致。
- 不创建 beam × product × execution mode 笛卡尔积；使用一个高信息量
  `munk_pchip` case、component anchors 和少量产品 smoke 覆盖公共接入。

## Scope

### In scope

- environment top SSP option `P`；
- minimal `CLinear | Pchip` model/evaluator dispatch；
- F2CPP-compatible PCHIP coefficients、endpoint/interior slopes、limiter、
  interval evaluation、derivative/Hessian 和 extrapolation；
- real geometry、dynamic ray、boundary geometry sampling；
- frequency-local complex PCHIP projection；
- 当前已支持 TL beam families 以及 R/A/a/E 对 P 的公共接入；
- nonreuse/reuse/parallel、cache fingerprint、Origin/F2CPP oracle；
- 最小 component/parser/product regression、共享 `munk_pchip` 接入和文档同步。

### Out of scope

- N²-linear `N`；
- cubic spline `S`；
- quadrilateral `Q` 和 `.ssp`；
- line source；
- multisource；
- irregular receiver；
- canonical curvilinear boundary；
- attenuation 新模型；
- Influence Geometry Reuse；
- frequency interpolation；
- shared-library/plugin framework 或整个 Influence hierarchy 重构。

如果 worker 发现 future feature 的接口依赖，只记录 dependency，并做当前 C/P 所需
的最小兼容设计。若本清单的架构与真实 production 代码存在根本冲突，停止对应
任务并在 Batch Report 中标记 `ARCHITECTURE_BLOCKER`；不得自行重设计整个阶段。

## Audited Dependency Map

施工前已确认的真实硬绑定包括：

- parser：`Bellhop_RayReuse/src/io/environment_parser.cpp` 仅接受 SSP `C`；
- model/source launch：`src/model/simulation_case.cpp` 直接构造 `CLinearSsp`；
- geometry：`include/rayreuse/ray/geometry_tracer.hpp` 与
  `src/ray/geometry_tracer.cpp` 持有/调用 `CLinearSsp`；
- stepper：`include/rayreuse/ray/ray_stepper.hpp` 与 `src/ray/ray_stepper.cpp`
  的参数和 gradient jump 为 C-linear 专用；
- Influence：`include/rayreuse/field/cartesian_cerveny_influence.hpp` 与
  `src/field/cartesian_cerveny_influence.cpp` 直接持有
  `CLinearSsp`；
- per-frequency projection：`src/field/frequency_projector.cpp` 直接构造
  `CLinearFrequencySsp`；
- source sampling：`src/solver/single_frequency_solver.cpp` 直接构造
  `CLinearSsp`；
- geometry oracle probe：`tests/tools/geometry_oracle_probe.cpp` 只有 `munk`，没有
  `munk-pchip`；
- shared case：`test/standard_cases/cases/munk_pchip/case.toml` 当前仅允许
  Origin/F2CPP 且只有 single profile。

以下工作项按上述真实依赖组织。

## G01 [GENERAL] — Baseline、SSP schema 与 parser plumbing

depends_on: none

can_parallelize_with: none

### Goal

冻结可复现的 C-linear 基线，并让环境模型和 parser 明确表达 `CLinear | Pchip`，
不允许 P silent fallback 到 C。

### Context

F2CPP 在 `environment_parser.cpp` 以 top option 第一字符选择 SSP interpolation。
RayReuse 当前仅接受 `C`，`SoundSpeedProfile` 也未保存 interpolation kind。

### Scope

- 记录施工起始 HEAD、RayReuse full CTest 状态及代表性 C-linear geometry/SHD hash；
- 增加只包含 `CLinear`、`Pchip` 的 interpolation enum；
- `SoundSpeedProfile` 保存 kind，并保持 programmatic/default construction 为 C-linear；
- parser 接受 `P`，保留原始 `C` 行为；
- `N/S/Q` 和未知字符继续明确非零拒绝；
- CMake/source/test registration 的机械准备；
- 只在 F2CPP/Origin 已有 observable wording 可复用时增加 PRT 诊断；禁止发明新
  文件格式或 CLI option。

### Files likely involved

- `Bellhop_RayReuse/include/rayreuse/model/environment.hpp`
- `Bellhop_RayReuse/src/model/environment.cpp`
- `Bellhop_RayReuse/include/rayreuse/model/sound_speed_types.hpp`（如采用）
- `Bellhop_RayReuse/src/io/environment_parser.cpp`
- `Bellhop_RayReuse/tests/component/environment_parser_test.cpp`
- `Bellhop_RayReuse/CMakeLists.txt`

### Required changes

1. enum 不包含 future N/S/Q backend。
2. 所有旧 constructor/call site 未显式给 kind 时仍选择 C-linear。
3. parser 将 `P` 存入 model；不得仅“接受字符”后仍构造 C-linear。
4. validation error 明确区分 unsupported SSP kind，不能退化为当前 TL/产品路径。
5. 将 baseline identity 和结果放入最终 Batch Report，不提交生成的 `.shd/.prt`。

### Must preserve

- 所有现有 `C...` 环境的 parse/model observable；
- top option 其他字符的现有限制；
- source/receiver/boundary/attenuation/beam validation；
- Bellhop_F2CPP 与 Bellhop_origin production 文件只读。

### Acceptance

- parser/model unit test 证明 `C` 保持 `CLinear`、`P` 保持 `Pchip`；
- 代表性真实 `.env` 的 top option `P` 可 parse，不发生 silent fallback；
- `N/S/Q` 与未知 kind 仍以明确诊断非零退出；
- programmatic legacy fixture 未指定 kind 时仍为 C-linear；
- targeted parser/model tests 通过；
- baseline HEAD、CTest 状态、C-linear geometry/SHD hashes 已记录供 G04 比较。

### Handoff to next task

向 A01 提供稳定的 `SoundSpeedProfile::interpolationKind()` 和公共 sample type 落点；
不得提前实现 PCHIP 数值核。

## A01 [ADVANCED] — Exact PCHIP coefficient kernel 与 concrete evaluators

depends_on: G01

can_parallelize_with: G03（仅 shared-case/adapter 准备部分）

### Goal

逐行迁移 F2CPP production PCHIP coefficient/evaluation semantics，建立独立可测的
`PchipSsp` 与 `PchipFrequencySsp`，不接入 GeometryTracer。

### Context

这是数值核心。F2CPP 并非常见的简单 harmonic-mean PCHIP：它先用 endpoint
derivatives 构造 clamped spline elimination/back-substitution，再对 interior
derivatives 做 monotonicity limiter。代数等价但 evaluation order 不同的实现会改变
动态射线和 oracle。

### Source-of-truth files

- `Bellhop_F2CPP/include/bellhop/numerics/pchip_coefficients.hpp`
- `Bellhop_F2CPP/src/numerics/pchip_coefficients.cpp`
- `Bellhop_F2CPP/include/bellhop/model/pchip_ssp.hpp`
- `Bellhop_F2CPP/src/model/pchip_ssp.cpp`
- `Bellhop_F2CPP/include/bellhop/model/pchip_frequency_ssp.hpp`
- `Bellhop_F2CPP/src/model/pchip_frequency_ssp.cpp`
- `Bellhop_F2CPP/tests/component/pchip_ssp_test.cpp`
- `Bellhop_origin/misc/pchipMod.f90`
- `Bellhop_origin/misc/splinec.f90`

### Target files likely involved

- `Bellhop_RayReuse/include/rayreuse/numerics/pchip_coefficients.hpp`
- `Bellhop_RayReuse/src/numerics/pchip_coefficients.cpp`
- `Bellhop_RayReuse/include/rayreuse/model/pchip_ssp.hpp`
- `Bellhop_RayReuse/src/model/pchip_ssp.cpp`
- `Bellhop_RayReuse/include/rayreuse/model/pchip_frequency_ssp.hpp`
- `Bellhop_RayReuse/src/model/pchip_frequency_ssp.cpp`
- `Bellhop_RayReuse/include/rayreuse/model/sound_speed_types.hpp`
- `Bellhop_RayReuse/tests/component/pchip_ssp_test.cpp`
- `Bellhop_RayReuse/CMakeLists.txt`

### Required changes

1. 保持 F2CPP 的 intervals、complex secants、endpoint estimates、endpoint limit、
   clamped-spline elimination、back-substitution、interior limit、coefficient
   construction 顺序。
2. endpoint formulas、`limitLeftEndpoint`、`limitRightEndpoint`、
   `limitInterior` 和 `limitParts` 的 branch/乘法顺序与 F2CPP 一致。
3. coefficient 使用：

   ```text
   a0 = value[i]
   a1 = derivative[i]
   a2 = (3*Δ - h*(2*d[i] + d[i+1])) / (h*h)
   a3 = (h*(d[i] + d[i+1]) - 2*Δ) / (h*h*h)
   ```

4. evaluation 保持 Horner 顺序，并返回 real `c`、`dc/dz`、`d²c/dz²`；range
   gradient/Hessian 为零。
5. density 与 F2CPP 一样线性插值；profile 外用首/末 cubic segment 外推。
6. exact node 优先保留 hinted segment；没有有效 hint 时按 F2CPP lower-bound 语义
   选择 segment。
7. `PchipFrequencySsp` 先在节点做当前 attenuation conversion，再用 complex values
   重建 coefficients；real/imaginary 分量独立 limiter。
8. `isLossless()` 和 `uniformComplexSoundSpeed()` 使用 F2CPP 的 exact node-value
   语义。
9. 两节点输入走 exact linear special case；无效、非有限、非递增输入明确失败。

### Forbidden shortcuts

- dense resampling；
- 调用 C-linear 作为 production evaluator；
- harmonic-mean 近似代替 F2CPP elimination；
- 对 complex magnitude/phase 做 limiter；
- 改写为数学等价但舍入顺序不同的公式；
- 第三方 PCHIP library；
- 修改 F2CPP production code或 Origin。

### Must preserve

- `CLinearSsp` 和 `CLinearFrequencySsp` 公式、branch 和输出；
- 当前 attenuation unit conversion；
- binary64；除 F2CPP 明确使用外不引入 float/long double；
- evaluator 不拥有或修改 RayPathCache。

### Acceptance

- two-point exact-linear anchors 与 F2CPP 一致；
- Munk surface、多个 interior depths 的 `c/dc/dz/d²c/dz²` anchors 一致；
- endpoint derivative、interior derivative、peak/plateau monotonicity limiter anchors
  一致；
- exact-node left/right hinted semantics 和 top/bottom cubic extrapolation 一致；
- complex 50 Hz attenuation anchors、imaginary interpolation、lossless/uniform flags
  与 F2CPP 一致；
- coefficient/evaluator invalid-input tests 与 F2CPP 行为一致；
- test 明确证明实现不是 dense-C-linear approximation；
- targeted PCHIP component tests 通过，且现有 C-linear component tests 无变化。

### Handoff to next task

向 A02/A03 提供经过 anchor 验证、只读使用的 real/frequency concrete backend；
Gemini 汇总时必须附 GLM 的重要 numerical decisions。

## A02 [ADVANCED] — Minimal geometry evaluator 与 dynamic-ray integration

depends_on: A01

can_parallelize_with: G03（仅测试工具/manifest 准备部分）

### Goal

用最小 `GeometrySspEvaluator<CLinear,Pchip>` 解耦所有 real-geometry C-linear
硬绑定，并让 PCHIP derivative/Hessian 正确进入 tracing、dynamic ray、boundary 和
Influence。

### Context

RayReuse 现有 `RayStepper` 在 segment 改变时无条件执行 C-linear gradient jump；
PCHIP 一阶导数连续且二阶导数非零。错误处理任一项都会改变 trajectory、dynamic
`p/q`、KMAH、beam membership 和产品输出。

### Source-of-truth files

- `Bellhop_F2CPP/include/bellhop/model/sound_speed_evaluator.hpp`
- `Bellhop_F2CPP/src/model/sound_speed_evaluator.cpp`
- `Bellhop_F2CPP/src/ray/geometry_tracer.cpp`
- `Bellhop_F2CPP/src/ray/ray_stepper.cpp`
- `Bellhop_F2CPP/src/model/simulation_case.cpp`
- `Bellhop_F2CPP/src/solver/single_frequency_solver.cpp`
- `Bellhop_F2CPP/src/field/cartesian_cerveny_influence.cpp`
- F2CPP geometry/PCHIP component tests and geometry oracle probe

### Target files likely involved

- `Bellhop_RayReuse/include/rayreuse/model/sound_speed_evaluator.hpp`
- `Bellhop_RayReuse/src/model/sound_speed_evaluator.cpp`
- `Bellhop_RayReuse/include/rayreuse/model/sound_speed_types.hpp`
- `Bellhop_RayReuse/include/rayreuse/ray/geometry_tracer.hpp`
- `Bellhop_RayReuse/src/ray/geometry_tracer.cpp`
- `Bellhop_RayReuse/include/rayreuse/ray/ray_stepper.hpp`
- `Bellhop_RayReuse/src/ray/ray_stepper.cpp`
- `Bellhop_RayReuse/src/model/simulation_case.cpp`
- `Bellhop_RayReuse/src/solver/single_frequency_solver.cpp`
- `Bellhop_RayReuse/include/rayreuse/field/cartesian_cerveny_influence.hpp`
- `Bellhop_RayReuse/src/field/cartesian_cerveny_influence.cpp`
- related component tests

### Required changes

1. 实现 value-owned、只含 `CLinearSsp | PchipSsp` 的 real evaluator dispatch；接口只
   暴露当前 depth-only consumers 实际需要的方法。
2. 引入 `SspGradientContinuity`，C-linear 报告 discontinuous、PCHIP 报告
   continuous。
3. `GeometryTracer`、`RayStepper`、source initialization、launch planning、
   `SingleFrequencySolver` source sampling 与 Cartesian Cerveny local sampling 使用
   同一个 interpolation kind。
4. 保留 SSP depth-node step reduction/minimum-forward-step；只对 discontinuous
   backend 调用现有 gradient jump。
5. PCHIP `d²c/dz²` 进入 `soundSpeedNormalSecondDerivativeOverSquaredSpeed` 和
   modified Heun/box dynamic equations，不能归零或有限差分近似。
6. boundary collision 使用 F2CPP 相同的 arrival sample、gradient 与 segment hint
   顺序。
7. 审计 production `CLinearSsp` 直接引用：允许 concrete backend/factory 保留，
   核心 consumers 不应再绕开 evaluator。
8. 不加入 Q 的 range segment、future virtual hierarchy 或 geometry cache。

### Must preserve

- C-linear segment hint、gradient jump、step limiter 和 exact-node behavior；
- 现有 ray point/step/reflection event sequencing；
- existing Cartesian/ray-centered beam Influence observable；
- Frozen RayPathCache 只保存 real geometry/state；
- no per-frequency trace in reuse/parallel。

### Acceptance

- evaluator dispatch test 证明 C/P 分别选择正确 backend，未知 enum 无 fallback；
- C-linear geometry probe CSV、cache fingerprint 和代表性 SHD 与 G01 baseline
  byte-identical；若平台现有 comparator 不承诺 byte identity，则使用既有更严格
  comparator并在 Batch Report 说明，禁止放宽 tolerance；
- PCHIP node traversal 保留 F2CPP step limiter，但不执行 C-linear gradient jump；
- PCHIP geometry test 证明 `d²c/dz²` 非零并真实改变 dynamic `p/q`；
- source sound speed/gradient、launch fan、tracer 与 Cartesian Cerveny Influence 使用
  相同 P evaluator；
- reflected anchor 证明 boundary SSP sample/gradient 与 F2CPP 一致；
- targeted GeometryTracer/RayStepper/Influence/solver tests 通过；
- 没有新增 range-dependent SSP 或 future feature API。

### Handoff to next task

向 A03 提供冻结的 real evaluator API、可重复的 PCHIP geometry cache 和 C-linear
zero-regression evidence。

## A03 [ADVANCED] — Frequency-local PCHIP projection 与 cache contract

depends_on: A02

can_parallelize_with: G02（仅 product-test scaffolding）、G03（仅 oracle scaffolding）

### Goal

接入最小 `FrequencySspEvaluator<CLinear,Pchip>`，使每频 complex travel time、
attenuation 和 reflection sampling 使用 PCHIP production semantics，同时保持 frozen
cache 不受污染。

### Context

RayReuse 当前 `FrequencyProjector` 每次直接构造 `CLinearFrequencySsp`。PCHIP 的
imaginary sound speed 不是对 real PCHIP 结果做后处理，也不是节点 imaginary 的
C-linear interpolation；它需要从目标频率 complex node values 重新构造同一 PCHIP
polynomial。

### Source-of-truth files

- `Bellhop_F2CPP/src/model/pchip_frequency_ssp.cpp`
- `Bellhop_F2CPP/src/model/sound_speed_evaluator.cpp`
- `Bellhop_F2CPP/src/field/frequency_projector.cpp`
- F2CPP frequency-projector/PCHIP tests
- RayReuse current `c_linear_frequency_ssp.*`、`frequency_projector.cpp` 和 cache tests

### Target files likely involved

- `Bellhop_RayReuse/include/rayreuse/model/sound_speed_evaluator.hpp`
- `Bellhop_RayReuse/src/model/sound_speed_evaluator.cpp`
- `Bellhop_RayReuse/include/rayreuse/model/pchip_frequency_ssp.hpp`
- `Bellhop_RayReuse/src/model/pchip_frequency_ssp.cpp`
- `Bellhop_RayReuse/src/field/frequency_projector.cpp`
- `Bellhop_RayReuse/tests/component/frequency_projector_test.cpp`
- broadband/reuse/parallel component tests

### Required changes

1. frequency evaluator variant 只包含 C-linear/PCHIP，且不存在 default-to-C fallback。
2. 每个 `project(cache, frequency)` 构造或拥有该 frequency 的 immutable evaluator；
   worker 之间不共享 mutable evaluator/state。
3. target frequency 下按当前 attenuation contract 转换每个 node，再构造 complex
   PCHIP coefficients；保持 A01 evaluation order。
4. lossless PCHIP 可复用 frozen real travel time；attenuating PCHIP 沿现有 start/
   midpoint/end quadrature 积分 complex slowness。
5. reflection water sound speed/density sample、reflection coefficient、complex travel
   time、amplitude/phase 和 active prefix 全部来自本频率 evaluator/state。
6. projection 前后 cache fingerprint 必须一致；不得将 epsilon、complex tau、
   reflection result 或 product state写入 cache。
7. 不新增 frequency sampling/interpolation 或 cross-frequency kernel reuse。

### Must preserve

- C-linear frequency projection 的数值/evaluation order；
- current attenuation subset 和 unit conversion；
- reflection event identity 与 frozen topology；
- per-frequency `RayFrequencyState` ownership；
- serial publication order 和 existing writer lifecycle。

### Acceptance

- frequency evaluator unit test 证明 C/P dispatch 正确且无 fallback；
- PCHIP lossless and attenuating projection anchors 与 F2CPP 一致；
- complex travel-time start/midpoint/end 和 reflection sample anchors 一致；
- 相同 frozen PCHIP cache 投影两个频率得到独立、不同且稳定的 acoustic state；
- projection 前后 cache fingerprint 不变；
- nonreuse/reuse/parallel 的 per-frequency states/products 适用时 byte-identical；
- ThreadSanitizer 不是本阶段硬门槛，但测试设计不得依赖共享 mutable evaluator；
- C-linear frequency-projector、broadband、reuse、parallel tests 无回归。

### Handoff to next task

向 G02 提供已验证的 real cache + per-frequency P projector 公共路径；若 numerical
parity 无法解释，返回 GLM 调试，不能通过产品层 tolerance 掩盖。

## G02 [GENERAL] — TL/R/A/a/E runtime plumbing 与最小产品回归

depends_on: A03

can_parallelize_with: G03（oracle/manifest 准备与 product-test scaffolding）

### Goal

让 P SSP 从正式 executable 端到端进入当前适用的 TL、R、A、a、E 公共路径，并
证明不存在 product-specific C-linear fallback。

### Context

核心 solver 已共享 GeometryTracer/FrequencyProjector。正常情况下本任务只需机械
validation/plumbing 和最小高信息量测试，不得为每种 beam/product 复制 PCHIP
实现。

### Scope

- 检查 `SimulationCase` validation、solver dispatch、app/main lifecycle 对 P 的处理；
- 当前已支持 TL beam/coordinate families 通过共享 evaluator 获得 P；
- R 使用 P real geometry；
- A/a/E 使用 P geometry + frequency-local acoustic state；
- products 的 writer、file format、naming、cleanup 和 publish order不变；
- 用 targeted matrix/smoke 证明公共接入，不创建组合爆炸测试。

### Files likely involved

- `Bellhop_RayReuse/src/model/simulation_case.cpp`
- `Bellhop_RayReuse/src/solver/single_frequency_solver.cpp`
- `Bellhop_RayReuse/src/solver/ray_trace_solver.cpp`
- `Bellhop_RayReuse/src/solver/arrival_solver.cpp`
- `Bellhop_RayReuse/src/solver/eigenray_solver.cpp`
- `Bellhop_RayReuse/app/main.cpp`（仅存在真实 P validation/lifecycle gap 时）
- related parser/solver/product tests

### Required changes

1. 移除或收窄任何遗留的 “C-linear only” product validation，但不放宽 source、
   receiver、beam、boundary 或 attenuation 范围。
2. 不为各 beam/product 增加 PCHIP switch；所有路径消费共享 evaluator/cache。
3. 加入最小 runtime matrix：代表性 TL、R、A、a、E P inputs；A/a 只验证 encoding
   差异，不复制 candidate test。
4. 至少一个非平凡 Munk anchor 证明 P 与相同节点 C 的 geometry/output 不同，防止
   parser-accepts-but-runtime-C-fallback。
5. P failure 使用现有 atomic publish/failure cleanup；不改变产品命名。

### Must preserve

- current TL coherence/beam/component/width/curvature semantics；
- R/A/a/E traversal、candidate/hit、writer和lifecycle；
- current source/receiver restrictions；
- N/S/Q、line、multisource、irregular 继续拒绝。

### Acceptance

- 正式 executable 可运行至少一个 P TL、P R、P A、P a、P E 代表输入；
- targeted dispatch test 覆盖所有当前 TL family 通过同一 evaluator factory，不要求
  为每个 family 建 standard case；
- P 与 C 的非平凡 Munk geometry/output 明显不同，且 P 与 F2CPP一致；
- R/A/a/E observable schema、sequencing 和 writer bytes 在适用 comparator下不变；
- unsupported N/S/Q 和本阶段其他 out-of-scope 组合仍明确非零拒绝；
- existing product switching/stale cleanup/failure cleanup tests 通过；
- 没有新增 product-specific SSP implementation。

### Handoff to next task

向 G03 提供正式 executable、代表性 product commands 和 mode outputs；测试文件与
生成产品不得进入共享 case source 目录。

## G03 [GENERAL] — Shared standard case、geometry oracle 与 execution parity

depends_on: G01

can_parallelize_with: A01、A02、A03、G02（仅准备；最终执行依赖 A03/G02）

### Goal

把 RayReuse 加入既有共享 `munk_pchip` 资产，复用 Origin/F2CPP oracle 和现有
比较工具，闭环 PCHIP geometry、SHD 和三执行模式证据。

### Context

`test/standard_cases/cases/munk_pchip/` 已有 Origin/F2CPP production case 和
容差，无需新增第二个 PCHIP case。`compare_f2cpp_geometry_oracle.py` 已认识
`munk-pchip`；RayReuse probe 和 `intermediate_state_matrix.py` 尚未接入该配置。

### Scope

- `munk_pchip` compatibility 增加 RayReuse；
- 增加与 Munk 现有 policy 一致的 `broadband_smoke = [50.0, 250.0]`；
- RayReuse geometry probe 支持 `munk-pchip`，fixture 明确设置 P kind；
- intermediate-state matrix 增加 `munk_pchip → munk-pchip`；
- 更新现有 standard-case model/allow-list tests；
- single-frequency Origin/F2CPP/RayReuse 与 broadband modes；
- 除非记录现有 corpus 的真实 correctness gap，否则不新增 case。

### Files likely involved

- `test/standard_cases/cases/munk_pchip/case.toml`
- `test/standard_cases/codes/intermediate_state_matrix.py`
- `test/standard_cases/codes/tests/test_case_model.py`
- `test/standard_cases/codes/tests/test_standard_cases.py`
- `Bellhop_RayReuse/tests/tools/geometry_oracle_probe.cpp`
- `Bellhop_RayReuse/tests/support/munk_case_fixture.*`
- existing standard-case allow-list/manifest files only as required

### Required changes

1. 复用原 case template、launch policy、dimensions 与已冻结 tolerance；不得复制
   `munk_pchip`。
2. Probe 输出 schema、precision、column order 与现有 F2CPP/RayReuse probe不变。
3. intermediate matrix 继续要求 F2CPP/RayReuse probe CSV byte-identical；如失败，
   先回到 A01/A02 定位 evaluation order，不能给 P 添加宽松例外。
4. Origin comparison 使用当前 `compare_f2cpp_geometry_oracle.py` 的既有 field
   tolerances；禁止自行放宽。
5. RayReuse broadband smoke 运行 nonreuse/reuse/parallel，并验证 publish order、
   per-frequency separation、byte equality 和 cache fingerprint。
6. 生成的 `.prt/.shd/.ray/.arr`、oracle 临时目录和 reports 不提交。

### Must preserve

- shared standard-case single source of truth；
- Origin/F2CPP case behavior；
- current standard adapter semantics；
- all unrelated case manifests and tolerances。

### Acceptance

- `munk_pchip` single profile 对 Origin、F2CPP、RayReuse 均可生成/运行/验证；
- Origin→F2CPP、Origin→RayReuse geometry oracle 在既有 tolerance 内通过；
- F2CPP 与 RayReuse `munk-pchip` geometry probe CSV byte-identical；
- F2CPP/RayReuse SHD 使用既有 case comparator通过；任何非零差异在 Batch Report
  解释，禁止修改 tolerance掩盖；
- RayReuse `broadband_smoke` nonreuse/reuse/parallel 每频产品一致、publish order稳定、
  cache fingerprint前后不变；
- `uv run make -C test/standard_cases test-unit` 通过；
- 没有新增重复 PCHIP case。

### Handoff to next task

最终 oracle 执行只能在 A03/G02 完成后进行。将命令、producer hashes、worst error、
mode hashes 和 cache fingerprints交给 G04 Batch Report。

## G04 [GENERAL] — Documentation closure、full validation 与 Batch Report

depends_on: A01、A02、A03、G02、G03

can_parallelize_with: none

### Goal

在全部实现和 targeted oracle 通过后更新实际 support 声明，执行批次验收并生成
供 Codex 最终审查的可复现报告。

### Context

本任务不再调数值算法。若 full regression 暴露原因不明的 core/numerical failure，
必须回交相应 `[ADVANCED]` 任务给 GLM，不得由 GENERAL worker 猜测修复或放宽
tolerance。

### Scope

- 根据真实 evidence 更新 RayReuse parity report、feature support matrix、usage/
  progress/roadmap 中的 P SSP slice；
- 明确 P 为 supported，N/S/Q 和所有本阶段排除项仍是 GAP/deferred；
- isolated clean build、full CTest、repository Python tests、standard cases、
  intermediate geometry gate、TL/R/A/a/E regression、execution parity、diff check；
- 输出规定格式的 FP-2B Batch Report。

### Files likely involved

- `Bellhop_RayReuse/doc/reports/REPORT_F2CPP_RAYREUSE_PARITY.md`
- RayReuse current feature support matrix
- RayReuse current usage/progress/feature-sync status documents
- build/test scripts only if an existing entry point has a real P registration gap
- Batch Report 的交付位置由 OpenCode batch约定决定；不要把生成产品纳入版本控制

### Required changes

1. 只标记 PCHIP `P` slice；不要把整个 SSP parity 或 N/S/Q关闭。
2. 文档明确 real geometry/frequency-local acoustic ownership与 frozen contract。
3. 使用现有完整入口，不建立新大型 regression framework。
4. 记录 compiler/build identity、Origin/F2CPP/RayReuse executable hashes、测试命令、
   oracle worst errors、execution-mode product hashes、cache fingerprints。
5. 原因不明的 regression 返回 A01/A02/A03；若架构根本冲突则报告
   `ARCHITECTURE_BLOCKER`。
6. 最终声明只能是 `Ready for Codex Review: YES/NO`。

### Must preserve

- Bellhop_F2CPP/Bellhop_origin production code不变；
- C-linear和所有既有 beam/product family；
- existing tests/cases作为统一资产；
- no future SSP/source/receiver/reuse research implementation。

### Acceptance

- `uv run ctest --test-dir Bellhop_RayReuse/build/release --output-on-failure`
  全通过；
- `uv run pytest` 全通过；
- `uv run make -C test/standard_cases test-unit` 全通过；
- `munk_pchip` Origin/F2CPP/RayReuse single 与 RayReuse broadband三模式通过；
- current intermediate-state gate（含 `munk_pchip`）通过；
- current TL/R/A/a/E representative regressions通过；
- isolated clean build通过；
- `git diff --check`通过；
- `git status --short` 被原样记录，未跟踪生成产品为零；
- Batch Report包含以下规定 sections，且未越权声明 accepted。

### Handoff to next task

无施工后续。将 Batch Report、diff、test logs 和 working-tree状态提交 Codex Final
Review；在 Codex结论前停止。

# Execution Order

```text
G01
  → A01
  → A02
  → A03
  → G02
  → G03 final oracle execution
  → G04
  → Batch Acceptance
  → FP-2B Batch Report
  → Codex Final Review
```

允许的有限并行：

```text
G01 完成后：
  A01/A02/A03 保持串行（共享数值类型和核心 evaluator API）
  G03 可并行准备 shared-case manifest、probe config 和 adapter tests

A02 API 冻结后：
  A03 执行 frequency-local integration
  G02 可并行准备 product-test scaffolding，但不得在 A03 前宣布通过

A03 + G02 完成后：
  G03 执行正式 oracle/mode matrix
```

不要为了增加并行度拆碎 A01/A02/A03；它们的接口和 evaluation order 存在真实依赖。

# Batch Acceptance

由 `general-worker` 完成所有 work items 后统一执行：

- [ ] P SSP parser 正式接通
- [ ] PCHIP evaluator production semantics 与 F2CPP 一致
- [ ] GeometryTracer 不再硬绑定 `CLinearSsp`
- [ ] C-linear 结果无数值回归
- [ ] PCHIP 真正改变 geometry/dynamic ray
- [ ] TL 支持 P
- [ ] 当前适用 R/A/a/E 支持 P
- [ ] nonreuse == reuse == parallel
- [ ] frozen cache fingerprint 稳定
- [ ] F2CPP oracle 通过
- [ ] Origin 既有 tolerance 通过
- [ ] RayReuse CTest 全部通过
- [ ] repository Python tests 全部通过
- [ ] standard-case tests 全部通过
- [ ] isolated clean build 通过
- [ ] `git diff --check` 通过
- [ ] F2CPP/Origin production source 无修改
- [ ] 无 N/S/Q 越界实现
- [ ] 无 line/multisource/irregular/canonical-curvilinear/attenuation-extension 越界实现
- [ ] 无 Influence Geometry Reuse 或 frequency interpolation
- [ ] 未跟踪生成产品为零

任一项失败时，Batch Report 的 `Ready for Codex Review` 必须为 `NO`，并给出失败
命令、首个错误和归属 work item。GENERAL worker 不得通过放宽 tolerance 或扩大
scope 将其改成通过。

# OpenCode Batch Report

施工完成后，`general-worker` 必须按以下格式输出：

```markdown
# FP-2B Batch Report

## A. Completed Tasks

## B. GENERAL Work

## C. ADVANCED Work
- task
- GLM implementation summary
- important numerical decisions

## D. Architecture Deviations
- none / details

## E. F2CPP Oracle

## F. Origin Oracle

## G. C-linear Zero Regression

## H. Execution Parity
- nonreuse
- reuse
- parallel

## I. Tests

## J. Files Changed

## K. Remaining GAPs

## L. Git Diff Summary

## M. Working Tree

## N. Ready for Codex Review
YES / NO
```

若有 deviation，必须指出它影响哪个 Architecture Decision、为什么不能遵守原设计、
是否构成 `ARCHITECTURE_BLOCKER`。只有 Codex 可以给出 FP-2B final acceptance。

# Codex Final Review

施工完成后，Codex 必须重点审查以下内容。

## Architecture

- SSP abstraction 是否只有 C/P 所需的最小 surface；
- 是否为 N/S/Q、range-dependent SSP 或插件体系过度设计；
- value ownership/lifetime 是否清晰，是否存在 global/current-frequency state；
- GeometryTracer、RayStepper、source launch、Cartesian Cerveny 和
  FrequencyProjector 是否都使用一致的 interpolation kind；
- frozen trajectory 与 per-frequency acoustic state 的边界是否保持；
- reuse/parallel 是否仍 trace once，worker 是否只持有 frequency-local mutable state。

## Numerical

- endpoint slopes 与 endpoint limiter；
- clamped-spline elimination/back-substitution 顺序；
- interior monotonicity limiter；
- complex real/imaginary independent limiting；
- cubic coefficients和 Horner evaluation order；
- interval lookup、hinted exact-node 与 cubic extrapolation；
- density linear interpolation；
- `dc/dz`、`d²c/dz²` 和 gradient continuity；
- mixed precision/binary64、literal、乘除顺序；
- dynamic-ray Hessian、gradient jump、node step limiter 和 boundary sampling；
- frequency-local attenuation conversion、complex coefficients 和 lossless fast path。

## Regression

- C-linear zero regression（geometry probe、cache fingerprint、SHD/products）；
- F2CPP PCHIP component/geometry/SHD parity；
- Origin trajectory/SHD parity；
- nonreuse/reuse/parallel per-frequency equality；
- current Cartesian/ray-centered Cerveny、GeoHat、GeoGaussian、Simple Gaussian；
- R/A/a/E writer/lifecycle；
- `.sbp`、C/I/S、F/M/W、D/S/Z、P/V/H existing behavior；
- full CTest、repository Python、standard-case、isolated build和 diff check。

## Scope

确认没有实现或提前开放：

- N；
- S；
- Q / `.ssp`；
- line source；
- multisource；
- irregular receiver；
- canonical curvilinear boundary；
- attenuation 新模型；
- Influence Geometry Reuse；
- frequency interpolation。

如果以上审查与 Batch Acceptance 全部通过且没有 unresolved correctness blocker，
Codex 才可以声明 FP-2B accepted；否则返回对应 work item 修正或报告 blocker。
