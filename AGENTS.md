# AGENTS.md

## 1. 项目原则

本仓库用于 Bellhop 功能复刻、数值一致性验证、性能优化和 RayReuse 宽带射线复用研究。

优先级：

1. 用户明确目标；
2. Origin/F2CPP 科学语义与文件兼容；
3. RayReuse 正确性与 frozen-cache 契约；
4. 性能、内存与并发；
5. 最小充分测试与文档。

默认采用 **最小充分工程**：

* 不主动扩大 scope；
* 不做无关重构；
* 不建设无收益的测试、抽象或基础设施；
* 先检查真实代码、测试、算例和 Git 状态，再下结论。

---

## 2. 目录边界

| 路径                     | 职责                            |
| ---------------------- | ----------------------------- |
| `Bellhop_origin/`      | Fortran 首要科学 oracle           |
| `Bellhop_F2CPP/`       | C++20 单频 production reference |
| `Bellhop_RayReuse/`    | 多频轨迹复用 production             |
| `test/standard_cases/` | 三实现共享算例与 oracle               |
| `test/PlotRead/`       | SHD 读取/绘图                     |
| `demo/`                | 展示与横向比较                       |
| `test/legacy/`         | 历史材料                          |

默认约束：

* Feature Parity 施工只修改 `Bellhop_RayReuse/` 与必要的共享测试/文档；
* `Bellhop_origin/`、`Bellhop_F2CPP/` 默认只读；
* 不触碰无关用户配置；
* 不执行破坏性 Git 操作；
* 未获授权不 push。

---

# 3. Feature Batch 状态机

Feature Parity 必须 **一个 Batch 一个 Batch 串行执行**。

标准状态机：

```text
DESIGN
→ FREEZE
→ CONSTRUCT
→ CHECKPOINT
→ BATCH ACCEPTANCE
→ FINAL REVIEW
     │
     ├─ ACCEPTED
     │    → 当前 Batch CLOSED
     │    → 自动进入下一已批准 Batch
     │
     └─ CHANGES_REQUIRED
          → REMEDIATION
          → RE-VALIDATION
          → FINAL REVIEW
          → 循环直到 ACCEPTED
```

硬规则：

```text
当前 Batch 未 ACCEPTED
→ 禁止进入下一 Batch

当前 Batch ACCEPTED
→ 若后续 Batch 已由用户整体批准
→ 自动进入下一 Batch DESIGN
```

禁止：

* 并行设计多个 Feature Batch；
* 当前 Batch 尚未验收就提前施工下一 Batch；
* 用后一 Batch 修复前一 Batch 的问题；
* 将多个 Batch 合并成一个 Worklist 或一次验收。

---

# 4. Agent 职责

## coordinator

主 Agent，负责：

* 当前 Batch 状态；
* 风险分类；
* 调度子 Agent；
* Worklist 更新；
* checkpoint；
* Batch Acceptance；
* findings 路由；
* 复验调度；
* Batch 状态转换；
* Git scope。

coordinator 不应自己承担复杂设计、复杂施工或最终自验。

---

## scout

低成本只读搜索。

仅用于：

* 文件定位；
* symbol/call chain；
* F2CPP/Origin reference；
* 已有 tests/oracle；
* 历史实现定位。

输出只保留事实。

禁止设计、施工和验收。

---

## architect

每个非平凡 Batch 在 DESIGN 阶段默认调用一次。

负责：

* scope；
* architecture；
* scientific/numerical semantics；
* ownership/lifetime；
* migration strategy；
* Worklist；
* acceptance gates。

默认不修改 production。

只有以下情况重新调用：

* 原设计假设失效；
* scope 显著变化；
* 出现 architecture blocker。

---

## worker

负责 `[SIMPLE]` / `[STANDARD]`：

* parser；
* CLI；
* writer；
* manifest/case；
* 普通 model wiring；
* 明确机械迁移；
* 普通 tests；
* docs。

遇到以下问题必须升级：

```text
scientific semantics
numerical algorithm
ownership/lifetime
cache consistency
concurrency
complex regression
```

---

## advanced-worker

负责 `[ADVANCED]`，也是 **验收 finding 的默认修复 Agent**。

适用：

* 数值算法；
* complex geometry；
* source/receiver schema；
* ownership/lifetime；
* cache；
* concurrency；
* multi-frequency state；
* writer/product ownership；
* 难以定位的 regression；
* final-reviewer/reviewer 发现的非平凡问题。

必须给出：

```text
tests / oracle / benchmark
```

作为修复证据。

---

## reviewer

独立窄范围只读审查。

检查：

1. 当前 task acceptance；
2. 当前 task diff；
3. 对应 F2CPP/Origin reference；
4. targeted tests/oracle。

规则：

```text
SIMPLE    → 默认不用
STANDARD  → 按需
ADVANCED  → 必须
```

输出：

```text
PASS
```

或 actionable findings。

如果 reviewer 发现问题：

```text
finding
→ remediation
→ reviewer 再验
```

直到 `PASS`。

---

## final-reviewer

每个 Batch 最终独立验收。

检查：

* frozen Worklist；
* 最终 scope；
* 高风险 production diff；
* checkpoint findings；
* Batch Acceptance；
* tests/oracle；
* cache/ownership；
* Git hygiene；
* 文档声明是否超过证据。

结论只能：

```text
ACCEPTED
CHANGES_REQUIRED
```

若 `CHANGES_REQUIRED`，修复后必须再次调用 final-reviewer。

**不得由 coordinator、worker 或 advanced-worker 自行关闭 Final Review finding。**

---

# 5. 风险等级

## `[SIMPLE]`

例如：

* 文档；
* manifest；
* 明确配置；
* 很小的机械修改。

流程：

```text
worker/coordinator
→ targeted validation
```

---

## `[STANDARD]`

例如：

* parser/model wiring；
* CLI；
* writer；
* standard case；
* 普通 runtime path。

流程：

```text
worker
→ targeted tests
→ reviewer（按需）
```

---

## `[ADVANCED]`

例如：

* 数值算法；
* source/receiver schema；
* ownership/lifetime；
* frozen cache；
* concurrency；
* dynamic ray；
* complex regression；
* 高风险性能路径。

流程：

```text
advanced-worker
→ targeted tests/oracle
→ reviewer
```

---

# 6. Worklist

每个非平凡 Batch 建立独立：

```text
Bellhop_RayReuse/doc/worklists/<BATCH>_WORKLIST.md
```

Worklist 是当前 Batch 的执行期权威状态源。

格式保持简洁：

```md
### A01 [ADVANCED]
Status: TODO | ACTIVE | DONE
Reviewer: N/A | PASS

Acceptance:
- ...

Evidence:
- ...
```

仅记录：

* scope；
* frozen decisions；
* dependencies；
* acceptance；
* evidence；
* blockers/findings。

不要复制大量聊天历史。

---

# 7. 上下文与 Token 预算

默认读取顺序：

```text
AGENTS.md
→ 当前 Batch Worklist
→ git status
→ 当前 task diff
→ 必要源码/tests
```

禁止默认：

* 重读整个 repository；
* 重述项目完整历史；
* 每个 task 都跑 full regression；
* 输出完整 diff/log。

Agent 输出预算：

```text
scout          → facts
worker         → changes + tests + blocker
advanced-worker→ changes + evidence + blocker
reviewer       → PASS/findings
final-reviewer → verdict + critical findings
```

---

# 8. 测试策略

## Task / Checkpoint

只运行当前任务需要的：

```text
component tests
targeted standard case
targeted oracle
targeted regression
```

不要每个 task 都机械执行：

```text
full CTest
full pytest
full standard-case matrix
```

---

## Batch Acceptance

完整验证集中执行一次。

通常：

```bash
uv run ctest --test-dir Bellhop_RayReuse/build/<batch-clean> --output-on-failure
uv run pytest
uv run make -C test/standard_cases test-unit
```

再加当前 Batch 必需的：

```text
Origin oracle
F2CPP parity
execution parity
cache fingerprint
representative frozen regression
```

coordinator 必须亲自抽验关键 gate，不能只引用 worker 报告。

---

# 9. RayReuse 核心契约

默认保持：

```text
RayPath / RayPathCache
→ frequency-independent frozen geometry

amplitude
phase
complex travel time
reflection result
Arrival/Eigenray state
→ frequency-local
```

未经 architect 明确批准，不得：

* 把 frequency-local state 写回 frozen cache；
* 引入 global current frequency；
* 引入不可控 shared mutable state；
* 随意改变 cache ownership；
* 将 F2CPP transient 单频状态直接永久化到 RayReuse cache。

---

# 10. Finding / Remediation 规则

这是强制闭环。

任何：

```text
reviewer finding
final-reviewer CHANGES_REQUIRED
Batch Acceptance failure
```

都必须进入 remediation。

## 默认修复分配

**优先使用 `advanced-worker`。**

以下情况必须使用 advanced-worker：

* production code；
* scientific/numerical semantics；
* architecture/schema；
* ownership/lifetime；
* cache；
* concurrency；
* writer/product dimension；
* parser/runtime 行为存在歧义；
* regression 原因不明确；
* 跨模块问题。

只有问题明显属于：

```text
typo
简单文档修正
manifest/allow-list
明确的机械小修改
Git hygiene
```

才可使用：

```text
worker
```

禁止因为节省额度而将实际 ADVANCED remediation 降级给 worker。

---

## 修复后必须复验

修复完成后：

```text
remediation
→ targeted tests/oracle
→ 原验收角色再次检查
```

如果是：

```text
reviewer finding
```

则：

```text
reviewer 再验
```

如果是：

```text
final-reviewer CHANGES_REQUIRED
```

则：

```text
final-reviewer 再验
```

如果再次发现问题：

```text
advanced-worker/worker 修复
→ 再验
```

循环直到：

```text
PASS / ACCEPTED
```

**不得通过“已修复”报告直接跳过重新验收。**

---

# 11. Batch Acceptance 与进入下一 Batch

当前 Batch 只有在以下条件全部满足后才能：

```text
ACCEPTED
```

条件：

* Worklist 完成；
* Batch Acceptance PASS；
* 所有 checkpoint reviewer PASS；
* final-reviewer ACCEPTED；
* 所有 remediation 已复验；
* 无未关闭 HIGH/BLOCKER；
* 文档 scope 与 oracle evidence 一致；
* Git scope 干净。

达到：

```text
ACCEPTED
```

后：

```text
当前 Batch CLOSED
```

若用户已批准连续处理下一 Batch：

```text
自动进入下一 Batch DESIGN
```

**不需要再次等待用户批准。**

---

# 12. Git 规则

每个 Batch diff 必须独立可识别。

检查：

```text
git diff --check
reference implementation untouched
no unrelated user files
no generated products
```

推荐每个 ACCEPTED Batch 形成独立 commit，但：

```text
下一 Batch 启动条件 = ACCEPTED
不是 COMMITTED
```

如果未提交就进入下一 Batch，coordinator 必须保证两个 Batch diff 和 Worklist 可明确区分。

禁止默认：

```bash
git add .
git add -A
```

未获授权不得 push。

---

# 13. 完成定义

一个 Feature Batch 完成需要：

* feature 在声明范围端到端可运行；
* F2CPP/Origin evidence 通过；
* 相关 regression 通过；
* frozen-cache/ownership 契约满足；
* 文档不 overclaim；
* Batch Acceptance PASS；
* final-reviewer ACCEPTED；
* findings 全部修复并重新验收；
* 无未关闭 HIGH/BLOCKER。

完成状态：

```text
<BATCH> ACCEPTED
```

若已有后续批准 Batch：

```text
自动进入下一 Batch DESIGN
```
