# FP-2D-R1 Final Review Remediation Report

> 批次：FP-2D-R1 — FP-2D cubic spline SSP final-review remediation
> 日期：2026-08-28
> 状态：remediation 与 coordinator 复验完成；独立 Re-Final Review `ACCEPTED`
> 原批次报告：`FP-2D_CUBIC_SPLINE_SSP_BATCH_REPORT.md`

## A. Final Review findings

| ID | Finding | 处理结果 |
|---|---|---|
| HIGH / R01 | `munk_spline` broadband 250 Hz Origin final-field TL 差 `0.00634765625 dB`，超过既有 `0.005 dB` | 完成 root-cause 调查；落盘经 reviewer 批准的 scoped oracle policy 与 C++ exact hard gate；默认 matrix 入口复验通过 |
| LOW / R02 | 缺少真实 cubic-spline evaluator 经 production `stepRay` 跨 SSP node 的 no-jump regression | 新增真实跨 100 m node 回归，显式证明误施 `applyGradientJump` 会产生可检测 dynamic-p 偏差 |
| LOW / R03 | 原 Batch Report 错称 3-node 分支有独立 F2CPP literal anchor | 改为 4+ node literal anchors、2-node 独立解析 anchor；3-node 由 structural/continuity regression 覆盖 |
| LOW / R04 | `STATUS_PROGRESS.md` 与 Batch Report 状态滞后且含“已知限制：无”等过强结论 | 文档先改为 R1 remediation 完成、等待 Re-Final Review；验收后同步为 `ACCEPTED` |

## B. Root cause

R01 调查结论：

```text
RAYREUSE BUG: NO
F2CPP LEGACY DIFFERENCE: YES
CASE/POLICY BUG: YES
TOLERANCE CHANGE REQUIRED: YES（仅限下述 scoped comparison policy）
```

250 Hz 的 F2CPP 与 RayReuse decoded complex64 pressure 完全一致；Origin 与两者的
差异已存在于未修改的 F2CPP reference implementation，不是 RayReuse port regression。
最大 TL 差位于 source depth 1000 m、receiver depth 975 m、range 15.6 km：Origin 与
F2CPP complex pressure 只差约 `3.1351e-13`，但近零 pressure 经 `log10` 放大后，
float64 TL 差约 `0.006356 dB`，比较器在 binary32 TL 上得到
`0.00634765625 dB`。93200 个有效 cell 中只有该 cell 超过 `0.005 dB`。

single-250 与 broadband 的 250 Hz 输出一致，排除了多频 state contamination；
F2CPP/RayReuse geometry probe byte-identical，Origin intermediate-state matrix 仍在原
严格预算内。历史 `0.005 dB` 是 50 Hz case policy，把它未经说明地用于新增的 250 Hz
Origin final-field comparison 才是标准案例 policy 缺口。

remediation 预审又发现默认 `model_matrix.py` 曾统一使用 shared tolerance，使
`munk_spline` 50 Hz 被错误按 `0.001 dB` 门控。最终修复让默认 matrix 优先解析
case-local `tolerances.toml`，缺失时回退 shared file；显式 `--tolerances` 仍是所有
selected cases 的全局 override。

## C. Changes made

- `compare_fields.py`：允许调用方提供单次 TL absolute override，并提供 canonical
  little-endian complex64 decoded-pressure payload。
- `model_matrix.py`：
  - 仅对 `case=munk_spline + reference=origin + candidate=f2cpp/rayreuse-* + 250 Hz`
    使用 `0.0065 dB`；
  - 对 F2CPP→每个 RayReuse mode 的 250 Hz decoded payload执行 bytes equality hard
    gate，并把结果纳入总体 `passed`；
  - 默认优先 case-local tolerance，回退 shared tolerance；显式 override 语义不变；
  - 报告记录 effective tolerance、policy、payload bytes/hash 与实际 tolerance path。
- `test_model_matrix.py`：增加方向/频率/case scope、50 Hz 不放宽、payload exact、
  默认 case-local 路由与显式 override 回归。
- `ray_stepper_test.cpp`：增加真实 cubic spline `GeometrySspEvaluator → stepRay`
  跨 node no-jump regression。
- 更新 standard-case README、FP-2D Batch Report 与 progress 状态。

Spline production、Bellhop_F2CPP production、Bellhop_origin production 均未修改。

## D. Numerical evidence

### Origin ↔ F2CPP

- 250 Hz max pressure absolute `5.936040103904361e-09`；
- max pressure relative `7.373288390226662e-04`；
- max TL difference `0.00634765625 dB`；
- scoped limit `0.0065 dB`，PASS。

### Origin ↔ RayReuse

- nonreuse/reuse/parallel 的 250 Hz 指标与 F2CPP comparison 相同；
- 三种模式均按同一 scoped `0.0065 dB` gate PASS；
- 50 Hz 仍使用 case-local `0.005 dB`，RayReuse max TL
  `0.00225830078125 dB`，PASS。

### F2CPP ↔ RayReuse

- 250 Hz ordinary field comparison：pressure/TL difference 均为 zero；
- decoded complex64 hard gate：nonreuse/reuse/parallel 各 `805608` bytes；
- 三者与 F2CPP SHA-256 均为
  `94ffd3638de4f286079e65489db70a643ca38469f706b330ade897f23becf4d0`，byte-exact。

Intermediate-state matrix：F2CPP/RayReuse probe byte-identical；370 points、367 steps、
2 reflection events；相对 Origin worst `h_m`@369，absolute
`1.4523493518936448e-11`、scaled `5.536846572998383e-4`，PASS。

## E. Oracle policy

没有修改 `test/standard_cases/cases/munk_spline/tolerances.toml`，也没有 case-wide
放宽。经独立 reviewer checkpoint 批准的 policy 只覆盖 Origin→C++ 250 Hz TL，阈值
为 `0.0065 dB`；50 Hz TL 仍为 `0.005 dB`，pressure、phase、trajectory、
intermediate-state 与 C++→C++ rules 均不变。

新增的 F2CPP→RayReuse decoded-payload exact gate比原 pressure/TL tolerance 更严格，
直接防止 scoped Origin policy 掩盖 RayReuse port regression。默认 per-case tolerance
路由修复只恢复既有 case contract，不改变 tolerance 数值。

## F. Regression

| Gate | 结果 |
|---|---|
| clean RayReuse build + CTest | 36/36 passed |
| repository pytest | 173/173 passed |
| standard-case unit | 158/158 passed |
| 默认 CLI `munk_spline` broadband matrix（三模式、不传 `--tolerances`） | EXIT=0，PASSED |
| `munk_spline` intermediate-state matrix | PASSED |
| C-linear probe | `809b126d4b2657b8c54100e9f0e867c69bd26633963a58a883b2e985b98492e2`，match |
| PCHIP probe | `eb51ced19656a7724594e0dc7e0c2c5977daa8447962d76b9ca8112c55c646a0`，match |
| N²-linear probe | `360dda437550e396b531ed9a4692a006ebe8e5e29ddcaddde40fe8ddbcc00be8`，match |
| git checks | `git diff --check` 干净；F2CPP/Origin production diff 为空 |

numerical production 未修改，因此未机械重跑巨大 R/A/a/E 产品；原 Batch Acceptance
的五产品 byte-identical 与三模式 cache/execution 证据仍适用。

## G. Working tree

- FP-2D 与 FP-2D-R1 全部修改保持未提交、未暂存状态；未 commit、未 push。
- `.pi/settings.json` 的现有修改是用户维护的执行配置，不属于本批次，任何 Agent
  均未触碰。
- 复验产物位于 `/private/tmp/fp2d-r1-*` 与 build/results 可再生成目录，没有 generated
  `.prt/.shd/.ray/.arr` 进入 tracked diff。
- Bellhop_F2CPP 与 Bellhop_origin production diff 为空。

## H. Re-Final Review

`ACCEPTED`

R01–R04 已落盘，独立 reviewer 首轮发现的默认 tolerance-routing finding 也已修复；
coordinator 在最终工作树复跑全部必要 gate后，独立 final-reviewer 检查完整 Worklist、
两份报告、真实 diff、production/source-of-truth、oracle 与当前测试证据，未发现阻断
finding。
