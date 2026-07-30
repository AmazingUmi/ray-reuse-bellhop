# Bellhop RayReuse 构建与验收计划

本文记录 `Bellhop_RayReuse` 从独立派生到有界频率并行的实施顺序。所有命令
默认从仓库根目录运行；Python 固定使用 Conda 的 `py` 环境：

```bash
conda run -n py python --version
```

阶段必须按 A → B → C → D → E 推进。每一阶段只有在出口条件关闭后，才进入
下一阶段。命令块是对应阶段的验收入口，不等同于已经通过；实际结果统一记录
在 [`DERIVATION_RECORD.md`](./DERIVATION_RECORD.md)。

截至 2026-07-30，A～E 均已按顺序实施并关闭出口；本文保留各阶段门和复现
命令，不以完成状态替代验收细节。

里程碑后的统一质量门为：

```bash
RAYREUSE_BUILD_JOBS=4 Bellhop_RayReuse/scripts/quality_gate.sh
```

脚本默认以 `conda run -n py python` 执行 Python 测试，并包含 Debug、
Release、独立性扫描和无 F2CPP 隔离副本构建。CI 通过
`RAYREUSE_PYTHON_MODE=system` 使用固定版本 Python/NumPy，但调用同一质量
门，避免本地与云端验收逻辑分叉。

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
