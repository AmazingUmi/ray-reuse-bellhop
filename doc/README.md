# 项目文档索引

本目录保存跨 Origin、F2CPP 和 RayReuse 的项目级设计、稳定参考、当前工作和
历史过程。具体组件的构建、使用、状态与验证证据由各组件自己的 `doc/README.md`
管理。

## 分类规则

| 目录 | 内容 | 时效 |
|---|---|---|
| [`architecture/`](./architecture/) | 跨组件架构、算法映射与 Ray-Reuse 总体设计 | 设计基线，按架构变更更新 |
| [`reference/`](./reference/) | 理论推导、变量、单位和数值契约 | 稳定参考 |
| [`plans/`](./plans/) | 已批准但尚未完成的当前工作 | 只保留活动项 |
| [`archive/`](./archive/) | 已完成计划、阶段清单和历史过程 | 只供追溯，不作为当前待办 |

组件内部统一采用同样语义：`guides/` 是可操作说明，`reference/` 是支持矩阵或
契约，`status/` 是当前状态，`reports/` 是带基线身份的验证证据，`decisions/`
是已冻结决策，`archive/` 是完成或被替代的过程材料。

## 从这里开始

- 当前工作与待决事项：[`plans/CURRENT_WORK.md`](./plans/CURRENT_WORK.md)
- 总体设计：
  [`architecture/Bellhop源码分析与宽带复用设计.md`](./architecture/Bellhop源码分析与宽带复用设计.md)
- 射线理论：
  [`reference/射线轨迹方程以及动态射线追踪方程推导.html`](./reference/射线轨迹方程以及动态射线追踪方程推导.html)
- 数值规范：
  [`reference/基础变量单位与数值规范.md`](./reference/基础变量单位与数值规范.md)
- 历史项目实施清单：
  [`archive/项目实施清单-2026-08-14.md`](./archive/项目实施清单-2026-08-14.md)

## 组件文档

| 组件 | 文档入口 | 当前角色 |
|---|---|---|
| Origin | [`Bellhop_origin/doc/README.md`](../Bellhop_origin/doc/README.md) | Fortran 单频行为 oracle |
| F2CPP | [`Bellhop_F2CPP/doc/README.md`](../Bellhop_F2CPP/doc/README.md) | 独立 C++20 二维单频实现，功能已封板 |
| RayReuse | [`Bellhop_RayReuse/doc/README.md`](../Bellhop_RayReuse/doc/README.md) | 独立多频轨迹复用实现，等待下一研究目标 |
| PlotRead | [`test/PlotRead/README.md`](../test/PlotRead/README.md) | SHD 读取、绘图和导出 |
| 标准算例 | [`test/standard_cases/README.md`](../test/standard_cases/README.md) | 三模型共用运行与比较框架 |
| 展示 | [`demo/README.md`](../demo/README.md) | 可靠性与多频展示 |

## 维护规则

1. `plans/` 只放尚未完成且已获批准的工作；完成后移入对应 `archive/`。
2. 当前可用能力以组件 `reference/` 为准，当前施工状态以 `status/` 为准。
3. 可重复操作放 `guides/`；一次性验证结果放 `reports/`，并记录日期、提交或
   可执行文件身份。
4. 冻结报告不因目录整理改写数值内容；被新报告替代时整体移入 `archive/`。
5. 子系统 README 与测试说明继续靠近代码，不为追求集中而搬离其使用位置。
6. 新文档必须从本索引或所属组件索引可达，并使用相对链接。
