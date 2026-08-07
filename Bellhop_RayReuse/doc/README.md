# Bellhop RayReuse 内部文档

本目录只保存 RayReuse 的实施、验证和工程决策资料。构建与运行方法先看
[`../README.md`](../README.md)；跨工程总体设计仍以仓库级
[`../../doc/00-文档索引.md`](../../doc/00-文档索引.md) 为入口。

## 从这里开始

| 目的 | 文档 |
|---|---|
| 查看当前阶段与验收出口 | [`BUILD_PLAN.md`](./BUILD_PLAN.md) |
| 查看跨编译器历史计划 | [`plans/CROSS_COMPILER_PLAN.md`](./plans/CROSS_COMPILER_PLAN.md) |
| 复现性能测试 | [`guides/BENCHMARKING.md`](./guides/BENCHMARKING.md) |
| 查看当前本地验证基线 | [`reports/LOCAL_VALIDATION_RESULTS_C417095.md`](./reports/LOCAL_VALIDATION_RESULTS_C417095.md) |
| 查看当前三模型数值矩阵 | [`reports/MODEL_MATRIX_RESULTS_06E390F.md`](./reports/MODEL_MATRIX_RESULTS_06E390F.md) |
| 查看 H4 跨编译器结果 | [`reports/CROSS_COMPILER_RESULTS_H4.md`](./reports/CROSS_COMPILER_RESULTS_H4.md) |

当前 A～G、F1/F2 和 H1～H4 矩阵已完成。GNU Fortran/gfortran 是唯一支持
的 Fortran oracle 工具链；不再等待第二套 Fortran 编译器。远端推送、云端
CI 首跑和分支保护等待用户决定，不作为当前本地开发中的活动计划。

## 分类

### guides

- [`BENCHMARKING.md`](./guides/BENCHMARKING.md)：性能采样、分级运行和报告规则；
- [`SINGLE_THREAD_MICROBENCHMARK.md`](./guides/SINGLE_THREAD_MICROBENCHMARK.md)：三模型单线程阶段微基准；
- [`RELEASE.md`](./guides/RELEASE.md)：内部包验证与公开发布前置条件。

### plans

- [`CROSS_COMPILER_PLAN.md`](./plans/CROSS_COMPILER_PLAN.md)：已完成的 H4 C++
  操作计划；第二 Fortran 编译器项已按后续项目决策关闭。

总实施状态仍集中在 [`BUILD_PLAN.md`](./BUILD_PLAN.md)，避免多个活动总计划并存。

### decisions

- [`HDF5_SCHEMA_DECISION.md`](./decisions/HDF5_SCHEMA_DECISION.md)：HDF5 schema 候选与延后实现决策。

### reports

- [`LOCAL_VALIDATION_RESULTS_C417095.md`](./reports/LOCAL_VALIDATION_RESULTS_C417095.md)：H1～H3 本地验证结果；
- [`MODEL_MATRIX_RESULTS_06E390F.md`](./reports/MODEL_MATRIX_RESULTS_06E390F.md)：当前三模型 single/宽带数值矩阵。
- [`CROSS_COMPILER_RESULTS_H4.md`](./reports/CROSS_COMPILER_RESULTS_H4.md)：
  AppleClang/GCC 构建、数值、中间状态、性能与资源结果。

### archive

历史材料用于追溯，不代表当前操作指南：

- [`archive/README.md`](./archive/README.md)：派生记录、旧模型矩阵和性能历史入口；
- [`archive/benchmarks/README.md`](./archive/benchmarks/README.md)：F1/F2 benchmark 报告索引。

## 维护规则

1. `doc/` 根目录只保留本索引和唯一的总实施计划；
2. 可复用操作方法放入 `guides/`，尚未执行的专项计划放入 `plans/`；
3. 已冻结的架构选择放入 `decisions/`，当前有效的验证结果放入 `reports/`；
4. 被新报告取代的结果和阶段性实验记录移入 `archive/`，不删除历史证据；
5. 新文档必须从本索引或所属分类索引可达，并使用相对链接。
