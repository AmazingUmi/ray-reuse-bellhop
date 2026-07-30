# Bellhop F2CPP

`Bellhop_F2CPP` 是可独立配置、构建和运行的 C++20 单频二维 Bellhop
实现。它支持目标范围内的 Cartesian Cerveny coherent complex pressure，
从 Bellhop `.env` 读取环境并输出 PRT 与单频 SHD。

核心数据流：

```text
.env
  → SimulationCase
  → GeometryTracer
  → frozen RayPathCache
  → FrequencyProjector
  → CartesianCervenyInfluence
  → FrequencyWorkspace
  → .prt + .shd
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
写出 `<file-root>.prt` 和 `<file-root>.shd`。

## 文档

- [完整使用说明](./doc/USAGE.md)：环境、编译、测试、CLI、输入输出和排错；
- [F2CPP 文档索引](./doc/README.md)；
- [构建与验收计划](./doc/BUILD_PLAN.md)；
- [最终派生清单](./doc/DERIVATION_MANIFEST.md)。

全项目设计与数值契约：

- [Bellhop 源码分析与宽带复用设计](../doc/01-Bellhop源码分析与宽带复用设计.md)；
- [项目实施待办](../doc/02-项目实施待办.md)；
- [基础变量、单位与数值规范](../doc/04-基础变量单位与数值规范.md)。

## 当前状态

G0、M1、M2 已全部完成：

- Debug ASan/UBSan 与 Release 各 20/20 CTest；
- 标准算例 Python 测试 21/21；
- 六个单频案例 PRT/SHD 校验和完整复压力/TL 比较 6/6；
- R-15 的 16 频 trace-once 摊销性能门 6/6；
- 最终快照已允许派生 `Bellhop_RayReuse`。

支持范围和已知限制以[完整使用说明](./doc/USAGE.md)为准。
