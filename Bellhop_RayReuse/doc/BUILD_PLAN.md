# Bellhop RayReuse 构建与验收计划

本文记录 `Bellhop_RayReuse` 从独立派生到有界频率并行的实施顺序。所有命令
默认从仓库根目录运行；Python 固定使用 Conda 的 `py` 环境：

```bash
conda run -n py python --version
```

阶段必须按 A → B → C → D → E 推进；阶段 F 在这些正确性和并行契约冻结后
按小步性能提交推进。每一阶段只有在出口条件关闭后，才进入下一阶段。命令块
是对应阶段的验收入口，不等同于已经通过；实际结果统一记录在
[`DERIVATION_RECORD.md`](./DERIVATION_RECORD.md) 和对应基准记录中。

截至 2026-08-01，A～E、F1 和 F2 均已关闭。F2 的 range-major 临时布局和
range-batch 换序因 2频回退而回滚；`eedc790` 的图像专化和 `fe6b33f` 的
Hermite 内部快路径、Release 末端有限性校验和 `7ce9c7d` 的只读循环
不变量提升均已保留。相对 F1 前，Munk 2频 reuse 累计下降 `63.51%`，
16频 reuse/p8/p10 累计下降 `68.75%/60.63%/61.61%`，SHD 逐字节一致。
segment 端点与插值差值缓存、receiver-depth 数量与连续数据指针及
64-depth tile 均因 2频回退已回滚。Munk 64频精选矩阵已完成，p8/p10
相对 reuse 为 `3.543×/3.513×`，当前保留 8 workers。详见
[`BENCHMARK_RESULTS_FDAAF56.md`](./BENCHMARK_RESULTS_FDAAF56.md)。

里程碑后的本地工程化也已关闭：全量 clang-format 门、compilation database
驱动的 Clang static analyzer、版本 `0.1.0`、安装烟测和 CPack TGZ 已合并为
`scripts/engineering_gate.sh`。HDF5 已完成 schema 评估并延后实现，SHD 继续
作为默认兼容输出。仓库远端、云端 workflow 首次运行、分支保护、许可证和
目标平台矩阵属于外部前置条件，不计作本地构建未完成项。

里程碑后的统一质量门为：

```bash
RAYREUSE_BUILD_JOBS=4 Bellhop_RayReuse/scripts/quality_gate.sh
```

脚本默认以 `conda run -n py python` 执行 Python 测试，并包含 Debug、
Release、25 项 CTest、54 项标准工具测试、9 项 PlotRead 测试、独立性扫描
和无 F2CPP 隔离副本构建。CI 通过
`RAYREUSE_PYTHON_MODE=system` 使用固定版本 Python/NumPy，但调用同一质量
门，避免本地与云端验收逻辑分叉。

本地工程化验收为：

```bash
RAYREUSE_BUILD_JOBS=4 Bellhop_RayReuse/scripts/engineering_gate.sh
```

它完成格式检查、静态分析、Release 安装、版本烟测、TGZ 打包和 SHA-256
输出。产物当前定位为内部验证包；公开发行限制见
[`RELEASE.md`](./RELEASE.md)，HDF5 决策见
[`HDF5_SCHEMA_DECISION.md`](./HDF5_SCHEMA_DECISION.md)。

## 阶段 A：独立派生工程

### 入口

- F2CPP 的 M2/P7 单频快照已经验收并允许派生；
- 来源身份和文件范围已经记录；
- 工作区修改已提交，可区分派生变更与其他工作。

### 实施内容

- 建立独立的 C++20/CMake 工程、源码副本和测试；
- 使用 `rayreuse` 命名空间、`include/rayreuse/` 头文件前缀和
  `bellhop_rayreuse` 自有目标；
- 不链接 F2CPP 目标，不通过相对路径包含 F2CPP 头文件或源码，也不使用
  `add_subdirectory` 引入 F2CPP；
- Debug 启用 ASan/UBSan，Debug/Release 都将编译警告视为错误；
- 构建目录固定为 `Bellhop_RayReuse/build/debug` 和
  `Bellhop_RayReuse/build/release`。

### 验收命令

```bash
cmake --preset debug -S Bellhop_RayReuse
cmake --build Bellhop_RayReuse/build/debug --parallel
ctest --test-dir Bellhop_RayReuse/build/debug --output-on-failure

cmake --preset release -S Bellhop_RayReuse
cmake --build Bellhop_RayReuse/build/release --parallel
ctest --test-dir Bellhop_RayReuse/build/release --output-on-failure

rg -n \
  'Bellhop_F2CPP|bellhop_f2cpp|add_subdirectory.*F2CPP|include_directories.*F2CPP' \
  Bellhop_RayReuse \
  -g 'CMakeLists.txt' -g '*.cmake' -g '*.cpp' -g '*.hpp'
```

最后一条命令应无输出。文档中的来源说明不在扫描范围内。

### 出口

- Debug 与 Release 均可独立配置、构建和运行 CTest；
- 源码、头文件和 CMake 中不存在跨目录 F2CPP 依赖；
- 删除或移走 F2CPP 不会改变 RayReuse 的配置、编译和测试输入；
- 可执行文件位于
  `Bellhop_RayReuse/build/release/bellhop_rayreuse`。

## 阶段 B：派生后的单频零漂移

### 入口

- 阶段 A 已关闭；
- `SimulationCase` 仍只接受一个频率；
- 几何追踪、求积、反射、逐频投影、Influence 和 SHD 语义尚未为宽带改写。

### 实施内容

- 保留 F2CPP 单频调用顺序和固定射线累加顺序；
- 运行六个共享单频算例；
- 同时对照 F2CPP 派生来源和 Fortran oracle 的复压力、TL、轨迹及关键中间
  状态；
- 派生命名和目录调整不得引入数值漂移。

### 验收命令

```bash
conda run -n py python -m unittest discover \
  -s test/standard_cases/codes/tests -p 'test_*.py'

conda run -n py python test/standard_cases/codes/standard_cases.py test \
  --version rayreuse \
  --case all \
  --profile single \
  --executable Bellhop_RayReuse/build/release/bellhop_rayreuse
```

逐个 SHD 对照使用同一比较入口：

```bash
conda run -n py python test/standard_cases/codes/compare_fields.py \
  /path/to/reference.shd \
  /path/to/rayreuse.shd
```

其中 `reference.shd` 必须分别取 F2CPP 和 Fortran 的对应单频结果。实际回归
记录需列出六例的最大复压力绝对误差、组合相对误差及最大 TL 差。

### 出口

- 六个 RayReuse 单频算例均能生成并校验兼容 PRT/SHD；
- 相对 F2CPP 和 Fortran 的结果满足共享容差；
- 关键轨迹、动态 `p/q`、反射事件及终止原因没有派生漂移；
- 未引入多频调度或复用优化来掩盖单频差异。

## 阶段 C：宽带非复用基线

### 入口

- 阶段 B 已关闭；
- 单频实现已成为 RayReuse 自身的可信参照；
- 多频 CLI 和标准算例适配器的契约已冻结。

### 实施内容

- 解除 `SimulationCase` 的单频限制；
- 使用 `max(frequencies)` 一次规划全频共享发射角集合；
- 每个频率仍执行一次完整几何追踪，建立不复用轨迹的宽带基线；
- 每频独立计算衰减、边界反射、epsilon、Influence 和缩放；
- 一次输出多频 SHD，并由 PlotRead 兼容读取；
- 将一次宽带运行逐频对照多个独立单频运行。

### 预定验收命令

以下命令要在多频适配器完成后成为可执行验收门；当前阶段 A/B 不以其是否
可运行为通过条件。

```bash
conda run -n py python test/standard_cases/codes/standard_cases.py test \
  --version rayreuse \
  --case all \
  --profile broadband_smoke \
  --executable Bellhop_RayReuse/build/release/bellhop_rayreuse

conda run -n py python test/standard_cases/codes/standard_cases.py test \
  --version rayreuse \
  --case all \
  --profile broadband_regression \
  --rayreuse-execution-mode reuse \
  --executable Bellhop_RayReuse/build/release/bellhop_rayreuse
```

共享工具应在该阶段补充“一次多频结果 vs 多次独立单频结果”的逐频比较，
并继续使用：

```bash
conda run -n py python test/standard_cases/codes/compare_fields.py \
  /path/to/independent-single-frequency.shd \
  /path/to/broadband.shd \
  --candidate-frequency-index 0
```

### 出口

- 两频 smoke 与适用的 16 频 regression 均通过；
- 宽带每个频率与独立单频结果满足同一数值容差；
- 多频 SHD 的频率轴、维度、记录顺序和复压力可由现有读取器校验；
- 性能记录明确显示本阶段仍然是“每频完整追踪”。

## 阶段 D：串行 Ray-Reuse

### 入口

- 阶段 C 已关闭；
- 宽带非复用结果和分阶段计时已经冻结为直接参照；
- 频率无关缓存与逐频状态的所有权边界已经复核。

### 实施内容

- 按共享发射角集合只追踪一次完整射线扇并冻结 `RayPathCache`；
- 按 `frequency → all cached paths` 的固定顺序逐频投影和累加；
- 串行路径只保留一个 `FrequencyWorkspace`，写出后清空复用；
- 增加几何追踪调用计数；
- 在逐频投影前后校验缓存未被修改。

### 预定验收命令

```bash
ctest --test-dir Bellhop_RayReuse/build/release \
  -L reuse \
  --output-on-failure

conda run -n py python test/standard_cases/codes/standard_cases.py test \
  --version rayreuse \
  --case all \
  --profile broadband_regression \
  --executable Bellhop_RayReuse/build/release/bellhop_rayreuse
```

`reuse` 标签测试应至少覆盖追踪调用次数、缓存不可变性、单工作区峰值和与
阶段 C 冻结结果的逐频比较；添加这些测试之前，该验收命令不视为有效门。

### 出口

- 几何追踪次数与 `Nfreq` 无关；
- 复用结果与阶段 C 非复用结果满足共享容差；
- `RayPathCache` 在全部逐频计算前后保持不变；
- 串行压力工作区内存不随总频率数线性增长；
- 分阶段计时证明 trace-once 已实际发生，而非只由模型估算。

## 阶段 E：有界频率并行

### 入口

- 阶段 D 已关闭；
- 串行复用结果、内存和性能基线已经冻结；
- 单 writer 输出边界及确定性要求已经明确。

### 实施内容

- 几何缓存只读共享，每个活动频率独占 `FrequencyWorkspace`；
- 根据线程数、频率数和内存预算计算 `activeFrequencyLimit`；
- 使用容量 1～2 的有界完成队列和单 writer；
- 禁止使用复压力原子加法，禁止多个工作线程直接写 SHD；
- 检查 1、2、16、64 频的正确性、确定性、峰值内存和端到端性能。

### 预定验收命令

```bash
ctest --test-dir Bellhop_RayReuse/build/release \
  -L parallel \
  --output-on-failure

conda run -n py python test/standard_cases/codes/standard_cases.py test \
  --version rayreuse \
  --case constant_speed_direct \
  --profile broadband_stress \
  --rayreuse-execution-mode parallel \
  --executable Bellhop_RayReuse/build/release/bellhop_rayreuse
```

`parallel` 标签测试应覆盖线程数/内存预算边界、单 writer、无压力原子累加及
重复运行确定性。性能验收需另存原始命令、线程数、频率数、机器信息、阶段
计时和峰值 RSS，不能只记录加速比。需要固定 worker/预算时，直接执行
`bellhop_rayreuse` 并传入 `--workers`、`--output-queue-capacity` 和
`--memory-budget-mib`；标准算例 runner 负责固定输入和输出校验。
正式性能记录使用
`test/standard_cases/codes/benchmark_rayreuse.py`，由其固定并记录配置、
轮换多轮样本、测量外部 wall/隔离 max RSS，并执行 ENV/SHD 哈希门；完整协议
见 [`BENCHMARKING.md`](./BENCHMARKING.md)。

### 出口

- 1、2、16、64 频矩阵在支持的算例上通过；
- 并行与串行复用结果满足共享容差，重复运行具有可解释的确定性；
- 峰值压力工作区随活动频率上限增长，而非随总频率数增长；
- 输出保持频率顺序且只有一个 writer；
- 代表性 8 核宽带算例端到端加速达到项目目标 `≥ 4×`，或记录未达标的
  可复现实测证据与后续决策。

## 阶段 F：Influence 热路径优化与规模确认

### 入口

- 阶段 A～E 已关闭，数值结果、单 writer 和有界内存契约已经冻结；
- 提交 `c77ff60` 的正式 16 频基准确认 Munk nonreuse 中 Trace 只占
  `2.13%`、Influence 占 `97.42%`；
- Munk parallel-8/10 已达到 `4.424×/4.643×`，但 10 workers 五轮范围为
  `27.3%`，不能直接设为默认。

### F1：计数与安全热路径

- 为 Influence 增加低开销计数：射线数、活动点/段数、跨越 receiver range
  次数、depth/image 评估数、窗口拒绝数和实际非零贡献数；
- 将计时细分为输入校验、逐射线预计算、range/depth/image 热循环和最终
  工作区校验；计数/细分计时默认关闭，避免污染正式数据；
- 冻结几何缓存和 receiver grid 的校验只执行一次；逐频状态在投影后校验
  一次，不得在每条射线入口重新扫描完整压力工作区；
- 保留公共防御性 API，为 solver 增加只接受已验证输入的内部快路径；
- 在已验证索引范围内使用连续 span/reference，消除热循环中重复的
  `workspace.at()` 索引和边界检查。

F1 不改变射线、频率、segment、range、depth、image 或复数贡献的累加顺序。
Debug/诊断路径继续保留有限性检查，Release 快路径在每频完成后统一验证压力
工作区。

#### F1 完成状态（2026-07-30）

- [x] 默认关闭的 Influence 工作计数与 validation/precompute/hot-loop 计时；
- [x] 公共 `accumulate` 保留完整防御性校验；
- [x] solver 对冻结缓存和已校验逐频投影使用私有预验证入口；
- [x] 单频、nonreuse、reuse、parallel 的统计聚合与
  `--profile-influence` 诊断输出；
- [x] Release/Debug、50 个标准工具测试、9 个 PlotRead 测试、独立性扫描和
  隔离构建质量门；
- [x] Munk 2频 smoke：`20.1310 s → 18.7690 s`，SHD 哈希不变；
- [x] 将已验证 `(depth, range)` 映射为一次线性索引，消除同一贡献的两次
  `workspace.at()` 边界检查；
- [x] Munk 16频 reuse、parallel-8、parallel-10 三轮确认。

`--profile-influence` 的一次 2频诊断记录了 10,000 次射线累积、约
9.996 亿次 receiver-depth 评估和约 29.987 亿次 image 评估；热循环
`18.438 s`，预计算 `0.143 s`，轻量校验 `0.0002 s`。该证据用于选择
后续线性压力访问优化，而没有先扩大并行 worker 搜索。

线性压力访问提交 `4af3f7f` 使 Munk 2频 reuse 相对 `96f23f8` 再下降
`10.17%`，相对 F1 前累计下降 `16.25%`；16频 reuse、parallel-8、
parallel-10 相对 `c77ff60` 分别下降 `18.04%`、`13.95%`、`10.68%`。
三配置 SHD 与旧基线哈希一致。parallel-10 仅比 parallel-8 快约 `1.1%`
且三轮范围更大，F1 不改变默认 worker。

### F2：布局和局部性实验

按“一次只改一个变量”的顺序比较：

1. [x] range-major 临时工作区：2频慢 `0.19%` 且增加内存，已回滚；
2. [x] range-batch/depth-major 换序：2频慢 `5.31%`，已回滚；
3. [x] 图像数量/类型编译期专化：2/16频稳定获益，提交 `eedc790`；
4. [x] AppleClang 向量化报告与预计算布局审计：当前已是四 vector SoA，
   depth 循环受调用、控制流和压力依赖阻止；
5. [x] 诊断路径模板专化：2频慢 `46.34%`，因代码体积倍增而回滚；
6. [x] Hermite taper 内部已验证快路径：2/16频稳定获益，提交 `fe6b33f`；
7. [x] Release 有限性检查所有权：公共 API/Debug/诊断保留，solver 收敛到
   每频末端完整场扫描，提交 `f1511b9`；
8. [x] 环境边界深度与右端频率状态显式提升，提交 `7ce9c7d`；
9. [x] segment 左右端状态与插值差值缓存：2频慢 `3.69%`，已回滚；
10. [x] receiver-depth 数量、深度标量与连续数据指针：2频慢 `1.35%`，
    已回滚；
11. [x] 64-depth receiver tile：2频慢 `1.95%`，已回滚；
12. [x] Munk 64频 reuse/p8/p10 精选矩阵：`626.852/176.951/178.422 s`，
    SHD 一致，8 workers 略优；
13. [x] 用 direct/acoustic-bottom 的 2/16/64频梯度分离固定开销：外部
    进程边界约 `3–7 ms`，不是 Munk 主成本；
14. [x] 取得 Munk 逐频任务负载分布：耗时与频率强负相关，升频 FIFO 与
    LPT 模拟完工时间相同，不实施加权队列；
15. [x] F2 局部优化与调度诊断关闭；p8 保留为 64频主验收基线，p10 仅为
    16频吞吐候选。

#### F2 当前状态（2026-07-31）

图像专化在每条射线入口按 `imageCount=1/2/3` 选择模板实例，在 depth 热
循环中以 `if constexpr` 保留 true → surface → bottom 的固定求和顺序。
新增组件测试逐位比较专化热路径和详细诊断路径，并显式覆盖原先缺少的
`imageCount=2`。最终完整质量门 Debug/Release 25/25、标准 Python 50/50、
PlotRead 9/9、独立构建 25/25 通过。

`7ce9c7d` 后 Munk 16频中位数为 reuse `87.578 s`、parallel-8
`25.525 s`、parallel-10 `23.715 s`；相对当前 reuse 加速为
`3.431×/3.693×`。并行绝对时间继续下降，但串行改善更大；p10 三轮范围
仍大于 p8，因此暂不改变默认 worker。

任何改变浮点累加顺序的方案必须独立评审，并从“逐字节一致”转入明确误差
预算；在此之前不得作为默认实现。

### 验证阶梯

1. 单元/组件测试和 Debug ASan/UBSan；
2. Munk 2频 smoke：功能、计数器和单项性能回归；
3. Munk 16频：reuse、parallel-8、parallel-10，1 次预热 + 3 次计量；
4. 六例 2/16频 SHD 与 `c77ff60` 基线逐字节一致；
5. 候选稳定后再运行 Munk 64频 reuse/parallel 精选矩阵，不重复 64频
   nonreuse 全矩阵；
6. direct/acoustic-bottom 紧预算梯度校准固定进程、线程栈和分配器余量。

### 出口

- Munk 16频 reuse 的 Influence wall 出现可重复下降，且不是由关闭必要校验
  或改变数值语义换取；
- 每个优化提交都有原始 benchmark JSON、提交/二进制哈希和回滚判断；
- 16频所有模式 SHD 继续逐字节一致，完整质量门通过；
- 8/10 workers 的选择在三轮以上复测中稳定，或明确保留显式配置而不设默认；
- 64频精选矩阵完成后，再决定 `parallel` 默认值和后续 receiver tile 范围。

## 阶段 G：数值参考与可移植性加固

本阶段完全使用本地构建与结果，不以配置远端或云端 CI 为入口。原版 Bellhop
Fortran 是场结果主要 oracle，F2CPP 是单频 C++ 派生一致性参考，RayReuse
是覆盖 single、nonreuse、reuse、parallel 的被验收对象。

### 实施顺序

1. [x] 冻结三模型职责、紧凑快照 schema、更新规则和快速/完整矩阵；
2. [x] 为六例 `single` 生成原版 oracle 紧凑快照，每例 16 个代表点；
3. [ ] 实现快照校验器、复压力/TL/相位 floor 规则及失败报告；
4. [ ] 建立六例 single、两频 smoke 的三模型自动对照门；
5. [ ] 为原版 Fortran 增加默认关闭的 Trace/Influence/Scale/输出计时；
6. [ ] 运行完整回归、记录本机工具链/RSS 并关闭阶段文档。

契约和显式更新命令见
[`../../test/standard_cases/REFERENCE_SNAPSHOTS.md`](../../test/standard_cases/REFERENCE_SNAPSHOTS.md)。
首批快照以原版可执行文件 SHA-256
`f77b7bb60509fdb5e0f22b03a71c27ad4998718ba864e5206e5e48bd461ddcee`
和来源提交 `f35bbdd` 标识，不提交完整 SHD。

### 出口

- 已提交快照只能显式更新，快速门不会启动求解器或覆盖 oracle；
- 六例 single 和两频 smoke 均产生原版/F2CPP/RayReuse 可追溯误差报告；
- RayReuse 三个多频模式继续满足既定容差和确定性要求；
- Fortran 诊断计时开启前后 SHD 不变；
- 完整质量门和工程门通过，长时 16/64频矩阵仍只在里程碑运行。
