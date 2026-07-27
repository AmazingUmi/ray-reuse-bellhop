# 05 SHD 结果读取与绘图

## 设计

`test/PlotRead/bellhop_io_py/` 直接实现 `Bellhop_origin/misc/RWSHDFile.f90` 的固定记录格式。读取时先校验字节序、记录长度、七个维度以及文件总长度，再只映射所选频率和水平源位置对应的记录。

这取代了测试目录中的多份 `read_shd*.m` 副本，并解决了旧脚本的几个问题：

- 多频文件默认只加载一个频率切片，内存占用与总频率数无关；
- 可按 Hz 或 0 起始索引选频；
- 选水平源位置时，记录偏移同时包含频率维；
- 压力维度固定为 `bearing × source_depth × receiver_depth × range`；
- 损坏文件、维度越界和不完整的源坐标参数会明确报错；
- 二进制 `.shd/.grn` 与旧式 ASCII shade 文件使用同一结果对象。

当前相关内容集中在同一测试域：

```text
test/PlotRead/
├── pyproject.toml                 Python 包、依赖与命令行入口配置
├── bellhop_io_py/                 Python SHD 读取、绘图与命令行
├── tests/                         按读取格式和功能拆分的回归测试
├── Makefile                       安装与测试统一入口
└── reference/
    └── acoustic_toolbox_matlab/   Acoustic Toolbox 原始 MATLAB 参考
```

原始 MATLAB 函数只作为格式来源，不再作为运行依赖。

PlotRead 回归在临时目录中生成最小单频、多频和不同字节序 SHD fixture，不读取 `standard_cases` 或历史求解器结果。求解器数值正确性由 `test/standard_cases/` 独立负责。

## Python API

```python
from bellhop_io_py import ShdReader, read_shd

reader = ShdReader("result.shd")
print(reader.header.dimensions)

field = reader.read(frequency_hz=200.0)
pressure = field.pressure

# 等价的便捷入口
field = read_shd("result.shd", frequency_index=7)
```

`field.pressure` 的固定索引顺序是：

```text
[bearing, source_depth, receiver_depth, receiver_range]
```

若文件含多个水平源位置，可同时传入 `source_x_km` 和 `source_y_km`，读取器选择最近的坐标。两个参数不能只给一个。

## 命令行

项目中的 VS Code Python 解释器和 PlotRead Makefile 默认使用 Conda 的 `py` 环境。首次使用时，在项目根目录执行：

```bash
make -C test/PlotRead setup-python
```

Makefile 默认通过 `conda run -n py python` 执行；如有需要，可以用 `PYTHON=...` 覆盖。安装采用 editable 模式，源码仍位于 `test/PlotRead/bellhop_io_py/`，修改后不需要重新安装。

在终端直接使用命令前激活环境：

```bash
conda activate py
```

之后可在项目内直接使用：

```bash
# 校验文件并查看元数据
bellhop-shd info result.shd

# 选择最接近 200 Hz 的切片并绘图
bellhop-shd plot result.shd --frequency 200 -o field.png

# 选择第 8 个频率（Python 索引 7），导出复压力与全部坐标轴
bellhop-shd export result.shd field.npz --frequency-index 7
```

## 回归测试

测试入口由 `test/PlotRead/Makefile` 统一管理：

```bash
make -C test/PlotRead test
```

兼容显式目标名称：

```bash
make -C test/PlotRead test-plotread
```

`.npz` 可由 NumPy 直接读取，适合后续误差统计；如确需在 MATLAB 中消费，可在导出层增加 SciPy `.mat` 适配，而无需再次实现 SHD 解析。

## 支持边界

当前支持 Acoustic Toolbox 二进制 `.shd/.grn`（含 FIELD3D 的 `TL` 压缩源坐标）和旧式 ASCII shade 文件。`read_shd.m` 中顺带分派的 RAM `tl.grid` 与 MATLAB `.mat` 并不是 SHD 格式，暂未混入本模块；需要时应分别增加独立 reader。
