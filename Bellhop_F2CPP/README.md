# Bellhop F2CPP

本目录用于实现裁剪后的优化版 C++ 单频 Bellhop，负责项目里程碑 M1/M2：

1. 组件级复刻 SSP、中心射线、动态射线、边界反射和 Cartesian Cerveny Influence；
2. 端到端生成与可重现 Fortran 单频 oracle 一致的 coherent complex pressure 和 SHD；
3. 在数值一致性通过后建立单线程性能与峰值内存基线；
4. 产出可独立运行的 `bellhop_f2cpp` 程序；如内部拆分 `bellhop_f2cpp_core`，该目标只服务本工程的程序和测试；
5. 在单频阶段即验证完整轨迹缓存能够脱离追踪器供后续声场计算只读使用。

## 设计边界

- 只实现二维、固定环境/收发位置、Cartesian Cerveny coherent pressure；
- 不移植原 Bellhop 全部运行模式；
- 单频阶段仍使用宽带就绪的 `SimulationCase::frequencies`，但要求其恰含一个频率；
- 必须直接采用 RayReuse 最终需要的数据模型，不允许先使用只够单频计算的临时变量布局；
- `RayPath` 完整保存 `position/slowness/dynamicP/dynamicQ/soundSpeed/realTravelTime`、逐步 `StepQuadrature`、`ReflectionEvent` 和终止原因；
- `RayPathCache` 在追踪完成后冻结为只读；单频 F2CPP 也通过“轨迹缓存 → 单频投影 → Influence”计算链使用它；
- 逐频复走时、吸收、反射幅相、active 状态和压力只进入 `RayFrequencyState/FrequencyWorkspace`，不得回写 `RayPath`；
- 数值正确性优先于并行优化，不在 M1/M2 阶段实现 Ray-Reuse 调度；
- 完成单频验收后，其代码作为创建 `Bellhop_RayReuse/` 数值核心的重构起点；
- RayReuse 从基线代码派生后维护独立源码副本和构建系统，不反向影响 F2CPP，也不在运行时链接 F2CPP；
- 二者继续通过共同标准算例、变量规范、中间状态和最终结果互相参照。

计划工程结构见 [`doc/01-Bellhop源码分析与宽带复用设计.md`](../doc/01-Bellhop源码分析与宽带复用设计.md) 第 21.2 节，实施任务见 [`doc/02-项目实施待办.md`](../doc/02-项目实施待办.md)。
