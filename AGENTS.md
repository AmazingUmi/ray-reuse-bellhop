# AGENTS.md

## 1. Project Principles

本仓库用于 Bellhop 功能复刻、数值一致性验证、RayReuse 宽带复用与性能研究。

优先级：

1. 用户目标；
2. 科学/数值正确性；
3. Origin / F2CPP 兼容；
4. RayReuse cache / ownership / frequency-local 契约；
5. 功能、性能与内存目标；
6. 最小充分测试与文档。

默认原则：

* 目标优先，不扩大 scope；
* 不做无关重构；
* 不为增加测试覆盖率阻塞核心施工；
* 先检查真实代码、测试、Worklist 和 Git 状态；
* 已批准的工作应连续执行，不主动暂停等待确认。

---

## 2. Repository Boundaries

| Path                   | Role                           |
| ---------------------- | ------------------------------ |
| `Bellhop_origin/`      | Fortran scientific oracle      |
| `Bellhop_F2CPP/`       | C++ single-frequency reference |
| `Bellhop_RayReuse/`    | RayReuse production / research |
| `test/standard_cases/` | shared cases / oracle          |

默认：

* production 修改集中在 `Bellhop_RayReuse/`；
* `Bellhop_origin/`、`Bellhop_F2CPP/` 只读；
* 必要时可修改共享测试和文档；
* 不修改无关文件；
* 未授权不 push；
* 不执行破坏性 Git 操作。

---

## 3. Batch Workflow

所有非平凡研发任务按 Batch 执行，例如：

```text
FP-*    feature parity
IGR-*   influence geometry reuse
PERF-*  performance
EXP-*   experiment
```

标准流程：

```text
DESIGN
→ FREEZE
→ CONSTRUCT
→ CHECKPOINT
→ BATCH ACCEPTANCE
→ FINAL REVIEW
```

若 Final Review：

```text
ACCEPTED
→ Batch CLOSED
→ 自动进入下一已批准 Batch

CHANGES_REQUIRED
→ REMEDIATION
→ RE-VALIDATION
→ FINAL REVIEW
```

硬规则：

* 当前 Batch 未 ACCEPTED，不进入下一 Batch；
* 下一 Batch 已批准时，ACCEPTED 后自动继续；
* task / checkpoint 完成后也默认继续；
* 不因阶段完成主动暂停等待用户确认。

仅以下情况停止：

* 用户明确要求暂停；
* 出现真实 blocker；
* 需要新的授权；
* 后续工作未获批准；
* frozen design 的核心假设失效。

---

## 4. Agents

模型选择原则：

scout / worker
→ 优先使用低成本、高性价比模型

reviewer
→ 默认使用中高等级模型；高风险数值/架构审查使用高级模型

architect / advanced-worker / final-reviewer
→ 必须优先使用高级推理模型

若无法按照上述原则选择subagent所模型，则可继承主agent的模型选择，以继续任务。

### coordinator

负责：

* Batch 状态；
* scope；
* Agent 调度；
* Worklist；
* checkpoint；
* Batch Acceptance；
* remediation；
* Git scope；
* 自动推进。

有明确下一步时继续执行，不主动暂停。

### scout

只读定位：

* files / symbols；
* call chain；
* Origin / F2CPP reference；
* tests / oracle。

只输出事实。

### architect

非平凡 Batch DESIGN 阶段使用。

负责：

* architecture；
* scientific semantics；
* ownership / lifetime；
* data / memory / concurrency model；
* Worklist；
* acceptance gates。

默认不修改 production。

只有设计失效或 scope 显著变化时重新调用。

### worker

处理 `[SIMPLE]` / `[STANDARD]`：

* parser / CLI / writer；
* wiring；
* manifest / docs；
* 机械迁移；
* 普通测试；
* 低风险优化。

遇到数值、cache、ownership、concurrency 等问题时升级。

### advanced-worker

处理 `[ADVANCED]`：

* 数值算法；
* influence / geometry；
* dynamic ray；
* cache；
* ownership / lifetime；
* multi-frequency state；
* concurrency；
* fused execution；
* memory / performance hot path；
* complex regression；
* 非平凡 remediation。

输出：

```text
changes
evidence
remaining risk
blocker
```

### reviewer

独立只读审查。

```text
SIMPLE    → 默认不用
STANDARD  → 按需
ADVANCED  → 风险驱动
```

以下必须独立 review：

* scientific / numerical semantics；
* frozen cache；
* ownership；
* concurrency；
* frequency state；
* output compatibility；
* 可能改变结果的性能优化。

纯 loop restructuring、buffer reuse、allocation reduction 等可集中到 checkpoint review。

输出：

```text
PASS
```

或 actionable findings。

### final-reviewer

每个 Batch 最终独立验收。

结论只能：

```text
ACCEPTED
CHANGES_REQUIRED
```

Final Review finding 必须修复后再次交 final-reviewer。

---

## 5. Risk Levels

### `[SIMPLE]`

文档、配置、manifest、机械小改。

```text
worker/coordinator
→ minimal validation
```

### `[STANDARD]`

parser、CLI、writer、普通 runtime wiring、低风险优化。

```text
worker
→ targeted validation
→ reviewer if needed
```

### `[ADVANCED]`

数值算法、cache、ownership、concurrency、multi-frequency、geometry、fused execution、高风险性能路径。

```text
advanced-worker
→ targeted evidence
→ reviewer by risk
```

---

## 6. Worklist

每个非平凡 Batch 使用：

```text
Bellhop_RayReuse/doc/worklists/<BATCH>_WORKLIST.md
```

最小格式：

```md
### A01 [ADVANCED]
Status: TODO | ACTIVE | DONE
Reviewer: N/A | PASS

Goal:
- ...

Acceptance:
- ...

Evidence:
- ...
```

Worklist 只记录：

* scope；
* frozen decisions；
* task；
* acceptance；
* evidence；
* blocker / finding。

不要复制聊天历史或长日志。

---

## 7. Context / Token Budget

默认读取顺序：

```text
AGENTS.md
→ current Worklist
→ git status
→ current diff
→ necessary source/tests
```

不要默认：

* 扫描整个仓库；
* 重述完整历史；
* 每 task 跑 full regression；
* 输出完整 diff / log；
* 让多个 Agent 重复分析同一问题。

---

## 8. Validation Strategy

原则：

```text
测试服务于风险和目标，而不是测试数量。
```

Task 阶段只运行：

* targeted tests；
* targeted oracle；
* targeted regression；
* 必要 parity。

不要机械执行 full suite。

Batch Acceptance 集中执行完整验证，并按 Batch 需要加入：

* Origin / F2CPP parity；
* execution parity；
* byte identity；
* cache fingerprint；
* worker-count parity；
* benchmark；
* memory evidence。

性能任务优先顺序：

```text
实现目标
→ correctness
→ targeted evidence
→ benchmark / memory
→ full acceptance
```

---

## 9. RayReuse Core Contracts

### Frozen geometry

```text
RayPath / RayPathCache
→ frequency-independent
→ read-only after trace
```

禁止把 frequency-local 或 worker-local mutable state 写回 frozen cache。

### Frequency-local

默认包括：

* amplitude；
* phase；
* attenuation；
* complex travel time；
* reflection result；
* active prefix；
* influence state；
* Arrival / Eigenray state。

```text
FrequencyProjector:
RayPath + frequency
→ RayFrequencyState
```

### Fused execution

允许共享：

* frozen geometry；
* geometry traversal；
* read-only candidate structure。

必须保持：

* 每频 acoustic state 独立；
* 每频 eligibility 独立；
* output semantics 独立；
* frozen geometry 不变。

### Parallelism / Memory

原则：

```text
shared read-only data
+ worker-local mutable workspace
+ deterministic output
```

避免不必要的：

```text
O(Ns × Nf × geometry)
```

复制。

---

## 10. Findings / Remediation

以下必须进入 remediation：

* reviewer finding；
* Final Review `CHANGES_REQUIRED`；
* Batch Acceptance failure；
* oracle / regression mismatch；
* performance / memory blocker。

复杂 production 问题优先 advanced-worker。

修复后：

```text
remediation
→ targeted validation
→ original reviewer re-check
```

不得用“已修复”直接关闭 finding。

---

## 11. Git Rules

检查：

```text
git status
git diff --check
reference implementations untouched
no unrelated files
no generated products
```

推荐一个 ACCEPTED Batch 一个 commit。

禁止默认：

```bash
git add .
git add -A
```

未授权不得：

* push；
* force push；
* reset --hard；
* clean -fd；
* 修改 Origin / F2CPP。

---

## 12. Definition of Done

Batch 完成要求：

* 声明目标已实现；
* scientific / numerical semantics 正确；
* 必要 parity / regression 通过；
* cache / ownership / frequency-local 契约正确；
* concurrency / memory 行为合理；
* performance claim 有证据；
* findings 已闭环；
* Batch Acceptance PASS；
* final-reviewer ACCEPTED；
* 文档不 overclaim。

完成状态：

```text
<BATCH> ACCEPTED
```

随后：

```text
下一 Batch 已批准
→ 自动继续

没有已批准 Batch
→ STOP
```
