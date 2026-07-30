# Bellhop F2CPP 使用说明

## 1. 程序用途

`bellhop_f2cpp` 是裁剪后的 C++20 单频二维 Bellhop 实现。它读取一个
Bellhop `.env` 文件，计算 Cartesian Cerveny coherent complex pressure，
并写出 PRT 日志和单频 SHD 声场。

程序内部始终先生成完整的频率无关射线轨迹，冻结为只读
`RayPathCache`，再执行单频投影和声场累加：

```text
.env
  → SimulationCase
  → GeometryTracer
  → frozen RayPathCache
  → FrequencyProjector
  → CartesianCervenyInfluence
  → pressure scaling
  → .prt + .shd
```

F2CPP 一次运行只接受一个频率。多频调度、实际轨迹复用和多频 SHD 属于
后续独立的 `Bellhop_RayReuse` 工程。

## 2. 环境要求

编译程序需要：

- CMake 3.24 或更高版本；
- 支持 C++20 的 Clang、Apple Clang 或 GCC；
- `make`，因为当前 preset 使用 `Unix Makefiles`；
- 约 2 GB 可用内存，以运行最大的 5 kHz 标准案例。

标准算例、SHD 校验和场比较还需要：

- Python 3.11 或更高版本；
- NumPy；
- 仓库自带的
  [`test/PlotRead/bellhop_io_py`](../../test/PlotRead/README.md)，标准算例会直接
  引用源码；人工使用 `bellhop-shd` 时按 PlotRead 文档完成一次 editable install。

检查常用工具：

```bash
cmake --version
c++ --version
make --version
python3 --version
python3 -c "import numpy; print(numpy.__version__)"
```

当前最终快照在 macOS arm64、Apple Clang 21、CMake 4.0.2 上通过验证。

## 3. 编译

以下命令均从 `Bellhop_F2CPP/` 目录运行：

```bash
cd Bellhop_F2CPP
```

### 3.1 Release

Release 用于正式计算和性能测试：

```bash
cmake --preset release
cmake --build --preset release --parallel
```

生成：

```text
build/release/bellhop_f2cpp
```

### 3.2 Debug + sanitizer

Debug preset 开启高警告、`-Werror`、AddressSanitizer 和
UndefinedBehaviorSanitizer：

```bash
cmake --preset debug
cmake --build --preset debug --parallel
```

生成：

```text
build/debug/bellhop_f2cpp
```

Debug 版本用于诊断，不应用于性能基线。

### 3.3 重新配置或重新编译

修改 CMake 配置后重新运行对应 configure preset：

```bash
cmake --preset release
cmake --build --preset release --parallel
```

要求清理目标后再编译时：

```bash
cmake --build --preset release --clean-first --parallel
```

本工程没有安装步骤；直接使用 `build/<配置>/bellhop_f2cpp`。

## 4. 测试

### 4.1 C++ 单元与组件测试

先完成相应配置和构建，再执行：

```bash
ctest --preset debug
ctest --preset release
```

显示失败测试的完整输出：

```bash
ctest --preset release --output-on-failure
```

只运行某一类测试：

```bash
ctest --test-dir build/release \
  -R 'frequency_projector|single_frequency_solver' \
  --output-on-failure
```

最终快照的 Debug 和 Release 均为 20/20。

### 4.2 Python 标准算例基础测试

以下命令从仓库根目录运行：

```bash
python3 -m unittest discover \
  -s test/standard_cases/codes/tests -p 'test_*.py'
```

最终快照为 21/21。

### 4.3 六个单频端到端案例

从仓库根目录运行：

```bash
python3 test/standard_cases/codes/standard_cases.py test \
  --version f2cpp \
  --case all \
  --profile single \
  --executable Bellhop_F2CPP/build/release/bellhop_f2cpp
```

该命令依次生成 `.env`、运行 F2CPP，并检查 PRT/SHD 结构、维度、频率和
复压力有限性。结果写入：

```text
test/standard_cases/results/f2cpp/<case>/single/
```

只校验已有结果而不重新计算：

```bash
python3 test/standard_cases/codes/standard_cases.py validate \
  --version f2cpp --case all --profile single
```

### 4.4 与 Fortran SHD 比较

```bash
python3 test/standard_cases/codes/compare_fields.py \
  /path/to/reference.shd \
  /path/to/f2cpp.shd
```

程序检查坐标轴、频率、复压力组合误差和 TL 误差。默认容差位于
`test/standard_cases/codes/tolerances.toml`。

### 4.5 摊销性能门

先运行六个单频标准案例，然后从 `Bellhop_F2CPP/` 执行：

```bash
python3 tests/tools/test_evaluate_amortized_performance.py

python3 tests/tools/evaluate_amortized_performance.py \
  --frequency-count 16 \
  --minimum-savings-percent 1 \
  ../test/standard_cases/results/f2cpp/*/single/f000_*/*.prt
```

该工具使用 PRT 中的分阶段时间计算：

```text
T_repeat(N) = N × (T_trace + T_freq)
T_reuse(N)  = T_trace + N × T_freq
```

这是 F2CPP 的缓存就绪性能门，不表示 F2CPP 已实现多频运行。实际宽带收益
必须在 RayReuse 中再次实测。

## 5. 可执行文件用法

### 5.1 命令格式

```bash
bellhop_f2cpp <file-root>
```

查看帮助：

```bash
./build/release/bellhop_f2cpp --help
```

`<file-root>` 可以是相对路径或绝对路径，但必须省略 `.env` 扩展名。
程序读取：

```text
<file-root>.env
```

并写出：

```text
<file-root>.prt
<file-root>.shd
```

输出与输入位于同一目录。程序会截断同名 PRT，并重新写入同名 SHD；需要
保留旧结果时应先复制或更换 file-root。

### 5.2 直接运行

假设当前目录包含 `example.env`：

```bash
/absolute/path/to/Bellhop_F2CPP/build/release/bellhop_f2cpp example
```

也可以从任意目录传入绝对 file-root：

```bash
/absolute/path/to/bellhop_f2cpp \
  /absolute/path/to/cases/example
```

### 5.3 用标准案例生成一个可运行输入

从仓库根目录执行：

```bash
python3 test/standard_cases/codes/standard_cases.py generate \
  --version f2cpp \
  --case constant_speed_direct \
  --profile single

f2cpp_case_root="test/standard_cases/results/f2cpp/constant_speed_direct/single/f000_50Hz/constant_speed_direct_f000_50Hz"

Bellhop_F2CPP/build/release/bellhop_f2cpp "$f2cpp_case_root"

python3 test/standard_cases/codes/standard_cases.py validate \
  --version f2cpp \
  --case constant_speed_direct \
  --profile single
```

### 5.4 退出状态

| 状态码 | 含义 |
|---:|---|
| `0` | 计算成功，或成功显示 `--help` |
| `1` | 输入、数值计算或输出发生错误 |
| `2` | 命令行参数数量错误 |

运行错误会写到标准错误。如果 PRT 已成功打开，文件末尾还会包含
`FATAL ERROR:` 和具体原因。

## 6. 支持的输入范围

当前 parser 有意只支持标准案例所需子集：

| 项目 | 支持范围 |
|---|---|
| 维度与场 | 二维、coherent complex pressure、压力分量 `P` |
| 运行类型 | Cartesian Cerveny point-source rectilinear `CC` |
| 频率与声源 | 每次运行恰好一个频率、一个声源深度 |
| 水体 | 一个水层、C-linear SSP、真空海面 |
| 海底 | 平坦刚性底 `R`，或无剪切的声学半空间 `A` |
| 衰减 | dB/波长输入，可选 Thorp 水体衰减 |
| 接收网格 | 一个或多个接收深度；至少两个、严格递增且等间距的接收距离 |
| 波束 | minimum-width、standard-curvature `MS`，1～3 个图像源 |
| 发射角 | 显式数量，或 `0` 表示按设计频率自动规划 |

不支持的选项会明确报错，而不会静默降级。当前不支持：

- 多频 `.env` 或一次运行多频；
- arrivals、eigenray、ray plot、速度分量；
- 弹性海底、非平坦边界、边界粗糙度；
- PCHIP、Spline、beam shift；
- incoherent/semi-coherent 和不规则接收网格；
- 3D 或 N×2D。

输入沿用 Bellhop I/O 单位：接收距离和范围盒使用 km，角度使用 degree，
密度输入使用 g/cm³；parser 在边界转换为内部 SI 单位。

可从共享标准案例模板开始修改：

```text
test/standard_cases/cases/<case-id>/origin.env.in
```

## 7. 输出说明

### PRT

PRT 是文本日志，包含：

- 解析后的主要环境和网格配置；
- 发射角、射线数、轨迹点数和缓存字节数；
- Trace、Project、Influence、Scale、SHD 分阶段时间；
- 成功标记或致命错误。

成功文件以以下标记结束：

```text
Bellhop F2CPP completed successfully
```

### SHD

SHD 保存单频复压力场：

- 内部累加使用 `complex<double>`；
- 仅在 writer 边界量化为 `complex<float>`；
- 布局可由仓库的
  [`test/PlotRead/bellhop_io_py`](../../test/PlotRead/README.md) 读取；
- 维度顺序为 frequency/source depth/receiver depth/range。

## 8. 常见问题

### `No module named tomllib`

当前 `python3` 低于 3.11。切换到 Python 3.11 或更高版本。

### `No module named numpy`

当前 Python 环境没有 NumPy。安装 NumPy 或切换到项目已有的 Python/conda
测试环境。

### `Could not read presets`

确认当前目录是 `Bellhop_F2CPP/`，并检查 CMake 版本不低于 3.24。

### 程序提示 `unable to open environment file`

传入的是 file-root，不是文件名。对于 `/data/example.env`，参数应为
`/data/example`。

### PRT 中出现 `FATAL ERROR`

读取该行后的具体原因。常见情况是 `.env` 使用了未支持的 Bellhop 模式、
非等距接收距离、多个声源或弹性海底。

### 5 kHz 标准案例占用较多内存

这是完整冻结轨迹缓存的预期成本。当前 5 kHz 基线约含 1013 万个轨迹点，
缓存约 1.23 GB，实测峰值 RSS 约 1.32 GB。

## 9. 相关文档

- [F2CPP 文档索引](./README.md)
- [构建与实施计划](./BUILD_PLAN.md)
- [最终派生清单](./DERIVATION_MANIFEST.md)
- [共享标准算例说明](../../test/standard_cases/README.md)
- [全项目基础变量、单位与数值规范](../../doc/04-基础变量单位与数值规范.md)
