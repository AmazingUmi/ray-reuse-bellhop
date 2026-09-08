# BB-2 B02 — Support Matrix Facts (from production code)
Date: 2026-09-06
Code state: working tree (BB-1 ACCEPTED, baseline a3f20d8 + BB-1 changes)

路径基准：仓库根。`app/main.cpp` = `Bellhop_RayReuse/app/main.cpp`，其余同理。
所有引用行号以当前工作树为准。只报代码事实，含错误文案原文；不做文档改写建议（B03 消费此表）。

Dispatch 事实（两层模型）：`--execution-mode <nonreuse|reuse>`（默认 nonreuse）+
`--reuse-mode <serial|frequency|range>`（reuse 下默认 serial）。
CLI 层校验 `src/io/command_line.cpp:123-166`；产品层校验 `validateProductOptions`
`app/main.cpp:255-364`；产品分支 `app/main.cpp:829-1508`。

---

## TL（多频）

| 路线 | 支持 | gate 证据 |
|---|---|---|
| NonReuse | 支持 | dispatch `app/main.cpp:1177-1248`（`NonReuseSolver::solve` `src/solver/nonreuse_solver.cpp:29-58`，逐频 `solveAtFrequency`，trace passes = Nfreq×NSz） |
| Serial | 支持 | `app/main.cpp:1249-1331`（`ReuseSerialSolver::solveStreaming` `src/solver/reuse_serial_solver.cpp:29-104`，trace once 后逐频投影） |
| Frequency | 支持 | `app/main.cpp:1411-1507`（`ReuseFreqParaSolver::solveStreaming` `src/solver/reuse_freq_para_solver.cpp:139-363`） |
| Range | 支持（限域） | `app/main.cpp:1332-1410`（`ReuseRangeParaSolver::solveStreaming` `src/solver/reuse_range_para_solver.cpp:579-771`） |
| 单频 TL | 只走 SingleFrequencySolver；显式 reuse 拒绝 | dispatch 顺序：RayTrace/ARR/E 分支之后 `app/main.cpp:1149-1176` 先判 `frequencies().size() == 1U` → `SingleFrequencySolver::solve`；单频 + `--execution-mode reuse` 在 validate 拒绝 `app/main.cpp:276-281`："--execution-mode reuse requires a multi-frequency TL run" |

TL 的 beam family × run mode 边界（全路线共有，模型层）：`src/model/simulation_case.cpp:404-410` —
SimpleGaussian 仅 coherent："simple Gaussian beams require coherent point-source TL on a rectilinear
receiver grid"（同时要求点源与规则网格）；`src/solver/single_frequency_solver.cpp:332-343`（单频/serial/frequency
共用的逐频入口）：family 限 Cerveny/GeoHat/GeoGaussian/SimpleGaussian，SimpleGaussian 非 coherent 再拒
"simple Gaussian TL requires coherent pressure"。非 Cerveny family 拒绝 Cerveny 专属 P/V/H、curvature、
width tail：`src/model/simulation_case.cpp:399-421`。

### Range 路线 TL 的 beam family 边界

CLI/产品层（`app/main.cpp:282-312`）：TL + reuse + range 时 family 必须是 Cerveny/GeoHat/GeoGaussian/SimpleGaussian，
否则 "--reuse-mode range requires Cerveny Gaussian, geometric hat, geometric Gaussian, or simple Gaussian TL"
（`app/main.cpp:300-302`）；单 source（`app/main.cpp:304-307` "--reuse-mode range requires a single source"）；
规则网格（`app/main.cpp:308-311` "--reuse-mode range requires a rectilinear receiver grid"）。

Solver 层 defense in depth（`fusedScopeFailure` `src/solver/reuse_range_para_solver.cpp:59-110`，错误文案
`validateFusedScope` 同文件 :114-145）：≥2 频 :88-90、规则网格 :91-93、≥2 range :94-97、等距 range
:98-108（文案见 :133-143）。

| family（坐标系） | 允许的 run mode | 证据 |
|---|---|---|
| Cerveny Gaussian（Cartesian） | coherent / incoherent / semi-coherent | family gate `src/solver/reuse_range_para_solver.cpp:71-76`；coherent 走 `accumulateFrequencies` :383-429（Cartesian 分支 :425-428），I/S 走 `accumulateFrequenciesIntensity` :431-479（Cartesian 分支 :475-478）；run-mode sink 选择 `solveStreaming` :620-632 |
| Cerveny Gaussian（ray-centered） | coherent / incoherent / semi-coherent | 同上；ray-centered 分支 :418-423（coherent）与 :468-473（intensity） |
| Geometric hat（Cartesian） | coherent / incoherent / semi-coherent | 分支 :397-401（coherent）与 :456-460（intensity） |
| Geometric hat（ray-centered） | coherent / incoherent / semi-coherent | 同一 `GeometricHatFusedAdapter`，kernel 内部选坐标系（注释 :64-70、:222-224） |
| Geometric Gaussian（Cartesian） | coherent / incoherent / semi-coherent | 分支 :403-407（coherent）与 :462-466（intensity）；ray-centered GeoGaussian 模型不存在（REFERENCE 列为 F2CPP_OUT_OF_SCOPE） |
| Simple Gaussian | 仅 coherent | 模型层拒绝非 coherent `src/model/simulation_case.cpp:404-410`；scope gate `src/solver/reuse_range_para_solver.cpp:81-84`；intensity 入口显式拒绝 :448-452："fused ray-reuse solver requires a run mode that is legal for the beam family"；coherent 分支 :409-416 |

Range TL 不额外限制：≥2 频（隐含——单频 TL 已在 main 层拒绝 reuse；solver 层 :88-90 再查）、
单 source、规则网格、≥2 等距 range（后者仅在 solver 层查，main 层不查 range 数）。
`--reuse-mode range` 的 requestedRangeWorkers=0 拒绝："fused ray-reuse requested range worker count
must be positive"（`src/solver/reuse_range_para_solver.cpp:239-242`）。

---

## ARR（A/a）

| 路线 | 支持 | gate 证据 |
|---|---|---|
| NonReuse | 支持 | `app/main.cpp:939-942` → `ArrivalSolver::solveNonReuse` `src/solver/arrival_solver.cpp:267-342`（逐频 re-trace） |
| Serial | 支持 | `app/main.cpp:943-946` → `ArrivalSolver::solve` `src/solver/arrival_solver.cpp:172-265`（trace once，逐频投影） |
| Frequency | 支持 | `app/main.cpp:947-950` → `ArrivalSolver::solveParallel` `src/solver/arrival_solver.cpp:344-434` |
| Range | 支持（限域，多 source） | `app/main.cpp:951-1005` → `ReuseRangeParaSolver::solveArrivalStreaming` `src/solver/reuse_range_para_solver.cpp:498-577` |

全路线共有 gate：ARR 仅 GeometricHat/GeometricGaussian family —"arrival solver supports only
geometric beam families"（`src/solver/arrival_solver.cpp:180-183`（serial）、:275-278（nonreuse）、:352-355（frequency））。
ray-centered GeoHat `g` 合法（`src/model/simulation_case.cpp:379-390` 允许 ray-centered 用于 GeoHat 的
TL/ARR/E）。

Range ARR 的产品层 gate（`app/main.cpp:330-349`）：
- ≥2 频：":332-335" — "--reuse-mode range requires a multi-frequency arrival run"；
- G/g/B：":336-343" — "--reuse-mode range arrivals require geometric hat or geometric Gaussian beams"；
- 规则等距网格：":344-348" — "--reuse-mode range arrivals require a rectilinear receiver grid"。

Solver 层 defense in depth（`validateFusedArrivalScope` `src/solver/reuse_range_para_solver.cpp:165-207`）：
ARR mode :167-171、family :172-177、sourceIndex 界 :178-180、≥2 频 :181-184、规则网格 :185-188、
≥2 range :189-193、等距 :194-206。

多 source：**Range ARR 无 source 数限制**（`validateFusedArrivalScope` 不查 sourceCount；产品层
`app/main.cpp:330-349` 也不查）。source streaming 证据（`src/solver/reuse_range_para_solver.cpp:516-568`）：
逐 source 循环 = `traceSourceFan(source i)`（:520-522）→ fingerprint before（:533-537）→
`accumulateArrivalFrequencies`（全频累积进 source-local `[range][depth][frequency]` raw workspace，
:539-541）→ `consumer(sourceIndex, rawWorkspace)`（:555-557）→ fingerprint after 校验
"fused arrival projection modified the frozen ray cache"（:559-567）→ 下一 source。注释 :518-519
"Deliberately source-local: neither frozen caches nor all-frequency arrival lanes accumulate across
sources."。无全局 all-source barrier；一次只持有一个 source 的 frozen cache 与 broadband workspace。
writer 侧：`app/main.cpp:966-995` 一次构造 `BroadbandArrivalWriterSet`，`fusedConsumer` 逐 source
`appendSource`，最后 `finalize` 协调整组发布（backup + 全组 commit，`src/io/arrival_writer.cpp:343-389`；
`FusedArrivalSourceConsumer` 契约注释 `include/rayreuse/solver/reuse_range_para_solver.hpp:98-103`：
每 source 一次、workspace 仅回调期内有效）。

编码：ASCII（`A`）与 binary（`a`）均可，四路线相同 — `app/main.cpp:889-894`（runMode →
`ArrivalEncoding::Ascii/Binary`），Range 分支同样传 encoding（`app/main.cpp:967-968`）。

单频 ARR + reuse（serial/frequency）合法（validate 仅 Range 要求 ≥2 频）；PRT 首行
"execution mode = single-frequency reuse"（`app/main.cpp:683-689`）。

---

## Eigenray

| 路线 | 支持 | gate 证据 |
|---|---|---|
| NonReuse | 支持 | `app/main.cpp:1101-1104` → `EigenraySolver::solveNonReuse` `src/solver/eigenray_solver.cpp:259-331` |
| Serial | 支持 | `app/main.cpp:1105-1108` → `EigenraySolver::solve` `src/solver/eigenray_solver.cpp:155-258` |
| Frequency | 支持 | `app/main.cpp:1109-1112` → `EigenraySolver::solveParallel` `src/solver/eigenray_solver.cpp:333-417` |
| Range | 拒绝 | 产品层 `app/main.cpp:358-362` — "--reuse-mode range is not defined for eigenray products"；dispatch 最后一臂防御性再抛同文案 `app/main.cpp:1113-1118` |

Eigenray family gate：仅 GeometricHat/GeometricGaussian — "eigenray solver supports only geometric
beam families"（`src/solver/eigenray_solver.cpp:163-166`（serial）、:265-268（nonreuse）、:343-346（frequency））。
ray-centered `g` 支持（`src/model/simulation_case.cpp:379-390` 含 Eigenray）。

## R

- 仅单频：多频 R 拒绝 "multi-frequency R products are not supported by the executable"
  （`app/main.cpp:261-264`）。
- 显式 reuse 拒绝："--execution-mode reuse is not defined for R products"（`app/main.cpp:265-268`）。
  R 只有 NonReuse 一条路线（dispatch `app/main.cpp:829-888`，`traceRayProducts`
  `src/solver/ray_trace_product.cpp:36-49`）。
- trace-workers 合法（含 N>1）：`traceSettings` 传入（`app/main.cpp:824-825`），worker 语义同下文
  trace 一节；PRT 仅在显式 `--trace-workers` 时打印 requested/effective/worker seconds
  （`app/main.cpp:878-888`）。
- 多 source fan 顺序：`SimulationCase::sources()` 按 depth 升序 `stable_sort`
  （`src/model/simulation_case.cpp:432-435`）；R 产品逐 source 一个 fan block，header `1 1 NSz`
  （`app/main.cpp:849-853` 注释与循环）。
- R 的 profiling/parallel-tuning 拒绝："profiling and parallel tuning options are only supported
  for TL"（`app/main.cpp:269-273`，覆盖 profileInfluence/profileFrequencyTasks/
  outputQueueCapacity/memoryBudget）。

---

## 参数域

| 参数 | 域 | 证据 |
|---|---|---|
| `--trace-workers` | 全产品（R/ARR/E/TL）全路线（nonreuse/serial/frequency/range）；正整数，默认 1；PRT 行仅显式指定时打印 | 解析 `src/io/command_line.cpp:192-204`（0 拒绝 "requires a positive integer"）；solver 端 0 拒绝 "trace worker count must be positive"（`src/solver/single_frequency_solver.cpp:159-161`）；传参 `app/main.cpp:824-825`；打印点：R `app/main.cpp:878-888`、ARR :1045-1053、E :1140-1148、单频 TL :1171-1176、多频 nonreuse :1232-1248、serial :1285-1315、range :1403-1410、frequency :1500-1507 |
| `--reuse-workers` | 仅 frequency / range（两产品层通用）；正整数，默认 1 | CLI：非 reuse 拒绝 "--reuse-workers requires --execution-mode reuse"（`src/io/command_line.cpp:269-272`）；serial 拒绝 "--reuse-workers is not accepted with --reuse-mode serial"（:273-277）；解析 :205-217。使用点：TL frequency `app/main.cpp:1433`、TL range :1358、ARR frequency :949、ARR range :989（E 无 range 路线） |
| `--output-queue-capacity` | 仅 reuse + frequency + 多频 TL；值域 1 或 2，默认 2 | CLI：非 frequency 拒绝（`src/io/command_line.cpp:278-286`，文案 "--output-queue-capacity, --memory-budget-mib, and --profile-frequency-tasks require --execution-mode reuse --reuse-mode frequency"）；>2 拒绝 "--output-queue-capacity must be 1 or 2"（:229-231）；R/ARR/E 再拒（`app/main.cpp:269-273`、:322-329、:351-357，`unsupportedParallelTuning` 定义 :258-259）；使用 `app/main.cpp:1434` |
| `--memory-budget-mib` | 仅 reuse + frequency + 多频 TL；正整数（显式 0 被拒绝）；未指定 = 0 = 无预算 | CLI 同上 :236-249 + `parsePositiveSize` :65-80（0 拒绝）；使用与换算 `app/main.cpp:662-669`（MiB→bytes）、:1435；预算语义见 worker 一节 |
| `--profile-frequency-tasks` | 仅 reuse + frequency + 多频 TL | CLI :183-191 + :278-286；R/ARR/E 拒绝（`app/main.cpp:269-273`、:324-328、:352-356）；打印 `app/main.cpp:1496-1499`（`writeFrequencyTaskTimings` :637-660） |
| `--profile-influence` | 仅 TL + Cartesian Cerveny（全路线：单频 nonreuse、多频 nonreuse、serial、frequency、range） | 产品层 `app/main.cpp:313-321` — 非 Cartesian Cerveny TL 拒绝 "--profile-influence is currently defined only for Cartesian Cerveny TL"；R/ARR/E 拒绝（:269-273、:324-328、:352-356）；打印点：单频 :1167-1169、多频 nonreuse :1228-1231、serial :1328-1331、range :1399-1402、frequency :1492-1495 |
| `--verify-cache` | CLI 对全产品接受；实际打印 before/after fingerprint 的域见下 | 解析 `src/io/command_line.cpp:167-174` |

### `--verify-cache` 实际适用域（打印 fingerprint 的路线）

| 产品/路线 | 打印 fingerprint | 证据 |
|---|---|---|
| R | 是（before/after + "cache fingerprint verification = enabled"） | `app/main.cpp:861-877` |
| ARR NonReuse | 是 | solver `src/solver/arrival_solver.cpp:289-300、331-337`；PRT `app/main.cpp:920-933、1032-1044` |
| ARR Serial | 是 | `src/solver/arrival_solver.cpp:197-203、255-263`；PRT 同上 |
| ARR Frequency | 是 | `src/solver/arrival_solver.cpp:360-368、426-432` |
| ARR Range | 是（per source before/after，逐 source 校验后统一报告） | `src/solver/reuse_range_para_solver.cpp:533-537、559-575` |
| E NonReuse/Serial/Frequency | 是 | `src/solver/eigenray_solver.cpp:180、244-251、277-287、313-318`；PRT `app/main.cpp:1084-1098、1127-1139` |
| TL 单频（nonreuse） | 否 — 选项被接受但无 fingerprint 输出/校验 | `SingleFrequencySolver::solve` 无 verifyCache 参数（`include/rayreuse/solver/single_frequency_solver.hpp:127`）；dispatch `app/main.cpp:1149-1176` 无相关打印 |
| TL 多频 NonReuse | 否 — 同上 | `NonReuseSolver::solve` 无 verifyCache 参数（`src/solver/nonreuse_solver.cpp:29-32`）；`app/main.cpp:1177-1248` 无打印 |
| TL Serial / Frequency / Range | 是 | serial `app/main.cpp:1316-1327`；frequency :1480-1491；range :1387-1398（solver 端 `src/solver/reuse_serial_solver.cpp:58-67、89-101`、`src/solver/reuse_freq_para_solver.cpp:211-220、347-359`、`src/solver/reuse_range_para_solver.cpp:605-612、758-768`） |

多 source 时额外逐 source fingerprint 对（`writePerSourceCacheFingerprints` `app/main.cpp:712-728`，
仅 sourceCount>1 打印）。

---

## worker 数语义

| 维度 | requested → effective | 证据 |
|---|---|---|
| trace（全产品） | effective = min(requested, launchCount)；requested=1 走 serial fast path（requested=effective=1） | `src/solver/single_frequency_solver.cpp:159-161`（0 拒）、:181-209（N=1 fast path）、:216（min）、:262-266（requested/effective 记录） |
| frequency（TL） | activeFrequencyLimit = min(frequencyCount, requested)，再受 memory budget 向下 clamp：budget>0 时从 unconstrained 递减至 estimatedPeakBytes ≤ budget 的最大值；连 1 都放不下则报错 "parallel ray-reuse memory budget cannot accommodate one active frequency" | `src/solver/reuse_freq_para_solver.cpp:104-123`（selectActiveFrequencyLimit）、:184-186、:223-225（线程数 = activeFrequencyLimit）；queueCapacity 有效值 = min(选项值, frequencyCount) :178-179；内存估计 :87-102 |
| frequency（ARR） | workers = min(requested, frequencyCount) | `src/solver/arrival_solver.cpp:369-381`（:378 min；:356-357 0 拒绝 "arrival worker count must be positive"） |
| frequency（E） | workers = min(requested, frequencyCount) | `src/solver/eigenray_solver.cpp:359-361`（0 拒绝 "eigenray worker count must be positive"） |
| range（TL 与 ARR） | activeWorkerCount = min(requested, rangeCount)；=1 时不开线程直接调用 | `src/solver/reuse_range_para_solver.cpp:262-265、332-344`；ARR 路线 main 侧同式打印 `app/main.cpp:981-984` |
| workers=1 不改路线 | 是：trace N=1 fast path、frequency/range N=1 单 worker 原地执行；CLI 默认值本身即为 1（trace/reuse），显式 1 与不指定行为一致 | `src/solver/single_frequency_solver.cpp:181-209`；`src/solver/reuse_range_para_solver.cpp:332-334`；`src/io/command_line.hpp:38-41`（默认 traceWorkerCount=1、reuseWorkerCount=1） |

PRT 标签：trace 为 "requested/effective trace worker count"；frequency TL 为
"requested reuse worker count" + "active frequency limit"（`app/main.cpp:1454-1458`）；
range TL/ARR 为 "requested/effective reuse worker count"（`app/main.cpp:1018-1021、1368-1371`）。

---

## 频率输入

- ENV：`frequency` record 至少一个值、正数、严格递增 —"frequencies must be strictly increasing"
  （`src/io/environment_parser.cpp:187-208`）；scalar（单值）即频率数 1。
- CLI `--frequencies-hz`：逗号分隔，正向有限数（"--frequencies-hz values must be positive finite
  numbers"，`src/io/command_line.cpp:36-40`）且严格递增（"--frequencies-hz values must be strictly
  increasing"，:52-55）；只能指定一次（:113-115）。override 语义：传给 parser 后整体替换 ENV 频率
  （`app/main.cpp:809-811` → `src/io/environment_parser.cpp:1323-1324`）。
- 模型层再校验：FrequencyGrid 非空、正数、严格递增 —"frequency grid must be strictly increasing"
  （`src/model/simulation_case.cpp:232-247`）。
- 产品命名：单频 `<root>.shd/.arr/.ray`，多频 `<root>_fNNN_<token>Hz.*`（12 位有效数字，`.`→`p`，
  `app/main.cpp:156-184`）。

---

## 与 REFERENCE_FEATURE_SUPPORT_MATRIX.md 的差异

（`Bellhop_RayReuse/doc/reference/REFERENCE_FEATURE_SUPPORT_MATRIX.md`，当前版本封板 2026-09-04）

- :15-43（"fused execution-mode 支持域"全节）— 文档以 `fused`/range-parallel 术语描述 — 代码 CLI 实为
  `--execution-mode reuse --reuse-mode range`；`--execution-mode fused` 已被拒绝（"--execution-mode must
  be 'nonreuse' or 'reuse'"，`src/io/command_line.cpp:137-138`）。支持域本身（family×mode、单 source、
  ≥2 频、规则等距网格）与代码一致。
- :51 — 文档说 fused PRT 报告 "execution mode = broadband fused reuse"、"range parallel"、
  "requested/effective range worker count" — 代码实际打印 "execution mode = broadband reuse" +
  "reuse mode = range"（`app/main.cpp:1366-1367`）与 "requested/effective reuse worker count"
  （`app/main.cpp:1368-1371`）。
- :96（末尾"多频 `nonreuse/reuse/parallel` SHD"）、:101（"`nonreuse/reuse/parallel` 三模式"）、
  :104（"`nonreuse/reuse/parallel` 下输出逐字节一致"）— 文档三模式命名 — 代码四路线
  nonreuse/serial/frequency/range（`--reuse-mode` 三值，`src/io/command_line.cpp:144-166`）；
  trace passes 语义 Nfreq×NSz / NSz / NSz（`app/main.cpp:703-709`）在代码中对应 nonreuse / 三条
  reuse 路线，文档的 "reuse/parallel" 两列未覆盖 range。
- :108 — 文档说 fused "默认请求 4 workers" — 代码 `--reuse-workers` 默认 1
  （`src/io/command_line.hpp:40-41`），无 4 默认。
- :108 — 文档说 "legacy `parallel` 下 A/a/E 与 TL 支持外层 frequency parallelism" — 代码该路线现名
  `--reuse-mode frequency`，支持面一致（TL `app/main.cpp:1411`、ARR :947、E :1109），仅命名滞后。
- :6-8 — 文档 commit 引用（`0721fb3`/`88ba8b7`/`e7f2705`/`dda1c2c`/`0050f59`）为历史封板信息，
  未含 BB-1 改名 commit（工作树未提交，baseline `a3f20d8`）；非支持域差异，仅记录。

### GUIDE_USAGE.md 滞后事实（对照材料，B03 消费）

（`Bellhop_RayReuse/doc/guides/GUIDE_USAGE.md`）

- :19、:41、:59 等 — 可执行名 `bellhop_rayreuse` — 代码 target/usage 为 `bellhop_broadband`
  （`Bellhop_RayReuse/CMakeLists.txt:143`、`app/main.cpp:39`）。
- :43、:50-54 — `--execution-mode <nonreuse|fused|reuse|parallel>` 四值 — 代码仅
  `nonreuse|reuse` + `--reuse-mode <serial|frequency|range>`（`src/io/command_line.cpp:123-166`）。
- :56-69 — `--range-parallel` + `--workers` — 代码两选项均已不存在（"unknown command-line option"，
  `src/io/command_line.cpp:250-252`）；现等价物为 `--reuse-mode range` + `--reuse-workers`。
- :66-67 — "未指定 `--workers` 时默认请求 4 workers" — 代码默认 1。
- :71-72 — "legacy `parallel` 模式中，`--workers` 未指定时使用硬件并发数" — 代码 frequency 路线
  requested 默认 1，无 hardware_concurrency 回退（`src/solver/reuse_freq_para_solver.cpp:104-112`）。
- :75-78 — fused 域内显式 `reuse`/`parallel` 会收到 deprecation warning — 代码无任何 deprecation
  warning 机制（`app/main.cpp` 全文无此逻辑；serial/frequency 永远合法执行）。
- :85-89 — "`R/E/A/a` 产品不进入 fused"、"IGR-3B 的 Arrival contribution sink 适配尚未
  construction" — 代码事实：Range（原 fused）已支持 A/a 的 G/g/B 多频 ARR 含多 source source
  streaming（见 ARR 节）；R/E 不进 Range 与代码一致（R 根本无 reuse；E 被 Range 拒绝）。
  这是 GUIDE_USAGE 与代码最大的语义级滞后：ARR 的 Range 路线已完全 production 化。

## 结论

- 当前生产代码的支持矩阵为：TL 四路线（Range 限单 source、规则等距网格、≥2 range、四 family
  全部合法 run mode、SimpleGaussian 仅 coherent）；ARR 四路线（全路线限 G/g/B family；Range 另限
  ≥2 频与规则等距网格，允许多 source 且按 source streaming 执行）；Eigenray 三路线（Range 拒绝）；
  R 仅单频 nonreuse（trace-workers 合法）。
- REFERENCE_FEATURE_SUPPORT_MATRIX.md 的支持域内容与代码一致，差异集中在执行模型命名
  （fused/parallel → reuse/range/frequency）、PRT 标签（broadband fused reuse → broadband reuse +
  reuse mode）与默认 worker 数（4 → 1）。
- GUIDE_USAGE.md 是主要滞后文档：可执行名、整套 CLI 选项（`--execution-mode fused`、
  `--range-parallel`、`--workers`）、默认值、deprecation warning 均与代码不符，且仍声明
  A/a 不进入 fused / IGR-3B 未施工，与 Range ARR 多 source 的生产现实直接矛盾。
