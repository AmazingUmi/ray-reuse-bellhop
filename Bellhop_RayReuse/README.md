# Bellhop RayReuse

本目录用于在已验收 `Bellhop_F2CPP` 代码基础上派生并重构面向固定环境、固定收发位置的 C++ 宽带 Bellhop Ray-Reuse，负责项目里程碑 M3～M5：

1. 保留并验证从 F2CPP 派生的单频计算链；
2. 实现每频完整追踪的宽带非复用基线；
3. 沿用 F2CPP 已验证的完整声线轨迹缓存，逐频重算吸收、反射、波束参数和复压力；
4. 在串行结果稳定后，实现有界频率并行与单 writer 输出。

## 工程边界

- 在 F2CPP 单频验收后复制/派生其必要代码，再进行宽带数据模型和 Ray-Reuse 改造；
- 直接沿用 F2CPP 已冻结的 `RayPath/StepQuadrature/ReflectionEvent/RayPathCache` 契约，除非回归证明确有缺陷，不在本阶段重新设计轨迹变量；
- 派生后拥有独立的 CMake 配置、可执行程序、头文件、源码副本和测试；
- 不链接或调用 `Bellhop_F2CPP/` 的构建目标，后续构建也不从 F2CPP 目录包含头文件或源码；
- 两个工程只通过共同变量规范、标准算例、中间状态、SHD、复压力和 TL 结果互相参照；
- RayReuse 的单频模式属于本工程自身的校验路径；派生完成后，即使移除 F2CPP 目录也应能独立编译和运行。

## 构建入口

命令默认从仓库根目录运行：

```bash
cmake --preset debug -S Bellhop_RayReuse
cmake --build Bellhop_RayReuse/build/debug --parallel
ctest --test-dir Bellhop_RayReuse/build/debug --output-on-failure

cmake --preset release -S Bellhop_RayReuse
cmake --build Bellhop_RayReuse/build/release --parallel
ctest --test-dir Bellhop_RayReuse/build/release --output-on-failure
```

Python 工具统一使用 Conda 的 `py` 环境。例如：

```bash
conda run -n py python -m unittest discover \
  -s test/standard_cases/codes/tests -p 'test_*.py'

conda run -n py python test/standard_cases/codes/standard_cases.py test \
  --version rayreuse \
  --case all \
  --profile single \
  --executable Bellhop_RayReuse/build/release/bellhop_rayreuse
```

提交前的完整质量门可从仓库根目录一次运行：

```bash
RAYREUSE_BUILD_JOBS=4 Bellhop_RayReuse/scripts/quality_gate.sh
```

该入口依次执行 Debug ASan/UBSan、Release、两套 25 项 CTest、Conda `py`
下 50 项标准算例工具测试和 9 项 PlotRead 测试、F2CPP 源码/生成构建元数据/
动态链接独立性扫描，以及无 F2CPP 目录的 Release 隔离构建。GitHub Actions
使用固定的 Python 3.12.9 和 NumPy 2.2.6 调用同一脚本；本地默认仍严格使用
名为 `py` 的 Conda 环境。

格式、静态分析和内部发布包的工程门为：

```bash
RAYREUSE_BUILD_JOBS=4 Bellhop_RayReuse/scripts/engineering_gate.sh
```

该入口检查全量 C++ 格式，按 compilation database 运行 Clang static
analyzer，构建并安装 `0.1.0`，验证 `--version`，生成 CPack TGZ 并输出
SHA-256。当前包只用于内部验证；许可证和目标平台契约冻结前不得作为公开
发行包。具体限制见 [`doc/RELEASE.md`](./doc/RELEASE.md)。

## 运行入口

单频兼容调用继续使用 `.env` 中的频率：

```bash
Bellhop_RayReuse/build/release/bellhop_rayreuse <file-root>
```

宽带调用保留同一 `.env`，由严格升序逗号列表覆盖频率：

```bash
Bellhop_RayReuse/build/release/bellhop_rayreuse <file-root> \
  --frequencies-hz 50,100,250 \
  --execution-mode nonreuse
```

`nonreuse` 是阶段 C 的默认模式，每个频率完整追踪一次。阶段 D 的
`reuse` 模式用于一次追踪后逐频流式投影。阶段 E 的有界频率并行可使用：

```bash
Bellhop_RayReuse/build/release/bellhop_rayreuse <file-root> \
  --frequencies-hz 50,100,250 \
  --execution-mode parallel \
  --workers 8 \
  --output-queue-capacity 2 \
  --memory-budget-mib 4096
```

`--workers` 未给出时采用硬件并发数；完成队列容量只能为 1 或 2，默认 2。
内存预算未给出时不施加显式预算，给出后会限制活动频率数，连一个活动频率
都无法容纳时直接拒绝运行。预算覆盖射线缓存及有界频率工作区，不等同于
进程全部 RSS。三个模式均保留为数值与性能对照入口。

Influence 诊断默认关闭。需要记录射线/segment/range/depth/image 工作量及
validation、precompute、hot-loop 细分计时时，可显式添加：

```bash
Bellhop_RayReuse/build/release/bellhop_rayreuse <file-root> \
  --frequencies-hz 50,250 \
  --execution-mode reuse \
  --profile-influence
```

该选项会增加计数与计时开销，只用于热点定位，不用于正式性能比较。

parallel solver 已保存逐频 Project/Influence/Scale 计时，但默认不写入 PRT。
需要分析任务长尾和调度负载时，可显式添加：

```bash
Bellhop_RayReuse/build/release/bellhop_rayreuse <file-root> \
  --frequencies-hz 50,100,250 \
  --execution-mode parallel \
  --workers 8 \
  --profile-frequency-tasks
```

该选项只输出已有计时，不给 solver 热路径增加时钟或同步；仅适用于 parallel
模式。

## 性能基准

可重复 benchmark 使用共享标准算例、轮换采样顺序、外部 wall、隔离进程
max RSS 和 SHD 哈希门。正式运行默认要求干净工作区，并将提交、可执行文件
哈希、机器、工具链、workers、频率、预算及原始样本统一写入 JSON。完整协议
和命令见 [`doc/BENCHMARKING.md`](./doc/BENCHMARKING.md)。

首轮正式 Munk 16频五轮结果为 parallel-8 `4.424×`、parallel-10
`4.643×`，达到项目 `≥4×` 门槛；但 Influence 占 nonreuse wall 的
`97.42%`。阶段 F1 的 `96f23f8` 和 `4af3f7f` 将 Munk 16频 reuse 降低
`18.04%`。F2 提交 `eedc790` 将固定图像数和类型专化到编译期；提交
`fe6b33f` 再移除内部 Hermite taper 的重复参数校验；`f1511b9` 将 Release
逐贡献有限性检查收敛到公共 API 或 solver 每频末端的完整场扫描；
`7ce9c7d` 显式提升环境与 segment 只读标量。相对 F1 前，当前 2频 reuse
累计下降 `63.51%`，16频 reuse/p8/p10 累计下降
`68.75%/60.63%/61.61%`，SHD 逐字节一致。range-major、range-batch 和
诊断模板专化、segment 端点差值缓存、receiver-depth 数量/连续数据指针
及 64-depth receiver tile 均因 2频回退而回滚。安全局部性候选至此
收敛。64频精选矩阵中 reuse/p8/p10 为
`626.852/176.951/178.422 s`，p8/p10 相对 reuse 为 `3.543×/3.513×`，
SHD 一致；64频保留 8 workers。紧预算与逐频诊断进一步确认固定开销不是
主成本，升频 FIFO 已近似最长任务优先，加权队列没有收益依据；p10 的逐频
总 CPU 时间比 p8 高约 `15%`，限制来自资源争用而非队列不均。

## 文档

- [`doc/BUILD_PLAN.md`](./doc/BUILD_PLAN.md)：阶段 A～F 的入口、出口和验收命令；
- [`doc/BENCHMARKING.md`](./doc/BENCHMARKING.md)：可重复性能基准协议、命令和报告字段；
- [`doc/BENCHMARK_RESULTS_C77FF60.md`](./doc/BENCHMARK_RESULTS_C77FF60.md)：首轮 16 频 direct/Munk 正式基准；
- [`doc/BENCHMARK_RESULTS_FDAAF56.md`](./doc/BENCHMARK_RESULTS_FDAAF56.md)：F2 收敛后的 Munk 64频精选矩阵；
- [`doc/BENCHMARK_RESULTS_96F23F8.md`](./doc/BENCHMARK_RESULTS_96F23F8.md)：F1 重复校验移出及 Munk 2频对照；
- [`doc/BENCHMARK_RESULTS_4AF3F7F.md`](./doc/BENCHMARK_RESULTS_4AF3F7F.md)：F1 线性压力访问及 2/16频确认；
- [`doc/BENCHMARK_RESULTS_EEDC790.md`](./doc/BENCHMARK_RESULTS_EEDC790.md)：F2 布局 screen、图像专化及 2/16频确认；
- [`doc/BENCHMARK_RESULTS_FE6B33F.md`](./doc/BENCHMARK_RESULTS_FE6B33F.md)：F2 向量化审计、Hermite 快路径及 2/16频确认；
- [`doc/BENCHMARK_RESULTS_F1511B9.md`](./doc/BENCHMARK_RESULTS_F1511B9.md)：F2 末端有限性校验及 2/16频确认；
- [`doc/BENCHMARK_RESULTS_7CE9C7D.md`](./doc/BENCHMARK_RESULTS_7CE9C7D.md)：F2 循环不变量提升及 2/16频确认；
- [`doc/BENCHMARK_RESULTS_4F8B227.md`](./doc/BENCHMARK_RESULTS_4F8B227.md)：F2 紧预算算例的固定开销与 worker 梯度；
- [`doc/BENCHMARK_RESULTS_23E36AA.md`](./doc/BENCHMARK_RESULTS_23E36AA.md)：F2 Munk 逐频任务分布与调度结论；
- [`doc/DERIVATION_RECORD.md`](./doc/DERIVATION_RECORD.md)：派生来源、独立工程身份、CLI 契约建议和实际验收记录；
- [`doc/RELEASE.md`](./doc/RELEASE.md)：`0.1.0` 内部包的构建、验证和公开发布前置条件；
- [`doc/HDF5_SCHEMA_DECISION.md`](./doc/HDF5_SCHEMA_DECISION.md)：HDF5 候选 schema 与延后实现决策；
- [`../doc/01-Bellhop源码分析与宽带复用设计.md`](../doc/01-Bellhop源码分析与宽带复用设计.md)：总体设计；
- [`../doc/02-项目实施待办.md`](../doc/02-项目实施待办.md)：项目实施任务；
- [`../test/standard_cases/README.md`](../test/standard_cases/README.md)：共享标准算例入口。
