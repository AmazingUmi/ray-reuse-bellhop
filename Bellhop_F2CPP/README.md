# Bellhop F2CPP

`Bellhop_F2CPP` 是可独立配置、构建和运行的 C++20 单频二维 Bellhop
实现。它支持目标范围内的 Cartesian/ray-centered Cerveny coherent、
incoherent 与 semi-coherent 二维声场和二维几何射线输出，从 Bellhop
`.env` 读取环境；TL 模式输出 PRT/SHD，ray-trace 与 eigenray 模式输出
PRT/RAY，arrival 模式输出 PRT/ARR。

核心数据流：

```text
.env
  → SimulationCase
  → GeometryTracer
  → frozen RayPathCache
  ├─ C/I/S TL  → FrequencyProjector → Influence → pressure/intensity → .shd
  ├─ R         → FrequencyProjector → RayWriter → .ray
  ├─ A/a       → ArrivalSolver → ArrivalWriter → .arr
  └─ E         → EigenraySolver → EigenrayWriter → .ray
```

程序保留完整、频率无关、冻结只读的射线轨迹缓存；复走时、衰减、反射
幅相和压力只存在于逐频临时状态。F2CPP 一次运行只计算一个频率，实际多频
调度和轨迹复用由后续独立的 `Bellhop_RayReuse` 工程实现。

## 快速开始

从本目录编译并测试 Release：

```bash
cmake --preset release
cmake --build --preset release --parallel
ctest --preset release
```

可执行文件：

```text
build/release/bellhop_f2cpp
```

运行：

```bash
./build/release/bellhop_f2cpp <file-root>
```

参数必须省略 `.env` 扩展名。程序读取 `<file-root>.env`，并在相同位置
写出 `<file-root>.prt`；根据运行类型另写 `<file-root>.shd`、`.ray` 或 `.arr`。

## 文档

- [完整使用说明](./doc/USAGE.md)：环境、编译、测试、CLI、输入输出和排错；
- [当前进度](./doc/PROGRESS.md)：已完成范围、最新验证基线和当前施工入口；
- [二维单频支持矩阵](./doc/FEATURE_SUPPORT_MATRIX.md)：supported、intentional
  divergence 和 deferred/out-of-scope 的封板定义；
- [F2CPP 文档索引](./doc/README.md)；
- [构建与验收计划](./doc/BUILD_PLAN.md)；
- [二维功能进一步复刻计划](./doc/FURTHER_REPLICATION_PLAN.md)；
- [最终派生清单](./doc/DERIVATION_MANIFEST.md)。
- [数值接口与中间状态契约 v1](./doc/INTERMEDIATE_STATE_CONTRACT.md)。

全项目设计与数值契约：

- [Bellhop 源码分析与宽带复用设计](../doc/01-Bellhop源码分析与宽带复用设计.md)；
- [项目实施待办](../doc/02-项目实施待办.md)；
- [基础变量、单位与数值规范](../doc/04-基础变量单位与数值规范.md)。

## 当前状态

I0～I8 与 I9-B1～B3 已全部验收并冻结，I9-B4 已完成二维单频复刻封板。
当前基线为 AppleClang/GCC 14 CTest 37/37、Python 145/145 和 F2CPP 单频
标准案例 65/65；详细数值证据见[当前进度](./doc/PROGRESS.md)。

当前可正式声明的范围、兼容语义及明确延期项以
[二维单频支持矩阵](./doc/FEATURE_SUPPORT_MATRIX.md)为准。后续工作进入基于
profile 的性能阶段；除非发现真实 correctness bug，不再重新打开已冻结
iteration，也不在 F2CPP 中顺带实现 3D、N×2D、beam shift 或宽带调度。
