# BB-M1 — Broadband Execution Model & Naming Milestone Worklist

**Status:** `DRAFT / USER REVIEW`
**Milestone type:** Architecture / Naming / CLI Breaking Change
**Repository:** `AmazingUmi/ray-reuse-bellhop`
**Current development branch:** `feat/igr-influence-geometry-reuse`
**Implementation baseline:** construction 开始前记录实际 `HEAD` SHA
**Protected references:** `Bellhop_origin/`、`Bellhop_F2CPP/` 不得修改

---

# 1. Milestone 定义

本里程碑标志项目从：

> **Bellhop RayReuse — 以轨迹复用为中心的宽带实验/实现**

正式演进为：

> **Bellhop Broadband — 统一宽带 Bellhop 计算框架，其中 RayReuse 为宽带计算算法族之一**

本轮首先完成：

1. 产品级命名从 `RayReuse` 向 `Broadband` 迁移；
2. CLI 执行模型重新分层；
3. RayReuse 三条算法路线重新命名；
4. 并行参数重新定义；
5. 保持现有科学计算与输出语义；
6. BB-1 验收后，对当前有效文档进行全面术语审阅；
7. 历史文档保留历史名称，不伪造历史。

本里程碑**不是性能优化批次**，也不进行 8 种逻辑组合的性能优劣评价。性能比较在本里程碑关闭后独立开展。

---

# 2. 冻结后的计算模型

## 2.1 顶层计算模式

```text
Bellhop Broadband
│
├── NonReuse
│
└── RayReuse
```

CLI：

```text
--execution-mode <nonreuse|reuse>
```

定义：

### `nonreuse`

每个频率独立完成完整计算：

```text
frequency 0:
    Trace
    → Projection
    → Influence
    → Product

frequency 1:
    Trace
    → Projection
    → Influence
    → Product

...
```

不同频率之间不共享 RayPathCache。

### `reuse`

每个 source 的声线轨迹只计算一次：

```text
Trace
→ Frozen RayPathCache
→ Reuse Stage
→ Products
```

随后由 `reuse-mode` 决定 RayPathCache 的复用组织方式。

---

# 3. Trace Stage 定义

**Trace Stage 同时存在于 NonReuse 与 RayReuse。**

它不是 RayReuse 独有阶段，也不属于 `reuse-mode`。

完整逻辑：

```text
Bellhop Broadband
│
├── NonReuse
│   │
│   └── for each frequency
│       ├── Trace Stage
│       │   └── --trace-workers N
│       └── Projection / Influence / Product
│
└── RayReuse
    │
    ├── Trace Stage
    │   └── --trace-workers N
    │
    ├── Frozen RayPathCache
    │
    └── Reuse Stage
        ├── serial
        ├── frequency
        └── range
```

区别仅在于：

```text
NonReuse:
Trace 次数 ≈ Nfrequency × Nsource

RayReuse:
Trace 次数 ≈ Nsource
```

具体数量继续服从现有产品与 source 生命周期语义。

---

# 4. Trace 并行模型

CLI：

```text
--trace-workers N
```

规则：

```text
N = 1    → serial trace
N > 1    → static trace parallel
```

默认：

```text
trace-workers = 1
```

不引入：

```text
--trace-mode
```

原因：

`serial/parallel trace` 当前仅为同一 Trace 算法的 worker 数变化，不存在独立算法路线。

因此：

```text
--trace-workers
```

足以完整描述 Trace Stage。

`--trace-workers` 对以下路线全部有效：

```text
nonreuse
reuse + serial
reuse + frequency
reuse + range
```

---

# 5. RayReuse 算法重新命名

## 5.1 新算法树

```text
RayReuse
│
├── Serial Reuse
│
├── Frequency Reuse
│
└── Range Reuse
```

CLI：

```text
--reuse-mode <serial|frequency|range>
```

---

## 5.2 Serial Reuse

```text
Trace once
→ Frozen RayPathCache
→ frequency 0
→ frequency 1
→ frequency 2
→ ...
```

特点：

* 共享 ray trajectory；
* frequency 顺序执行；
* 不进行 reuse-stage parallel partition；
* 用作最直接的 RayReuse reference / compatibility route。

CLI：

```bash
bellhop_broadband case \
  --execution-mode reuse \
  --reuse-mode serial
```

默认：

```text
reuse-mode = serial
```

当且仅当：

```text
execution-mode = reuse
```

且用户没有指定 `--reuse-mode` 时使用。

---

## 5.3 Frequency Reuse

原历史实现：

```text
ParallelRayReuse
--execution-mode parallel
```

重新定义为：

```text
Frequency Reuse
```

结构：

```text
Trace once
→ Frozen RayPathCache

       ┌→ frequency 0 → Projection → Influence
       ├→ frequency 1 → Projection → Influence
Cache ─┼→ frequency 2 → Projection → Influence
       └→ ...
```

worker 分割对象：

> frequency tasks

CLI：

```bash
bellhop_broadband case \
  --execution-mode reuse \
  --reuse-mode frequency \
  --reuse-workers 8
```

---

## 5.4 Range Reuse

原历史实现：

```text
FusedRayReuse
+
optional range parallel
```

重新定义为：

```text
Range Reuse
```

用户层算法语义：

```text
Trace once
→ Frozen RayPathCache
→ cross-frequency fused projection/influence
→ receiver-range partition
```

结构：

```text
Frozen RayPathCache
        ↓
cross-frequency fused processing
        ↓
 ┌──────┼──────┬──────┐
range   range  range  range
block0  block1 block2 block3
```

worker 分割对象：

> receiver range blocks

CLI：

```bash
bellhop_broadband case \
  --execution-mode reuse \
  --reuse-mode range \
  --reuse-workers 8
```

`range` 是用户层算法名称。

内部实现仍允许继续使用：

```text
fused kernel
fused workspace
fused influence
fused sink
fused adapter
```

因为这些名称准确描述底层跨频融合实现。

**禁止为了统一名称而机械删除所有 `fused` 术语。**

---

# 6. Reuse worker 模型

统一参数：

```text
--reuse-workers N
```

不再公开：

```text
--frequency-workers
--range-workers
```

因为：

```text
--reuse-mode
```

已经确定 worker 的分割维度。

解释规则：

```text
reuse-mode = frequency
    reuse-workers = frequency worker count

reuse-mode = range
    reuse-workers = range worker count
```

默认：

```text
reuse-workers = 1
```

### Serial

```text
--reuse-mode serial
```

不需要 `--reuse-workers`。

若显式给出：

```text
--reuse-workers N
```

建议规则：明确拒绝。

禁止静默忽略。

### Frequency / Range

允许：

```text
--reuse-workers >= 1
```

其中 `N=1` 仍保持所选算法路线：

```text
frequency + workers=1
```

仍走 Frequency Reuse 实现；

```text
range + workers=1
```

仍走 Range Reuse / fused 实现。

不得因为 worker=1 自动改成 Serial Reuse。

---

# 7. 最终核心 CLI

目标主界面：

```text
Usage:
  bellhop_broadband <file-root>
    [--frequencies-hz <f0,f1,...>]
    [--execution-mode <nonreuse|reuse>]
    [--reuse-mode <serial|frequency|range>]
    [--trace-workers <count>]
    [--reuse-workers <count>]
    [diagnostic/resource options...]
```

四个核心执行参数：

```text
--execution-mode
--reuse-mode
--trace-workers
--reuse-workers
```

---

# 8. 参数默认值

冻结：

```text
execution-mode = nonreuse
trace-workers   = 1
```

当：

```text
execution-mode = reuse
```

时：

```text
reuse-mode    = serial
reuse-workers = 1
```

继续保持默认 `nonreuse` 的原因：

* 避免本次命名重构同时改变默认科学执行路线；
* 不在 CLI rename milestone 内引入自动路由；
* 后续是否将 production 默认改为 RayReuse 单独决策。

本批次禁止引入：

```text
execution-mode = auto
```

---

# 9. 输入频率语义

`<file-root>` 继续表示：

```text
<file-root>.env
```

ENV 内已有频率定义。

正常运行：

```bash
bellhop_broadband case
```

直接使用 ENV 中：

* scalar frequency；
* 或合法 broadband frequency list。

CLI：

```text
--frequencies-hz
```

仅作为：

> ENV frequency override

保留该参数，但在 `--help` 中归入：

```text
Input overrides
```

而非核心 execution control。

---

# 10. 合法执行组合

按逻辑路线而非具体 worker 数计算，共形成 8 类组合：

| # | Execution | Trace          | Reuse Mode |
| - | --------- | -------------- | ---------- |
| 1 | NonReuse  | serial         | —          |
| 2 | NonReuse  | parallel       | —          |
| 3 | Reuse     | serial trace   | serial     |
| 4 | Reuse     | parallel trace | serial     |
| 5 | Reuse     | serial trace   | frequency  |
| 6 | Reuse     | parallel trace | frequency  |
| 7 | Reuse     | serial trace   | range      |
| 8 | Reuse     | parallel trace | range      |

定义：

```text
trace-workers = 1
    → serial trace

trace-workers > 1
    → parallel trace
```

后续 benchmark 可以基于这 8 个逻辑组合建立独立性能矩阵。

**本里程碑只保证这些路线语义明确且正确，不评价性能。**

---

# 11. 并行关系约束

Trace parallel 与 Reuse parallel 属于不同阶段：

```text
Trace Stage
--trace-workers
      ↓
barrier / Frozen Cache
      ↓
Reuse Stage
--reuse-workers
```

因此：

```text
--trace-workers
```

与：

```text
--reuse-workers
```

不互斥。

合法：

```bash
bellhop_broadband case \
  --execution-mode reuse \
  --reuse-mode frequency \
  --trace-workers 8 \
  --reuse-workers 4
```

合法：

```bash
bellhop_broadband case \
  --execution-mode reuse \
  --reuse-mode range \
  --trace-workers 8 \
  --reuse-workers 4
```

本阶段禁止新增：

> Trace Stage 与 Reuse Stage 同时执行的 nested/overlapped concurrency。

当前要求仍为阶段顺序：

```text
Trace complete
→ Reuse begin
```

---

# 12. 旧 CLI → 新 CLI 映射

## 12.1 NonReuse

旧：

```bash
bellhop_rayreuse case \
  --execution-mode nonreuse \
  --trace-workers 8
```

新：

```bash
bellhop_broadband case \
  --execution-mode nonreuse \
  --trace-workers 8
```

---

## 12.2 Serial RayReuse

旧：

```bash
bellhop_rayreuse case \
  --execution-mode reuse
```

新：

```bash
bellhop_broadband case \
  --execution-mode reuse \
  --reuse-mode serial
```

---

## 12.3 Frequency Reuse

旧：

```bash
bellhop_rayreuse case \
  --execution-mode parallel \
  --workers 8
```

新：

```bash
bellhop_broadband case \
  --execution-mode reuse \
  --reuse-mode frequency \
  --reuse-workers 8
```

---

## 12.4 Range Reuse — single worker

旧：

```bash
bellhop_rayreuse case \
  --execution-mode fused
```

新：

```bash
bellhop_broadband case \
  --execution-mode reuse \
  --reuse-mode range \
  --reuse-workers 1
```

---

## 12.5 Range Reuse — parallel

旧：

```bash
bellhop_rayreuse case \
  --execution-mode fused \
  --range-parallel \
  --workers 8
```

新：

```bash
bellhop_broadband case \
  --execution-mode reuse \
  --reuse-mode range \
  --reuse-workers 8
```

---

# 13. 删除的旧 CLI 概念

BB-M1 最终树删除：

```text
--execution-mode parallel
--execution-mode fused
--range-parallel
--workers
```

它们分别被：

```text
reuse + frequency
reuse + range
reuse-workers
```

取代。

最终产品不长期保留旧参数 compatibility aliases。

原因：

1. 本节点本身就是明确的 breaking naming milestone；
2. 保留旧参数会重新形成两套语义；
3. 项目内部 scripts/tests 可一次性迁移；
4. 历史运行方式由 milestone 前 baseline SHA 与历史报告永久保存。

禁止：

* silent alias；
* old/new 混用；
* 根据旧参数自动猜测新 mode。

旧参数应在最终 parser 中表现为 unknown/deprecated-removed option，而不是继续参与 dispatch。

---

# 14. 现有高级参数处理

以下参数暂不因命名改革而删除：

```text
--output-queue-capacity
--memory-budget-mib
--verify-cache
--profile-influence
--profile-frequency-tasks
```

原则：

> 本批次只把它们绑定到新的执行模型，不重新设计其科学/资源语义。

其中现有 Frequency Parallel 专属参数迁移为：

```text
execution-mode = reuse
reuse-mode = frequency
```

例如：

```text
--output-queue-capacity
--memory-budget-mib
--profile-frequency-tasks
```

继续只在 Frequency Reuse 合法时接受。

不得为了 CLI 美观顺带扩大其作用域。

---

# 15. 产品级命名迁移

## 15.1 Executable

必须：

```text
bellhop_rayreuse
→
bellhop_broadband
```

---

## 15.2 CMake project

建议同步：

```text
BellhopRayReuse
→
BellhopBroadband
```

---

## 15.3 Product/package

同步迁移：

```text
bellhop-rayreuse
→
bellhop-broadband

Bellhop RayReuse
→
Bellhop Broadband
```

版本输出：

旧：

```text
Bellhop RayReuse X.Y.Z
```

新：

```text
Bellhop Broadband X.Y.Z
```

PRT 产品级 header：

旧：

```text
BELLHOP RAYREUSE
```

新：

```text
BELLHOP BROADBAND
```

仅修改产品身份，不修改 Origin-compatible 科学产品格式。

---

# 16. 项目目录命名

本 milestone 建议同步完成：

```text
Bellhop_RayReuse/
→
Bellhop_Broadband/
```

原因：

`Bellhop_RayReuse` 已不再准确描述包含 NonReuse 与 RayReuse 两大计算路线的产品。

仓库名称：

```text
ray-reuse-bellhop
```

本轮不要求修改。

仓库本身仍具有明确的研究历史含义。

---

# 17. 本轮不进行的全局重命名

为了避免把命名 milestone 扩大为无价值的大规模 churn，本轮**不要求**全局修改：

```text
namespace rayreuse
include/rayreuse/
RAYREUSE_* internal compatibility macros
```

除非它们直接阻碍：

* executable rename；
* CMake/package rename；
* solver rename；
* CLI 重构。

这些内部 namespace/include-root 是否迁移，应作为后续独立 architecture cleanup 决策。

禁止本轮仅为了字符串统一而重写数百个无关文件。

---

# 18. Solver 顶层算法命名

必须将用户层执行路线对应 solver 重命名。

目标：

```text
BroadbandNonReuseSolver
→ NonReuseSolver

SerialRayReuseSolver
→ ReuseSerialSolver

ParallelRayReuseSolver
→ ReuseFreqParaSolver

FusedRayReuseSolver
→ ReuseRangeParaSolver
```

对应 settings/statistics/predicate 的**路线级名称**同步调整，例如：

```text
ParallelRayReuseSettings
→ ReuseFreqParaSettings

ParallelRayReuseStatistics
→ ReuseFreqParaStatistics

FusedRayReuseExecutionSettings
→ ReuseRangeParaExecutionSettings

FusedRayReuseStatistics
→ ReuseRangeParaStatistics

supportsFusedRayReuse(...)
→ supportsReuseRangePara(...)
```

具体符号由 architect 审阅后统一，但必须满足：

> 外层算法名称使用 Serial / Freq / Range。

---

# 19. `fused` 术语保留边界

以下低层实现术语原则上保留：

```text
FusedPressureWorkspace
FusedIntensityWorkspace
fused influence kernel
fused adapters
fused sinks
cross-frequency fused layout
fused accumulation internals
```

理由：

它们描述的是 Range Reuse 内部真实的跨频融合实现机制，而不是用户选择的 execution route。

因此最终允许：

```text
ReuseRangeParaSolver
    └── internally uses fused kernel/workspaces
```

禁止：

```text
global search/replace "Fused" → "Range"
```

---

# 20. CommandLineOptions 目标模型

建议结构至少达到：

```cpp
enum class ExecutionMode {
    NonReuse,
    Reuse,
};

enum class ReuseMode {
    Serial,
    Frequency,
    Range,
};

struct CommandLineOptions {
    ...
    ExecutionMode executionMode{ExecutionMode::NonReuse};
    ReuseMode reuseMode{ReuseMode::Serial};

    std::size_t traceWorkerCount{1U};
    std::size_t reuseWorkerCount{1U};

    bool executionModeSpecified{};
    bool reuseModeSpecified{};
    bool traceWorkerCountSpecified{};
    bool reuseWorkerCountSpecified{};
    ...
};
```

移除：

```text
BroadbandExecutionMode::{Parallel,Fused}
rangeParallel
workerCount
```

是否保留 `BroadbandExecutionMode` 类型名由 architect 判断，但枚举值最终只能表达：

```text
NonReuse
Reuse
```

---

# 21. CLI validation 规则

必须明确实现：

### execution

```text
--execution-mode nonreuse
--execution-mode reuse
```

其他值拒绝。

### reuse-mode

```text
--reuse-mode
```

仅在：

```text
--execution-mode reuse
```

下合法。

### reuse-workers

仅在：

```text
--execution-mode reuse
```

下合法。

### serial

```text
reuse-mode = serial
```

若显式：

```text
reuse-workers > 1
```

必须拒绝。

### nonreuse

以下组合非法：

```text
nonreuse + --reuse-mode ...
nonreuse + --reuse-workers ...
```

### frequency

Frequency Reuse 继续服从原 ParallelRayReuse 的产品支持边界。

### range

Range Reuse 继续服从原 FusedRayReuse 的产品支持边界。

本批次不得因为命名变化扩大或缩小科学 support matrix。

---

# 22. Product support 保持原则

BB-M1 核心要求：

> old route capability → new route capability 一一映射。

映射：

```text
old nonreuse
→ new nonreuse

old reuse
→ new reuse/serial

old parallel
→ new reuse/frequency

old fused
→ new reuse/range
```

对于：

* TL；
* ARR；
* Eigenray；
* Ray product；
* single/multi source；
* beam family；
* regular/irregular receiver；

均不得根据名称推测新支持范围。

必须以当前 production code 的真实 support gate 为基线进行迁移。

若发现现有文档与代码不一致：

> BB-1 以 production code 当前实际行为为准记录；
> BB-2 再修正文档。

---

# 23. Scientific Freeze

本里程碑禁止修改：

* ray integration equations；
* reflection physics；
* boundary acoustics；
* attenuation；
* FrequencyProjector 数学；
* Influence equations；
* beam width；
* active thresholds；
* arrival accumulation semantics；
* SHD/ARR/RAY binary layout；
* frequency interpolation；
* geometry reuse math；
* fused hot-loop 数值顺序，除非纯 rename 必须且证明输出不变。

目标是：

> **Rename + dispatch refactor，not numerical refactor。**

---

# 24. Parallelism Freeze

禁止：

* 新增 nested parallel；
* Trace 与 Reuse 重叠执行；
* 修改 static trace partition algorithm；
* 修改 range partition algorithm；
* 修改 frequency scheduling algorithm；
* 修改 worker ownership；
* 修改 writer publication order；
* 修改 deterministic exception semantics。

只允许：

> 旧 worker 参数 → 新 worker 参数的语义映射。

---

# 25. BB-1 Implementation Tasks

## A01 `[ADVANCED]` — Architecture Freeze

由 architect 完成：

1. 审查本任务书；
2. 建立 old → new execution mapping；
3. 冻结 product / algorithm / internal implementation 三层命名边界；
4. 明确 solver rename 清单；
5. 明确 directory/CMake/package rename 影响面；
6. 明确 support gate 不变化；
7. 记录 BB-M1 baseline HEAD SHA；
8. 输出简短 architecture decision。

不得施工 production behavior。

---

## A02 `[STANDARD]` — Product Identity Rename

完成：

```text
bellhop_rayreuse → bellhop_broadband
BellhopRayReuse → BellhopBroadband
Bellhop RayReuse → Bellhop Broadband
```

以及构建/install/package/script 中直接依赖 executable 名称的当前有效引用。

若执行目录迁移：

```text
Bellhop_RayReuse/
→
Bellhop_Broadband/
```

同步修复当前有效构建路径。

不修改历史报告文本。

---

## A03 `[ADVANCED]` — CLI Execution Model Refactor

实现：

```text
--execution-mode <nonreuse|reuse>
--reuse-mode <serial|frequency|range>
--trace-workers N
--reuse-workers N
```

删除：

```text
parallel
fused
--range-parallel
--workers
```

实现第 21 节 validation contract。

保持：

```text
--frequencies-hz
```

为 input override。

---

## A04 `[ADVANCED]` — Solver Route Rename

完成：

```text
SerialRayReuse → SerialReuse
ParallelRayReuse → FrequencyReuse
FusedRayReuse → RangeReuse
```

只重命名 route-level symbols/files。

保留合理的 low-level fused terminology。

不得修改算法计算顺序。

---

## A05 `[ADVANCED]` — Dispatch Remap

重新整理 `main` / product dispatch，使执行模型显式表现为：

```text
ExecutionMode
    ├── NonReuse
    └── Reuse
         └── ReuseMode
              ├── Serial
              ├── Frequency
              └── Range
```

避免继续存在：

```text
if parallel ...
else if fused ...
```

这类旧概念作为顶层 execution 分支。

Trace settings 必须独立注入：

```text
NonReuse
Reuse/Serial
Reuse/Frequency
Reuse/Range
```

---

## A06 `[STANDARD]` — Resource / Profiling Parameter Remap

把：

```text
--output-queue-capacity
--memory-budget-mib
--profile-frequency-tasks
```

从旧：

```text
execution-mode parallel
```

迁移到：

```text
execution-mode reuse
reuse-mode frequency
```

其他 profiling/cache 参数仅按现有能力迁移，不扩大作用域。

---

## A07 `[STANDARD]` — Build / Script / Test Invocation Migration

更新当前有效：

* CMake executable references；
* install rules；
* package rules；
* quality/engineering gates；
* benchmark scripts；
* standard-case runner；
* CI；
* demo invocation；
* CLI tests。

历史 archived report/worklist 不在 A07 修改。

---

## A08 `[ADVANCED REVIEW]` — Naming Boundary Review

Reviewer 专门检查：

1. 是否还把 `parallel/fused` 当作顶层 execution mode；
2. Trace Stage 是否正确存在于 NonReuse 与 RayReuse 两条路线；
3. `trace-workers` 是否与 `reuse-workers` 正交；
4. Frequency / Range 是否被错误地允许同时运行；
5. 是否误把 low-level `fused` 术语机械替换；
6. 是否产生算法行为变化；
7. 是否存在旧 executable/CLI 的活跃引用。

结论：

```text
PASS
或
CHANGES_REQUIRED
```

---

# 26. BB-1 Validation

测试比重服从：

> 功能与命名目标优先，不为 rename milestone 扩建大型测试体系。

最低要求：

### CLI

验证：

```text
--help
--version
missing root
invalid execution mode
invalid reuse mode
invalid worker combinations
```

### Route mapping

至少证明：

```text
old nonreuse      ↔ new nonreuse
old reuse         ↔ new reuse/serial
old parallel      ↔ new reuse/frequency
old fused serial  ↔ new reuse/range workers=1
old fused range   ↔ new reuse/range workers=N
```

输出应保持原路线数值/产品等价。

### Trace

验证：

```text
trace-workers = 1
trace-workers > 1
```

分别可用于：

```text
nonreuse
reuse
```

### Regression

执行现有 full CTest / 项目现有主回归体系。

不设置新的性能提升门槛。

### Hygiene

至少：

```text
git diff --check
working tree inspection
Bellhop_origin unchanged
Bellhop_F2CPP unchanged
```

---

# 27. BB-1 Acceptance Gate

Final Reviewer 必须使用高级模型。

验收问题：

1. 产品是否已真正成为 `Bellhop Broadband`？
2. `nonreuse/reuse` 是否成为唯一顶层 computation distinction？
3. `serial/frequency/range` 是否成为唯一 RayReuse strategy distinction？
4. Trace Stage 是否同时正确服务 NonReuse 与 RayReuse？
5. 是否仅用 worker 数表达 trace serial/parallel？
6. `trace-workers` 与 `reuse-workers` 是否互不冲突？
7. old `parallel/fused/range-parallel/workers` 是否退出最终用户模型？
8. solver route 命名是否与新算法模型一致？
9. low-level fused implementation terminology 是否被正确保留？
10. 是否保持原科学行为与输出？
11. 是否没有进入性能优化？
12. protected references 是否未修改？

最终结论只能：

```text
ACCEPTED
或
CHANGES_REQUIRED
```

BB-1 `ACCEPTED` 后：

> 自动进入 BB-2，不额外暂停等待。

---

# 28. BB-1 Milestone Commit

BB-1 ACCEPTED 后独立 commit。

建议：

```text
refactor(broadband): establish broadband execution model and reuse naming
```

该 commit 是项目历史中的主要 architecture anchor。

报告中必须记录：

```text
pre-milestone SHA
milestone commit SHA
```

可在最终整体验收后决定是否增加 milestone tag。

---

# 29. BB-2 — Documentation & Terminology Audit

BB-1 ACCEPTED 后进入。

目标：

> 使当前有效文档全部采用 Broadband execution model，同时完整保存 RayReuse/IGR 历史。

---

# 30. 文档分类原则

所有文档先分类：

## A. Living Documentation

描述“现在项目是什么、现在怎么使用”的文档。

必须更新。

典型包括：

```text
README.md
Bellhop_Broadband/README.md
GUIDE_USAGE.md
GUIDE_BENCHMARKING.md
GUIDE_RELEASE.md
REFERENCE_FEATURE_SUPPORT_MATRIX.md
STATUS_PROGRESS.md
doc/README.md
demo/README.md
current architecture docs
current developer instructions
```

以及任何仍指导当前命令运行的文档。

---

## B. Historical Documentation

包括已经关闭的：

```text
FP-*
RR-*
IGR-0
IGR-1
IGR-2
IGR-3A
IGR-3B
PERF-TRACE-PAR-1
历史 review/report/worklist
```

原则：

> 不重写历史。

历史文件中当时真实存在的：

```text
bellhop_rayreuse
parallel
fused
range-parallel
RayReuse
```

继续保留。

不得把历史报告改写成：

```text
当时已经叫 Frequency Reuse / Range Reuse
```

---

# 31. Historical Terminology Mapping

必要时可在历史文档入口或文档索引增加统一说明：

```text
Terminology after BB-M1:

bellhop_rayreuse
    → bellhop_broadband

legacy execution-mode reuse
    → Serial Reuse

legacy execution-mode parallel
    → Frequency Reuse

legacy execution-mode fused
    → Range Reuse implementation family

legacy fused + range-parallel
    → Range Reuse with reuse-workers > 1
```

该说明用于阅读历史，不修改原始历史结论。

---

# 32. BB-2 Known Documentation Issue

必须专项检查：

> 当前 GUIDE_USAGE 与实际 IGR-3B / fused Arrival implementation 已出现历史滞后。

BB-2 必须重新以 production code 为 source of truth 审阅：

* TL；
* ARR；
* Eigenray；
* R；
* beam family；
* multi-frequency；
* multi-source；
* regular/irregular receiver；
* Serial/Frequency/Range Reuse 支持矩阵。

不得简单复制旧 GUIDE_USAGE 的 fused support 描述。

---

# 33. BB-2 Tasks

## B01 `[STANDARD]` — Living Doc Inventory

列出所有当前有效文档与脚本说明。

分类：

```text
CURRENT
HISTORICAL
OBSOLETE
```

---

## B02 `[ADVANCED]` — Support Matrix Audit

从 production validation/dispatch 出发重新核对：

```text
NonReuse
Serial Reuse
Frequency Reuse
Range Reuse
```

分别支持哪些 product / beam / receiver / source 组合。

文档不能反向决定代码行为。

---

## B03 `[STANDARD]` — Current Documentation Rewrite

统一当前文档：

```text
Bellhop Broadband
NonReuse
RayReuse
Serial Reuse
Frequency Reuse
Range Reuse
Trace workers
Reuse workers
```

删除 current docs 中把：

```text
parallel
fused
range-parallel
```

当作当前用户 execution mode 的描述。

低层实现讨论中可以继续使用 `fused`。

---

## B04 `[ADVANCED REVIEW]` — Historical Integrity Audit

确认：

* 历史报告没有被重写；
* milestone 前术语仍保持原貌；
* 新旧术语映射清楚；
* 当前文档没有引用已经删除的 CLI；
* current support matrix 与 production code 一致。

---

## B05 `[FINAL REVIEW]` — Milestone Closure

最终检查：

```text
Code naming
CLI
Build/install
Current docs
Historical docs
Support matrix
Repository cleanliness
```

结论：

```text
ACCEPTED / CLOSED
```

---

# 34. BB-2 Commit

建议独立 commit：

```text
docs(broadband): align documentation with broadband naming milestone
```

不得把 BB-1 的 production code rename 与 BB-2 大规模历史文档整理压进同一个 commit。

---

# 35. Milestone Closure Report

最终创建：

```text
REPORT_BB_M1_BROADBAND_NAMING_MILESTONE.md
```

至少记录：

## Before

```text
bellhop_rayreuse

execution-mode:
  nonreuse
  reuse
  parallel
  fused

parallel controls:
  trace-workers
  workers
  range-parallel
```

## After

```text
bellhop_broadband

execution-mode:
  nonreuse
  reuse

reuse-mode:
  serial
  frequency
  range

parallel controls:
  trace-workers
  reuse-workers
```

并记录：

* baseline SHA；
* BB-1 commit；
* BB-2 commit；
* tests；
* output parity；
* support matrix audit；
* protected references status；
* final reviewer verdict。

---

# 36. Explicit Non-Goals

本里程碑不得顺带开展：

* 8 路线性能比较；
* worker auto tuning；
* execution auto-selection；
* default mode 改为 reuse；
* nested parallelism；
* trace/reuse pipeline overlap；
* memory architecture optimization；
* new fused algorithm；
* frequency interpolation；
* new beam support；
* support matrix expansion；
* scientific threshold change；
* C++ namespace 全局迁移；
* repository rename。

发现相关优化机会只记录：

```text
FOLLOW-UP
```

不得扩大 BB-M1 scope。

---

# 37. 后续路线

BB-M1 完成后，可独立启动：

```text
BB-PERF-1 — Broadband Execution Combination Benchmark
```

对比 8 类逻辑组合：

```text
NonReuse × Trace serial/parallel

Serial Reuse × Trace serial/parallel

Frequency Reuse × Trace serial/parallel

Range Reuse × Trace serial/parallel
```

后续再研究：

* wall time；
* trace time；
* influence time；
* RSS；
* scaling；
* worker efficiency；
* frequency count scaling；
* receiver-range scaling；
* 最佳默认 execution strategy。

该研究不得提前进入 BB-M1。

---

# 38. Definition of Done

本 milestone 只有在以下条件全部满足时才可 `CLOSED`：

* [ ] executable 为 `bellhop_broadband`
* [ ] 产品身份为 Bellhop Broadband
* [ ] `execution-mode` 仅有 `nonreuse|reuse`
* [ ] 新增 `reuse-mode=serial|frequency|range`
* [ ] `trace-workers` 对 NonReuse 与 RayReuse 都有效
* [ ] `trace-workers` 默认 1
* [ ] `reuse-workers` 统一 Frequency/Range worker 参数
* [ ] old `parallel/fused` 不再是顶层 execution modes
* [ ] `--range-parallel` 删除
* [ ] old generic `--workers` 删除
* [ ] Serial/Frequency/Range solver 顶层命名完成
* [ ] low-level fused terminology 未被错误清除
* [ ] scientific algorithms 未改变
* [ ] existing products/output semantics preserved
* [ ] current scripts/tests 已迁移
* [ ] BB-1 Final Review `ACCEPTED`
* [ ] living docs 完成新术语迁移
* [ ] support matrix 已按 production code 复核
* [ ] historical docs 未被伪造性重写
* [ ] BB-2 Final Review `ACCEPTED`
* [ ] `Bellhop_origin` 未修改
* [ ] `Bellhop_F2CPP` 未修改
* [ ] working tree clean
* [ ] milestone report 完成

最终状态：

```text
BB-M1 — ACCEPTED / CLOSED
```

此节点之后，项目正式使用：

> **Bellhop Broadband**

作为产品级名称，并使用：

> **NonReuse / RayReuse → Serial / Frequency / Range**

作为宽带计算执行模型的标准术语。
