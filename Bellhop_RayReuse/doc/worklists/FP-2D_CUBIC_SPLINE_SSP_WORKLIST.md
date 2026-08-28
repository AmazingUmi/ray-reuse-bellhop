# FP-2D — Cubic Spline SSP (`S`) Parity Worklist

> **Batch:** FP-2D
> **Feature:** Cubic spline SSP (`S`) parity
> **Baseline:** `34c88fdc31e1dcd67263ca626a4d2538577d4137`
> **Production source of truth:** `Bellhop_F2CPP`
> **Numerical / observable oracle:** `Bellhop_origin`
> **Status:** architecture/worklist complete；implementation not started

Workers may not expand FP-2D scope. 本清单只同步 F2CPP 已正式支持的二维 cubic
spline SSP `S` production slice。遇到 Q/range-dependent SSP 或其他 future feature
接口问题时，只记录 dependency；不得顺手实现。若真实代码与本清单的核心架构边界
冲突，停止对应任务并在 Batch Report 中标记 `ARCHITECTURE_BLOCKER`，不得自行重设
整个阶段。

## 1. Architecture Decisions

### 1.1 功能 source of truth

实现必须逐行审计并迁移以下 F2CPP production 路径：

- `Bellhop_F2CPP/src/numerics/cubic_spline_coefficients.cpp`
- `Bellhop_F2CPP/include/bellhop/numerics/cubic_spline_coefficients.hpp`
- `Bellhop_F2CPP/src/model/cubic_spline_ssp.cpp`
- `Bellhop_F2CPP/include/bellhop/model/cubic_spline_ssp.hpp`
- `Bellhop_F2CPP/src/model/cubic_spline_frequency_ssp.cpp`
- `Bellhop_F2CPP/include/bellhop/model/cubic_spline_frequency_ssp.hpp`
- `Bellhop_F2CPP/src/model/sound_speed_evaluator.cpp`
- `Bellhop_F2CPP/src/io/environment_parser.cpp`
- `Bellhop_F2CPP/tests/component/cubic_spline_ssp_test.cpp`

Origin 只承担 observable/numerical oracle。禁止根据理论上的“普通 cubic spline”
自行替代 F2CPP 的 `splinec.f90::CSPLINE` production translation。

### 1.2 最小扩展现有 evaluator abstraction

FP-2B/FP-2C 已建立：

```text
GeometrySspEvaluator
    ├── CLinearSsp
    ├── PchipSsp
    └── N2LinearSsp

FrequencySspEvaluator
    ├── CLinearFrequencySsp
    ├── PchipFrequencySsp
    └── N2LinearFrequencySsp
```

FP-2D 只增加：

```text
    └── CubicSplineSsp
    └── CubicSplineFrequencySsp
```

不新增 virtual hierarchy、plugin registry、type erasure 或 future SSP factory。
`GeometryTracer`、`RayStepper`、`FrequencyProjector` 已经消费通用 evaluator；除发现真实
漏接，不应重构这些稳定接口。

### 1.3 Cubic spline 必须是真实 coefficient evaluator

禁止：

```text
Spline → dense resampling → C-linear/PCHIP
```

也禁止调用 SciPy、Boost 或第三方 spline library 替代 production 语义。必须迁移
F2CPP 当前 coefficient construction，包括：

- `IBCBEG=IBCEND=0` 的 legacy endpoint/not-a-knot construction；
- 2-node、3-node 与 4+-node 特殊分支；
- forward elimination 与 backward substitution 的原始顺序；
- value、derivative、curvature、thirdDerivative coefficient 形成顺序；
- complex coefficient arithmetic；
- non-finite 与非递增节点验证；
- `splinec.f90` 默认实数常量留下的 binary32 rounding。

尤其保留：

```cpp
static_cast<double>(1.0F / 6.0F)
```

不得替换成 `1.0 / 6.0`、预先十进制常量或其他数学等价写法。

### 1.4 Real geometry semantics

real spline evaluator 负责：

- spline `c(z)`；
- 一阶导数 `dc/dz`；
- 二阶导数 `d²c/dz²`；
- density 继续按 segment 线性插值，不做 spline；
- profile 外使用首/末 cubic polynomial extrapolation；
- hinted segment 与当前 C/P/N locator 契约一致。

Spline gradient 在节点连续，因此：

```text
SspGradientContinuity::ContinuousAtNodes
```

不得调用 C/N² 的 reduced-step `applyGradientJump`。但非零 Hessian 必须继续进入
dynamic-ray equations；不能因为不需要 node jump 就丢弃 curvature。

### 1.5 Frequency-local complex spline semantics

每个目标频率独立执行：

```text
SSP node
  → target-frequency attenuation conversion
  → complex node sound speed
  → complex cubic-spline coefficients
  → complex polynomial evaluation
```

observable sample 按 F2CPP 顺序形成：

- sound speed = complex polynomial value 的 real part；
- imaginary sound speed = complex polynomial value 的 imaginary part；
- gradient = complex derivative 的 real part；
- Hessian = complex second derivative 的 real part；
- density/segment identity 来自 real evaluator；
- lossless/uniform fast path使用 F2CPP 的 exact node-value判断。

F2CPP 对 spline 内部的 imaginary sound speed只要求 finite；不得从 PCHIP 路径复制
`imaginary >= 0` 约束，也不得 clamp、取绝对值或静默回退。

禁止只对 real `c(z)` 做 spline、再在查询点附加 attenuation；也禁止先构造一套共享
mutable complex coefficients供多个频率复用。

### 1.6 Segment ownership 与 extrapolation

必须保持 F2CPP 当前 observable behavior：

- previous segment hint 合法且 depth 位于该闭区间时保留 hint；
- 内部 node 可由 arrival-side hint 决定 segment identity；
- 无有效 hint 命中时 `lower_bound` 选择左段；
- profile 上方使用第一段 polynomial；
- profile 下方使用最后一段 polynomial；
- `evaluateAtSegment` 对段外 depth 明确失败；
- non-finite query、invalid segment、无效 coefficients 明确失败；
- 不做 clamp、abs、silent fallback。

### 1.7 Frozen trajectory / frequency-local contract

必须继续保持：

```text
real cubic-spline environment
        ↓
trace geometry once
        ↓
Frozen RayPathCache
        ↓
per-frequency complex spline projection
        ↓
frequency-local Influence / product
```

Frozen cache 只允许保存频率无关的 trajectory、real dynamic-ray bases、real travel-time
quadrature、reflection-event identity 与 geometry topology。以下状态不得写入 cache：

- frequency 或 complex spline coefficients；
- complex amplitude/phase/travel time；
- attenuation/reflection result；
- active/terminal prefix；
- pressure/intensity workspace；
- ArrivalWorkspace 或 Eigenray hits。

### 1.8 Product 与 execution surface

Spline `S` 使用现有 generic evaluator path 接入当前合法产品：

- TL；
- R；
- A；
- a；
- E。

适用的多频产品继续验证：

- `nonreuse`：每频独立 trace + solve；
- `reuse`：trace once + serial frequency-local projection；
- `parallel`：trace once + independent worker-local evaluator/workspace + ordered publish。

不得增加 product-specific `if (ssp == S)` solver 分支。现有 R 单频限制、A/a/E 多频
命名、writer ownership、stale/failure cleanup 均保持不变。

### 1.9 Standard-case 策略

复用现有共享 `munk_spline`：

- 已有 Origin/F2CPP input 与 tolerances；
- 已有 F2CPP `munk-spline` geometry probe；
- 只增加 RayReuse compatibility、broadband profile、RayReuse probe/config mapping 与
  必要 product gates；
- 不创建重复 spline case；
- 不扩大 `munk_spline/tolerances.toml`。

## 2. Scope

### In scope

- parser 正式接受 SSP option `S`；
- `SspInterpolationKind::CubicSpline`；
- exact F2CPP-compatible coefficient builder；
- real cubic-spline evaluator；
- target-frequency complex cubic-spline evaluator；
- generic geometry/frequency evaluator dispatch；
- continuous-gradient/no-jump 与 nonzero-Hessian dynamic-ray semantics；
- 当前合法 TL/R/A/a/E runtime path；
- nonreuse/reuse/parallel；
- frozen-cache fingerprint；
- `munk_spline` Origin/F2CPP/RayReuse oracle；
- C/P/N zero regression；
- support matrix、parity report、progress 与 Batch Report。

### Out of scope

- quadrilateral/range-dependent SSP `Q` 与 `.ssp`；
- N²/PCHIP/C-linear algorithm changes；
- line source；
- multisource；
- irregular receivers；
- canonical curvilinear boundary；
- 3D / N×2D；
- attenuation新模型；
- Influence Geometry Reuse；
- frequency interpolation/sampling/reconstruction；
- performance optimization、SIMD 或新并行策略；
- F2CPP/Origin production修改；
- shared-library重构。

## 3. Worker Protocol

- `general-worker` 直接执行 `[GENERAL]` 项。
- 遇到 `[ADVANCED]` 项必须委派 `advanced-worker`，不得由低成本 worker 降级实现。
- 每个 ADVANCED checkpoint 完成后先做只读 reviewer checkpoint，再进入依赖任务。
- worker 不得修改 `.pi/settings.json` 的 provider/model/thinking/modelScope。
- provider/auth/runtime 故障标记 `AGENT_RUNTIME_BLOCKER`，不得切换用户模型路由。
- 保留用户已有修改，不得自动 stage/commit/push。
- worker 不得自行声明 `FP-2D ACCEPTED`；最多声明 `Ready for Final Review`。

## A01 [ADVANCED] — Exact coefficient kernel and real spline evaluator

**depends_on:** none
**can_parallelize_with:** none

### Goal

迁移 F2CPP cubic-spline coefficient construction 与 real evaluator，建立独立可测的
production-equivalent `CubicSplineSsp`，但暂不接 parser/runtime。

### Source-of-truth files

- `Bellhop_F2CPP/src/numerics/cubic_spline_coefficients.cpp`
- `Bellhop_F2CPP/include/bellhop/numerics/cubic_spline_coefficients.hpp`
- `Bellhop_F2CPP/src/model/cubic_spline_ssp.cpp`
- `Bellhop_F2CPP/include/bellhop/model/cubic_spline_ssp.hpp`
- `Bellhop_F2CPP/tests/component/cubic_spline_ssp_test.cpp`
- `Bellhop_origin/misc/splinec.f90`

### Files likely involved

- `Bellhop_RayReuse/include/rayreuse/numerics/cubic_spline_coefficients.hpp`
- `Bellhop_RayReuse/src/numerics/cubic_spline_coefficients.cpp`
- `Bellhop_RayReuse/include/rayreuse/model/cubic_spline_ssp.hpp`
- `Bellhop_RayReuse/src/model/cubic_spline_ssp.cpp`
- `Bellhop_RayReuse/tests/component/cubic_spline_ssp_test.cpp`
- `Bellhop_RayReuse/CMakeLists.txt`

### Required changes

1. 迁移 `ComplexSplinePolynomial` 与 coefficient builder。
2. 保留 2/3/4+ nodes 的 production branch 和 elimination/back-substitution顺序。
3. 保留 complex arithmetic，即使 real evaluator输入 imaginary=0。
4. 实现 real spline的 locator、polynomial evaluation、gradient、curvature、density与
   extrapolation。
5. 保留 binary32-rounded `kFortranSixth`。
6. 明确验证 finite nodes/values、strictly increasing depth、positive sound speed、finite
   derivative/curvature/density。
7. 增加 F2CPP/Fortran 独立冻结 anchors，不从被测 evaluator动态生成 expected值。

### Forbidden shortcuts

- dense resampling；
- 调用第三方 spline；
- 使用通用 linear algebra solver改变 evaluation order；
- 用自然边界或 clamped boundary替代 F2CPP endpoint语义；
- 把 density改为 spline；
- 把 `1.0F/6.0F` 改为 binary64 `1/6`。

### Must preserve

- C/P/N source与行为不变；
- 现有 evaluator interface不重构；
- F2CPP/Origin production code不修改。

### Acceptance

- 2-node退化为 exact linear；
- 3-node退化行为与 F2CPP一致；
- first/middle/last segment value、gradient、curvature与 F2CPP anchors一致；
- node左右 value/gradient/curvature连续；
- hinted node保留 arrival-side segment；
- profile上下使用edge cubic extrapolation；
- invalid node arrays、non-finite query、invalid segment明确失败；
- component test通过；
- `git diff --check`通过；
- reviewer checkpoint确认没有第三方/近似实现。

### Handoff to next task

向 A02 提供稳定的 coefficient API、polynomial layout、locator/evaluation contract和所有
mixed-precision决定。

## A02 [ADVANCED] — Frequency-local complex spline and dynamic-ray contract

**depends_on:** A01
**can_parallelize_with:** G02 的标准案例元数据准备部分

### Goal

实现 target-frequency complex spline evaluator，并证明 spline gradient/Hessian 在
generic tracer/projector 中保持 F2CPP语义、没有污染 frozen cache。

### Source-of-truth files

- `Bellhop_F2CPP/src/model/cubic_spline_frequency_ssp.cpp`
- `Bellhop_F2CPP/include/bellhop/model/cubic_spline_frequency_ssp.hpp`
- `Bellhop_F2CPP/src/model/sound_speed_evaluator.cpp`
- `Bellhop_F2CPP/src/ray/ray_stepper.cpp`
- RayReuse 当前 `pchip_frequency_ssp.*`、`n2_linear_frequency_ssp.*`
- RayReuse 当前 `frequency_projector.cpp`、`ray_stepper.cpp`

### Files likely involved

- `Bellhop_RayReuse/include/rayreuse/model/cubic_spline_frequency_ssp.hpp`
- `Bellhop_RayReuse/src/model/cubic_spline_frequency_ssp.cpp`
- `Bellhop_RayReuse/tests/component/cubic_spline_ssp_test.cpp`
- `Bellhop_RayReuse/tests/component/ray_stepper_test.cpp`
- `Bellhop_RayReuse/tests/component/geometry_tracer_ssp_interface_test.cpp`
- `Bellhop_RayReuse/tests/component/frequency_projector_test.cpp`

### Required changes

1. 每频先转换所有 node attenuation，再构造 complex coefficients。
2. 复用 A01 exact coefficient kernel，不维护第二套 spline算法。
3. evaluation先取得 real evaluator 的 density/segment，再按 F2CPP顺序覆盖 complex value
   与 real-part gradient/Hessian。
4. `isLossless`、`uniformComplexSoundSpeed` 与 F2CPP exact判断一致。
5. 保持 spline interior imaginary值的 F2CPP finite-only validation，不添加非负约束。
6. 证明 `ContinuousAtNodes` 不执行 gradient jump。
7. 证明 nonzero Hessian 实际改变 dynamic p/q，而不是只存在 sample中。
8. 两个频率 evaluator/value storage完全独立；重复投影稳定。
9. 投影前后逐字段检查 frozen ray/cache不变。

### Forbidden shortcuts

- 查询点 attenuation后处理；
- 跨频共享 mutable complex coefficients；
- 对 complex derivative做另一个理论推导；
- 对 imaginary sound speed做 clamp、abs或PCHIP式非负限制；
- 为 spline增加 product-specific tracer/projector分支；
- 关闭 Hessian以迁就某个 oracle。

### Acceptance

- attenuating profile的 complex value/gradient/curvature anchors与 F2CPP一致；
- lossless节点产生 zero imaginary state；
- 50/250 Hz evaluator结果不同且互不串频；
- spline node不执行 C/N² gradient jump；
- Hessian非零并进入 dynamic-ray equation；
- frequency projector不改变 frozen trajectory/dynamic bases/reflection topology；
- targeted component tests通过；
- reviewer checkpoint确认 ownership与 mixed precision。

### Handoff to next task

向 G01 提供 final real/frequency evaluator类型、continuity语义和 constructor签名。

## G01 [GENERAL] — Parser, enum, CMake and generic runtime dispatch

**depends_on:** A01、A02
**can_parallelize_with:** G02 的已隔离 standard-case adapter准备部分

### Goal

把已验收的 spline evaluators机械接入 RayReuse model/parser/build和两条 generic dispatch，
仅放开真正完成的 `S`。

### Files likely involved

- `Bellhop_RayReuse/include/rayreuse/model/sound_speed_types.hpp`
- `Bellhop_RayReuse/include/rayreuse/model/sound_speed_evaluator.hpp`
- `Bellhop_RayReuse/src/model/sound_speed_evaluator.cpp`
- `Bellhop_RayReuse/src/io/environment_parser.cpp`
- `Bellhop_RayReuse/CMakeLists.txt`
- `Bellhop_RayReuse/tests/component/environment_parser_test.cpp`
- `Bellhop_RayReuse/tests/component/sound_speed_evaluator_test.cpp`
- `Bellhop_RayReuse/tests/component/single_frequency_solver_test.cpp`

### Required changes

1. 增加 `SspInterpolationKind::CubicSpline`。
2. continuity mapping将 spline标记为 `ContinuousAtNodes`。
3. geometry variant加入 `CubicSplineSsp`。
4. frequency variant加入 `CubicSplineFrequencySsp`。
5. parser将 `SVW` 等当前合法 top-options形式映射到 spline。
6. `Q` 与 unknown kind继续显式拒绝；不得用 default回落到 C。
7. 注册 source/test targets。
8. 通过 shared `SimulationCase → SingleFrequencySolver/FrequencyProjector` 路径做一个
   executable-level smoke，证明不是 enum-only support。

### Must preserve

- C/P/N parser和dispatch不变；
- Q/.ssp继续unsupported；
- 当前 source/receiver/boundary/attenuation限制不放宽；
- TL/R/A/a/E不增加 spline专用 solver branch。

### Acceptance

- parser接受合法 `S` environment并生成 `CubicSpline` kind；
- geometry与frequency variants都真实构造 spline evaluator；
- S与C/P/N numerical result不同，排除 silent fallback；
- Q与unknown kind parser tests继续失败；
- CMake configure/build成功；
- targeted parser/evaluator/solver tests通过；
- `git diff --check`通过。

### Handoff to next task

向 G02 提供可运行的 `bellhop_rayreuse` 与 geometry probe target。

## G02 [GENERAL] — Shared oracle, product and execution parity

**depends_on:** A01；最终 oracle/product execution依赖 A02、G01
**can_parallelize_with:** A02、G01，仅限 disjoint 的 case metadata/probe adapter准备

### Goal

复用现有 `munk_spline` 完成 Origin/F2CPP/RayReuse geometry、TL/R/A/a/E与三执行模式
闭环，不新增重复案例或 tolerance。

### Files likely involved

- `test/standard_cases/cases/munk_spline/case.toml`
- `test/standard_cases/codes/intermediate_state_matrix.py`
- `test/standard_cases/codes/tests/test_case_model.py`
- `Bellhop_RayReuse/tests/tools/geometry_oracle_probe.cpp`
- 必要的现有 product comparator/allow-list

### Required changes

1. 将 RayReuse加入 `munk_spline` compatibility。
2. 增加最小 `[50,250]` broadband profile；不新建 spline case。
3. RayReuse geometry probe增加 `munk-spline`配置，并与 F2CPP相同 launch/config。
4. 将 `munk_spline → munk-spline` 接入 intermediate-state matrix。
5. 运行 Origin/F2CPP/RayReuse single standard case。
6. 比较 F2CPP/RayReuse geometry probe CSV；目标 byte-identical。
7. 在既有 `munk_spline/tolerances.toml` 下运行 Origin intermediate oracle；不得修改
   tolerance。
8. 使用同一环境验证 TL/R/A/a/E；每个 writer/solver family一个代表 smoke即可。
9. 验证 nonreuse/reuse/parallel output一致、trace passes为 2/1/1、cache fingerprint
   前后不变。
10. 冻结 C/P/N geometry probe和代表性 broadband outputs，确认 zero regression。

### Must preserve

- existing `munk_spline` Origin/F2CPP结果；
- output schema、record ordering、bounce counts与writer lifecycle；
- parallel publish order与worker-local workspace；
- S不得被误当成 PCHIP；
- Q case/adapter不接入 RayReuse。

### Acceptance

- `munk_spline` Origin/F2CPP/RayReuse single均PASS；
- F2CPP/RayReuse spline geometry CSV尽可能 byte-identical；若非 exact，必须定位真实
  evaluation-order原因，不得先改 tolerance；
- Origin trajectory/intermediate oracle在既有 tolerance下PASS；
- TL/R/A/a/E executable paths均消费 spline evaluator；
- S与相同节点 C/P/N结果明确不同；
- nonreuse/reuse/parallel产品一致；
- trace passes = 2/1/1；
- cache fingerprint before == after；
- zero-arrival/eigenray与failure cleanup语义不退化；
- standard-case unit tests通过；
- 无新增重复 case。

### Handoff to next task

向 G03 提供 executable/probe hashes、oracle worst errors、product hashes、mode hashes、
fingerprints与C/P/N冻结回归值。

## G03 [GENERAL] — Documentation closure, full validation and Batch Report

**depends_on:** A01、A02、G01、G02
**can_parallelize_with:** none

### Goal

在所有 numerical/runtime/oracle gate通过后更新真实支持声明，运行批次级验证并生成
可复现 Batch Report。

### Exact documentation targets

- `Bellhop_RayReuse/doc/reference/REFERENCE_FEATURE_SUPPORT_MATRIX.md`
- `Bellhop_RayReuse/doc/reports/REPORT_F2CPP_RAYREUSE_PARITY_2026-08-25.md`
- `Bellhop_RayReuse/doc/status/STATUS_PROGRESS.md`
- `Bellhop_RayReuse/doc/workreports/FP-2D_CUBIC_SPLINE_SSP_BATCH_REPORT.md`

### Required changes

1. 只将 cubic spline `S` 从 Deferred/GAP更新为已支持。
2. 当前 SSP wording从 C/P/N²更新为 C/P/N²/Spline。
3. 明确记录 exact coefficient、continuous gradient、nonzero Hessian、complex per-frequency
   coefficients、frozen ownership、TL/R/A/a/E和three-mode evidence。
4. `Q/.ssp`继续明确 Deferred/unsupported。
5. 不把 S支持外推到 line/multisource/irregular或其他未支持维度。
6. 运行完整验证并生成 Batch Report；只声明 `Ready for Final Review`。

### Acceptance

- 文档与真实 parser/runtime/tests一致，无 C/P/N-only stale current wording；
- S标记为 supported，Q/.ssp仍unsupported；
- Batch Report记录 commands、producer/executable hashes、worst errors、product/mode hashes、
  trace passes、cache fingerprints和C/P/N zero-regression；
- isolated clean build成功；
- RayReuse full CTest通过；
- repository `uv run pytest`通过；
- standard-case unit与目标案例通过；
- `git diff --check`通过；
- `git diff -- Bellhop_F2CPP Bellhop_origin`为空；
- generated products未进入版本控制；
- `.pi/settings.json`未被worker修改。

## 4. Execution Order

```text
A01 [ADVANCED]
  ↓ reviewer checkpoint
A02 [ADVANCED] ──────────────┐
  ↓ reviewer checkpoint      │
G01 [GENERAL]                │
  ↓                          │
G02 [GENERAL] ← metadata prep may begin after A01
  ↓
G03 [GENERAL]
  ↓
Batch Acceptance
  ↓
read-only reviewer
  ↓
Codex Final Review
```

G02 的 case metadata/probe adapter准备可与 A02并行，但任何 oracle/product execution必须
等待 A02与G01完成。A01/A02不得并行修改同一 coefficient/evaluator API。

## 5. Batch Acceptance

由 coordinator/general-worker 在所有任务完成后统一执行：

- [ ] `S` parser正式接通，`Q`与unknown继续明确拒绝
- [ ] exact CSPLINE coefficient construction与F2CPP一致
- [ ] 2/3/4+ node branches均有独立anchor
- [ ] binary32-rounded `1.0F/6.0F` preserved
- [ ] real `c/dc/dz/d²c/dz²`与F2CPP一致
- [ ] density保持linear segment interpolation
- [ ] edge cubic extrapolation与hinted node ownership一致
- [ ] frequency-local complex coefficients与F2CPP一致
- [ ] node attenuation先转换、再构造complex spline
- [ ] spline gradient连续且不执行node jump
- [ ] nonzero Hessian进入dynamic ray
- [ ] GeometryTracer/Projector无product-specific S branch
- [ ] TL支持S
- [ ] 当前适用R/A/a/E支持S
- [ ] `munk_spline` Origin/F2CPP/RayReuse通过
- [ ] F2CPP/RayReuse geometry probe尽可能byte-identical
- [ ] Origin既有tolerance未修改
- [ ] nonreuse == reuse == parallel
- [ ] trace passes = 2 / 1 / 1
- [ ] frozen cache fingerprint稳定
- [ ] frequency evaluators/workspaces不串频
- [ ] C/P/N geometry与代表产品zero regression
- [ ] RayReuse CTest全部通过
- [ ] repository Python tests全部通过
- [ ] standard-case unit与目标case tests全部通过
- [ ] `git diff --check`通过
- [ ] `git diff -- Bellhop_F2CPP Bellhop_origin`为空
- [ ] 无Q/.ssp或其他越界实现
- [ ] `.pi/settings.json`未被修改
- [ ] generated products未进入版本控制

建议验证入口：

```bash
cmake \
  -S Bellhop_RayReuse \
  -B Bellhop_RayReuse/build/fp2d-clean \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON

cmake --build Bellhop_RayReuse/build/fp2d-clean --parallel

uv run ctest \
  --test-dir Bellhop_RayReuse/build/fp2d-clean \
  --output-on-failure

uv run pytest
uv run make -C test/standard_cases test-unit

uv run make -C test/standard_cases \
  test VERSION=origin CASE=munk_spline PROFILE=single
uv run make -C test/standard_cases \
  test VERSION=f2cpp CASE=munk_spline PROFILE=single
uv run make -C test/standard_cases \
  test VERSION=rayreuse CASE=munk_spline PROFILE=single

git diff --check
git diff -- Bellhop_F2CPP Bellhop_origin
git status --short
```

RayReuse broadband三模式使用现有 `standard_cases.py --rayreuse-execution-mode` 分别运行
`nonreuse`、`reuse`、`parallel`，结果写入隔离目录，避免互相覆盖。

## 6. OpenCode Batch Report Format

施工结束后生成：

`Bellhop_RayReuse/doc/workreports/FP-2D_CUBIC_SPLINE_SSP_BATCH_REPORT.md`

格式：

```markdown
# FP-2D Cubic Spline SSP Batch Report

## A. Completed Tasks

## B. GENERAL Work

## C. ADVANCED Work
- task
- advanced-worker implementation summary
- coefficient/mixed-precision decisions
- reviewer checkpoint

## D. Architecture Deviations
- none / details / ARCHITECTURE_BLOCKER

## E. F2CPP Oracle

## F. Origin Oracle

## G. C/P/N Zero Regression

## H. Execution Parity
- nonreuse
- reuse
- parallel
- trace passes
- cache fingerprints

## I. Product Validation
- TL
- R
- A
- a
- E

## J. Tests

## K. Files Changed

## L. Remaining GAPs

## M. Git Diff Summary

## N. Working Tree

## O. Ready for Final Review
YES / NO
```

Batch Report不得自行声明 `FP-2D ACCEPTED`。

## 7. Codex Final Review

最终验收必须只读检查 Worklist、Batch Report、真实diff、production code与当前测试证据。

### Architecture

- evaluator extension是否只增加 CubicSpline；
- 是否为Q/future SSP过度设计；
- coefficient storage ownership/lifetime是否合理；
- frozen cache contract是否保持；
- frequency workers是否持有独立complex coefficients/workspace。

### Numerical

- endpoint/not-a-knot construction；
- 2/3/4+ node branches；
- elimination/back-substitution order；
- polynomial coefficient order；
- `1.0F/6.0F` mixed precision；
- value/gradient/curvature与density；
- interval lookup、hinted node、extrapolation；
- attenuation conversion before complex coefficients；
- real-part derivative/Hessian observable semantics；
- nonzero Hessian dynamic-ray consumption；
- no gradient jump for spline。

### Runtime and regression

- S parser无fallback；
- Q/unknown明确拒绝；
- TL/R/A/a/E真实接入；
- F2CPP geometry/product parity；
- Origin既有tolerance；
- nonreuse/reuse/parallel；
- cache fingerprint；
- C/P/N zero regression；
- all existing beam/product families。

### Scope

确认没有实现或修改：

- Q/.ssp；
- range-dependent SSP；
- line/multisource/irregular；
- canonical curvilinear boundary；
- attenuation新模型；
- Influence Geometry Reuse；
- frequency interpolation；
- F2CPP/Origin production；
- `.pi/settings.json`模型路由。

Final Review结论只能为：

```text
FP-2D ACCEPTED
```

或：

```text
FP-2D REJECTED
```
