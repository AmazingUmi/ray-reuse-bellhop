# BB-2 B01 — Documentation Inventory
Date: 2026-09-06

只读盘点。核对基准（来自当前源码，非文档）：

- executable：`bellhop_broadband`（`Bellhop_RayReuse/app/main.cpp` usage、`test/standard_cases/codes/standard_cases.py:214`、`benchmark_rayreuse.py` DEFAULT_EXECUTABLE）
- CLI 旗标全集：`--frequencies-hz`、`--execution-mode <nonreuse|reuse>`（默认 nonreuse）、`--reuse-mode <serial|frequency|range>`（默认 serial，仅 reuse 接受）、`--trace-workers`（默认 1）、`--reuse-workers`（默认 1；serial 路径拒绝该选项）、`--verify-cache`、`--profile-influence`、`--profile-frequency-tasks`（仅 reuse+frequency）、`--output-queue-capacity`（默认 2）、`--memory-budget-mib`、`--version`/`--help`（`src/io/command_line.cpp`）
- solver：NonReuseSolver / ReuseSerialSolver / ReuseFreqParaSolver / ReuseRangeParaSolver
- PRT 标记：`execution mode = broadband nonreuse` / `execution mode = broadband reuse` + `reuse mode = serial|frequency|range`（`app/main.cpp:680-689` 等；`standard_cases.py` RAYREUSE_EXECUTION_PRT_MARKERS）
- standard_cases runner choices：`nonreuse|reuse-serial|reuse-frequency|reuse-range`（默认 nonreuse）
- benchmark runner：`--modes` 新值 `nonreuse,reuse-serial,reuse-frequency`（默认）；`--parallel-workers`（展开 reuse-frequency）与 `--fused-range-workers`（展开 reuse-range）旗标名保留但语义已改

## CURRENT（须更新）

### README.md（仓库根）
- :23-26 — "提供 `nonreuse`、production `fused`，以及兼容保留的 legacy `reuse` / frequency-`parallel` broadband execution。当前已验收的 fused production 支持域是 Cartesian Cerveny TL，并可显式开启静态 receiver-range parallelism" — 旧四模式模型 + 过时的 fused 支持域（IGR-3A/3B 后已覆盖全部合法 TL families 与 `G/g/B × A/a`） — 改写为 Bellhop Broadband 两层 CLI（`--execution-mode <nonreuse|reuse>` + `--reuse-mode <serial|frequency|range>`）与 IGR-3 后支持域
- :33-37 — "IGR-3 已冻结 future architecture direction，但尚未开始 construction" — 与 `dda1c2c`/`0050f59`（IGR-3 已 ACCEPTED/CLOSED）直接冲突 — 改为已关闭并指向 closure 证据
- （可选）:9 — 组件表 RayReuse 角色描述可补 Bellhop Broadband 产品名与新 CLI 入口

### Bellhop_RayReuse/README.md
- :3-6 — "`nonreuse` reference、production `fused` RayReuse，以及兼容保留的 legacy `reuse` / frequency-`parallel` broadband execution" — 旧四模式叙述 — 改为两层 execution/reuse-mode 模型
- :124-128 — 模式列表 `nonreuse`/`fused`/`reuse`/`parallel` 四项 — 旧值 — 改为 `nonreuse` + `reuse × {serial|frequency|range}` 及对应 solver 语义
- :140-142 — "`--range-parallel` 未指定 `--workers` 时默认请求 4 workers……单独指定 `--workers` 不会隐式开启 range parallel" — 已删除旗标；且新默认是 `--reuse-workers` 默认 1（非 4）— 改为 `--reuse-mode range` + `--reuse-workers`（默认 1，clamp 到 range 数）
- :154-155 — "legacy `parallel` 的 `--workers` 默认采用硬件并发数；完成队列容量只能为 1 或 2，默认 2" — 旧旗标与旧默认 — 改为 reuse+frequency 路径下 `--reuse-workers` 默认 1；queue 仅限该路径
- :169 — "parallel 逐频任务计时可用 `--profile-frequency-tasks`" — 旧模式名 — 改为 reuse+frequency
- :171-175 — "fused 运行中只有 Cartesian Cerveny 填充 Influence 计数……fused PRT 模式行见 …… 的 fused 支持域小节" — fused 术语 + 指向 matrix 中将被改名的小节 — 与 support matrix 改名联动更新交叉引用
- （可选）:1 — 标题/首段可声明产品名 Bellhop Broadband（目录名不变）

### Bellhop_RayReuse/doc/README.md
- :15-18 — "IGR-3 scope/architecture direction 已 `USER-FROZEN / PRE-CONSTRUCTION`，但 construction 尚未开始" — 与 IGR-3 已关闭冲突 — 改为 ACCEPTED/CLOSED（`dda1c2c`/`0050f59`）
- :42 — IGR-3 行标签 "用户冻结 scope/architecture handoff（非 design/worklist）" — 过时定位 — 改为已关闭并指向 IGR-3B worklist / final review
- :46-52 — "当前路径是……IGR-3A 是 documentation preflight 后的 next Batch；IGR-3A 独立验收并提交后才可进入 IGR-3B" — 过时路线叙述 — 改为 IGR-3 已关闭 + BB-1 CLI 重构完成（Bellhop Broadband、两层模式）为当前状态
- （可选）— 增加 BB-1 状态行与 PLAN_CURRENT_WORK 指向

### Bellhop_RayReuse/doc/guides/GUIDE_USAGE.md
- :19 — `Bellhop_RayReuse/build/release/bellhop_rayreuse <file-root> [options]` — 旧 binary 名 — 改为 `bellhop_broadband`
- :41-45 — 示例 `--execution-mode fused` — 已删除的 mode 值 — 改为 `--execution-mode reuse [--reuse-mode serial]`
- :47-54 — 执行模式四项列表（nonreuse/fused/reuse/parallel） — 旧值 — 改为两层模型 + 四 solver 说明
- :56-64 — 示例 `--range-parallel` + `--workers 8` — 已删除旗标 — 改为 `--execution-mode reuse --reuse-mode range --reuse-workers 8`
- :66-69 — "range parallel 仅由 `--range-parallel` 显式开启；未指定 `--workers` 时默认请求 4 workers……" — 旧旗标与旧默认（4） — 改为 `--reuse-mode range` + `--reuse-workers` 默认 1
- :71-73 — "legacy `parallel` 模式中，`--workers` 未指定时使用硬件并发数。SHD 的 `--output-queue-capacity`……A/a/E 的 parallel worker……" — 旧模式/旗标；queue 现仅适用 reuse+frequency 多频 TL — 重写为 reuse+frequency 语义
- :75-78 — "显式选择 `reuse` 或 `parallel` 会收到一次 deprecation warning……CLI 全局默认也保持 `nonreuse`" — 旧四模式下的 deprecation 行为已随 BB-1 模式删除而消失；默认 nonreuse 仍正确 — 删除/改写 deprecation 叙述，保留默认值说明
- :80-90 — fused 支持域小节："fused 支持域（IGR-3A）……`R/E/A/a` 产品不进入 fused……IGR-3B 的 Arrival contribution sink 适配尚未 construction" — 双重过时：A/a 的 `G/g/B` 已由 IGR-3B 接入 fused；"IGR-3B 未施工"与 `0050f59` 冲突 — 改写为 reuse 支持域（TL 全 family + `G/g/B × A/a`），R/E 边界保留，指向已关闭的 IGR-3 决策文档
- :114 — `--executable Bellhop_RayReuse/build/release/bellhop_rayreuse` — 旧 binary 路径 — 改为 `bellhop_broadband`
- :124-125 — `--rayreuse-execution-mode parallel` + 旧 executable 路径 — 已删除的 runner mode 值 — 改为 `reuse-frequency`（现 choices：nonreuse|reuse-serial|reuse-frequency|reuse-range）与新路径

### Bellhop_RayReuse/doc/guides/GUIDE_BENCHMARKING.md
- :5-7 — "比较 `nonreuse`、`reuse`、`fused` 和 `parallel`。它直接调用 Release 可执行程序，并记录 legacy frequency-parallel workers……" — 旧模式集合 — 改为 `nonreuse`/`reuse-serial`/`reuse-frequency`/`reuse-range`
- :24-37 — 正式示例 `--modes nonreuse,reuse,parallel`、`--parallel-workers 8,10`、`--executable …/bellhop_rayreuse` — 旧 mode 值 + 旧 binary — 改为 `--modes nonreuse,reuse-serial,reuse-frequency`（旗标名保留）与 `bellhop_broadband`
- :39-42 — "`--parallel-workers` 只展开 legacy `parallel` 配置。`--fused-range-workers 1,2,4,8` 则将 `fused` 展开为静态 receiver-range worker 配置" — 旗标名仍在但语义已变 — 改为分别展开 `reuse-frequency` 与 `reuse-range` 配置（旗标名未改，需明确说明）
- :47-49 — "需要诊断 parallel 逐频任务分布时，将模式限制为 parallel 并增加 `--profile-frequency-tasks`" — 旧模式名 — 改为 reuse-frequency（该开关现要求 reuse-frequency-only modes）
- :52-64 — 诊断示例 `--modes parallel`、`--parallel-workers 8,10`、旧 executable 路径 — 同上 — 同上
- :73 — "必要时16频 reuse/parallel" — 旧模式名 — 改为 reuse-frequency/reuse-range
- :97-98 — "PRT 必须包含……正确 execution mode……与请求一致的 workers" — 语义仍成立但 mode 标记已变 — 确认措辞与新 PRT 标记（broadband reuse + reuse mode 行）一致

### Bellhop_RayReuse/doc/guides/GUIDE_RELEASE.md
- :28 — "安装目录内 `bellhop_rayreuse --version` 烟测" — 旧 binary 名 — 改为 `bellhop_broadband --version`（以 engineering_gate.sh 实际行为为准）
- （保留）:38、:49 — 历史 TGZ 文件名/SHA-256 为当时内部验证记录，不改

### Bellhop_RayReuse/doc/reference/REFERENCE_FEATURE_SUPPORT_MATRIX.md
- :15 — 小节标题 "## fused execution-mode 支持域" — 旧术语（CLI 已无 fused 值） — 改为 reuse execution 支持域之类的两层命名
- :43 — "legacy `nonreuse/reuse/parallel` 行为不受影响" — 旧模式三元组 — 改为 nonreuse 与 reuse 三路径表述
- :45-71 — "fused PRT 模式报告与 Influence 统计 envelope" 小节：引述 PRT 块 `execution mode = broadband fused reuse`、`range parallel`、`requested/effective range worker count` — 与现 PRT 标记不符（现为 `execution mode = broadband reuse` + `reuse mode = …`） — 按当前 main.cpp 输出更新块名与字段
- :96 — TL 行末 "多频 `nonreuse/reuse/parallel` SHD" — 旧三元组 — 改为四路径（nonreuse + reuse×3）
- :101 — multisource 行 "`nonreuse/reuse/parallel` 三模式逐频产品一致，trace passes 冻结语义为 `Nfreq×NSz / NSz / NSz`" — 旧三元组；reuse 三路径下 trace passes 语义需按 BB-1 后实测确认 — 更新模式列举并复核 trace passes 表述
- :103 — SSP 行两处 "`nonreuse/reuse/parallel`" — 同上 — 同上
- :104 — 衰减行 "宽带 profile 在 `nonreuse/reuse/parallel` 下输出逐字节一致" — 同上 — 同上
- :108 — 并行行 "legacy `parallel` 下 A/a/E 与 TL 支持外层 frequency parallelism……默认请求 4 workers" — 旧模式与旧默认（4） — 改为 reuse+frequency / reuse+range，默认 `--reuse-workers 1`
- :120 — "`nonreuse/reuse/parallel` 以及上文支持域内的 fused Arrival……" — 旧三元组 — 同上
- :146 — Intentional divergence 并行所有权行 "legacy `parallel` 选择 frequency worker" — 旧模式名 — 改为 reuse+frequency
- （可选）:3 — 封板日期行可补 BB-1 CLI 重构封板日期与说明

### Bellhop_RayReuse/doc/status/STATUS_PROGRESS.md
- :3 — 更新日期 2026-09-04，未含 BB-1 — 更新日期并补 BB-1（Bellhop Broadband 更名 + 两层 CLI + solver 重命名 + 旗标删除）状态与 commit
- :33-40 — "Current accepted production (IGR-3 CLOSED)" 代码块 — 缺 BB-1 CLI/产品面状态 — 追加 BB-1 行或新小节
- （保留）:87-88 — "宽带 `nonreuse` / `reuse` / `parallel`……" 属 FP 最终验收基线的历史证据记录，可原样保留（如改动需加"当时模式名"注释，不重写历史）

### test/standard_cases/README.md
- :173 — 示例 `--rayreuse-execution-mode reuse` — 已删除的 runner mode 值（现 choices 无裸 `reuse`） — 改为 `reuse-serial`
- :201-203 — "运行时适配器只调用一次 `bellhop_rayreuse`……以及 `--execution-mode <nonreuse|reuse|fused|parallel>`" — 旧 binary 名 + 旧 mode 值集 — 改为 `bellhop_broadband` 与现传参（`--execution-mode reuse --reuse-mode <…>`）
- :212-213 — "`reuse` 和 `parallel` 则必须等于 1" — 旧模式名 — 改为 reuse 三路径下 Trace passes == 1 的表述
- :228-229 — "直接比较 `nonreuse`、`reuse`、`fused`、`parallel`。`--parallel-workers` 针对 legacy frequency-`parallel`，`--fused-range-workers` 针对 fused range-parallel" — 旧模式与旧旗标语义 — 改为四新模式名及两旗标现语义（展开 reuse-frequency / reuse-range）
- :244 — "默认可执行文件为 `Bellhop_RayReuse/build/release/bellhop_rayreuse`" — 旧 binary 路径（代码已默认 `bellhop_broadband`） — 更新
- :247 — "`--rayreuse-execution-mode nonreuse|reuse|fused|parallel` 选择" — 旧 choices — 改为 `nonreuse|reuse-serial|reuse-frequency|reuse-range`

### test/standard_cases/REFERENCE_SNAPSHOTS.md
- :7 — "`Bellhop_RayReuse` 是覆盖 single、nonreuse、reuse 和 parallel 的被验收对象" — 旧模式列举 — 改为 single、nonreuse 与 reuse 三路径

### doc/README.md（根项目文档索引）
- :40-41 — "IGR-3 用户冻结 scope/architecture handoff（`PRE-CONSTRUCTION`）" — 与 IGR-3 已关闭冲突 — 改为已关闭状态并指向 closure 证据
- （可选）:61 — RayReuse 组件"当前角色"可补 Bellhop Broadband 产品名

### doc/plans/PLAN_CURRENT_WORK.md
- :3 — 更新日期 2026-09-04，未含 BB-1 — 更新日期
- :7-15 — "当前状态"表缺 BB-1 行（Bellhop Broadband 更名、两层 CLI、旧旗标删除） — 增加 BB-1 `ACCEPTED / CLOSED` 行
- （可选）:13 — IGR-1 行内 "`--execution-mode fused` 落地为 opt-in 实验模式" 属历史叙述（当时旗标名），可加注"旗标已于 BB-1 移除"避免误用，不必改写史实

### doc/architecture/ARCHITECTURE_BELLHOP_RAY_REUSE.md
- :1186 — 目录树注释 "app/  bellhop_rayreuse 宽带程序" — 旧 binary 名 — 改为 `bellhop_broadband`
- （可选）:1264-1285 — §22 "当前基线事实"可补一句 BB-1 产品/CLI 重构已完成

## CURRENT（核对后无需更新）

以下 living 文档已核对，不含旧 binary/旧 mode/旧旗标引用：

- `demo/README.md` — 已使用 `bellhop_broadband`（:57、:118）与 `--execution-mode reuse`（:119）
- `Bellhop_RayReuse/doc/guides/GUIDE_SINGLE_THREAD_MICROBENCHMARK.md` — 单频三模型微基准，不涉及 execution mode/binary 名
- `AGENTS.md` — 用户指令文件，本 Batch 只读列出；其中 "fused execution" 契约术语描述内部执行架构（源码仍有 `fused_influence_adapters.hpp` 等），非 CLI 值，无冲突
- `doc/reference/REFERENCE_NUMERICAL_CONVENTIONS.md` — 稳定数值契约，无 CLI/模式引用
- `doc/reference/REFERENCE_INFLUENCE_GEOMETRY_REUSE.md` — IGR 理论契约，无 CLI/模式引用
- `test/PlotRead/README.md`、`test/PlotRead/reference/README.md`、`test/PlotRead/tests/README.md`、`demo/cases/README.md`、`demo/results/README.md`、`test/standard_cases/results/README.md` — 无滞后 token

## HISTORICAL（保留）

- `Bellhop_RayReuse/doc/status/STATUS_FEATURE_PARITY_SEQUENCE_2026-08-29.md` — 自标"执行期进度快照"的 FP-2F→FP-2I 序列记录，文件名带日期，doc/README 已定位为历史快照
- `Bellhop_RayReuse/doc/decisions/DECISION_HDF5_SCHEMA.md` — 已冻结工程决策；其中 "parallel solver" 措辞指假设性未来 HDF5 writer，不指导当前运行
- `Bellhop_RayReuse/doc/workreports/FP-2B_PCHIP_SSP_BATCH_REPORT.md` — 已关闭 FP-2B 验收证据
- `Bellhop_RayReuse/doc/workreports/FP-2C_N2_LINEAR_SSP_BATCH_REPORT.md` — 已关闭 FP-2C 验收证据
- `Bellhop_RayReuse/doc/workreports/FP-2D_CUBIC_SPLINE_SSP_BATCH_REPORT.md` — 已关闭 FP-2D 验收证据
- `Bellhop_RayReuse/doc/workreports/FP-2D-R1_FINAL_REVIEW_REMEDIATION_REPORT.md` — 已关闭 FP-2D-R1 remediation 证据
- `Bellhop_RayReuse/doc/workreports/FP-2E_QUADRILATERAL_SSP_BATCH_REPORT.md` — 已关闭 FP-2E 验收证据
- `Bellhop_RayReuse/doc/workreports/FP-2F_SOURCE_RECEIVER_GENERALIZATION_BATCH_REPORT.md` — 已关闭 FP-2F 验收证据
- `Bellhop_RayReuse/doc/workreports/FP-2G_BOUNDARY_MATERIAL_CLOSURE_BATCH_REPORT.md` — 已关闭 FP-2G 验收证据
- `Bellhop_RayReuse/doc/workreports/FP-2H_ATTENUATION_CLOSURE_BATCH_REPORT.md` — 已关闭 FP-2H 验收证据
- `Bellhop_RayReuse/doc/workreports/FP-2I_LINE_SOURCE_CLOSURE_BATCH_REPORT.md` — 已关闭 FP-2I 验收证据
- `Bellhop_RayReuse/doc/archive/`（含 benchmarks/、plans/ 子树） — 完成计划、派生记录与被替代的性能/矩阵证据，目录语义即历史
- `doc/archive/`（根，含 PLAN_PROJECT_IMPLEMENTATION_2026-08-14.md） — 历史项目实施清单
- `test/legacy/`（含 ray_reuse_experiment/） — 迁移前历史材料，不参与当前测试

## OBSOLETE（仅列出）

- 无 — 未发现"既不指导当前运行又无历史价值"的文档；全部 dated 材料均已处于 archive/legacy 语义目录且保留证据价值。BB-2 无删除候选。

## 汇总

- CURRENT：13 个文件须更新，合计约 54 处（另有约 8 处可选项）；另 11 个 living 文档核对后无需更新
- HISTORICAL：14 个条目（1 status 快照 + 1 decision + 9 workreports + 2 archive 树 + 1 legacy 树）
- OBSOLETE：0 个
