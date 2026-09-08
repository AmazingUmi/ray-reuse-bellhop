# FP-2E — Quadrilateral Range-Dependent SSP (`Q`/`.ssp`) Parity Worklist

> **Batch:** FP-2E
> **Feature:** Quadrilateral range-dependent SSP (`Q` + `.ssp`) parity
> **Baseline commit:** `6b3428c1adc145fe9659bc6504f9eaf3b545bd7f`
> **Baseline working tree:** clean（无未提交用户修改；`git status --short` 为空）
> **Production source of truth:** `Bellhop_F2CPP`
> **Numerical / observable oracle:** `Bellhop_origin`
> **Contract:** FP-2E-A Q/.ssp Integration Contract（已完成独立 Contract Final Review，
> 结论 **`FP-2E-A ACCEPTED`**，reviewer 为 Gemini 3.7 Flash，外部完成；本 worklist
> 记录其获批条款，不重新设计）
> **Status:** worklist complete；construction authorized

Workers may not expand FP-2E scope. 本批次只机械迁移 F2CPP 已正式支持的二维
quadrilateral SSP `Q` production slice。遇到 3D、Nx2D、line source、multisource、
irregular receivers 等 future 接口问题时，只记录 dependency；不得顺手实现。若真实
代码与本清单的核心架构边界冲突，停止对应任务并在 Batch Report 中标记
`ARCHITECTURE_BLOCKER`，不得自行重设整个阶段。

## 1. Baseline 与全局约束

### 1.1 施工基线

```text
baseline commit: 6b3428c1adc145fe9659bc6504f9eaf3b545bd7f
git status --short: (empty)
```

前置批次：FP-2B（PCHIP）、FP-2C（N²-linear）、FP-2D（cubic spline）已提交并
验收（FP-2D 独立 Re-Final Review `ACCEPTED`，只关闭 `S` slice）。

### 1.2 排除规则（对所有 worker 生效）

- 不修改 `Bellhop_F2CPP` production；不修改 `Bellhop_origin` production。
- 不重新设计 Q 数值算法；机械迁移 F2CPP production semantics。
- 不改变 `RayPath` / `RayPathCache` frozen contract。
- 不把 `rangeSegmentIndex` 写入 `RayState` / `RayPath` / cache（只允许存在于
  transient sample/step/limit 结构与 tracer local stack）。
- 不改变 nonreuse/reuse/parallel ownership model。
- 不做无关重构。
- 不修改用户维护的 `.pi/settings.json`（provider/model/thinking/modelScope）。
- 不 stage、不 commit、不 push。
- 不进入 FP-2F 或其他新阶段。
- provider/auth/runtime 故障标记 `AGENT_RUNTIME_BLOCKER`，不得切换模型路由。
- worker 不得自行声明 `FP-2E ACCEPTED`；最多声明 Ready for Final Review。

### 1.3 Architecture invariants（FP-2E-A 合同条款）

1. `RayState`、`RayPath`、`RayPathCache` 的 fields/layout/`contentFingerprint`
   算法与 freeze contract 全部不变；Q 工作不得为方便实现修改 cache。
2. `rangeSegmentIndex` 只允许出现在：
   - `SoundSpeedSample`（transient sample）；
   - `StepLimitRequest::initialRangeSegmentIndex`；
   - `RayStepResult::rangeSegmentIndex`；
   - `GeometryTracer` local variable。
   禁止加入 `RayState`、`StepQuadrature`、`ReflectionEvent`、`RayPath`、
   `RayPathCache`。
3. 非 Q profile 不分配 quadrilateral grid heap storage。
4. `.ssp` 缺失、维度不匹配 → explicit failure；禁止 fallback。
5. Q real `c(r,z)` 只在 geometry trace 阶段决定轨迹；frequency projection 不得
   修改 frozen path，不得将 `rangeSegmentIndex` 写回 cache。
6. 唯一任务依赖链：A01 → A02 → (G01) → G02 → G03；A01/A02 checkpoint 未 PASS
   不得进入下游。

### 1.4 Q 数值 source of truth（F2CPP production）

- `Bellhop_F2CPP/include/bellhop/model/quadrilateral_ssp.hpp`
- `Bellhop_F2CPP/src/model/quadrilateral_ssp.cpp`
- `Bellhop_F2CPP/include/bellhop/model/quadrilateral_frequency_ssp.hpp`
- `Bellhop_F2CPP/src/model/quadrilateral_frequency_ssp.cpp`
- `Bellhop_F2CPP/include/bellhop/model/environment.hpp`（`QuadrilateralSspGrid` /
  `SharedQuadrilateralSspGrid` / `SoundSpeedProfile::quadrilateralGrid()`）
- `Bellhop_F2CPP/src/model/environment.cpp`
- `Bellhop_F2CPP/src/model/sound_speed_evaluator.cpp`
- `Bellhop_F2CPP/src/io/environment_parser.cpp`（`readQuadrilateralSspGrid`）
- `Bellhop_F2CPP/src/ray/ray_stepper.cpp`
- `Bellhop_F2CPP/src/ray/geometry_tracer.cpp`（`GeometryStepLimiter`）
- `Bellhop_F2CPP/src/model/simulation_case.cpp`
- Origin 参考：`Bellhop_origin/Bellhop/sspMod.f90` Quad、`Bellhop_origin/Bellhop/Step.f90`
  ReduceStep2D。

### 1.5 已有 oracle / cases（不得新造重复）

- 标准算例：`q_range_dependent_cross_gradient`、`q_range_independent_control`
  （`test/standard_cases/cases/`，当前 compatibility `["origin","f2cpp"]`）。
- F2CPP Q geometry oracle baseline：
  `Bellhop_F2CPP/doc/reports/validation/i5_q_geometry_oracle_report.json`
  —— 715 points / 714 integrated steps / 0 reflections / ExitedDomain，
  probe CSV SHA-256 `4e22fd057eeca5dcabca171aeeb9129fba09e7616c0d8fdb5621f26c6029d32f`。
- F2CPP Q SSP validation report：
  `Bellhop_F2CPP/doc/reports/validation/i5_quadrilateral_ssp_report.json`。
- Oracle 工具：`generate_i5_q_oracle.py`、`validate_i5_quadrilateral_ssp.py`、
  `compare_f2cpp_geometry_oracle.py`、`validate_ray_oracle.py`。

## 2. Task Definitions

## A01 [ADVANCED] — Q Grid, Parser and Evaluators

**Status:** DONE（advanced-worker 实现；read-only reviewer mechanical-diff review
PASS，Release CTest 37/37；`A01 CHECKPOINT: PASS` 2026-08-28）

**depends_on:** none（FP-2D 已在基线内）
**can_parallelize_with:** none
**executor:** advanced-worker
**reviewer:** read-only reviewer（必须对照 F2CPP production 做 mechanical-diff review）

### Goal

机械迁移 F2CPP Q SSP 数据模型、`.ssp` reader 与两个 evaluator，建立独立可测的
production-equivalent `QuadrilateralSsp` / `QuadrilateralFrequencySsp`，接通
generic dispatch（enum/factory/parser），但暂不接 stepper/tracer runtime。

### A01.1 Grid ownership

引入与 F2CPP 对齐的 `QuadrilateralSspGrid` / `SharedQuadrilateralSspGrid`
（RayReuse namespace），由 `SoundSpeedProfile` immutable shared ownership
（`std::shared_ptr<const QuadrilateralSspGrid>`）。非 Q profile 不分配 grid heap
storage。`RayState`/`RayPath`/`RayPathCache`/`contentFingerprint` 不变。

### A01.2 `.ssp` reader

机械迁移 F2CPP `readQuadrilateralSspGrid` 语义（`.env` top option `Q` +
same-root `.ssp`）。严格验证：range count（≥2）、depth count、matrix shape、
ranges strictly increasing、finite values、positive real sound speed、
km → m conversion、`.ssp` missing → explicit failure、dimension mismatch →
explicit failure。禁止 fallback。

### A01.3 QuadrilateralSsp evaluator

机械迁移 F2CPP `QuadrilateralSsp`：depth slope precomputation（逐 column
initialization order）、bilinear interpolation、`cr`、`cz`、`crz`、`crr = 0`、
`czz = 0`、density depth interpolation、range locator、depth locator。

Exact-node ownership：

- Range：内部 cells `[r_i, r_{i+1})`，最终 cell `[r_{N-2}, r_{N-1}]`；内部 exact
  range node 默认选择右 cell，除非 hinted lookup 明确保留当前合法 cell。
- Depth：严格复制 F2CPP hinted locator / internal-node semantics（internal exact
  node 选右 cell，除非 hint cell 已保留）。

### A01.4 Outside-grid behavior

严格保持 F2CPP：`range < ranges.front()` 或 `range > ranges.back()` →
`ValidationError`。禁止 extrapolation / silent clamp / fallback。Cell 内 bilinear
fraction `s1` 的 clamp 行为与 F2CPP 一致（`min(1.0)`/`max(0.0)` 顺序）。

### A01.5 Frequency evaluator

机械迁移 `QuadrilateralFrequencySsp`：real `c(r,z)` 来自 2D matrix；imaginary
`c_i(z,f)` 来自 `.env` reference depth profile 逐节点 frequency conversion 后仅沿
depth 插值；不从 `.ssp` 创建 2D attenuation；按 F2CPP 顺序逐频 conversion；
imaginary 结果要求 finite 且 non-negative（F2CPP `addImaginarySoundSpeed` 语义）。
至少两频 evaluator 独立、重复 evaluation bit-stable。

### A01.6 Generic dispatch

扩展 `SspInterpolationKind::Quadrilateral`、`GeometrySspEvaluator`、
`FrequencySspEvaluator`、factories、parser dispatch（当前 RayReuse parser 对 `Q`
显式拒绝的诊断改为接通）。禁止 default silent fallback。

### A01 tests（最少覆盖）

grid validation、exact range nodes、exact depth nodes、outside range rejection、
bilinear value、`cr`、`cz`、`crz`、density、frequency-local imaginary speed、
Q evaluator 与 C/P/N/S evaluator dispatch 不混淆。

### Acceptance

- 组件测试通过；targeted CTest 全绿；
- reviewer 对照 F2CPP production mechanical-diff review PASS；
- 输出 `A01 CHECKPOINT: PASS`，否则停止 A02 并修复。

### Handoff

向 A02 提供稳定 evaluator API（`rangeSegmentCount` / `locateRangeSegment` /
`minimumRangeForSegment` / `maximumRangeForSegment` / `evaluateAtSegments` /
双 hint `evaluate`）与 locator 契约。

## A02 [ADVANCED] — Stepper / Limiter / Tracer 2D Integration

**Status:** DONE（advanced-worker 实现；read-only reviewer mechanical compare PASS：
双 hint `stepRay` 与 F2CPP 逐字节一致、gradient jump depth-priority/corner
single-jump/singular 算术一致、limiter SSP range 接入一致、`ray_path.hpp`/
`ray_path_cache.*`/field/solver diff 全部 0 字节；Release CTest 37/37；
`A02 CHECKPOINT: PASS` 2026-08-28）

**depends_on:** A01
**can_parallelize_with:** none（本批次最高风险任务）
**executor:** advanced-worker
**reviewer:** read-only reviewer（mechanical compare F2CPP + RayPath/Cache diff 检查）

### A02.1 Transient range state

按 F2CPP 扩展：`SoundSpeedSample::rangeSegmentIndex`、
`StepLimitRequest::initialRangeSegmentIndex`、`RayStepResult::rangeSegmentIndex`；
允许 `GeometryTracer` local `rangeSegmentIndex`。禁止加入 `RayState`、
`StepQuadrature`、`ReflectionEvent`、`RayPath`、`RayPathCache`。

### A02.2 RayStepper

机械对齐 F2CPP `stepRay(evaluator, state, depthSegment, rangeSegment, ...)`：
predictor / midpoint / endpoint evaluator 都正确传递双 hint。保留既有单
depth-hint API 兼容 C/P/N/S（range hint 恒为 0 的 overload）。

### A02.3 Gradient jump

机械迁移 F2CPP `crossedDepthSegment` / `crossedRangeSegment`：

- 无 crossing → return（不执行 jump）；
- depth crossing：`tan(alpha) = tr / tz`；
- range crossing：`tan(alpha) = -tz / tr`；
- corner crossing：depth branch 优先，single jump only，不额外执行第二次 range
  correction。

Singular behavior：禁止自行添加 epsilon denominator、clamp、skip、finite
substitute；保持 F2CPP arithmetic，继续使用最终 finite validation。

### A02.4 GeometryStepLimiter

Limiter 持有 / 可访问 `GeometrySspEvaluator`。有效 horizontal segment：

```text
max(top.minimumRange, bottom.minimumRange, SSP.minimumRangeForSegment)
min(top.maximumRange, bottom.maximumRange, SSP.maximumRangeForSegment)
```

Trial crossing range grid boundary → `reduceAtRange(...)` 确保落到 grid line。
完整保留 F2CPP `minimumStep = 1.0e-3 * nominalStepLength`，不得删除或改写。

### A02.5 GeometryTracer

初始化 `initialSegment` + `initialRangeSegment`（source position 经
`locateRangeSegment(sourcePosition.range, 0)`）；每步
`result = stepRay(..., segmentIndex, rangeSegmentIndex, ...)` 后
`segmentIndex = result.segmentIndex; rangeSegmentIndex = result.rangeSegmentIndex`；
range index 只存在于 tracer local stack。source speed Q path 必须按 F2CPP 从
`Vec2{0, sourceDepth}` 真实二维采样。

### A02.6 Cache hard invariant

显式验证 `RayState`/`RayPath`/`RayPathCache` sizeof/layout/fields、
`contentFingerprint` 算法、freeze contract 均不受 Q 工作影响。

### A02 tests（production-path，至少 9 项）

1. cross range boundary；
2. cross depth boundary；
3. simultaneous range+depth corner crossing；
4. exact range-node landing；
5. range outside grid rejection；
6. range jump actually changes dynamic P；
7. non-Q modes do not trigger range behavior；
8. no zero-step oscillation at grid line；
9. minimum-step clamp retained。

### Acceptance

- targeted + 全量 CTest 通过；
- reviewer mechanical compare F2CPP PASS + RayPath/Cache 无 diff 确认；
- 输出 `A02 CHECKPOINT: PASS`，否则不进入 G01。

## G01 [GENERAL] — Q Geometry / CLI / Oracle Closure

**Status:** DONE（worker 实现；read-only reviewer 亲验 PASS：probe 与冻结基线
`4e22fd05…` byte-identical（715 points/714 steps/ExitedDomain）、Origin
intermediate oracle PASS（worst scaled 1.06e-9）、C/P/N/S 四 probe SHA match、
缺 `.ssp` EXIT=1、freeze contract diff 空。`G01 CHECKPOINT: PASS` 2026-08-28）

**depends_on:** A01、A02
**can_parallelize_with:** G02 的 isolated case metadata 准备
**executor:** worker（reviewer 按需）
**gate:** 完成后必须单独形成 `Q GEOMETRY CHECKPOINT`

### G01.1 CLI / parser

RayReuse 正式接受 `Q` 及合法组合（如 `QVW`）。`.ssp` 自动按 environment root
staging / resolution（与 origin/f2cpp adapter 行为一致）。缺 `.ssp` 必须 nonzero
exit。

### G01.2 Standard case

复用既有 `q_range_dependent_cross_gradient`、`q_range_independent_control`，
不新造重复 case。runtime 实际通过后将 RayReuse 加入 compatibility：
`versions = ["origin", "f2cpp", "rayreuse"]`。

### G01.3 Geometry oracle

RayReuse probe 增加 `i5-quadrilateral` 配置（与 F2CPP 相同 launch/config：
source 50 m、depths [0,100]、ranges [0,350,800]、speeds 2×3 matrix、step 1.0、
range limit 710、depth limit 101、vacuum/rigid）。目标 F2CPP ↔ RayReuse probe
byte-identical（基线 715 points / 714 steps / 0 reflections / ExitedDomain，
SHA `4e22fd05…`）。复核 positions、slowness、p/q、c、real travel time、
quadrature、midpoint、termination、point count、step count。

### G01.4 Origin oracle

接入已有 Origin Q geometry/intermediate oracle（`generate_i5_q_oracle.py` /
`validate_ray_oracle` / `compare_f2cpp_geometry_oracle.py`；Origin final-field
走 `validate_i5_quadrilateral_ssp.py` 既有 policy）。不得重新放宽 tolerance。
失败先分类 `RAYREUSE BUG` / `F2CPP LEGACY DIFFERENCE` / `ORACLE POLICY` /
`CASE CONFIG`，禁止直接扩大 tolerance。

### Geometry checkpoint（G01 完成后）

```text
[ ] F2CPP/RayReuse probe byte-identical
[ ] Origin intermediate oracle PASS
[ ] range boundary crossing observed
[ ] nonzero crz consumed
[ ] dynamic p/q correct
[ ] C/P/N/S geometry probes unchanged
[ ] RayPathCache unchanged
```

全部 PASS 才进入 G02。

## G02 [GENERAL] — Broadband / Products / Reuse

**Status:** DONE（worker 完成：三模式 Trace passes 2/1/1、fingerprint
`2879552213476552188` before==after、三模式 SHD 逐字节一致；五产品（TL/R/A/a/E
两频 8 对文件）f2cpp=rayreuse byte-identical；Origin final-field
`validate_i5_quadrilateral_ssp.py` 12 runs 全 PASS、tolerance 未动；C/P/N/S
broadband SHD 四冻结值 + probe SHA + munk_spline fingerprint
`1526667602348633172` 全部 match；新增
`testQuadrilateralFrozenPathProjection`。2026-08-28）

**depends_on:** G01（execution 依赖 A02）
**executor:** worker
**已有 broadband profile：** 1000 / 2000 Hz（case 自带 `broadband_smoke`）

### G02.1 Execution modes

Q 验证 `nonreuse` / `reuse` / `parallel`：Trace passes 2 / 1 / 1；reuse/parallel
cache fingerprint before == after；三种模式产品一致。

### G02.2 Frozen geometry

证明 Q real `c(r,z)` 只在 geometry trace 阶段决定轨迹；1000/2000 Hz projection
不修改 frozen path；不将 `rangeSegmentIndex` 写回 cache。

### G02.3 Products

TL / R / A / a / E 建立 F2CPP → RayReuse parity，优先 byte-identical。既有
byte-exact 浮点 contract 不得退化成 loose tolerance。

### G02.4 Origin final-field

复用既有 Q Origin/F2CPP oracle policy（`validate_i5_quadrilateral_ssp.py`，
control case tolerance `tolerances_i5_q_control.toml`）。不新造全局 tolerance。
若存在 legacy difference，先证明 `F2CPP == RayReuse`，再讨论 oracle policy。

### G02.5 Zero regression

C/P/N/S frozen regression 全部重跑：四者 geometry probe SHA 与 broadband SHD
frozen hashes 保持原值；既有 nonreuse/reuse/parallel cache fingerprint 不因 Q
range API 改变。

## G03 [GENERAL] — Docs / Full Validation / Batch Report

**Status:** DONE（worker 完成：3 个文档更新（Q → supported/parity，只声明二维
Q slice；FP-2C 行经核查无验收记录故保持原文，依据写入 Batch Report N 节）；
clean-build full validation 37/37 + 173 + 158 全过；Batch Report A–N 全节落盘，
结尾 `Ready for Final Review: YES`。2026-08-28）

**depends_on:** A01、A02、G01、G02
**executor:** worker

### 文档

更新 `REFERENCE_FEATURE_SUPPORT_MATRIX.md`、
`REPORT_F2CPP_RAYREUSE_PARITY_2026-08-25.md`、`STATUS_PROGRESS.md`，必要时
`PLAN_CURRENT_WORK.md`。Q/.ssp 从 Deferred/GAP → supported/parity，但只关闭实际
完成的 Q slice；不扩大声明到 3D、Nx2D、line source、multisource、irregular
receivers 等未支持范围。若 FP-2C status 仍错误写"待最终验收"而项目历史已完成
验收，可基于已有验收证据在本批次修正。

### Full validation

最终工作树 clean validation（`build/fp2e-clean`）：

```bash
uv run cmake -S Bellhop_RayReuse -B Bellhop_RayReuse/build/fp2e-clean \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
uv run cmake --build Bellhop_RayReuse/build/fp2e-clean --parallel 8
uv run ctest --test-dir Bellhop_RayReuse/build/fp2e-clean --output-on-failure
uv run pytest
uv run make -C test/standard_cases test-unit
```

然后执行 Q single standard case、Q broadband standard case、Q geometry probe、
Q intermediate-state oracle、Q TL/R/A/a/E、Q nonreuse/reuse/parallel、Q cache
fingerprint、C/P/N/S frozen regressions。最后：

```bash
git diff --check
git diff -- Bellhop_F2CPP Bellhop_origin   # 必须为空
git status --short
git diff --stat
```

不得存在 tracked generated（`.prt/.shd/.ray/.arr`、probe csv、temporary result）。

### Batch Report

生成 `Bellhop_RayReuse/doc/workreports/FP-2E_QUADRILATERAL_SSP_BATCH_REPORT.md`，
包含：A. Completed Tasks（A01/A02/G01/G02/G03）；B. Architecture Contract
（FP-2E-A ACCEPTED、RayPath unchanged、RayPathCache unchanged、
rangeSegmentIndex transient only）；C. F2CPP Migration（机械迁移与有意差异）；
D. Origin Oracle；E. Q Geometry；F. Frequency Projection；G. Execution Parity；
H. Product Validation；I. C/P/N/S Zero Regression；J. Tests；K. Files Changed；
L. Remaining GAPs；M. Working Tree；N. Known Limitations。禁止写
"known limitations: none"，除非确实不存在任何 Q scope 相关验收边界。

## 3. Execution Order

```text
A01 [ADVANCED]  → reviewer checkpoint（A01 CHECKPOINT: PASS）
A02 [ADVANCED]  → reviewer checkpoint（A02 CHECKPOINT: PASS）
G01 [GENERAL]   → Q GEOMETRY CHECKPOINT
G02 [GENERAL]
G03 [GENERAL]
Batch Acceptance（coordinator 亲验）
```

## 4. Batch Acceptance（coordinator 亲自抽验，不引用 worker 声称）

**Status:** PASS（coordinator 于 2026-08-28 在最终工作树亲验，全部 gate 实际运行）

| Gate | 亲验结果 |
|---|---|
| Q geometry oracle | 两侧 probe（oracle 角度 −0.002626749710359303 rad）`cmp` byte-identical，SHA `4e22fd05…` = 冻结基线，715/714/ExitedDomain |
| Origin intermediate oracle | `intermediate_state_matrix.py --case q_range_dependent_cross_gradient` PASSED；f2cpp/rayreuse 双侧 worst scaled `1.0583956e-09`（t_z@131）；`cpp_probe_byte_identical: true` |
| Q broadband 三模式 | clean executable + `--verify-cache` 亲跑：Trace passes 2/1/1；三模式 SHD 均 `b53c02cb…`；reuse/parallel fingerprint before==after `2879552213476552188` |
| 代表产品 | TL/R/A/E 用 clean RayReuse 与 F2CPP 双侧重新生成：TL `132d6af2…`、R `00e11586…`、A 两频 `19ef39a8…`/`0dd74fb1…`、E 两频 `deae13ce…`/`e5324d32…` 全部 byte-identical 且 = G02 冻结值 |
| Origin final-field | `validate_i5_quadrilateral_ssp.py` 三 leg 重跑：status passed；origin↔rayreuse 6/6 与 origin↔f2cpp worst 完全相同（range-dependent `1.53e-05 dB`、control `0.0113 dB`）；tolerance 未动 |
| C/P/N/S frozen regression | 四 probe 亲重跑 SHA 全 match（C `809b126d…`/P `eb51ced1…`/N `360dda43…`/S `1fd0e4f8…`）；四 broadband SHD `cf1f9711…`/`fd5b2e2c…`/`18817c67…`/`74028065…` match；munk_spline reuse `--verify-cache` 亲跑 fingerprint `1526667602348633172` before==after |
| clean CTest | `build/fp2e-clean` 37/37 passed |
| pytest | 173 passed（含 360 subtests） |
| standard-case unit | Ran 158 tests, OK |
| git checks | HEAD=`6b3428c`（未 commit）；`git diff --check` 干净；`git diff -- Bellhop_F2CPP Bellhop_origin` 空；无 tracked generated；`rangeSegmentIndex` 在 ray_path.hpp/ray_path_cache.* 中出现 0 次 |
| `.pi/settings.json` | 未被修改（不在 diff 中） |
| 缺 `.ssp` | 亲复现 EXIT=1 + `unable to open quadrilateral SSP file` |

最终状态只能输出：

```text
FP-2E CONSTRUCTION COMPLETE
BATCH ACCEPTANCE: PASS
READY FOR FINAL REVIEW: YES/NO
```

不得输出 `FP-2E ACCEPTED`；正式 `ACCEPTED` 只能由后续独立 final-reviewer 给出。
若存在未关闭 HIGH/BLOCKER → `READY FOR FINAL REVIEW: NO` 并停止。
