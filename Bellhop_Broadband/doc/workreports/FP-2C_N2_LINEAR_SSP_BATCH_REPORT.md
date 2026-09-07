# FP-2C N²-linear SSP Batch Report

> Batch：FP-2C — N²-linear (`N`) SSP parity（RayReuse）
> Worklist：[`FP-2C_N2_LINEAR_SSP_WORKLIST.md`](../worklists/FP-2C_N2_LINEAR_SSP_WORKLIST.md)
> 报告日期：2026-08-28；状态：A01–G02 施工完成、A01/A02 checkpoint PASS、
> batch review PASS、Batch Acceptance 全部通过；Ready for Final Review: YES

本报告数字来源限定为两类：Worklist 冻结的批次事实（baseline、A01/A02/G01
checkpoint 记录），以及 G02 施工期间亲手运行的命令输出。每个数字的来源在文中
标注；无法归入这两类的数字一律未使用。

## A. Baseline

- HEAD：`3130e60509c87334cdf4f6fd2ee0360b04db7fe`（施工前，冻结事实）
- RayReuse Release CTest：34/34；F2CPP targeted（`n2_linear_ssp`/
  `sound_speed_evaluator`/`environment_parser`）：3/3（冻结事实）
- 编译器：Apple clang 21.0.0（clang-2100.1.1.101）、`/usr/bin/c++`、Release、
  无额外 flags（冻结事实；G02 在 clean build 的 `CMakeCache.txt` 复核一致）
- 施工前可执行文件 SHA-256：
  - `bellhop_rayreuse` = `dafa55aca53ecff8f47df7c74ecdacb7c861ec68134e55dd9646e14480ec68df`
- 施工前 probe / broadband SHD 基线（冻结事实）：
  - C probe（munk，0.0125 rad）= `809b126d4b2657b8c54100e9f0e867c69bd26633963a58a883b2e985b98492e2`
  - P probe（munk-pchip）= `eb51ced19656a7724594e0dc7e0c2c5977daa8447962d76b9ca8112c55c646a0`
  - C broadband SHD（nonreuse 默认）= `cf1f9711aefcab087bd766c395a03b935c1c9cf13980335a368035515fd126bc`
  - P broadband SHD = `fd5b2e2cf77a524ec4972e8563c19efe0e33de48c87677a193c2d20c80d85cde`

## B. Completed Work Items

| 项 | 类型 | 状态 | 执行者 |
|---|---|---|---|
| A01 — concrete N² backend + parser/model dispatch | [ADVANCED] | 完成，reviewer checkpoint PASS | advanced-worker agent（ZCode subagent） |
| A02 — dynamic ray + frequency-local/cache integration + `applyGradientJump` 重命名 | [ADVANCED] | 完成，reviewer checkpoint PASS | worker agent（ZCode subagent） |
| G01 — 三方 oracle 与 execution/product parity | [GENERAL] | 完成，八 gate 全过 | worker agent（ZCode subagent；其中一次核验因超时后由续派 agent 完成，结论一致） |
| G02 — documentation closure、full validation、本报告 | [GENERAL] | 完成（本报告） | worker agent（ZCode subagent） |

A01/A02 施工后由 reviewer agent（ZCode subagent）完成只读预审，结论分别为
`A01 CHECKPOINT: PASS`、`A02 CHECKPOINT: PASS`（冻结事实）。本报告不代替
batch 级 review 与 final review。

## C. A01 implementation

（来源：Worklist 冻结的 A01 checkpoint 事实与 G02 对当前代码/调用的抽查。）

- 新增 5 个文件、修改 7 个文件（冻结事实；完整清单见 R 节）：
  - `include/rayreuse/model/n2_linear_ssp.hpp`、`src/model/n2_linear_ssp.cpp`：
    real geometry backend，逐段线性 N²（段内保存浅节点 N² 与常数 N² 深度梯度），
    `c = 1/sqrt(N²)`；segment locator 与 C-linear hinted 行为一致（hinted 段两端
    属于该段，profile 外用首/末段外推）。
  - `include/rayreuse/model/n2_linear_frequency_ssp.hpp`、
    `src/model/n2_linear_frequency_ssp.cpp`：frequency-local complex N² evaluator，
    节点声速先按目标频率转复数，再形成复 N² 系数，声速经 principal complex
    square root 恢复；gradient/curvature 保持 F2CPP 的 real-part observable，
    不是复解析导数。
  - `tests/component/n2_linear_ssp_test.cpp` + `CMakeLists.txt` 注册
    `rayreuse.component.n2_linear_ssp`。
- dispatch：`SspInterpolationKind` 增加 `N2Linear`；
  `GeometrySspEvaluator`/`FrequencySspEvaluator` 的 variant backend 均加入
  N²；parser 将 `'N'` 解析为 `N2Linear`。
- anchors（冻结事实）：
  - real：`c(50 m) = 1547.5821125259863`、left-node gradient
    `1.10222222222222155`、right-node gradient `-2.44897959183673342`、
    second-midpoint curvature `0.00787663434191761790`；
  - complex 50 Hz：real sound speed `1489.91621090979174`、imaginary sound speed
    `6.87988934309839983`、gradient `-0.0397683421458134914`、curvature
    `3.18444961960798548e-6`。
- 拒绝路径：`'SVW'`、`'QVW'` 与未知 kind 继续明确失败；NVW 解析为 N2Linear，
  无 C fallback；non-finite query、invalid segment、non-positive N² 明确失败
  （不 clamp/abs/fallback）。

## D. A01 reviewer checkpoint

- reviewer agent（ZCode subagent）只读预审结论：`A01 CHECKPOINT: PASS`
  （冻结事实）。
- reviewer 同时标注：`.pi/settings.json` 存在施工前已发生的模型路由修改，建议
  用户知悉（详见 S 节；该标注在 A02 预审中再次出现）。

## E. A02 implementation

（来源：Worklist 冻结的 A02 checkpoint 事实。）

- 纯机械重命名：`applyCLinearGradientJump` → `applyGradientJump`，公式不变，
  使 C-linear 与 N²-linear 共用同一 node gradient jump。
- 9 个 read-only consumer 零修改；frozen `RayPathCache` 未增加任何字段。
- 关键数值证据（冻结事实）：
  - N² 非零 Hessian 使 dynamic 变量偏离 C-linear 路径：`p` 偏离 9.38e-6、
    `q` 偏离 34.2（若错误地把 Hessian 置零，该差异将消失，测试可捕获）；
  - jump 恒等式残差：0 / 2.6e-23；
  - N² frequency projector 两频独立，逐字段复用 path 状态不变；
  - 全量 CTest 35/35（A01 后为 35 个测试）。

## F. A02 reviewer checkpoint

- reviewer agent（ZCode subagent）只读预审结论：`A02 CHECKPOINT: PASS`
  （冻结事实）。
- reviewer 再次标注 `.pi/settings.json` 的施工前模型路由修改（同 D 节）。

## G. G01 work

（来源：Worklist 冻结的 G01 gate 事实；G01 临时产物在 `/tmp/fp2c_g01/`，
未进入版本控制。G02 抽查确认关键 CSV 与 manifest 仍在该目录且 hash 一致。）

- 三方 single（`munk_n2`，Origin/F2CPP/RayReuse）：PASSED。
- 三模式 broadband SHD 逐字节一致：
  `18817c6788b6e7a4c0c7cbd73cb5b8de78c4c92ea90e06a059badf80c27d29c4`；
  PRT Trace passes：nonreuse=2、reuse=1、parallel=1。
- F2CPP/RayReuse `munk_n2` geometry probe CSV byte-identical：
  `360dda437550e396b531ed9a4692a006ebe8e5e29ddcaddde40fe8ddbcc00be8`
  （233 points / 232 steps / 0 events @0.0125 rad）。
- Origin intermediate-state matrix：366 points / 363 steps / 2 events；
  worst error q2@158：abs `1.81e-14`、scaled `1.21e-3`（既有预算 `3e-9`，
  未放宽 tolerance）。
- 产品 TL/R/A/a/E F2CPP=RayReuse 逐字节一致（两频；run type 覆盖 `CC` TL、
  `RG`、`AG/aG`、`EG`，G02 在 `/tmp/fp2c_g01/products` 复核 env 记录）：
  - TL SHD `1dcec8713169f7c7862b1649c536b56ea14b5940f348625c52f14a213b583ba8`
  - R `81dd81ab9ebf7565ee48336f93c9ed37b6c5cae1a372a8daecdd512467844815`
  - A `1322fda04a950b14b9fabf93fd9af7f08d3936f198a26a6cd473688eee202406`
  - a `41ddac86242c04683ac5ecf5af3c6912da73e15dd3164ec2f0835e5d3f854b51`
  - E `78b3ba5e2b4805a457591860bc89ad631c8531783aa6be85390bb5fb380a77b6`
  - ARR 统计：552,440 arrivals / 100,701 cells / 0 nonfinite。
- N 区别于 C/P（防止 accidental fallback 证据）：三组 SHA 互异，
  point_index=2 起分歧（N vs C p1 `+1.34e-6`；N vs P p1 `-2.22e-4`）。
- zero-regression：C/P probe SHA、CTest 35/35、standard-case test-unit 153/153、
  C/P broadband SHD SHA 均与施工前一致（冻结事实）。

## H. G02 work

（来源：G02 施工亲手运行，命令与结果见 Q/U 节。）

- 文档收口（3 个文件，只更新真实受影响的声明，Worklist R07）：
  - `REFERENCE_FEATURE_SUPPORT_MATRIX.md`：4 处 — 头部日期；
    TL 行 support wording `C-linear 或 PCHIP` → `C-linear、PCHIP 或 N²-linear`；
    SSP 行改写为三 kind 支持并记录 N² real geometry / nonzero Hessian /
    discontinuous gradient / frequency-local complex N² / frozen cache ownership /
    TL/R/A/a/E 范围 / three-mode parity；Deferred 列表移除 N²-linear（`S`、
    `Q/.ssp` 原文保留）。
  - `REPORT_F2CPP_RAYREUSE_PARITY_2026-08-25.md`：新增 `FP2C-ORACLE` 证据行与
    FP-2C 基线行；同步受影响的现行声明（scope 段、结论摘要、evidence 标签
    R-PARSER/R-MODEL/R-GEOM/TEST/DOC-01、SSP-03 行、Section 5 execution 表、
    Section 6 追加 FP-2C 验证记录、Section 7 GAP 列表移除 SSP-03、TL/PRD 行的
    排除注记 `N/S/Q` → `S/Q`）；历史批次行（如 `FP2B-ORACLE`）按惯例保持
    快照原文。
  - `STATUS_PROGRESS.md`：头部日期/状态 + FP-2C 完成条目（只关闭 N² slice，
    状态注明"待最终验收"）。
- Full validation 与 clean build：见 Q 节；clean 与 release 可执行文件逐字节
  相同，clean build probe 复现 C/P/N 三个冻结 hash。
- 本报告（A–W）。

## I. Numerical semantics

（来源：A01/A02/G01 冻结事实 + 代码抽查。）

- real geometry：逐段线性 N²，`c = 1/sqrt(N²)`；节点梯度不连续，与 C-linear
  共用同一 reduced-step node jump；段内非零二阶导数（N² Hessian）进入 dynamic
  ray（A02 证据：置零 Hessian 将使 p/q 偏离 9.38e-6/34.2 消失）。
- complex 路径：逐频 evaluator 先把节点声速按目标频率转复数，再形成复 N²；
  gradient/curvature 保持 F2CPP real-part observable，刻意不是复解析导数
  （Worklist R02：不得"改进"为复导数后取实部）。
- 拒绝语义：`S`、`Q/.ssp`、未知 kind、non-finite、non-positive N² 全部明确
  失败，无 silent fallback。
- Origin oracle 的 q2 near-caustic 容差为既有记录在案的 tolerance，未因 FP-2C
  放宽（R04）。

## J. Architecture deviations / blockers

- 无 `ARCHITECTURE_BLOCKER`。未引入 range-segment API、`.ssp` sidecar、mutable
  global frequency、product-specific N solver、frozen cache 频率字段、spline
  utility 或 RayReuse attenuation 范围之外的模型。
- 唯一生产代码重命名：`applyCLinearGradientJump` → `applyGradientJump`
  （A02，纯机械，9 个 read-only consumer 零修改）。
- 未修改 `Bellhop_F2CPP`、`Bellhop_origin` production code（见 U 节 diff 为空）。
- 未修改 build/test framework，除 `CMakeLists.txt` 注册新增
  `rayreuse.component.n2_linear_ssp` 测试目标（真实 registration 需求）。

## K. C/P zero regression

（G02 亲手复验，clean build `bellhop_rayreuse_geometry_oracle_probe`，0.0125 rad。）

| 项 | 施工前（冻结） | G02 clean build 复验 | 一致 |
|---|---|---|---|
| C probe CSV（munk） | `809b126d…98492e2` | `809b126d4b2657b8c54100e9f0e867c69bd26633963a58a883b2e985b98492e2` | 是 |
| P probe CSV（munk-pchip） | `eb51ced1…1c646a0` | `eb51ced19656a7724594e0dc7e0c2c5977daa8447962d76b9ca8112c55c646a0` | 是 |
| N probe CSV（munk-n2） | G01 `360dda43…c00be8` | `360dda437550e396b531ed9a4692a006ebe8e5e29ddcaddde40fe8ddbcc00be8` | 是 |

- C/P broadband SHD 基线（`cf1f9711…fd126bc` / `fd5b2e2c…80d85cde`）：G01 已
  复验不变（冻结事实）；G02 的 full validation（CTest/pytest/test-unit）未出现
  任何 C/P 相关失败。

## L. F2CPP oracle

- `munk_n2` geometry probe：F2CPP 与 RayReuse CSV byte-identical（G01 冻结：
  `360dda437550e396b531ed9a4692a006ebe8e5e29ddcaddde40fe8ddbcc00be8`）。
- 两频 TL SHD 与 R/A/a/E 产品：F2CPP=RayReuse 逐字节一致（G01 冻结，5 个
  SHA 见 G 节）。
- G02 clean build 复验：clean 与 release 可执行文件 SHA-256 相同（见 Q 节），
  probe 输出确定性地复现相同 hash。

## M. Origin oracle

- 三方 single（`munk_n2`）：PASSED（G01 冻结）。
- Origin intermediate-state matrix：366 points / 363 steps / 2 events；worst
  error q2@158 abs `1.81e-14` / scaled `1.21e-3`，既有预算 `3e-9`，未修改
  tolerance（G01 冻结）。
- Origin 参照保持只读：`git diff -- Bellhop_F2CPP Bellhop_origin` 为空（G02
  亲手运行，见 U 节）。

## N. TL/R/A/a/E parity

- F2CPP=RayReuse 逐字节一致（两频、`CC`/`RG`/`AG`/`aG`/`EG`；G01 冻结 5 SHA
  见 G 节；ARR 552,440 arrivals/100,701 cells/0 nonfinite）。
- 范围声明：N² 的产品证据覆盖上述 Cartesian Cerveny TL 与 Cartesian
  geometric-hat 产品 slice；不得外推为全部 beam/coordinate family 或整个 SSP
  family parity。
- N≠C/P：三组 SHA 互异，point_index=2 起 p1 分歧 `+1.34e-6`（N vs C）、
  `-2.22e-4`（N vs P），排除 accidental C/P fallback（G01 冻结）。

## O. nonreuse / reuse / parallel

- `munk_n2` 两频三模式 SHD 逐字节一致：
  `18817c6788b6e7a4c0c7cbd73cb5b8de78c4c92ea90e06a059badf80c27d29c4`
  （G01 冻结）。
- PRT Trace passes：nonreuse=2、reuse=1、parallel=1（即 reuse/parallel 各只
  trace once；G01 冻结）。
- 逐频产品独立发布、R 多频拒绝等既有生命周期语义未改动（由 full CTest 与
  standard-case 回归保护）。

## P. Cache fingerprint 证据等级

证据分层如下；第 4 层为 Batch Acceptance 期间补录的显式数值（关闭 batch review
的 LOW finding）：

1. 代码审计（A02，冻结事实）：frozen `RayPathCache` 无频率字段，A02 未向
   cache 增加任何字段；幅相、复走时、active prefix、反射结果留在逐频临时
   状态。
2. 既有 reuse/parallel component tests 断言 trace cache fingerprint 前后不变
   （包含在 G02 亲测的 clean/release CTest 35/35 内）。
3. 三模式 SHD byte-identity + PRT Trace passes（2/1/1，G01 冻结）：与
   nonreuse 逐字节一致意味着 reuse/parallel 复用的冻结几何与逐频投影没有
   引入可观察差异。
4. **显式 fingerprint（Batch Acceptance，coordinator 亲手运行）**：
   `munk_n2` 两频（`--frequencies-hz 50,250 --verify-cache`，隔离目录
   `/tmp/fp2c_accept/<mode>/`）：
   - reuse：`cache fingerprint before = 7534688792029655613`、
     `cache fingerprint after = 7534688792029655613`；
   - parallel：`cache fingerprint before = 7534688792029655613`、
     `cache fingerprint after = 7534688792029655613`；
   - reuse 与 parallel fingerprint 相同（同一单次 trace 的冻结几何），投影
     前后不变；nonreuse 模式无 cache verification 输出（逐频独立 trace，符合
     设计）。

四层证据共同支持"frozen cache 所有权未破坏"。

## Q. Tests（G02 亲手运行，2026-08-28）

| 命令 | 结果 | exit code |
|---|---|---|
| `uv run cmake -S Bellhop_RayReuse -B Bellhop_RayReuse/build/fp2c-clean -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON` | configure 成功 | 0 |
| `uv run cmake --build Bellhop_RayReuse/build/fp2c-clean --parallel 8` | build 成功（100%） | 0 |
| `uv run ctest --test-dir Bellhop_RayReuse/build/fp2c-clean --output-on-failure` | 35/35 passed（component 25、parallel 1、reuse 2、unit 10；总计 17.76 s） | 0 |
| `uv run ctest --test-dir Bellhop_RayReuse/build/release --output-on-failure` | 35/35 passed | 0 |
| `uv run pytest` | 168 passed（1.76 s） | 0 |
| `uv run make -C test/standard_cases test-unit` | Ran 153 tests, OK（0.925 s） | 0 |
| `git diff --check` | 无输出 | 0 |
| `git diff -- Bellhop_F2CPP Bellhop_origin` | 空 | 0 |

可执行文件 SHA-256（G02 亲手计算）：

| 文件 | clean build（fp2c-clean） | release build | 一致 |
|---|---|---|---|
| `bellhop_rayreuse` | `172891b562ca00986222904936d573047af7aff4de1b9d66bb7f7168febc5ea0` | `172891b562ca00986222904936d573047af7aff4de1b9d66bb7f7168febc5ea0` | 是 |
| `bellhop_rayreuse_geometry_oracle_probe` | `f4152e9e90c557d59c55899703216290e3cfbd63501194c9014c7b6137bd41ac` | `f4152e9e90c557d59c55899703216290e3cfbd63501194c9014c7b6137bd41ac` | 是 |

- 施工前 `bellhop_rayreuse` 为 `dafa55ac…ec68df`（A 节），施工后为
  `172891b5…ebc5ea0`：与 FP-2C 引入 N² backend 的预期一致。
- `pytest` 无失败；施工前未运行过全量 pytest（约束中已声明），但 168/168 全过，
  无需归因分析。

### Batch Acceptance（coordinator 亲手运行，2026-08-28，当前实际执行）

在 batch review PASS 后执行，未沿用任何旧报告结果：

| Gate | 结果 |
|---|---|
| clean Release build 重建（`build/fp2c-clean`，`cmake --build`） | 成功（100%） |
| `ctest --test-dir build/fp2c-clean --output-on-failure` | 35/35 passed |
| `ctest --test-dir build/release --output-on-failure` | 35/35 passed |
| `uv run pytest` | 168 passed |
| `uv run make -C test/standard_cases test-unit` | Ran 153 tests, OK |
| `make test VERSION=origin CASE=munk_n2 PROFILE=single` | PASSED |
| `make test VERSION=f2cpp CASE=munk_n2 PROFILE=single` | PASSED |
| `make test VERSION=rayreuse CASE=munk_n2 PROFILE=single` | PASSED |
| F2CPP/RayReuse `munk-n2` probe（0.0125 rad，重跑） | `cmp` byte-identical，SHA `360dda437550e396b531ed9a4692a006ebe8e5e29ddcaddde40fe8ddbcc00be8` |
| `intermediate_state_matrix.py --case munk_n2`（三可执行文件显式传入） | `INTERMEDIATE GEOMETRY MATRIX PASSED: 1 case(s)`；`passed: true`、`cpp_probe_byte_identical: true`；worst q2@158 abs `1.8144340196979414e-14` / scaled `1.2056301548043651e-3`；366 points / 363 steps / 2 events |
| 三模式 broadband（`--frequencies-hz 50,250` + `--verify-cache`，隔离目录） | 三模式 SHD 均为 `18817c6788b6e7a4c0c7cbd73cb5b8de78c4c92ea90e06a059badf80c27d29c4`（逐字节一致）；Trace passes 2/1/1；fingerprint 见 P 节 |
| TL/R/A/a/E 产品 smoke（两版可执行文件重跑并比对） | 5 个产品 f2cpp = rayreuse = 冻结 SHA（`1dcec871…`/`81dd81ab…`/`1322fda0…`/`41ddac86…`/`78b3ba5e…`），全部 EXIT=0 |
| C-linear zero regression | probe `809b126d4b2657b8c54100e9f0e867c69bd26633963a58a883b2e985b98492e2`；`munk_cerveny_cc` broadband_smoke PASSED，SHD `cf1f9711aefcab087bd766c395a03b935c1c9cf13980335a368035515fd126bc` |
| PCHIP zero regression | probe `eb51ced19656a7724594e0dc7e0c2c5977daa8447962d76b9ca8112c55c646a0`；`munk_pchip` broadband_smoke PASSED，SHD `fd5b2e2cf77a524ec4972e8563c19efe0e33de48c87677a193c2d20c80d85cde` |
| `git diff --check` | 干净 |
| `git diff -- Bellhop_F2CPP Bellhop_origin` | 空（0 行） |
| `git ls-files --others --exclude-standard` | 仅 R 节 7 个预期未跟踪条目，无生成产品 |

产品 smoke 重跑细节：env 输入取自 G01 冻结的 `/tmp/fp2c_g01/products_rerun/`
（`'NVW'` 50 Hz Munk 27 节点、1000 rays、201×501 receivers；run type 分别为
`CC`/`RG`/`AG RR`/`aG RR`/`EG RR`），每产品对两版可执行文件各自重跑
（全部 EXIT=0）后按 SHA-256 三方比对（f2cpp、rayreuse、冻结值）。

## R. Changed files（`git status --short` 实际输出，G02 完成时）

```text
 M .pi/settings.json
 M Bellhop_RayReuse/CMakeLists.txt
 M Bellhop_RayReuse/doc/reference/REFERENCE_FEATURE_SUPPORT_MATRIX.md
 M Bellhop_RayReuse/doc/reports/REPORT_F2CPP_RAYREUSE_PARITY_2026-08-25.md
 M Bellhop_RayReuse/doc/status/STATUS_PROGRESS.md
 M Bellhop_RayReuse/include/rayreuse/model/sound_speed_evaluator.hpp
 M Bellhop_RayReuse/include/rayreuse/model/sound_speed_types.hpp
 M Bellhop_RayReuse/include/rayreuse/ray/geometry_tracer.hpp
 M Bellhop_RayReuse/src/io/environment_parser.cpp
 M Bellhop_RayReuse/src/model/sound_speed_evaluator.cpp
 M Bellhop_RayReuse/src/ray/ray_stepper.cpp
 M Bellhop_RayReuse/tests/component/environment_parser_test.cpp
 M Bellhop_RayReuse/tests/component/frequency_projector_test.cpp
 M Bellhop_RayReuse/tests/component/geometry_tracer_ssp_interface_test.cpp
 M Bellhop_RayReuse/tests/component/ray_stepper_test.cpp
 M Bellhop_RayReuse/tests/component/sound_speed_evaluator_test.cpp
 M Bellhop_RayReuse/tests/tools/geometry_oracle_probe.cpp
 M test/standard_cases/cases/munk_n2/case.toml
 M test/standard_cases/codes/intermediate_state_matrix.py
 M test/standard_cases/codes/tests/test_case_model.py
?? Bellhop_RayReuse/doc/worklists/FP-2C_N2_LINEAR_SSP_WORKLIST.md
?? Bellhop_RayReuse/doc/workreports/FP-2C_N2_LINEAR_SSP_BATCH_REPORT.md
?? Bellhop_RayReuse/include/rayreuse/model/n2_linear_frequency_ssp.hpp
?? Bellhop_RayReuse/include/rayreuse/model/n2_linear_ssp.hpp
?? Bellhop_RayReuse/src/model/n2_linear_frequency_ssp.cpp
?? Bellhop_RayReuse/src/model/n2_linear_ssp.cpp
?? Bellhop_RayReuse/tests/component/n2_linear_ssp_test.cpp
```

批次归属（按冻结事实的批次级记录；工作树为 A01–G02 的单批次未提交聚合）：

- A01：新增 5 个（`n2_linear_ssp.hpp/.cpp`、`n2_linear_frequency_ssp.hpp/.cpp`、
  `n2_linear_ssp_test.cpp`）+ 修改 7 个（backend 接线、parser、evaluator、
  CMakeLists 及相关测试）。
- A02：`geometry_tracer.hpp`/`ray_stepper.cpp` 的重命名与 N² 接入 +
  `frequency_projector_test.cpp`、`ray_stepper_test.cpp`、
  `geometry_tracer_ssp_interface_test.cpp` 等测试扩展（冻结事实未提供逐文件
  归属，此处不虚构）。
- G01：`test/standard_cases` 3 个文件（`munk_n2/case.toml`、
  `intermediate_state_matrix.py`、`test_case_model.py`）。
- G02：3 个文档 + 本报告。
- `.pi/settings.json`：非本批次改动（见 S 节）。

未跟踪的生成产品（`.prt/.shd/.ray/.arr`、CSV、manifests）：仓库内为零；
G01/G02 数值产物均在 `/tmp/fp2c_g01/`、`/tmp/fp2c_g02/`，不参与版本控制。
无 spline 或 Q/.ssp production addition。

## S. Unrelated working-tree state

- `.pi/settings.json`（modified）：模型路由相关修改为**施工前已存在的用户
  环境变更**。FP-2C 各施工 agent（A01/A02/G01/G02）与 reviewer 均未触碰该
  文件；A01、A02 两次 reviewer 预审均标注该差异并建议用户知悉。本报告仅
  记录，不处理。
- 未跟踪的 Worklist 文档
  （`Bellhop_RayReuse/doc/worklists/FP-2C_N2_LINEAR_SSP_WORKLIST.md`）：批次
  合同本身，按编排流程保持未暂存。
- `Bellhop_RayReuse/build/fp2c-clean/`：G02 新建的隔离 build 目录，属可再生成
  构建产物（`build/` 不入版本控制）。

## T. Remaining GAPs

（与更新后的 parity report Section 7 一致；`S`、`Q/.ssp` 保持 Deferred/GAP。）

- `SSP-04` spline `S`：deferred/unsupported，parser 明确拒绝。
- `SSP-05` Q + `.ssp`：deferred/unsupported（architectural conflict）。
- 其余与 FP-2C 无关的 GAP 不变：SRC-02/PRD-08（line source）、SRC-04/PRD-07
  （multisource）、REC-02/PRD-06、REC-03/PRD-06（irregular）、BND-04
  （curvilinear）、BND-09、ATT-01、ATT-04、ATT-05。
- 不得从 FP-2C 外推：range-dependent SSP、line/multisource、irregular
  receivers、canonical curvilinear boundary、3D、Influence Geometry Reuse、
  frequency interpolation、整个 F2CPP SSP family parity。

## U. git diff / status

- `git rev-parse HEAD` = `3130e60509c87334cdf4f6fd2ee0360b04db7fe`（G02 亲测；
  批次未提交，与 baseline 相同）。
- `git diff --check`：空（无 whitespace 错误）。
- `git diff -- Bellhop_F2CPP Bellhop_origin`：空（两参考实现零修改）。
- `git ls-files --others --exclude-standard`：仅 R 节列出的 7 个未跟踪条目
  （Worklist、本报告、5 个 N² 源/测试文件）。
- `git diff --stat`（全部 tracked 修改）：20 files changed, 810 insertions(+),
  95 deletions(-)（含 `.pi/settings.json` 的既有差异与 G02 的 3 个文档）。
- 未执行任何 stage/commit/push。

## V. Reviewer batch result

`BATCH REVIEW: PASS`（reviewer agent，ZCode subagent，只读，2026-08-28）

- 逐项结论（9 项全 PASS）：scope violation、numerical semantic mismatch（含
  A01 两 evaluator 与 F2CPP 逐行比对、A02 重命名纯机械性复核）、stale
  C/P-only docs、silent N→C fallback、missing runtime/product path、unrelated
  diff、generated-file cleanup、Batch Report 与真实 git 状态一致性、测试真实
  性抽查（reviewer 亲手复跑：RayReuse CTest 35/35、F2CPP targeted 3/3、
  test-unit 153 OK、pytest 168 passed、`rayreuse munk_n2 single` 端到端
  PASSED、anchors 与 F2CPP 冻结测试逐字相同）。
- 唯一 finding（LOW）：`munk_n2` 缺少显式 cache fingerprint 数值。已在
  Batch Acceptance 中通过 `--verify-cache` 运行关闭（见 P 节第 4 层：
  reuse/parallel 前后均为 `7534688792029655613`）。
- reviewer 无权声明 FP-2C ACCEPTED；本报告同样不声明。

## W. Ready for Final Review

`YES`

全部 gate 状态：A01 checkpoint PASS；A02 checkpoint PASS；G01 八 gate 全过；
G02 full validation 全过（Q 节）；batch review PASS（V 节，LOW finding 已
关闭）；Batch Acceptance 全部通过（Q 节 Batch Acceptance 表）。文档收口完成
且未把支持声明外推到 `S`/`Q` 或整个 SSP family。完整未提交 diff 保留在工作
树，交由后续外部高级模型（Codex/GPT）做 Final Review；本批次未 stage、未
commit、未 push，不自行声明 FP-2C ACCEPTED。
