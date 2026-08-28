# AGENTS.md

## 1. 项目目标与优先级

本仓库用于 Bellhop 功能复刻、数值一致性验证、性能优化和宽带射线复用研究。

优先级：

1. 用户明确要求的功能与研究目标；
2. 相对 Bellhop Origin 的科学正确性与文件兼容性；
3. 运行时间、内存与可扩展性；
4. RayReuse 研究路线所需架构；
5. 防止重要回归的最小充分测试；
6. 必要的易用性、展示和文档。

默认采用“最小充分工程”。测试、CI、抽象、重构和文档服务于功能、正确性与性能，
不得主动把任务扩大为通用框架或大规模工程建设。

## 2. 目录职责

| 路径 | 职责 |
|---|---|
| `Bellhop_origin/` | Fortran 单频参考实现和首要 oracle |
| `Bellhop_F2CPP/` | 独立 C++20 二维单频复刻与优化 |
| `Bellhop_RayReuse/` | 独立 C++20 多频轨迹复用实现 |
| `test/standard_cases/` | Origin/F2CPP/RayReuse 共享标准算例与比较 |
| `test/PlotRead/` | 独立 SHD 读取、绘图和测试 |
| `demo/` | 可用性、横向对比和多频展示 |
| `test/legacy/` | 历史材料，不参与当前测试 |

约束：

- Origin 默认不修改数值语义。
- F2CPP 每次只处理一个频率。
- RayReuse 面向同一环境多频计算，验证 `nonreuse/reuse/parallel` 一致性和性能收益。
- F2CPP 与 RayReuse 保持独立源码、CMake 和可执行文件；除非用户要求，不自动同步。
- 不恢复 `test/legacy/` 的测试职责。

## 3. 通用工作规则

- 用户可见说明、项目文档默认中文；代码标识符和既有英文技术内容保持原风格。
- 先检查代码、文档、算例或日志，再下结论。
- 保留用户已有修改，不触碰无关文件，不执行破坏性 Git/文件操作。
- 除非用户明确要求，不提交、不推送、不创建 PR。
- 涉及科学语义、兼容范围、关键接口或显著性能取舍时，不自行扩大假设。
- 优先局部、渐进修改；避免无关重构、格式化和推测性未来功能。

# 4. Agent 工作流

核心原则：**使用最少必要 Agent，按风险升级，不机械运行完整流水线。**

## 4.1 风险等级

### `[SIMPLE]`

适用：局部机械修改、小 bug、配置/文档、低风险单文件修改。

```text
coordinator 或 worker → 窄测试 → DONE
```

默认不调用 architect、scout、reviewer、final-reviewer。

### `[STANDARD]`

适用：普通 parser/model 接线、writer、CLI、runtime path、标准算例和边界清晰的新功能。

```text
worker → 相关测试 → reviewer? → DONE
```

reviewer 仅在跨模块、兼容性、较大 diff 或明显遗漏风险时调用。

### `[ADVANCED]`

适用：数值算法、科学语义、复杂架构、ownership/lifetime、cache consistency、
concurrency、同步、多频状态、复杂性能优化和疑难 regression。

```text
advanced-worker → 测试/oracle/benchmark → reviewer → DONE
```

`[ADVANCED]` 必须使用 advanced-worker 和独立 reviewer。

## 4.2 Feature Batch

非平凡 feature batch 默认：

```text
architect ×1
→ Worklist
→ 各 SIMPLE/STANDARD/ADVANCED 工作项
→ final-reviewer ×1
```

architect 和 final-reviewer 按 batch/phase 调用，不按 work item 重复调用。

禁止默认采用：

```text
architect → scout → worker → reviewer → final-reviewer
```

作为每个工作项的固定流程。

## 4.3 Agent 职责

### scout
- 仅在目标文件、调用链、已有实现或测试位置不明确时调用。
- 只做搜索和事实定位，不做架构决策、施工或验收。
- 输出只保留关键文件、符号、调用链和结论。

### architect
- 负责 batch/phase 的范围、架构、科学/数值语义、关键接口、性能取舍和 Worklist。
- 默认高能力模型，原则上不修改 production code。
- Worklist 已明确时不重复调用。
- 只有设计假设被推翻、范围显著变化或 reviewer 要求重设计时重新调用。

### worker
- 负责 `[STANDARD]`，也可承担 `[SIMPLE]`。
- 严格围绕 acceptance criteria 施工，不主动扩大范围。
- 遇到科学语义、复杂架构、cache/concurrency 等问题时停止并请求升级。

### advanced-worker
- 负责 `[ADVANCED]`。
- 默认高能力模型。
- 重要数值/性能修改必须给出测试、oracle 或 benchmark 证据。

### reviewer
- 独立只读审查，默认只看：
  1. 当前 acceptance criteria；
  2. 当前工作项 git diff；
  3. 相关测试/oracle/benchmark；
  4. 判断 diff 必需的局部源码。
- 是窄范围 diff reviewer，不是第二个 worker。
- `[SIMPLE]` 默认不用；`[STANDARD]` 按需；`[ADVANCED]` 必须。
- 无问题输出 `PASS`；有问题只列 actionable findings。

### final-reviewer
- 每个 batch/phase 最终调用一次。
- 检查 Worklist、最终 diff、reviewer findings、测试/oracle/benchmark 和必要的高风险源码。
- 不无差别重读整个模块。
- 结论只能为 `ACCEPTED` 或 `CHANGES_REQUIRED`。

### coordinator
- 负责风险判断、调度、Worklist 状态、结果汇总和 findings 修复。
- 可以直接完成 `[SIMPLE]`。
- 不为体现多 Agent 而创建无收益子任务。
- `[ADVANCED]` 必须交给 advanced-worker，不自行降级。
- 仅在确有信息缺口时调用 scout。

## 4.4 Worklist

非平凡 batch 使用：

```text
Bellhop_RayReuse/doc/worklists/<BATCH>_WORKLIST.md
```

Worklist 是执行期唯一权威状态源。每项只保留：

```md
### A03 [ADVANCED]
Status: DONE
Reviewer: PASS

Acceptance:
- ...

Evidence:
- tests/oracle/benchmark: ...
```

平台切换时优先读取：

1. `AGENTS.md`
2. 当前 Worklist
3. `git status`
4. 必要的 `git diff` / `git log`
5. 相关代码与测试

不依赖聊天上下文维持长期项目状态。

WorkReport 默认不创建；只用于 phase closeout、冻结验证、重要性能阶段或用户明确要求。

## 4.5 Agent 上下文与输出预算

所有 Agent 使用“最短充分上下文、最短充分报告”。

- 不默认重新理解整个仓库。
- 已写入 Worklist 的背景和决策不重复推导。
- scout 不写项目综述。
- worker 只报告修改、结果、测试和 blocker。
- reviewer 只报告 `PASS` 或 findings。
- final-reviewer 只报告结论、关键证据和 findings。
- 不重复粘贴完整代码、diff、测试日志或项目背景。

## 5. Pi 配置保护

`.pi/settings.json` 中 provider、model、thinking、modelScope 属于用户维护配置。

任何 Agent 不得自行：

- 修改 defaultProvider/defaultModel；
- 修改 subagents.defaultModel；
- 修改 agentOverrides 中的 model/thinking；
- 修改 modelScope；
- 为绕过 provider/network/auth/runtime 错误自行换模型。

遇到上述运行问题，停止相关任务并报告 `AGENT_RUNTIME_BLOCKER`。只有用户明确授权后
才能修改模型路由。

## 6. Bellhop / RayReuse 开发契约

典型功能完成条件：

1. 真实输入可端到端运行；
2. 代表性输出与 Origin 在声明语义和容差内一致；
3. 相关既有回归通过；
4. 无已知严重正确性问题或明显性能退化；
5. 关键限制和设计决策已简洁记录。

F2CPP 与 RayReuse 保持：

- 频率无关的冻结射线路径和反射事件；
- 幅度、相位、复走时、反射系数和压力属于逐频状态；
- 避免全局“当前频率”和不可控共享可变状态；
- 几何追踪、逐频投影、Influence 和输出可独立验证与计时。

## 7. 测试与性能

验证强度与风险相称。优先运行最相关的窄测试，不机械执行全部昂贵组合。

普通新功能默认只需要：

- 一个最小可运行案例；
- 一个代表性 Origin 对比；
- 必要时一个重要边界案例。

只有在真实缺陷、脆弱数值语义、核心优化路径或现有测试无法覆盖重要失败时增加回归。

常用入口：

```bash
uv run ctest --test-dir Bellhop_F2CPP/build/release --output-on-failure
uv run ctest --test-dir Bellhop_RayReuse/build/release --output-on-failure

uv run make -C test/standard_cases test-unit
uv run make -C test/standard_cases test VERSION=f2cpp CASE=<case> PROFILE=single
uv run make -C test/standard_cases batch

uv run make -C test/PlotRead test
uv run make -C demo test
uv run make -C demo all
uv run make -C demo rayreuse-multifrequency
```

性能优化先 profile/benchmark，再优化热点；优化后同时检查代表性数值结果、运行时间，
必要时检查峰值内存和并行扩展性。

Python 默认使用仓库根目录 uv 环境：

```bash
uv sync
uv run pytest
```

不要硬编码 `.venv`、Conda 或 MATLAB 运行依赖。

## 8. 完成定义

除非任务另有要求，满足以下条件即可完成：

- 用户要求的功能真正可用；
- 代表性 Origin 对比通过；
- 相关回归通过；
- 无已知严重正确性问题或明显性能退化；
- 重要设计决定和限制已记录；
- 未引入无关重构、测试矩阵或基础设施。

若某项测试、抽象、重构或文档不会实质提升功能完整性、科学可信度、性能或研究路线，
则默认推迟。
