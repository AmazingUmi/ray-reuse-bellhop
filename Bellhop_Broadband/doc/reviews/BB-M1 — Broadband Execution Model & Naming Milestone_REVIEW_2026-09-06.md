# BB-M1 — Broadband Execution Model & Naming Milestone Review

**Status:** `RECONCILED DESIGN / PRE-CONSTRUCTION`（2026-09-06）
**Milestone type:** Architecture / Naming / CLI Breaking Change
**Repository:** `AmazingUmi/ray-reuse-bellhop`
**Current development branch:** `feat/igr-influence-geometry-reuse`
**Implementation baseline:** construction 开始前记录实际 `HEAD` SHA
**Protected references:** `Bellhop_origin/`、`Bellhop_F2CPP/` 不得修改

本次核对基线为 `a3f20d8c0d8ac207766fa0adb083de3526f74fd7`（初始工作树干净）。
第 1–24 节描述 BB-M1 的目标契约，不是当前实现完成声明；差异与逐节证据见
[正式里程碑 Worklist](../worklists/BB-M1_WORKLIST.md)。实施任务和验收以
[BB-1](../worklists/BB-1_WORKLIST.md) 与 [BB-2](../worklists/BB-2_WORKLIST.md)
为执行依据；下文第 25–38 节为同步后的里程碑说明，不另建重复 gate。
本次用户授权为审阅、修订和生成 Worklist，尚未启动 production construction。

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

历史 IGR-2/IGR-3 将 fused 定位为支持域内的 production 路线，并将旧
reuse/parallel 保留为带条件弃用告警的 compatibility 路线。BB-M1 将三条路线
作为可显式选择的策略，是新的产品组织决策；不改写历史结论，也不宣称三者性能等价。
新 CLI 应去掉旧 `warnIfReplaceableLegacyMode` 的弃用提示及 help 中的旧推荐命令。

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

上图适用于需要逐频声学计算的产品，或是作为对比基线；普通 R 仅 trace/write，不含 Projection/Influence。
单频 TL 保留 `SingleFrequencySolver` 路径，不能为了新模型强制进入宽带 solver。

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

这不是新增 trace 能力：PERF-TRACE-PAR-1 已将同一 trace seam 接入所有合法
TL/ARR/Eigenray/R 路径。复用阶段只适用于产品本来支持的路线。

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

N 是 requested count；effective count 沿用 `min(N, launchCount)`，不承诺
始终创建 N 个 worker。保留正整数校验、serial fast path 和已有异常/诊断语义。

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
→ receiver-range partition（single worker 覆盖全部 range）
→ 每个 range worker 逐 ray、逐频独立 Projection
→ worker 在所属 range 上执行 cross-frequency fused Influence
```

结构：

```text
Frozen RayPathCache（只读共享）
        ↓ static contiguous receiver-range partition
 ┌──────┼──────┬──────┐
worker0 worker1 worker2 worker3
 各自 Projection + fused Influence，写入不相交的 range
```

不得把该示意实现为一次全局 fused projection 后再分块；当前 projector 仍逐频
产生独立 `RayFrequencyState`，相同投影可能在不同 range worker 内重复。

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

冻结规则：任何显式 `--reuse-workers N` 均拒绝，包括 `N=1`，与第 21 节一致。

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

worker 数表示请求值；Frequency 继续受频率数及原 TL memory budget 约束，
Range 继续 clamp 到 receiver range 数，不保证实际启动数等于请求数。

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

这里的 1 是新 CLI 的资源默认决策，不是旧行为的原样保留：旧 `parallel`
未指定 `--workers` 时使用 `hardware_concurrency()`（不可用时回退 1）；旧
`fused --range-parallel` 默认 4；未开启 range parallel 的旧 fused 为 1。
迁移旧命令若要保留并行资源请求，必须显式写出对应 `--reuse-workers N`。
Serial 的内部默认 1 不代表允许显式指定该参数。

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

这是当前 RayReuse parser 已有扩展：频率记录接受正值、严格递增列表；
不表示 Origin/F2CPP 原生输入也接受该宽带列表，不改动两份 reference parser。

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

这 8 类是合法支持域内的执行形态，不是 product × source × receiver 的全支持矩阵。
普通 R 只有 NonReuse 的两种 trace 形态；单频 TL 不接受显式 reuse；
Eigenray 不支持 Range；Range TL 与 ARR 的 source 支持不同，见第 22 节。

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

当前要求在各路线原有 source/cache 生命周期内保持阶段顺序：

```text
Trace complete
→ Reuse begin
```

禁止把它加强为新的全局 all-source barrier。特别是 IGR-3B ARR 保持
`trace source i → all-frequency accumulation → source consumer → next source`；
其他路线继续保留其原有 caches 集合和 writer 生命周期，不统一重排或复制。

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

未显式指定旧 `--workers` 的命令不保证资源默认等价；第 8 节列出默认变化。
输出对比使用相同的显式 trace/reuse worker 请求。

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

还必须经过产品层校验：上述 queue/memory/frequency-task profiling 仅支持 TL，
不能因为 ARR/Eigenray 支持 Frequency 路线就开放这些参数。`--profile-influence`
仍仅适用 Cartesian Cerveny TL；`--verify-cache` 保留原有适用域。

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

同步迁移：

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

产品级身份与路线诊断文字按新命名更新；不修改 Origin-compatible 科学产品格式。
SHD/ASCII ARR/binary ARR/RAY 及文件命名、发布顺序保持原路线等价；PRT/stderr
只允许身份、路线/参数名称、已声明的 worker 默认值、旧弃用告警及运行时间差异。
不得要求改名后的 PRT 与旧 PRT 全文件 byte identity，也不得用忽略整份 PRT 掩盖差异。

---

# 16. 项目目录命名

原提案建议同步目录迁移；本次为减少路径 churn，冻结为本 milestone 保留：

```text
Bellhop_RayReuse/
```

原因：

产品身份由 executable/CMake/package/help/当前文档表达；历史目录名不限制产品架构。
目录迁移留待独立授权，不作为 BB-M1 gate；正式 Worklist 位于本目录下的 `doc/worklists/`。

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

上述四个 solver 目标名作为路线命名表；相关 result/statistics/settings 同前缀迁移。
`ArrivalSolver`/`EigenraySolver` 是产品类，保留类名，其 `solve`/`solveNonReuse`/
`solveParallel` 等入口按新 dispatch 对接；`SingleFrequencySolver` 与公共
`RayFanTraceSettings::workerCount` 不作全局机械重命名。
`supportsFusedRayReuse` 当前只判断 TL，改名后也不得用它统一判定 ARR 支持。

必须满足：

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

以上移除只针对 CLI options 旧路线字段；内部 trace/frequency/range settings 的
worker 字段与并行实现不受机械替换。range 开关由新路线和请求数映射到现有实现。

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
--reuse-workers N（包括 N=1）
```

必须拒绝。

两个 worker 参数均只接受正整数；Frequency/Range 的 `N=1` 保持所选路线。

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

已核对的关键边界（仍叠加 `SimulationCase` 各 beam/run-mode 的合法性）：

| 产品 | 当前支持与迁移约束 |
|---|---|
| TL | 单频走 SingleFrequencySolver；多频有四条旧路线。Range 仅单 source、规则网格；Cerveny 与 GeoHat 两坐标系、Cartesian GeoGaussian、SimpleGaussian 的各自合法 TL 模式；SimpleGaussian 仅 coherent。 |
| A/a | NonReuse/Serial/Frequency 保持现状；Range 需至少 2 频率、规则网格、G/g/B，支持多 source 的 source-streamed output。 |
| Eigenray | NonReuse/Serial/Frequency；Range 拒绝。 |
| R | 仅单频 trace/write；显式 reuse 各路线拒绝，trace-workers 合法。 |

依据：`app/main.cpp::validateProductOptions`、各 solver/SimulationCase gate，
以及 IGR-3A/3B、PERF-TRACE-PAR-1 已关闭 Worklist。不能把 Range TL 的单 source
限制套到 ARR，也不能将 ARR 的多 source 能力扩展给 TL。
Range TL/ARR 的规则网格还要求至少两个等距 range；不得只检查 `!isIrregular()`。

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

> **Rename + dispatch refactor + 已声明的 CLI 默认/校验变化；科学数值算法冻结。**

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
* 修改各路线已有异常传播规则（trace 的最低 launch-index failure 与 ARR/E
  frequency workers 的首先捕获异常规则分别保留，不宣称全部路线已有统一确定性）。

只允许：

> 旧 worker 参数 → 新 worker 参数的语义映射，以及第 8 节明确列出的资源默认变化。

不修改已有调度、clamp、每频状态隔离、cache freeze、source streaming 或发布语义；
新默认改变请求 worker 数，不构成重设计 scheduling algorithm 的授权。

---

# 25. BB-1 Implementation Tasks

正式任务与状态以 [BB-1 Worklist](../worklists/BB-1_WORKLIST.md) 为准。
此处只列映射，不另设同名任务或重复 gate。

| 任务 | 风险 | 内容 |
|---|---|---|
| A01 | ADVANCED | construction baseline、rename 清单和最小验证 fixture freeze |
| A02 | STANDARD | 产品身份、构建/install/package、活跃脚本/测试调用迁移 |
| A03 | ADVANCED | CLI/options、solver 命名、产品 dispatch、resource/profiling 参数映射 |
| A04 | ADVANCED | 集中独立 checkpoint review；findings 原 reviewer re-check |
| A05 | ADVANCED | 一次 Batch Acceptance，汇总最小 V1–V5 |
| A06 | ADVANCED | 独立 final review 与 remediation 闭环 |

名称严格使用第18节四 solver 目标名；保留产品类、低层 fused 术语和
`Bellhop_RayReuse/` 目录。trace settings 注入所有原有合法产品路径；
不统一重排 source/cache 生命周期。

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

BB-1 acceptance 集中执行一次 release full CTest，并运行实际改动的 Python
runner/demo 测试。若既有 gate 已覆盖同一构建，不重复执行；不默认叠加 debug、
isolated release、全仓 pytest、全 Origin/F2CPP matrix 或性能矩阵。
CLI、route mapping、trace、cache 证据合并采集，具体最小集合见正式 BB-1 Worklist。
BB-2 仅文档时不重跑数值回归。

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

> 后续施工已获授权时自动进入 BB-2；本次文档准备不构成施工授权。

---

# 28. BB-1 Milestone Commit

BB-1 ACCEPTED 后可独立 commit；本次仅文档准备，不自动提交。

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
Bellhop_RayReuse/README.md
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
    → Range Reuse with explicit reuse-workers N
      （旧 --workers N 原值；旧未指定时填 4；旧 N=1 仍映射为 1）
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

正式任务与状态以 [BB-2 Worklist](../worklists/BB-2_WORKLIST.md) 为准。

| 任务 | 风险 | 内容 |
|---|---|---|
| B01 | SIMPLE | CURRENT / HISTORICAL / OBSOLETE 清单 |
| B02 | ADVANCED | 按最终 production gate 核对支持矩阵并修正当前文档滞后 |
| B03 | SIMPLE | living docs、新旧术语映射与带日期 closure report |
| B04 | ADVANCED | 集中文档/Git 验证、历史完整性和独立 final review |

final-reviewer 结论只能 `ACCEPTED` 或 `CHANGES_REQUIRED`；findings 修复后
回原 reviewer。通过后由 coordinator 记录 `BB-2 ACCEPTED` 和里程碑 CLOSED。
不在纯文档阶段重复数值回归，也不另设重复的 B05 验收。

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
REPORT_BB_M1_BROADBAND_NAMING_MILESTONE_YYYY-MM-DD.md
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
* BB-1 commit（未提交则明确记录未提交）；
* BB-2 commit（未提交则明确记录未提交）；
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
* [ ] working tree scope 已核对；未提交工作明确列明（不以 clean 为由删除既有工作或自动提交）
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
