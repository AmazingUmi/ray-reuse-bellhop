# PlotRead：Bellhop SHD 读取、检查与绘图

PlotRead 是仓库内独立的 Python 小项目，用于读取 Acoustic Toolbox/Bellhop
结果文件、检查文件结构、绘制传输损失以及导出 NumPy 数据。运行时不依赖
MATLAB。

上级文档：[仓库总览](../../README.md) ·
[设计文档索引](../../doc/00-文档索引.md) ·
[共享标准算例](../standard_cases/README.md)

## 1. 项目职责

PlotRead 负责：

- 读取二进制 `.shd/.grn` 和旧式 ASCII shade 文件；
- 校验字节序、固定记录长度、维度、坐标轴和文件完整性；
- 只加载指定频率和水平源位置，避免一次读入完整宽带压力场；
- 绘制单个 bearing/source-depth 切片；
- 将复压力和坐标轴导出为压缩 `.npz`；
- 为标准算例和后续 C++ 输出提供统一、独立的 SHD reader。

PlotRead 不负责：

- 运行 Bellhop、生成物理算例或判断求解器数值是否正确；
- 保存正式的标准算例基线；
- 读取 RAM `tl.grid` 或 MATLAB `.mat` 文件；
- 替代 `test/standard_cases/` 的求解器矩阵和数值比较。

## 2. 目录结构

```text
test/PlotRead/
├── README.md                       本文档
├── Makefile                        安装与测试入口
├── pyproject.toml                  Python 包、依赖和 bellhop-shd 入口
├── bellhop_io_py/
│   ├── shd.py                      SHD/GRN/ASCII reader
│   ├── plotting.py                 TL 转换与绘图
│   ├── cli.py                      info/plot/export 命令
│   └── __main__.py
├── tests/                          自包含格式回归
└── reference/
    └── acoustic_toolbox_matlab/    Acoustic Toolbox 原始 MATLAB 参考
```

## 3. 与仓库其他部分的关系

```text
Bellhop_origin / Bellhop_F2CPP / Bellhop_RayReuse
                       │ 生成 .shd
                       ▼
             test/standard_cases/
             运行矩阵、物理验收、场比较
                       │ 调用同一个 ShdReader
                       ▼
              test/PlotRead/bellhop_io_py
                格式解析、检查、绘图、导出
```

- [`test/standard_cases/`](../standard_cases/README.md) 是活动算例、求解器执行和
  数值验收的权威入口。它直接使用 `bellhop_io_py.ShdReader` 检查真实输出。
- `tests/` 使用临时生成的小型 SHD fixture，只验证文件格式和 PlotRead 行为，
  不依赖任何求解器输出目录。
- `reference/acoustic_toolbox_matlab/` 只用于追溯格式来源，不是运行依赖。
- [`test/legacy/`](../legacy/README.md) 是历史材料，不参与 PlotRead 或标准算例测试。
- F2CPP/RayReuse 只需写出兼容 SHD；读取和可视化逻辑不应复制到各求解器。

## 4. 首次安装

以下命令均从仓库根目录执行。项目环境由 uv 统一同步：

```bash
uv sync
```

根项目将 PlotRead 作为 editable workspace 成员安装。源码仍位于
`test/PlotRead/bellhop_io_py/`，修改 Python 文件后无需重新安装。

如需使用其他 Python：

```bash
uv sync --python /path/to/python
```

## 5. 命令行使用

### 5.1 环境调用方式

无需激活环境，直接通过 uv 使用：

```bash
uv run bellhop-shd --help
```

### 5.2 查看文件信息

```bash
bellhop-shd info result.shd
```

输出包括标题、plot type、字节序、记录长度、七个维度和频率范围。`info`
会验证整个文件布局，但不会加载全部宽带压力场。

标准算例输出示例：

```bash
bellhop-shd info \
  test/standard_cases/results/origin/munk_cerveny_cc/single/\
f000_50Hz/munk_cerveny_cc_f000_50Hz.shd
```

### 5.3 绘制传输损失

默认读取第一个频率、水平源位置、bearing 和 source depth：

```bash
bellhop-shd plot result.shd
```

选择最接近 200 Hz 的频率并保存图片：

```bash
bellhop-shd plot result.shd --frequency 200 -o field.png
```

按 0 起始索引选择频率，并指定绘图切片：

```bash
bellhop-shd plot result.shd \
  --frequency-index 7 \
  --bearing-index 0 \
  --source-depth-index 0 \
  --range-unit km \
  --dpi 200 \
  -o field.png
```

未提供 `-o/--output` 时打开 Matplotlib 窗口。

### 5.4 导出 NumPy 数据

```bash
bellhop-shd export result.shd field.npz --frequency 200
```

主要导出项包括：

- `pressure`：复压力；
- `frequency_hz`、`frequency_index`、`frequencies_hz`；
- `bearings_deg`；
- `source_depths_m`；
- `receiver_depths_m`、`receiver_ranges_m`；
- 标题、plot type 和水平源坐标。

读取导出文件：

```python
import numpy as np

with np.load("field.npz") as result:
    pressure = result["pressure"]
    ranges_m = result["receiver_ranges_m"]
```

### 5.5 水平源位置

文件含多个水平源位置时，两个参数必须同时提供：

```bash
bellhop-shd plot result.shd \
  --source-x-km 1.5 \
  --source-y-km 0.5
```

reader 会选择最近的源坐标。

## 6. Python API

```python
from bellhop_io_py import ShdReader, read_shd

reader = ShdReader("result.shd")
print(reader.header.dimensions)
print(reader.header.frequencies_hz)

field = reader.read(frequency_hz=200.0)
pressure = field.pressure

# 等价的便捷入口
field = read_shd("result.shd", frequency_index=7)
```

`field.pressure` 的固定维度顺序为：

```text
[bearing, source_depth, receiver_depth, receiver_range]
```

所有 Python 索引从 0 开始。按 Hz 选择时使用最接近的可用频率。

## 7. 运行测试

PlotRead 测试不要求先生成标准算例：

```bash
uv run make -C test/PlotRead test
```

等价的显式目标：

```bash
uv run make -C test/PlotRead test-plotread
```

Makefile 从 `PATH` 发现 `python3`。覆盖解释器：

```bash
make -C test/PlotRead test PYTHON=/path/to/python
```

测试职责和扩展规则见 [`tests/README.md`](tests/README.md)。

## 8. 支持边界

| 格式/能力 | 状态 |
|---|---|
| Acoustic Toolbox 二进制 `.shd/.grn` | 支持 |
| 小端与大端固定记录 | 支持 |
| rectilinear、irregular pressure records | 支持 |
| FIELD3D `TL` 压缩源坐标 | 支持 |
| 旧式 `ASCFIL`、`.asc`、`.txt` shade | 支持 |
| MATLAB `.mat` | 不支持，应作为独立 reader |
| RAM `tl.grid` | 不支持，应作为独立 reader |

原始 MATLAB reader 位于
[`reference/acoustic_toolbox_matlab/`](reference/acoustic_toolbox_matlab/)。
发现格式差异时，应先增加最小 fixture 和回归测试，再集中修改
`bellhop_io_py/shd.py`，不要在求解器目录复制 reader。
