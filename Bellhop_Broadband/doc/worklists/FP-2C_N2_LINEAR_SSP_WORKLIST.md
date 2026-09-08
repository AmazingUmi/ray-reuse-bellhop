# FP-2C — N²-linear SSP (`N`) Parity Worklist

> **Batch:** FP-2C
> **Feature:** N²-linear SSP (`N`) parity
> **Production source of truth:** `Bellhop_F2CPP`
> **Numerical / observable oracle:** `Bellhop_origin`
> **Target implementation:** `Bellhop_RayReuse`
>
> 本批次严格禁止进入 cubic spline SSP `S` 或 quadrilateral/range-dependent SSP
> `Q/.ssp`。施工不得修改 `Bellhop_F2CPP` 或 `Bellhop_origin` production code。

## 1. Goal & Scope

### 1.1 Goal

在不改变 RayReuse 冻结轨迹与逐频声学状态边界的前提下，为
`Bellhop_RayReuse` 增加与当前 `Bellhop_F2CPP` production 实现一致的
N²-linear SSP：

```text
environment option `N`
        ↓
SoundSpeedProfile(interpolationKind = N2Linear)
        ├── GeometrySspEvaluator
        │       └── N2LinearSsp
        │               ├── c(z)
        │               ├── dc/dz
        │               ├── d²c/dz²
        │               └── density(z)
        │
        └── FrequencySspEvaluator(f)
                └── N2LinearFrequencySsp
                        ├── node attenuation conversion
                        ├── complex N² interpolation
                        ├── complex c(z)
                        └── frequency-local projection
```

完成后，`N` 必须从正式 `.env` 输入贯通当前 RayReuse 已支持的共享路径：

- real geometry tracing；
- SSP depth-node step reduction；
- dynamic ray curvature 与 node gradient jump；
- source/launch sampling；
- boundary-side water sampling；
- frequency-local complex travel-time projection；
- 当前适用的 TL、R、A、a、E 产品；
- `nonreuse`、`reuse`、`parallel` 三执行模式；
- frozen `RayPathCache` 一致性；
- F2CPP 与 Origin oracle。

### 1.2 In scope

- parser 接受 top SSP option `N`；
- `SspInterpolationKind::N2Linear`；
- `N2LinearSsp`；
- `N2LinearFrequencySsp`；
- `GeometrySspEvaluator<CLinear, Pchip, N2Linear>`；
- `FrequencySspEvaluator<CLinear, Pchip, N2Linear>`；
- N²-linear实数、复数声速、梯度、Hessian、密度、segment hint 和边缘外推语义；
- N²-linear在 profile node 处的一阶导数不连续语义；
- N²-linear非零 `d²c/dz²` 进入 dynamic ray；
- 当前 RayReuse attenuation 子集下的逐频节点转换；
- 当前 TL/R/A/a/E 公共 runtime path；
- 共享 `munk_n2` standard case、geometry probe、Origin/F2CPP/RayReuse 对照；
- C-linear 与 PCHIP zero-regression；
- support matrix、parity report、status 与 FP-2C Batch Report。

### 1.3 Out of scope

以下项目不得因接口相邻而进入 FP-2C：

- cubic spline SSP `S`；
- quadrilateral/range-dependent SSP `Q`；
- `.ssp` sidecar 解析、二维 SSP 网格或 range-segment API；
- F2CPP 的 Francois-Garrison/biological attenuation 能力向 RayReuse 扩展；
- 新 attenuation unit 或 volume attenuation model；
- line source；
- multisource；
- irregular/paired-irregular receiver；
- canonical curvilinear boundary；
- 3D / N×2D；
- Influence Geometry Reuse；
- frequency interpolation；
- cross-frequency N² coefficient cache；
- shared-library/plugin SSP framework；
- 与本功能无关的 evaluator、solver 或 writer 重构。

## 2. Architecture Decisions & Contracts

### 2.1 Audited current-state findings

#### Finding F01 — BLOCKER: RayReuse parser explicitly rejects `N`

- `Bellhop_RayReuse/src/io/environment_parser.cpp`
  - `parseEnvironment()` 当前只把 `C`、`P` 映射到 model；
  - `N` 与 `S/Q` 一起进入 unsupported 分支。
- `Bellhop_RayReuse/tests/component/environment_parser_test.cpp`
  - 当前测试明确要求 `'NVW'` 被拒绝。

因此 FP-2C 不是仅增加一个 concrete class；parser/model/runtime dispatch 必须同时闭环，
否则会出现“后端存在但正式 executable 不可达”。

#### Finding F02 — BLOCKER: RayReuse data model and evaluator variants have no N backend

- `Bellhop_RayReuse/include/rayreuse/model/sound_speed_types.hpp`
  - `SspInterpolationKind` 当前只有 `CLinear`、`Pchip`。
- `Bellhop_RayReuse/include/rayreuse/model/sound_speed_evaluator.hpp`
  - geometry variant 当前为 `CLinearSsp | PchipSsp`；
  - frequency variant 当前为
    `CLinearFrequencySsp | PchipFrequencySsp`。
- `Bellhop_RayReuse/src/model/sound_speed_evaluator.cpp`
  - 两个 factory 都没有 N dispatch，并对无效 enum 抛出异常。

`SoundSpeedProfile` 已在
`Bellhop_RayReuse/include/rayreuse/model/environment.hpp` 中保存 interpolation kind，
无需新增第二个 SSP model 或改变 profile ownership；只需扩展现有 enum 与 evaluator
variant。

#### Finding F03 — HIGH: F2CPP has complete production N² implementations and frozen anchors

Production source of truth：

- `Bellhop_F2CPP/include/bellhop/model/n2_linear_ssp.hpp`
- `Bellhop_F2CPP/src/model/n2_linear_ssp.cpp`
- `Bellhop_F2CPP/include/bellhop/model/n2_linear_frequency_ssp.hpp`
- `Bellhop_F2CPP/src/model/n2_linear_frequency_ssp.cpp`
- `Bellhop_F2CPP/include/bellhop/model/sound_speed_evaluator.hpp`
- `Bellhop_F2CPP/src/model/sound_speed_evaluator.cpp`
- `Bellhop_F2CPP/tests/component/n2_linear_ssp_test.cpp`
- `Bellhop_F2CPP/tests/component/sound_speed_evaluator_test.cpp`
- `Bellhop_F2CPP/tests/component/environment_parser_test.cpp`

这些实现已经覆盖：

- real N² coefficient construction；
- sound speed、gradient、curvature 和 density；
- exact-node arrival-side segment hint；
- first/last segment extrapolation；
- complex node conversion 与 complex N² interpolation；
- `isLossless()`；
- `uniformComplexSoundSpeed()`；
- Origin-frozen numerical anchors。

RayReuse 必须直接迁移这些 production semantics，不应重新推导另一套“数学等价”
实现。

#### Finding F04 — HIGH: N²-linear同时需要非零 Hessian和节点 gradient jump

- `Bellhop_origin/Bellhop/sspMod.f90::n2Linear`
  - 返回非零 `czz = 3 * gradc(2)^2 / c`。
- `Bellhop_origin/Bellhop/Step.f90` 与
  `Bellhop_origin/Bellhop/Step2DMod.f90`
  - 将 `N` 与 `C` 一样视为一阶导数存在跳变的 SSP。
- `Bellhop_F2CPP/src/ray/ray_stepper.cpp`
  - 对 `DiscontinuousAtNodes` 后端执行通用 gradient jump。
- `Bellhop_RayReuse/src/ray/ray_stepper.cpp`
  - 已按 `gradientContinuity()` 选择 jump，但 helper 仍名为
    `applyCLinearGradientJump`。

只实现 N²插值而不让 `d²c/dz²` 进入 dynamic equation，或把 N 错误标为连续梯度，
都会改变 dynamic `p/q`、caustic、beam membership 和最终产品。

#### Finding F05 — MEDIUM: RayReuse已有共享 evaluator integration，不应新增 product-specific N 分支

当前下列 production consumers 已使用 `GeometrySspEvaluator` 或
`FrequencySspEvaluator`：

- `Bellhop_RayReuse/src/model/simulation_case.cpp`
- `Bellhop_RayReuse/src/ray/geometry_tracer.cpp`
- `Bellhop_RayReuse/src/ray/ray_stepper.cpp`
- `Bellhop_RayReuse/src/solver/single_frequency_solver.cpp`
- `Bellhop_RayReuse/src/field/cartesian_cerveny_influence.cpp`
- `Bellhop_RayReuse/src/field/frequency_projector.cpp`

因此 N 应通过扩展现有 evaluator variant 自动进入公共路径。禁止在 TL、R、Arrival、
Eigenray 或各 Influence implementation 中复制 `if (N)` 分支。

#### Finding F06 — MEDIUM: Shared N² case and Origin tolerance already exist, but RayReuse is excluded

- `test/standard_cases/cases/munk_n2/case.toml`
  - 当前 compatibility 只有 `origin`、`f2cpp`；
  - 当前没有 `broadband_smoke` profile。
- `test/standard_cases/codes/compare_f2cpp_geometry_oracle.py`
  - 已支持 `munk-n2`；
  - 已有局部的 N² dynamic `q1/q2` tolerance 说明，不能进一步放宽。
- `Bellhop_F2CPP/tests/tools/geometry_oracle_probe.cpp`
  - 已支持 `munk-n2`。
- `Bellhop_RayReuse/tests/tools/geometry_oracle_probe.cpp`
  - 当前只支持 `munk`、`munk-pchip`。
- `test/standard_cases/codes/intermediate_state_matrix.py`
  - 当前没有 `munk_n2 → munk-n2` 映射。

应扩展现有资产，不能新建第二套 N² case 或 comparator。

#### Finding F07 — LOW: Support documentation explicitly reports N as unsupported

需要在真实验收通过后同步：

- `Bellhop_RayReuse/doc/reference/REFERENCE_FEATURE_SUPPORT_MATRIX.md`
- `Bellhop_RayReuse/doc/reports/REPORT_F2CPP_RAYREUSE_PARITY_2026-08-25.md`
- `Bellhop_RayReuse/doc/status/STATUS_PROGRESS.md`

只关闭 N² slice；`S` 与 `Q/.ssp` 必须继续保持 Deferred/GAP。

### 2.2 Exact real N² contract

每个 profile node：

```text
N²_i = 1 / (c_i * c_i)
```

每个 depth segment：

```text
g_i = (N²_{i+1} - N²_i) / (z_{i+1} - z_i)
N²(z) = N²_i + (z - z_i) * g_i
c(z) = 1 / sqrt(N²(z))
dc/dz = -0.5 * c(z)^3 * g_i
d²c/dz² = 3 * (dc/dz)^2 / c(z)
```

密度仍为节点间线性插值：

```text
w = (z - z_i) / (z_{i+1} - z_i)
rho(z) = (1 - w) * rho_i + w * rho_{i+1}
```

必须保留 F2CPP 的 operation order、branch、finite/positive validation 和
binary64 类型。禁止：

- 先对 `c` 线性插值再平方或求倒数；
- 由有限差分近似 gradient/Hessian；
- 将 curvature 置零；
- 用 C-linear dense resampling 近似；
- 为了匹配 Origin 源表达式而改写 F2CPP 已冻结的 production evaluation order。

### 2.3 Exact frequency-local N² contract

每个目标频率独立执行：

1. 使用 RayReuse 当前 `convertAttenuation(point.attenuation, frequency,
   point.soundSpeed)` 转换每个节点；
2. 构造节点复声速：

   ```text
   c_i(f) = realSoundSpeed_i + i * imaginarySoundSpeed_i(f)
   ```

3. 构造复 N²：

   ```text
   N²_i(f) = 1 / (c_i(f) * c_i(f))
   ```

4. 对复 N²按 depth 线性插值；
5. 使用 principal complex square root：

   ```text
   c(z,f) = 1 / sqrt(N²(z,f))
   ```

6. `SoundSpeedSample.soundSpeed` 使用复结果的 real part；
7. `imaginarySoundSpeed` 使用复结果的 imaginary part；
8. gradient 必须精确保留 F2CPP production 语义：

   ```text
   dc/dz = -0.5 * real(c)^3 * real(g_i)
   ```

9. curvature 必须使用该 real gradient：

   ```text
   d²c/dz² = 3 * (dc/dz)^2 / real(c)
   ```

不得改成复解析导数后再取 real part；这与 F2CPP/Origin observable 不等价。

`isLossless()` 和 `uniformComplexSoundSpeed()` 必须按节点复声速 exact equality
判断，与 F2CPP 一致。

### 2.4 Segment lookup and extrapolation contract

- hinted segment 的两个端点都属于该 segment；
- exact internal node 必须保留 caller 的 arrival-side hint；
- hint 不覆盖 query 时使用当前 F2CPP `lower_bound` 语义；
- global profile 上方使用首段外推；
- global profile 下方使用末段外推；
- `evaluateAtSegment()` 仍拒绝位于指定 segment 外的 depth；
- `evaluate()` 的边缘外推若产生非有限或非正 N²，必须明确失败；
- range gradient/Hessian 分量保持零；
- 不为本 depth-only feature增加 `rangeSegmentIndex` 或 Q 所需 API。

### 2.5 Gradient continuity and dynamic-ray contract

```text
CLinear  = DiscontinuousAtNodes
Pchip    = ContinuousAtNodes
N2Linear = DiscontinuousAtNodes
```

- N² depth node 继续使用现有 GeometryTracer step reduction；
- exact node 的 arrival-side segment sample 必须保留；
- node 后的 minimum forward step 更新 segment hint；
- N²的非零 Hessian必须进入
  `soundSpeedNormalSecondDerivativeOverSquaredSpeed()`；
- N²跨 node 时必须执行现有 Origin-compatible gradient jump；
- PCHIP 仍不得执行 jump；
- C-linear jump、step、trajectory 和 products 必须保持不变。

`applyCLinearGradientJump` 应机械重命名为通用 `applyGradientJump`，并同步错误信息/
注释；不得借此改变公式或 evaluation order。

### 2.6 Minimal evaluator dispatch

RayReuse 只扩展到本阶段需要的三种 depth-only backend：

```text
GeometrySspEvaluator
    variant<CLinearSsp, PchipSsp, N2LinearSsp>

FrequencySspEvaluator
    variant<CLinearFrequencySsp,
            PchipFrequencySsp,
            N2LinearFrequencySsp>
```

- 建议将 `N2Linear` 追加到 RayReuse enum，保留现有 `CLinear`、`Pchip` ordinal；
- factory switch 必须显式处理 N；
- invalid enum 必须抛出 `ValidationError`，不能 fallback 到 C；
- 不复制 F2CPP 的 spline、quadrilateral、range-segment 或额外 volume-attenuation
  evaluator surface；
- evaluator 按值拥有 immutable derived state；
- 不引入 global/current SSP 或 global/current frequency。

### 2.7 Frozen trajectory and per-frequency ownership contract

```text
real N² environment
        ↓
trace once
        ↓
Frozen RayPathCache
        ├── positions / slowness
        ├── real c / real travel time
        ├── dynamic p/q
        ├── step quadrature
        └── raw reflection topology
                ↓
FrequencySspEvaluator(f), local to projection/worker
                ↓
RayFrequencyState(f)
        ├── complex travel time
        ├── attenuation
        ├── amplitude / phase
        ├── reflection acoustics
        └── active prefix
```

N² coefficients属于 evaluator derived state，不属于 `RayPathCache`。以下内容禁止写回
frozen cache：

- complex N²或 complex sound speed；
- frequency；
- complex travel time；
- attenuation；
- reflection coefficient/result；
- amplitude、phase、active prefix；
- pressure/intensity workspace；
- ArrivalWorkspace、ArrivalCandidate、Eigenray hits；
- writer 或 publish state。

`nonreuse` 可以每频独立 trace；`reuse/parallel` 必须保持一次 real trace，多频只做
frequency-local projection/product。

### 2.8 Product integration contract

N SSP 只能通过共享 evaluator 接入当前产品：

```text
parser/model
  → shared real evaluator
  → shared geometry/cache
  → shared frequency evaluator
  → existing Influence/product/writer
```

不得新增：

- `N2LinearTlSolver`；
- product-specific N² evaluator；
- beam-specific N² interpolation；
- N²专用 writer 或文件格式；
- silent conversion `N → C`。

产品命名、header、dimensions、publish order、stale cleanup 和 failure cleanup保持不变。

## 3. Constraints & Invariants

### 3.1 Repository and oracle invariants

- `Bellhop_F2CPP` production source只读；
- `Bellhop_origin` production source只读；
- 不修改 Origin/F2CPP 数值语义来迁就 RayReuse；
- 不修改现有 Origin tolerance，特别是
  `compare_f2cpp_geometry_oracle.py` 中 `munk-n2` 的局部 `q1/q2` rule；
- 不提交生成的 `.prt/.shd/.ray/.arr`、probe CSV、manifest 或临时报告。

### 3.2 Numerical invariants

- 使用 binary64；
- 保持 F2CPP multiplication/division/square-root evaluation order；
- 不引入 float、long double、`pow(c, -2)` 或代数重排；
- complex square root使用标准 principal branch；
- real/imaginary sound speed、gradient、curvature 必须分别通过 finite validation；
- interpolated real sound speed必须为正；
- interpolated imaginary sound speed必须非负；
- density仍是 real linear interpolation；
- exact-node sound speed连续，但 gradient 可跳变；
- Hessian range components保持零。

### 3.3 Existing feature invariants

- C-linear结果不得变化；
- PCHIP结果不得变化；
- PCHIP continuous-gradient行为不得变化；
- current source/receiver/beam/boundary/attenuation限制不放宽；
- TL/R/A/a/E 文件布局不变；
- nonreuse/reuse/parallel publish order不变；
- parallel workers不共享 mutable evaluator；
- `S` 与 `Q/.ssp` 继续被 parser明确拒绝。

### 3.4 Scope-control invariants

- 不向 `SoundSpeedSample` 增加 Q 所需 range-segment字段；
- 不向 RayReuse evaluator 增加 F2CPP quadrilateral APIs；
- 不添加 `.ssp` resolver；
- 不添加 spline coefficient utility；
- 不建立通用 virtual SSP hierarchy；
- 不重写已通过 FP-2B 验收的 PCHIP kernel；
- 不以本批次为由同步其他 F2CPP-only feature。

## 4. Ordered Tasks

## A01 [ADVANCED] — Exact N² concrete evaluators, model dispatch and parser reachability

**depends_on:** none
**can_parallelize_with:** none

### Goal

直接迁移 F2CPP production N²实数/复数 evaluator，并让正式 parser/model/evaluator
完整表达 `N2Linear`，但不在本任务中改变 ray integration 公式。

### Exact target file list

新增：

- `Bellhop_RayReuse/include/rayreuse/model/n2_linear_ssp.hpp`
- `Bellhop_RayReuse/src/model/n2_linear_ssp.cpp`
- `Bellhop_RayReuse/include/rayreuse/model/n2_linear_frequency_ssp.hpp`
- `Bellhop_RayReuse/src/model/n2_linear_frequency_ssp.cpp`
- `Bellhop_RayReuse/tests/component/n2_linear_ssp_test.cpp`

修改：

- `Bellhop_RayReuse/include/rayreuse/model/sound_speed_types.hpp`
- `Bellhop_RayReuse/include/rayreuse/model/sound_speed_evaluator.hpp`
- `Bellhop_RayReuse/src/model/sound_speed_evaluator.cpp`
- `Bellhop_RayReuse/src/io/environment_parser.cpp`
- `Bellhop_RayReuse/tests/component/sound_speed_evaluator_test.cpp`
- `Bellhop_RayReuse/tests/component/environment_parser_test.cpp`
- `Bellhop_RayReuse/CMakeLists.txt`

不得修改：

- `Bellhop_RayReuse/include/rayreuse/model/environment.hpp`
- `Bellhop_RayReuse/src/model/environment.cpp`
- 任何 `Bellhop_F2CPP` 或 `Bellhop_origin` 文件。

### Inputs

Production source of truth：

- `Bellhop_F2CPP/include/bellhop/model/n2_linear_ssp.hpp`
- `Bellhop_F2CPP/src/model/n2_linear_ssp.cpp`
- `Bellhop_F2CPP/include/bellhop/model/n2_linear_frequency_ssp.hpp`
- `Bellhop_F2CPP/src/model/n2_linear_frequency_ssp.cpp`
- `Bellhop_F2CPP/include/bellhop/model/sound_speed_types.hpp`
- `Bellhop_F2CPP/include/bellhop/model/sound_speed_evaluator.hpp`
- `Bellhop_F2CPP/src/model/sound_speed_evaluator.cpp`
- `Bellhop_F2CPP/src/io/environment_parser.cpp`
- `Bellhop_F2CPP/tests/component/n2_linear_ssp_test.cpp`
- `Bellhop_F2CPP/tests/component/sound_speed_evaluator_test.cpp`

Origin oracle：

- `Bellhop_origin/Bellhop/sspMod.f90::n2Linear`
- `Bellhop_origin/Bellhop/Step.f90`
- `Bellhop_origin/Bellhop/Step2DMod.f90`

RayReuse adaptation inputs：

- `Bellhop_RayReuse/src/acoustics/attenuation.cpp`
- `Bellhop_RayReuse/include/rayreuse/acoustics/attenuation.hpp`
- current C-linear/PCHIP concrete evaluator patterns。

### Steps

1. 将 `N2Linear` 追加到 RayReuse `SspInterpolationKind`，保留现有 C/P ordinal。
2. 将 `N2Linear` 映射到 `DiscontinuousAtNodes`。
3. 逐行迁移 `N2LinearSsp`：
   - depths；
   - per-segment real N² minimum value和 gradient；
   - density endpoints；
   - hinted locator；
   - explicit-segment validation；
   - edge-segment extrapolation；
   - exact F2CPP `c/gradient/curvature/density` evaluation order。
4. 逐行迁移 `N2LinearFrequencySsp`：
   - frequency validation；
   - 使用 RayReuse 当前 attenuation conversion签名逐节点转换；
   - complex node speed；
   - complex N² coefficient；
   - exact `std::sqrt` path；
   - F2CPP real gradient/curvature observable；
   - lossless/uniform queries。
5. 将两个 N backend加入 geometry/frequency variants和显式 factory switch。
6. parser 将 top option `N` 保存为 `N2Linear`：
   - `C/P` 行为不变；
   - `S/Q` 继续明确拒绝；
   - unknown kind继续使用独立 unknown诊断；
   - 更新现有“C-linear and PCHIP”支持诊断为 C/P/N，不改变其余 option限制。
7. 注册新 production sources和 component test。
8. 从 F2CPP 移植高信息量 anchors，不另造一套公式测试。

### Acceptance criteria

- `'NVW'` 可由真实 environment parser保留为 `N2Linear`；
- `'CVW'`、`'PVW'` 保持原 kind；
- `'SVW'`、`'QVW'` 与未知 kind仍明确失败；
- invalid enum进入 evaluator时抛出异常，无 C fallback；
- geometry/frequency evaluator报告 `N2Linear` 和
  `DiscontinuousAtNodes`；
- 以下 F2CPP/Origin-frozen real anchors在原 tolerance内一致：
  - 50 m first midpoint；
  - 100 m node from left；
  - 100 m node from right；
  - 150 m second midpoint；
- 关键 anchors至少包括：
  - `c(50 m) = 1547.5821125259863`；
  - left-node gradient `1.10222222222222155`；
  - right-node gradient `-2.44897959183673342`；
  - second-midpoint curvature `0.00787663434191761790`；
- exact node在 left/right hint下保持相应 segment；
- sound speed在 node连续且 gradient jump非零；
- top/bottom edge extrapolation使用首/末段；
- complex 50 Hz anchor与 F2CPP一致：
  - real sound speed `1489.91621090979174`；
  - imaginary sound speed `6.87988934309839983`；
  - gradient `-0.0397683421458134914`；
  - curvature `3.18444961960798548e-6`；
- attenuating profile不是 lossless且没有 uniform fast path；
- non-finite query、invalid segment和 non-positive interpolated N²明确失败；
- targeted N²、parser和 evaluator component tests通过；
- F2CPP/Origin working tree没有变化。

---

## A02 [ADVANCED] — Dynamic-ray integration, gradient jump and frequency-local cache contract

**depends_on:** A01
**can_parallelize_with:** G01 的 standard-case manifest准备部分，但最终验收依赖 A02

### Goal

证明 N² backend通过既有共享 evaluator真实进入 tracing、dynamic ray、
FrequencyProjector 与 frozen cache路径，并修正 C-linear-only命名而不改变公式。

### Exact modification file list

- `Bellhop_RayReuse/src/ray/ray_stepper.cpp`
- `Bellhop_RayReuse/include/rayreuse/ray/geometry_tracer.hpp`
- `Bellhop_RayReuse/tests/component/ray_stepper_test.cpp`
- `Bellhop_RayReuse/tests/component/geometry_tracer_ssp_interface_test.cpp`
- `Bellhop_RayReuse/tests/component/frequency_projector_test.cpp`

### Required read-only integration audit

以下文件预期无需 production 修改；如发现必须修改，停止并记录
`ARCHITECTURE_BLOCKER`，不得增加 product-specific N 分支：

- `Bellhop_RayReuse/src/model/simulation_case.cpp`
- `Bellhop_RayReuse/src/ray/geometry_tracer.cpp`
- `Bellhop_RayReuse/src/solver/single_frequency_solver.cpp`
- `Bellhop_RayReuse/src/field/cartesian_cerveny_influence.cpp`
- `Bellhop_RayReuse/src/field/frequency_projector.cpp`
- `Bellhop_RayReuse/src/solver/broadband_nonreuse_solver.cpp`
- `Bellhop_RayReuse/src/solver/serial_ray_reuse_solver.cpp`
- `Bellhop_RayReuse/src/solver/parallel_ray_reuse_solver.cpp`
- `Bellhop_RayReuse/src/cache/ray_path_cache.cpp`

### Inputs

- A01 concrete backends and dispatch；
- `Bellhop_F2CPP/src/ray/ray_stepper.cpp`；
- `Bellhop_F2CPP/src/ray/geometry_tracer.cpp`；
- `Bellhop_F2CPP/src/field/frequency_projector.cpp`；
- `Bellhop_origin/Bellhop/Step.f90`；
- `Bellhop_origin/Bellhop/Step2DMod.f90`；
- existing RayReuse C-linear node-crossing and PCHIP curvature tests。

### Steps

1. 将 `applyCLinearGradientJump` 机械重命名为 `applyGradientJump`。
2. 将 helper错误信息和注释从 C-linear-only表述改为 discontinuous SSP表述。
3. 不改变以下数值公式：
   - gradient jump vector；
   - ray normal；
   - incidence tangent；
   - jump correction；
   - dynamic P update order。
4. 增加 N² direct step test，证明：
   - nonzero N² Hessian进入 predictor/corrector dynamic equations；
   - segment crossing执行 jump；
   - N²结果不是 C-linear或 PCHIP fallback。
5. 将 SSP-interface geometry test参数化或增加 N²高信息量路径：
   - node只存一次；
   - arrival-side hint；
   - minimum forward step；
   - departure后 segment更新；
   - horizontal slowness与 path invariants。
6. 增加 attenuating N² FrequencyProjector test：
   - 同一 frozen path分别投影 50 Hz、250 Hz；
   - complex travel times独立且稳定；
   - imaginary travel time符号符合当前 convention；
   - input `RayPath` 投影前后逐字段不变。
7. 审计 source sampling、launch planning、tracer、Influence和 reflection water sample
   均通过共享 evaluator；不得增加新 switch。

### Acceptance criteria

- N² evaluator的非零 `d²c/dz²` 实际改变 dynamic `p/q`；
- N²跨 node执行一次 Origin-compatible gradient jump；
- C-linear仍执行同一 jump且现有 anchors不变；
- PCHIP仍不执行 jump；
- GeometryTracer的 depth-node reduction对 C/P/N保持现有规则；
- source位于 N² internal node时不会停滞或循环；
- N² reflected path保持：
  - `points = 1 + steps + events`；
  - event ordering；
  - finite states；
  - boundary arrival-side sampling；
- N² lossless projection复用 frozen real travel time；
- attenuating N²使用本频率 complex evaluator与现有 start/midpoint quadrature；
- 两个频率生成独立 `RayFrequencyState`；
- projection前后 path/cache fingerprint一致；
- `nonreuse` 之外没有 per-frequency retrace；
- parallel worker不共享 mutable N² evaluator；
- required read-only consumers无需 product-specific修改；
- targeted stepper、geometry、projector、reuse和parallel component tests通过。

---

## G01 [GENERAL] — Shared `munk_n2` case, probes and executable/product parity

**depends_on:** A01；最终执行依赖 A02
**can_parallelize_with:** A02 仅限 manifest/probe scaffolding

### Goal

复用现有 `munk_n2` case和 oracle基础设施，闭环三方 geometry、单频输出、宽带三模式
以及当前产品公共路径。

### Exact target file list

- `test/standard_cases/cases/munk_n2/case.toml`
- `test/standard_cases/codes/intermediate_state_matrix.py`
- `test/standard_cases/codes/tests/test_case_model.py`
- `Bellhop_RayReuse/tests/tools/geometry_oracle_probe.cpp`

无需修改：

- `test/standard_cases/cases/munk_n2/origin.env.in`
- `test/standard_cases/codes/compare_f2cpp_geometry_oracle.py`
- `test/standard_cases/coverage.toml`
- `Bellhop_RayReuse/tests/support/munk_case_fixture.hpp`
- F2CPP probe或 Origin oracle production source。

### Inputs

- existing `munk_n2` Origin/F2CPP case；
- F2CPP `munk-n2` geometry probe configuration；
- existing local N² `q1/q2` tolerance；
- existing RayReuse `munk`/`munk-pchip` probe implementation；
- standard-case RayReuse execution-mode adapter；
- current cache fingerprint and product comparators。

### Steps

1. 将 `munk_n2` compatibility扩展为：

   ```toml
   versions = ["origin", "f2cpp", "rayreuse"]
   ```

2. 增加：

   ```toml
   [profiles.broadband_smoke]
   frequencies_hz = [50.0, 250.0]
   ```

3. RayReuse geometry probe增加命名配置 `munk-n2`：
   - 使用现有 Munk nodes；
   - 显式选择 `SspInterpolationKind::N2Linear`；
   - 保持 CSV schema、precision、column order和 manifest schema不变。
4. 将 `munk_n2 → munk-n2` 加入 intermediate-state matrix。
5. 更新 case-model test对 supported versions和 broadband profile的期望。
6. 最终执行三方 single-frequency标准案例。
7. 执行 RayReuse broadband smoke三模式。
8. 用临时生成、不提交的 N²输入执行最小 product smoke：
   - TL；
   - single-frequency R；
   - A；
   - a；
   - E。
9. 与 F2CPP逐产品比较；不得为 N 新建 writer tolerance。
10. 明确证明相同 Munk nodes下 `N` 与 `C/P` geometry不相同，防止 silent fallback。

### Acceptance criteria

- Origin、F2CPP、RayReuse均可从正式 executable运行 `munk_n2` single profile；
- F2CPP/RayReuse `munk-n2` geometry probe CSV逐字节一致；
- Origin→F2CPP与 Origin→RayReuse geometry comparison使用现有 tolerance通过；
- 不修改 `munk-n2` 现有局部 `q1/q2` tolerance；
- 50 Hz F2CPP/RayReuse SHD使用现有 comparator通过，优先逐字节一致；
- RayReuse N结果与同节点 C/P结果存在可观察差异；
- broadband `[50, 250] Hz`：
  - nonreuse每频独立 trace；
  - reuse trace一次；
  - parallel reuse trace一次；
  - 三模式每频产品逐字节一致；
  - publish order稳定；
  - cache fingerprint前后不变；
- TL/R/A/a/E均通过共享 evaluator运行，无 product-specific N fallback；
- A/a encoding语义保持不变；
- R/E ray topology与现有 writer schema保持不变；
- standard-case unit tests通过；
- 没有新增重复 N² case；
- 没有提交生成产品。

---

## G02 [GENERAL] — Documentation closure, full regression and Batch Report

**depends_on:** A01、A02、G01
**can_parallelize_with:** none

### Goal

在全部 numerical/runtime/oracle gate通过后更新真实支持声明，运行批次级回归并生成
可复现 FP-2C Batch Report。

### Exact target file list

- `Bellhop_RayReuse/doc/reference/REFERENCE_FEATURE_SUPPORT_MATRIX.md`
- `Bellhop_RayReuse/doc/reports/REPORT_F2CPP_RAYREUSE_PARITY_2026-08-25.md`
- `Bellhop_RayReuse/doc/status/STATUS_PROGRESS.md`
- `Bellhop_RayReuse/doc/workreports/FP-2C_N2_LINEAR_SSP_BATCH_REPORT.md`

除发现真实 registration gap外，不修改 build/test framework。

### Inputs

- A01 component anchor结果；
- A02 dynamic/frequency/cache结果；
- G01 Origin/F2CPP/RayReuse oracle结果；
- pre-batch C/P hashes；
- compiler/build identity；
- executable hashes；
- working-tree状态。

### Steps

1. 只将 N²-linear `N` 从 Deferred/GAP更新为已支持。
2. support wording从 `C-linear/PCHIP` 更新为 `C-linear/PCHIP/N²-linear`。
3. 明确记录：
   - N² real geometry；
   - nonzero Hessian；
   - discontinuous gradient；
   - frequency-local complex N²；
   - frozen cache ownership；
   - TL/R/A/a/E范围；
   - three-mode parity。
4. `S`、`Q/.ssp` 继续保持 Deferred/GAP。
5. 记录 C/P zero-regression hashes和 N oracle worst errors。
6. 运行 isolated clean build、full CTest、Python tests、standard cases和 diff检查。
7. 生成 Batch Report，至少包含：
   - completed tasks；
   - GENERAL work；
   - ADVANCED work及重要数值决定；
   - architecture deviations；
   - F2CPP oracle；
   - Origin oracle；
   - C/P zero regression；
   - execution parity；
   - frozen cache evidence；
   - tests；
   - changed files；
   - remaining gaps；
   - git diff/status；
   - `Ready for Final Review: YES/NO`。
8. 不得自行声明 FP-2C accepted。

### Acceptance criteria

- 文档与真实 parser/runtime/tests一致；
- N²被标为 supported；
- `S`、`Q/.ssp` 仍明确 unsupported/deferred；
- 文档没有把 FP-2C外推为整个 SSP family parity；
- Batch Report包含 producer/executable hashes、commands、worst errors和 mode hashes；
- 原因不明的 numerical regression回交 A01/A02，不能在 G02 放宽 tolerance；
- full validation全部通过；
- generated products未进入版本控制；
- `Ready for Final Review` 仅在全部 gate通过时为 `YES`。

## 5. Dependencies Between Tasks

### 5.1 Required execution order

```text
A01 — concrete N² backend + parser/model dispatch
  ↓
A02 — dynamic ray + frequency-local/cache integration
  ↓
G01 — final three-party oracle and execution/product parity
  ↓
G02 — documentation, full validation and Batch Report
  ↓
Reviewer / Final Reviewer
```

### 5.2 Limited parallel preparation

```text
A01 完成后：
  A02 执行 numerical integration
  G01 可准备 case.toml、probe option和 matrix mapping

A02 完成前：
  G01 不得宣布 geometry、product或 execution parity通过

A01 + A02 + G01 全通过后：
  G02 才能更新 supported状态
```

### 5.3 Dependency rationale

- parser构造 `SimulationCase` 时会立即通过 `GeometrySspEvaluator` sample source
  sound speed，因此不能先开放 `N` parser、后补 backend；
- dynamic ray与 geometry probe依赖 exact N² Hessian/jump，不能只依赖 evaluator
  midpoint anchors；
- broadband/product验收依赖 frequency-local evaluator和 immutable cache contract；
- 文档只能在实际 executable和 oracle通过后更新。

## 6. Batch-level Regression & Validation Requirements

### 6.1 Baseline before construction

施工前必须记录：

```bash
git rev-parse HEAD
git status --short

uv run ctest \
  --test-dir Bellhop_RayReuse/build/release \
  --output-on-failure

uv run ctest \
  --test-dir Bellhop_F2CPP/build/release \
  -R 'f2cpp\.component\.(n2_linear_ssp|sound_speed_evaluator|environment_parser)' \
  --output-on-failure
```

另需冻结：

- RayReuse C-linear `munk` geometry probe SHA-256；
- RayReuse PCHIP `munk-pchip` geometry probe SHA-256；
- 代表性 C/P SHD hashes；
- reuse/parallel cache fingerprints；
- baseline compiler、build type和 executable hashes。

### 6.2 Targeted component validation

至少执行：

```bash
uv run ctest \
  --test-dir Bellhop_RayReuse/build/release \
  -R 'rayreuse\.component\.(n2_linear_ssp|sound_speed_evaluator|environment_parser|ray_stepper|geometry_tracer_ssp_interface|frequency_projector)' \
  --output-on-failure
```

并执行现有：

- C-linear SSP tests；
- PCHIP SSP tests；
- geometry tracer；
- reflection；
- single-frequency solver；
- broadband nonreuse；
- serial reuse；
- parallel reuse。

### 6.3 Standard-case validation

```bash
uv run make -C test/standard_cases test-unit

uv run make -C test/standard_cases \
  test VERSION=origin CASE=munk_n2 PROFILE=single

uv run make -C test/standard_cases \
  test VERSION=f2cpp CASE=munk_n2 PROFILE=single

uv run make -C test/standard_cases \
  test VERSION=rayreuse CASE=munk_n2 PROFILE=single
```

RayReuse broadband三模式须分别运行，使用
`standard_cases.py --rayreuse-execution-mode`：

```text
nonreuse
reuse
parallel
```

### 6.4 Intermediate geometry oracle

必须使用现有：

- Origin oracle producer；
- F2CPP geometry probe；
- RayReuse geometry probe；
- `intermediate_state_matrix.py`；
- `compare_f2cpp_geometry_oracle.py`。

Gate：

- F2CPP/RayReuse N² probe CSV byte-identical；
- Origin comparison在现有 field rules内通过；
- 不修改 N² `q1/q2`局部 tolerance；
- 记录 point/step/event counts和 worst absolute/relative errors。

### 6.5 Product validation

使用一个高信息量 N² Munk环境验证：

- TL SHD；
- R RAY；
- A ASCII ARR；
- a binary ARR；
- E RAY。

要求：

- F2CPP/RayReuse在现有 comparator下通过；
- schema、dimensions、record ordering和 bounce counts不变；
- N不等于同节点 C/P结果；
- 无 product-specific N switch；
- no stale output；
- failure cleanup不留下正式 partial product或 `.tmp`。

不创建 beam × product × mode笛卡尔积；公共路径有 component evidence后，每种 writer/
solver family只保留一个代表 smoke。

### 6.6 Execution and cache validation

对 `[50.0, 250.0] Hz`：

- nonreuse/reuse/parallel每频数值产品一致；
- reuse与parallel只 trace一次；
- parallel worker输出按 frequency index串行发布；
- cache fingerprint投影前后不变；
- repeated run稳定；
- frequency state之间没有 alias或污染；
- N² evaluator/coefficient不进入 frozen cache。

### 6.7 Zero-regression requirements

- C-linear geometry probe与 pre-batch hash一致；
- PCHIP geometry probe与 pre-batch hash一致；
- C/P代表性 SHD/products不变；
- C-linear gradient jump不变；
- PCHIP不执行 gradient jump；
- parser仍拒绝 `S`、`Q`；
- existing attenuation、boundary、beam和writer regressions通过；
- 若 C/P observable变化，先定位实现原因；禁止先更新 baseline或放宽 tolerance。

### 6.8 Full validation

```bash
cmake \
  -S Bellhop_RayReuse \
  -B Bellhop_RayReuse/build/fp2c-clean \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON

cmake \
  --build Bellhop_RayReuse/build/fp2c-clean \
  --parallel

uv run ctest \
  --test-dir Bellhop_RayReuse/build/fp2c-clean \
  --output-on-failure

uv run ctest \
  --test-dir Bellhop_RayReuse/build/release \
  --output-on-failure

uv run pytest

uv run make -C test/standard_cases test-unit

git diff --check
git status --short
git ls-files --others --exclude-standard
```

Batch Report还必须证明：

- `git diff -- Bellhop_F2CPP Bellhop_origin`为空；
- 未跟踪生成的 `.prt/.shd/.ray/.arr`、CSV和 manifests为零；
- 没有 spline或 Q/.ssp production additions。

## 7. Known Risks & Non-goals

### 7.1 Known risks

#### R01 — HIGH: Algebraic rewrites can break byte parity

Origin使用 weight-form interpolation，F2CPP production使用
`minimum + offset * gradient`。两者数学等价但舍入顺序不同。RayReuse必须以 F2CPP
production顺序为准，再使用 Origin现有 tolerance作为 observable oracle。

#### R02 — HIGH: Complex derivative semantics are intentionally not the analytic complex derivative

`N2LinearFrequencySsp` 的 gradient使用 real sound speed和 complex N² slope的 real
part。将其重写为 complex derivative后取 real part会改变结果，不能视作改进。

#### R03 — HIGH: N² has both curvature and discontinuous gradient

将 N 类比为 C-linear而把 Hessian置零，或类比 PCHIP而禁用 node jump，都会产生错误
dynamic `p/q`。测试必须同时覆盖 segment interior curvature和 node crossing。

#### R04 — MEDIUM: Near-caustic dynamic variables amplify tiny node-step differences

现有 `munk-n2` Origin comparator已对 `q1/q2`提供局部、记录原因的 tolerance。不得扩大
到其他 fields/cases，也不得用它掩盖 F2CPP/RayReuse不一致；两个 C++ probe仍应
byte-identical。

#### R05 — MEDIUM: Edge extrapolation may make N² non-positive

N²首/末段外推不像普通 C-linear speed外推。对于远离 profile的查询可能产生非正 N²。
必须保留明确 validation failure，不能 clamp、abs或 fallback。

#### R06 — MEDIUM: Frequency conversion models differ between F2CPP and RayReuse data models

F2CPP的 frequency evaluator接收独立 `VolumeAttenuation`，RayReuse把当前 volume model
保存在 `RawAttenuation` 并仅支持现有子集。FP-2C必须使用 RayReuse当前 conversion
contract，不得借 N²实现移植 F/B volume model。

#### R07 — LOW: Stale C/P-only comments can hide incomplete runtime wiring

除 parser诊断外，`geometry_tracer.hpp` 和 parity/support文档仍有 C/P-only表述。只更新
真实受影响的声明；不要做全仓文案重写。

### 7.2 Unresolved architectural questions

无阻塞性架构问题。现有 FP-2B value-owned evaluator、generic geometry consumers和
frequency-local projector已经提供 N²所需接口。

若施工发现必须增加以下任一能力，应停止并报告 `ARCHITECTURE_BLOCKER`：

- range-segment API；
- `.ssp` sidecar；
- mutable global frequency；
- product-specific N solver；
- frozen cache内的 frequency-dependent字段；
- spline coefficient utility；
- RayReuse当前 attenuation范围之外的模型。

### 7.3 Explicit non-goals

FP-2C完成后仍不得声明支持：

- spline `S`；
- quadrilateral `Q`；
- `.ssp`；
- range-dependent SSP；
- line/multisource；
- irregular receivers；
- canonical curvilinear boundary；
- 3D；
- Influence Geometry Reuse；
- frequency interpolation；
- 整个 F2CPP SSP family parity。
