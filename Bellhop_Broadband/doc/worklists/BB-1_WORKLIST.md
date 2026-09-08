# BB-1 — Broadband Naming & Execution Model Implementation

Status: **BB-1 ACCEPTED**（final-reviewer，2026-09-06）
Date prepared: 2026-09-06
Authority: [BB-M1 design](BB-M1_WORKLIST.md)。准备轮（2026-09-06，历史快照）仅生成本 Worklist、未启动施工；同日施工授权后连续执行 A01–A06 并 ACCEPTED。
Audit baseline: `a3f20d8c0d8ac207766fa0adb083de3526f74fd7`；construction HEAD = 同 SHA（无漂移）。

## Scope / freeze

- 迁移产品身份、四个核心 CLI 参数、原路线命名与 dispatch、直接受影响的构建/脚本/测试调用。
- 保留 `Bellhop_RayReuse/`、namespace/include root、内部 fused 实现、科学计算顺序、support gate 和各路线生命周期。
- 不做新算法/beam 支持、auto routing、性能优化、全局替换或 Origin/F2CPP 修改。
- 历史文档不重写；BB-1 只更新运行/构建必需引用，完整 living docs 审阅归 BB-2。

| 旧路线 | 新 CLI | TL solver 目标 |
|---|---|---|
| nonreuse | `--execution-mode nonreuse` | `NonReuseSolver`（单频仍 SingleFrequencySolver） |
| reuse | `--execution-mode reuse --reuse-mode serial` | `ReuseSerialSolver` |
| parallel | `--execution-mode reuse --reuse-mode frequency` | `ReuseFreqParaSolver` |
| fused | `--execution-mode reuse --reuse-mode range --reuse-workers 1` | `ReuseRangeParaSolver` |
| fused + range-parallel + workers N | `--execution-mode reuse --reuse-mode range --reuse-workers N` | 同一 Range solver |

- 新 frequency/range 默认 workers=1；旧 parallel 默认 hardware concurrency、旧 range-parallel 默认4，属于明确默认变化。显式 old/new 对比固定相同 worker 请求。
- nonreuse 不接受 reuse 参数；serial 不接受任何显式 reuse-workers（含1）；两类 worker 均为正整数。frequency/range workers=1 不得改走 serial。
- Trace requested/effective、frequency 资源限制、range clamp 沿用；无新增 all-source barrier。ARR 保持 source-local trace→accumulate→consumer。
- queue/memory/profile-frequency-tasks 只允许 Frequency TL；profile-influence 仅 Cartesian Cerveny TL；verify-cache 保留原能力。
- 四 solver 的结果/统计/settings 使用相同路线前缀；ARR/E 产品类保留并映射现有入口。TL predicate 与 ARR gate 不合并。

### A01 [ADVANCED]
Status: DONE
Reviewer: N/A（范围/证据核对在 A04 集中 review）

Goal:
- 施工开始记录实际 HEAD/Git scope，确认与 audit baseline 的差异；复用已完成 architect 核对，只在核心假设失效时重新设计。
- 冻结 rename 清单与下面 V1–V5 的具体 fixture/命令；保留旧 binary 或隔离 baseline build 供同机同输入对比。

Acceptance:
- 旧输出在覆盖前保存；baseline 使用可追溯 SHA，不能以施工后的 binary 冒充旧实现。
- 仅构建需要的 baseline executable，不在基线重复跑 full suite；忽略目录存 evidence，不提交生成产品。

Evidence:
- Construction HEAD = `a3f20d8`（与 audit baseline 完全一致，无漂移）；初始 scope 仅 Review 修订 + 3 份 Worklist；src/include/app/CMakeLists 相对 HEAD clean。核心假设未失效，直接复用 BB-M1 设计。
- Baseline binary：`build/release` 现有 Release 产物经增量验证（无任何构建输入新于 binary，源码 == HEAD），副本存 `build/bb1_baseline_bellhop_rayreuse`，SHA-256 `63f4a8d8…844e05c`，`--version` = `Bellhop RayReuse 0.1.0`。未重建 full suite。
- Evidence 目录 `build/bb1_evidence/`（git-ignored）：`A01_freeze.md`（冻结的 rename 清单 + fixture 表 + 8 组新旧 CLI 映射）、`collect_bb1_baseline.py`、`run_manifest.json`。
- V2 fixture `munk_cerveny_cc` broadband_smoke（50/250 Hz，单 source，201×501 规则网格，CC Cerveny）；V3 fixtures：ARR ascii/binary（2 source G hat）、Eigenray（2 source）、R（单频双 source）、单频 TL。
- 旧 CLI 20 组输出已保存（8 V2 + 12 V3），全部 rc=0；V2 八组 SHD byte-identical（SHA-256 `cf1f9711…26bc`），reuse 路线 frozen cache fingerprint 一致（`2271226459307825052`），Trace passes nonreuse=2 / reuse=1。
- 发现并处置：共享 ARR 模板的 range 行（0.05/0.10/0.20/0.40 km）非等距，旧 binary 正确以 `fused arrival accumulation requires equally spaced receiver ranges` 拒绝 Range ARR；V3 ARR fixture 因此在渲染时仅将 range 行等距化（0.05/0.15/0.25/0.35 km），偏离已记录于 A01_freeze.md。

### A02 [STANDARD]
Status: DONE
Reviewer: N/A（集中 A04 review）

Goal:
- executable→bellhop_broadband，CMake project→BellhopBroadband，package→bellhop-broadband，版本/help/PRT 身份同步。
- 迁移 install/CPack、CI、engineering/quality gate、standard-case runner、demo 和 benchmark 脚本的实际命令引用。
- 保留历史 report/worklist 的旧命令；runner 的模型标识/历史报告 schema 不因字符串相似机械更名，保持其含义可追溯。

Acceptance:
- 新程序可构建、安装、打包；活跃调用路径无失效 executable；内部 library target/macro 可保留。
- package/install smoke 共用一次构建产物；本 task 不跑 full regression。

Evidence:
- Worker 施工 + coordinator 复核（git status/diff --stat、Origin/F2CPP diff=0、`bellhop_broadband --version` → `Bellhop Broadband 0.1.0`）。
- 改动 24 文件：CMakeLists（project/exe/install/add_test×3/PASS_REGULAR_EXPRESSION/CPACK）、main.cpp 身份串（usage 程序名、printVersion、PRT 头 `BELLHOP BROADBAND`、完成行、5 处 stderr 前缀）、scripts 4 个（check_independence/single_thread_microbenchmark/engineering_gate/quality_gate）、CI workflow rename 为 bellhop-broadband-quality-gate.yml（含自引用路径与 step 名）、runner 15 个 py（adapter 默认路径、PRT 完成断言、validate_* 路径字面量）、demo 3 个（路径值；图标题/Makefile target 等算法族语义保留）、README 命令块 binary 路径、environment_parser_test 的二进制定位名。
- 验证：release preset 构建成功；ctest help/version/missing_root 3/3 通过；install --prefix → bin/bellhop_broadband；package → bellhop-broadband-0.1.0-Darwin-arm64.tar.gz（含 bin 与 share/doc/BellhopBroadband/README.md）；pytest test_benchmark_rayreuse 25 passed、test_standard_cases 20 passed。
- 保留项：namespace/include 根、RAYREUSE_VERSION 宏、bellhop_rayreuse_core 与全部 *_tests target/CTest 名、测试 banner、demo 图标题（RayReuse 算法族名仍准确）、历史文档。
- 清单外同口径修复 1 处：tests/test_standard_cases.py:51 断言 adapter 默认路径（不改必挂），已由 coordinator 接受。
- 已知预存在问题（非本 task 引入）：build/package 的 CMakeCache 指向旧卷路径，engineering_gate.sh 的 package preset 在本机失败；A05 跑 gate 前需清理该 build 子目录。

### A03 [ADVANCED]
Status: DONE
Reviewer: N/A（集中 A04 review）

Goal:
- 实现 execution/reuse 两层 options、trace/reuse worker 参数及显式标记，移除旧 CLI 参数/枚举值。
- 迁移路线 solver 与 main dispatch；ARR/E/R/单频按原产品入口处理，保留 trace settings 注入。
- 删除新 Serial/Frequency 路线上的旧弃用告警；同步资源/profiling 校验和路线诊断文字。

Acceptance:
- 所有合法 old route 映射到对应 new route；support boundary、source/cache lifetime、每频状态与发布/异常规则不变。
- V1/V2 覆盖默认、显式冲突与 workers=1 路线；不修改 projector、influence hot loop 或调度算法。

Evidence:
- Advanced-worker 施工两轮（实现 + remediation）。
- CLI：`ExecutionMode::{NonReuse,Reuse}` + `ReuseMode::{Serial,Frequency,Range}`；新 `--reuse-mode`/`--reuse-workers`（默认 serial/1）；删除 `--range-parallel`/`--workers` 解析（落入 unknown-option 拒绝）；后置校验：reuse 参数需 reuse、serial 拒绝任何显式 reuse-workers（含1）、queue/memory/profile-frequency-tasks 需 frequency 路线。
- Solver rename（git mv 文件 + 类型全局同步）：NonReuseSolver/ReuseSerialSolver/ReuseFreqParaSolver/ReuseRangeParaSolver 及 NonReuseResult/Statistics、ReuseSerialStatistics(+Result/FrequencyResult)、ReuseFreqParaSettings/Statistics、ReuseRangeParaExecutionSettings/Statistics、supportsFusedRayReuse→supportsReuseRangePara；测试文件 4 个 rename；保留低层 fused 术语（FusedPressure/FusedIntensity/FusedArrivalSourceConsumer 等）、ArrivalSolver/EigenraySolver/SingleFrequencySolver、fused_*_parity_test.cpp 文件名。
- main.cpp：两层 dispatch（ARR/E/TL；Eigenray else 防御 throw）；删除 warnIfReplaceableLegacyMode/resolvedWorkerCount/<thread>；frequency workers=options.reuseWorkerCount（默认1，hw-concurrency 默认按冻结移除）；range requestedRangeWorkers=options.reuseWorkerCount（min(requested,rangeCount) clamp 保留）；validateProductOptions 按新枚举重表达；PRT：多频 nonreuse 行改 "broadband nonreuse"、reuse 两行（"execution mode = broadband|single-frequency reuse" + "reuse mode = …"，remediation 恢复单频前缀）、"requested reuse worker count"/"effective reuse worker count"、删除 "range parallel =" 行；printUsage 重写无旧词。
- V1：command_line_test 表驱动覆盖默认/非法值/组合冲突/旧选项拒绝/正整数边界 → ctest `rayreuse.unit.command_line` PASS；`rayreuse.cli` 3/3。
- V2（coordinator 采集，`build/bb1_evidence/run_new_cli_and_compare.py` + `new/compare_manifest.json`）：A01 冻结的 8 组 TL 新旧对比全部 **产品 byte-identical**（SHD sha256 相同）且 **PRT 归一化后一致**（归一化仅身份串/路线行/worker 行名/弃用告警/时间）；requested/effective、Trace passes、cache fingerprint 数值不变。
- V3（同采集）：ARR ascii 4 路线、ARR binary range、Eigenray 3 路线、R×2、单频 TL×2 共 12 组全部产品 byte-identical + PRT 归一化一致；gate 拒绝复核：Range TL 多 source（"requires a single source"）、Range ARR 单频（"requires a multi-frequency arrival run"）、R+reuse、E+range、单频 TL+reuse、serial+reuse-workers(1)、旧值 parallel/fused、旧选项 --workers/--range-parallel 均正确拒绝；ARR 不等距 range 的 solver 层 gate 保持。
- remediation 2：runner 链迁移（standard_cases.py 路线枚举 nonreuse/reuse-serial/reuse-frequency/reuse-range + 参数表 + PRT marker 两行；model_matrix/benchmark_rayreuse 同步；test_benchmark/test_standard_cases 45 passed + 50 subtests；端到端 constant_speed_direct 三 reuse 路线 PASSED；benchmark 微运行跨路线 SHD 一致）。demo/rayreuse_multifrequency.py 由 coordinator 同口径修复（路线枚举/参数表/marker/默认 reuse-serial）。
- 构建验证：release preset（Werror=ON）成功；13 项 solver/parity/multi_source ctest 全 PASS（139.8s）。
- 保留已知项：benchmark 旗标名 --parallel-workers/--fused-range-workers 与报告字段名（doc/guide pin，语义为 reuse workers 轴，BB-2 记录）；GUIDE_BENCHMARKING/README 示例旧值归 BB-2。

### A04 [ADVANCED]
Status: DONE
Reviewer: PASS（独立 reviewer，2026-09-06）

Goal:
- 集中 checkpoint review A02/A03 的 dispatch、support、ownership、命名边界与测试范围。

Acceptance:
- 独立 reviewer PASS；检查无新增全局 barrier、无 TL/ARR gate 混用、无 low-level fused 全局替换、无因 N=1 换路线。
- findings 修复后原 reviewer re-check；不为每个机械改名 task 单独加一轮 review。

Evidence:
- VERDICT: PASS，零阻塞 finding。reviewer 独立核验：dispatch 新旧逐条等价（含 validateProductOptions、R 分支 parser 层补齐）；12 组 CLI 拒绝 + 5 组产品 gate 实测；四 solver 净 diff 完全对称（66/66 行纯 rename）；supportsReuseRangePara 仅 TL；310 处低层 Fused 术语保留；workers=1 不换路线（PRT 证据）；旧类名全仓零残留；V1 测试覆盖完整；evidence 对比方法无过度归一化（科学计数行严格比较）。
- 3 个 LOW 非阻塞 note 已处置/记录：evidence label range_w2→range_w1 修正并重跑（本轮 A05）；"fused/parallel reuse wall seconds" 行名与 3 处 synthetic fixture 名留 BB-2 术语审计。
- 施工过程 finding 闭环（A04 review 前）：单频 reuse PRT 前缀退化（broadband→single-frequency 恢复）与 runner 旧 CLI 构造均由原 advanced-worker remediation + coordinator 复核，pytest/端到端重验。

### A05 [ADVANCED]
Status: DONE
Reviewer: N/A（A06 final review 汇总验收）

Goal:
- 汇总最小 V1–V5，执行一次 Batch Acceptance；复用已通过的同版本证据。

Acceptance:
- V1–V5 PASS；输出差异可解释且符合 freeze；无性能提升要求。

Evidence:
- V1 CLI：`rayreuse.unit.command_line` PASS（表驱动：默认/非法 execution/reuse、nonreuse+reuse 参数、serial 显式 workers=1/>1、正整数边界、旧值 parallel/fused 与旧选项 --workers/--range-parallel 拒绝、queue/memory/profile-frequency-tasks 产品/路线限制）；`rayreuse.cli` help/version/missing_root 3/3（version 正则 Bellhop Broadband）。
- V2 路线与 trace：A01 冻结 8 组（四路线×trace=1/2，frequency/range 交叉 workers=1/2）新 CLI 对旧 CLI —— **SHD 全部 byte-identical**（`build/bb1_evidence/new/compare_manifest.json`，产品 sha256 严格相等）；PRT 归一化后一致，归一化仅限冻结项（2 身份串、路线行映射、worker 行名、range parallel 行删除、弃用告警、时间）；requested/effective、Trace passes（nonreuse=2/reuse=1）、cache fingerprint（2271226459307825052）数值不变。
- V3 产品/gate：ARR ascii 4 路线、ARR binary range（label 修正为 range_w1 后重跑）、Eigenray 3 路线、R×2、单频 TL×2 全部产品 byte-identical + PRT 归一化一致；gate 拒绝实测：Range TL 多 source、Range ARR 单频/不等距、Range E、R+reuse、单频 TL+reuse、serial+reuse-workers(1)。
- V4 集中回归/构建：release full CTest **50/50 passed**（实际计数，152s）；targeted pytest：test_benchmark_rayreuse + test_standard_cases + test_model_matrix = 55 passed + 50 subtests；demo 端到端 reuse-serial/reuse-frequency 两路线 run PASS（新 CLI + 新 PRT marker）；install → `/tmp/.../bin/bellhop_broadband`（`Bellhop Broadband 0.1.0`）；package（清理陈旧 build/package 后重新配置）→ `bellhop-broadband-0.1.0-Darwin-arm64.tar.gz`（bin/bellhop_broadband + share/doc/BellhopBroadband/README.md）。
- V5 文档/Git：`git diff --check` clean；Origin/F2CPP 零改动（diff 与 untracked 均空）；untracked 仅 3 份 Worklist；**发现并处置**：git mv 自动 stage 的 13 个 rename 已 `git restore --staged .`（保持授权要求"不 stage"，工作树不变，commit 时由 rename 检测恢复配对）。
- 无性能 claim；无数值容差放宽。

### A06 [ADVANCED]
Status: DONE
Reviewer: ACCEPTED（高级 final-reviewer，2026-09-06）

Goal:
- 高级推理 final-reviewer 独立最终验收；必要 remediation→re-validation→同一 final-reviewer。

Acceptance:
- 结论只能 ACCEPTED 或 CHANGES_REQUIRED；所有 finding 闭环后才标 `BB-1 ACCEPTED`。
- 若 BB-2 施工已获授权则自动继续；否则停止。提交只按届时授权处理，不 push。

Evidence:
- VERDICT: **ACCEPTED**。12 项验收问题全部 PASS（身份/两层模型/trace 服务两 execution/参数正交/旧概念退出无 alias/solver 命名/fused 术语保留/科学行为/无性能优化/protected refs）。
- final-reviewer 独立复核：8 个 rename 文件基线归一化对比（7 个 0 diff，1 个仅 3 处注释措辞）；原始 PRT 逐行 diff 确认差异恰好等于冻结允许清单；cache fingerprint 一致；CTest 50/50 与 pytest 55+50 独立重跑复现；binary sha256 与 manifest 一致；默认变化仅影响请求资源数（workers=1/2 输出均 byte-identical）。
- 非阻塞观察处置：本 Worklist Status 行与旧"尚未施工"描述随 verdict 更新（本条）；README 旧路线散文归 BB-2 B03（其冻结范围）。
- Git 状态：全部修改保持未提交、未 stage（13 个 git mv 自动 stage 已 restore --staged；commit 时由 rename 检测恢复配对）；pre-milestone SHA `a3f20d8`；BB-1 独立 commit 未创建（本次授权不提交），BB-M1 closure report（BB-2 B03）将明确记录未提交状态。

## 最小验证计划（同一证据只采集一次）

| Gate | 必要证据 | 去重边界 |
|---|---|---|
| V1 CLI | 复用 command_line 单元测试及现有 help/version/missing-root CTest；表驱动覆盖默认、非法 execution/reuse、nonreuse 的 reuse 参数、serial 显式 workers=1/>1、正整数边界、旧值/旧选项拒绝、资源参数产品限制。 | 在原测试中补最少断言，不新建 parser 框架，不枚举所有字符串/重复参数排列。 |
| V2 路线与 trace | 一个小型合法≥2频、单 source、规则网格 TL case，覆盖四路线×trace=1/2；frequency/range 各一次 reuse=1 和一次 reuse=2（可与两种 trace 交叉配对，共8组）。每组 new 对对应 old 显式请求，比较 SHD、cache fingerprint/trace count、requested/effective 与实际路线证据。 | 一组运行同时证明命名映射、trace wiring、cache 和产品等价；不扩成 workers=1/2/4/8 全积，不加 benchmark。 |
| V3 产品/gate | 复用现有 ARR/E/R tests；对实际改动的 CLI dispatch 做小型 ASCII/binary ARR（含 Range 多 source）、Eigenray、R 和单频 TL smoke；ARR/E 的合法各路线均有现有测试或映射证据。旧/新科学文件对应比较；保留 Range E/R 拒绝、单频 TL reuse 拒绝、Range TL 多 source 拒绝、Range ARR 单频/irregular/非等距或不足2 range 拒绝等原 gate。 | 已有组件测试覆盖的 beam/run-mode/support 边界只迁移调用，不复制为新全产品矩阵；仅对没有现成 CLI 证据的改动分支补 smoke。 |
| V4 集中回归/构建 | 一次 release full CTest；`uv run pytest` 只选实际改动的 runner/demo 测试文件；一次 install/package 新身份 smoke。使用 `uv run cmake/ctest` 与项目 presets，避免硬编码环境解释器。 | task 阶段只 targeted；不再默认叠加 debug+isolated full suite、全仓 pytest、全 Origin/F2CPP oracle 或历史性能/RSS gate。若 CI/既有 gate 已覆盖相同版本与构建，可直接引用证据。 |
| V5 文档/Git | 活跃命令/链接检查、`git diff --check`、status/scope、Origin/F2CPP diff 空、无生成产品。 | 只检查受影响文件及其调用入口；不要求清除历史旧名称。 |

比较规则：SHD/ASCII ARR/binary ARR/RAY 在相同显式参数下优先 full-file identity；若存在差异先调查，不放宽数值容差。PRT/stderr 只归一化预先列明的产品/路线/参数名称、输出根路径、旧弃用告警与时间；默认变化另测，不掩盖科学配置/计数变化。

触发额外验证的条件：新改动、失败、解释不清的产品差异或 reviewer 具体 finding。若科学核心假设失效，进入 remediation/重新 freeze，不以“命名改动”豁免必要 oracle。

## Blockers / findings

- 无未闭环 finding。BB-1 ACCEPTED（2026-09-06）；BB-2 已获授权，自动继续。
- 遗留（非阻塞，转 BB-2）：README/guides 旧路线散文与默认值描述；"fused/parallel reuse wall seconds" PRT 行名与 3 处 synthetic fixture 名的术语统一；benchmark 旗标名 --parallel-workers/--fused-range-workers 的后续 rename 需 bump SCHEMA_VERSION（仅当 BB-2/M2 决定）。

## 提交归档

- 用户在验收与 remediation 完成后授权本地 commit；实现、schema v3 与测试已归档为 `77e53c45e5a828c0d2f3b7dffb5dc84772cb3f46`。
- 本 Worklist 与其余验收文档归入独立 docs 提交；以上 task evidence 中的未提交状态为验收时历史记录。未 push/tag。
