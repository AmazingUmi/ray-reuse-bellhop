# FP-2G Batch Report — Boundary / Material Closure

> Batch: FP-2G（canonical curvilinear `C` boundary + flat ordinary elastic halfspace P/S）
> 日期：2026-08-29
> 基线：`763c585`（HEAD，FP-2G 全部改动未提交于其上）
> Worklist：`doc/worklists/FP-2G_BOUNDARY_MATERIAL_CLOSURE_WORKLIST.md`

## A. 冻结 Scope（§0）

**做**：
1. Canonical curvilinear `C` boundary（BND-04）：`.ati`/`.bty` header 精确 token `C`（curvilinear short format）；`BoundaryGeometry` curvilinear kind（node tangent 0.5 平均、`Dss = (nextSlope - slope)/Δrange · t1³` curvature、首末延伸段回落 chord、scaled-NORM2 段长、`std::fma` 插值 node frame）；tracer seam 保持 chord collision + interpolated reflection frame + two-consecutive-outside 终止；`i3_curvilinear_oracle`（top vacuum + `.ati` C、bottom rigid + `.bty` C）459 角度 intermediate-state probe oracle 与 SHD 三方 closure；两频三执行模式 byte-identical。
2. Flat ordinary elastic halfspace P/S（BND-09）：`elastic_halfspace_flat` 与 `elastic_halfspace_fluid_control` 共享 case 纳入 RayReuse；`validate_i4_elastic_halfspace.py` 三方 SHD 比较与 `MINIMUM_SHEAR_EFFECT` shear guard 全部 PASS；两频三执行模式 byte-identical 与逐频求值确认。
3. 三执行模式：上述新 slice 在 `nonreuse/reuse/parallel` 下逐频产品 byte-identity（沿用既有门槛与 trace passes 语义）。

**不做**：
1. Attenuation units closure（ATT-01，移交 FP-2H DESIGN 裁定）。
2. `CS`/`CL` header 变体（F2CPP 同构显式拒绝）。
3. Curvilinear × halfspace/tabulated/grain（`A`/`G`/`F` + `C` 几何）。
4. Francois–Garrison / biological attenuation（volume attenuation gap，留待后续批次）。
5. `G/F` 与 LL 组合（BND-10）、`W`/`P` IRC 选项、Beam shift。
6. multisource / irregular / `Q` × curvilinear（不声明 parity）。
7. frozen `RayPathCache` schema / `contentFingerprint()` 算法变更（保持零改动）。

## B. 任务完成状态

| Task | 等级 | 状态 | Reviewer |
|---|---|---|---|
| A01 curvilinear 模型与 parser | ADVANCED | DONE | PASS |
| A02 tracer/reflection 集成与 geometry oracle | ADVANCED | DONE | PASS |
| A03 `i3_curvilinear_oracle` 三方 closure | STANDARD | DONE | N/A |
| A04 curvilinear broadband 三模式 | STANDARD | DONE | N/A |
| B01 flat elastic P/S 三方 closure | STANDARD | DONE | N/A |
| B02 flat elastic broadband 三模式与逐频性 | STANDARD | DONE | N/A |

## C. 关键设计决定（冻结）

1. **Curvilinear `C` 语义与数值实现**：
   - node tangent 采用相邻段 tangent 的 0.5 算术平均（Origin `ComputeBdryTangentNormal` 历史行为）。
   - curvature `Dss = (nextSlope - slope)/Δrange · t1³`，首末延伸段 curvature 为 0 回落 chord。
   - `reflectionSampleAtSegment` 采用 `std::fma` 严格复现 F2CPP 与 locked gfortran 的乘加融合。
   - tracer collision 判穿越采用 piecewise-linear chord，reflection 采样采用插值 node frame 与 curvature，保留 two-consecutive-outside 终止语义。
2. **frozen-cache 契约**：
   - curvilinear node frame 与 curvature 属 frequency-independent 几何量，写入 frozen `ReflectionEvent`（字段已存在并已参与 `contentFingerprint()`）。
   - elastic P/S、tabulated、grain、attenuation 等声学系数计算保持 frequency-local，在 `frequency_projector.cpp` 逐频求值，不写回 cache。
   - `RayPathCache` schema 与 `contentFingerprint()` 算法零改动。
3. **`CS`/`CL` header 变体显式拒绝**：
   - 与 F2CPP 保持一致，精确接受 canonical `C`，遇到 `CS`/`CL` 给出规范提示并拒绝。

## D. Batch Acceptance（coordinator 亲自抽验，2026-08-29）

| Gate | 结果 |
|---|---|
| 隔离 Release clean build（`build/fp2g-clean`，Warnings-as-Errors） | 通过，0 warning |
| 全量 CTest | **41/41 PASS** |
| `uv run pytest` | **178 passed** |
| `uv run make -C test/standard_cases test-unit` | **163 tests OK** |
| `i3_curvilinear_oracle` 459 角度 probe 对拍 | **459/459 PASS**，worst scaled error `3.24e-4`（p1/455角/41点，与 F2CPP 逐字节一致） |
| `i3_curvilinear_oracle` 三方 SHD 比较 | **PASS**（f2cpp↔rayreuse max TL diff `0.0 dB`，origin↔rayreuse passed；PRT markers 生效） |
| `i3_curvilinear_oracle` broadband 三模式 | **byte-identical**（100 Hz / 200 Hz SHD SHA-256 三模式一致；Trace passes `2/1/1`；`--verify-cache` PASSED） |
| `elastic_halfspace_flat(_fluid_control)` 三方 SHD 比较 | **PASS**（f2cpp↔rayreuse max TL diff `0.0 dB`，origin↔rayreuse passed；`MINIMUM_SHEAR_EFFECT` shear guards `0.01109` > `1e-6` 全部 PASS） |
| flat elastic broadband 三模式与逐频性 | **byte-identical**（两 case 三模式 SHD SHA-256 一致；Trace passes `2/1/1`；1000 Hz 与 2000 Hz 压强切片绝对差 > `4.7e-2` 呈现频率响应差异；frequency-local 声学计算由 `frequency_projector.cpp` 逐频调用与 `--verify-cache` before==after 严格闭环） |
| 冻结基线（munk_spline broadband 50/250 Hz reuse，亲跑） | fingerprint `1526667602348633172` before==after；Q fingerprint `2879552213476552188` before==after；multi-source / irregular baselines PASSED |
| Git 边界 | `git diff --check` 干净；`Bellhop_origin/`、`Bellhop_F2CPP/` 零改动；改动仅 `Bellhop_RayReuse/` + `test/standard_cases/`；无生成产品入库 |

## E. Oracle Case 清单

1. `i3_curvilinear_oracle`（top vacuum + `.ati` C curvilinear, bottom rigid + `.bty` C curvilinear; single 100 Hz, broadband 100/200 Hz）
2. `elastic_halfspace_flat`（constant-speed water over ordinary acousto-elastic halfspace; single 1000 Hz, broadband 1000/2000 Hz）
3. `elastic_halfspace_fluid_control`（fluid control for ordinary elastic halfspace; single 1000 Hz, broadband 1000/2000 Hz）

## F. 结论

FP-2G 施工与 Batch Acceptance 全部通过。无 HIGH/BLOCKER。
独立 Final Review 结论：**`FP-2G ACCEPTED`**（2026-08-29）；
Re-Final Review 结论：**`FP-2G ACCEPTED`**（2026-08-29）；
**FP-2G CLOSED**（commit `eb27045`）。
