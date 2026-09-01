# 项目文档索引

本目录保存跨 Origin、F2CPP 和 RayReuse 的项目级设计、稳定参考、当前工作和
历史过程。具体组件的构建、使用、状态与验证证据由各组件自己的 `doc/README.md`
管理。

## 分类规则

| 目录 | 内容 | 时效 |
|---|---|---|
| [`architecture/`](./architecture/) | 跨组件架构、算法映射与 Ray-Reuse 总体设计 | 设计基线，按架构变更更新 |
| [`reference/`](./reference/) | 理论推导、变量、单位和数值契约 | 稳定参考 |
| [`plans/`](./plans/) | 当前工作、待决事项和已批准未完成项 | 不保存历史过程 |
| [`archive/`](./archive/) | 已完成计划、阶段清单和历史过程 | 只供追溯，不作为当前待办 |

组件内部统一采用同样语义：`guides/` 是可操作说明，`reference/` 是支持矩阵或
契约，`status/` 是当前状态，`reports/` 是带基线身份的验证证据，`decisions/`
是已冻结决策，`archive/` 是完成或被替代的过程材料。

## 命名规则

手写文档统一使用 ASCII 大写蛇形命名：`CATEGORY_TOPIC.md`。类别前缀采用
`ARCHITECTURE`、`GUIDE`、`REFERENCE`、`PLAN`、`STATUS`、`DECISION`、
`REPORT`、`RECORD` 或 `TASK`；主题应能脱离目录表达具体内容。

报告是时间点证据，文件名末尾必须带 ISO 日期：`REPORT_TOPIC_YYYY-MM-DD.md`；
需要保留提交身份时使用 `REPORT_TOPIC_COMMIT_YYYY-MM-DD.md`。持续更新的指南、
参考、计划和状态文档不加日期，避免每次更新都改变链接。

`README.md` 保留目录入口惯例名。验证器生成的 JSON 等机器证据保留生成器定义的
稳定文件名，不纳入手写文档命名规则；不要仅为文档命名而修改验证接口。

## 从这里开始

- Feature Parity 最终整体验收：
  [`../Bellhop_RayReuse/doc/reports/REPORT_FEATURE_PARITY_FINAL.md`](../Bellhop_RayReuse/doc/reports/REPORT_FEATURE_PARITY_FINAL.md)
- RayReuse 当前支持边界：
  [`../Bellhop_RayReuse/doc/reference/REFERENCE_FEATURE_SUPPORT_MATRIX.md`](../Bellhop_RayReuse/doc/reference/REFERENCE_FEATURE_SUPPORT_MATRIX.md)
- 当前工作与待决事项：[`plans/PLAN_CURRENT_WORK.md`](./plans/PLAN_CURRENT_WORK.md)
- 当前 IGR 架构冻结（Trajectory Reuse → Cross-Frequency Influence Fusion）：
  [`../Bellhop_RayReuse/doc/reports/REPORT_IGR0_REVISION_CROSS_FREQUENCY_FUSION_2026-09-01.md`](../Bellhop_RayReuse/doc/reports/REPORT_IGR0_REVISION_CROSS_FREQUENCY_FUSION_2026-09-01.md)
- IGR 理论与数值契约：
  [`reference/REFERENCE_INFLUENCE_GEOMETRY_REUSE.md`](./reference/REFERENCE_INFLUENCE_GEOMETRY_REUSE.md)
- IGR-1 scope 草案（`NOT APPROVED, NOT IN CONSTRUCTION`）：
  [`../Bellhop_RayReuse/doc/worklists/IGR-1_CC_FUSION_DESIGN_DRAFT.md`](../Bellhop_RayReuse/doc/worklists/IGR-1_CC_FUSION_DESIGN_DRAFT.md)
- 总体设计：
  [`architecture/ARCHITECTURE_BELLHOP_RAY_REUSE.md`](./architecture/ARCHITECTURE_BELLHOP_RAY_REUSE.md)
- 射线理论：
  [`reference/REFERENCE_RAY_DYNAMIC_EQUATIONS.html`](./reference/REFERENCE_RAY_DYNAMIC_EQUATIONS.html)
- 数值规范：
  [`reference/REFERENCE_NUMERICAL_CONVENTIONS.md`](./reference/REFERENCE_NUMERICAL_CONVENTIONS.md)
- 历史项目实施清单：
  [`archive/PLAN_PROJECT_IMPLEMENTATION_2026-08-14.md`](./archive/PLAN_PROJECT_IMPLEMENTATION_2026-08-14.md)

## 组件文档

| 组件 | 文档入口 | 当前角色 |
|---|---|---|
| Origin | [`Bellhop_origin/doc/README.md`](../Bellhop_origin/doc/README.md) | Fortran 单频行为 oracle |
| F2CPP | [`Bellhop_F2CPP/doc/README.md`](../Bellhop_F2CPP/doc/README.md) | 独立 C++20 二维单频实现，功能已封板 |
| RayReuse | [`Bellhop_RayReuse/doc/README.md`](../Bellhop_RayReuse/doc/README.md) | 已完成 F2CPP production Feature Parity 的独立多频轨迹复用实现；无 active FP Batch |
| PlotRead | [`test/PlotRead/README.md`](../test/PlotRead/README.md) | SHD 读取、绘图和导出 |
| 标准算例 | [`test/standard_cases/README.md`](../test/standard_cases/README.md) | 三模型共用运行与比较框架 |
| 展示 | [`demo/README.md`](../demo/README.md) | 可靠性与多频展示 |

## 维护规则

1. `plans/` 只放当前工作和待决事项；候选路线必须明确标为未批准，已完成或
   被替代的过程移入对应 `archive/`。
2. 当前可用能力以组件 `reference/` 为准，当前施工状态以 `status/` 为准。
3. 可重复操作放 `guides/`；一次性验证结果放 `reports/`，并记录日期、提交或
   可执行文件身份。
4. 冻结报告不因目录整理改写数值内容；被新报告替代时整体移入 `archive/`。
5. 子系统 README 与测试说明继续靠近代码，不为追求集中而搬离其使用位置。
6. 新文档必须从本索引或所属组件索引可达，并使用相对链接。
7. `guides/` 和 `status/` 中的工具版本、测试数量与“下一步”必须标明核对日期
   或快照日期；会随测试新增变化的总数不写成永久“当前基线”。
8. `archive/`、dated reports 与 frozen Batch artifacts 保留当时事实；当前状态以
   final report、support matrix 和 progress status 为准，不混用两类语义。
