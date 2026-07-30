# Bellhop Ray-Reuse

本仓库包含 Acoustic Toolbox Bellhop 的 Fortran 参考实现、可重复标准算例，以及正在建设的宽带射线复用实现。

## 项目结构

```text
Bellhop_origin/     可重现 Fortran 单频 oracle
Bellhop_F2CPP/      独立的优化版 C++ 单频复刻
Bellhop_RayReuse/   独立的宽带轨迹复用实现
test/PlotRead/        独立 SHD 格式读取、绘图和自包含 fixtures
test/standard_cases/  原版、F2CPP、RayReuse 共用的单频/宽带算例矩阵
test/legacy/          不参与测试的迁移前历史材料
```

实施顺序为先完成 `Bellhop_F2CPP`，再以其已验证代码为起点复制/派生 `Bellhop_RayReuse`，并在后者中改造成宽带轨迹复用实现。F2CPP 从一开始就必须采用 RayReuse 所需的变量和所有权设计：完整保存频率无关的 `RayPath`、`StepQuadrature`、`ReflectionEvent` 和终止原因，将逐频幅相与压力隔离到 `RayFrequencyState/FrequencyWorkspace`。因此 RayReuse 主要增加多频调度和复用策略，而不是重新设计声线状态。派生后两者保持独立 CMake 工程、独立源码副本和独立可执行程序，互不链接。

## SHD 结果读取

结果读取与绘图已集中到 `test/PlotRead/bellhop_io_py/`，相关测试与 MATLAB 格式参考也统一放在 `test/PlotRead/`。运行不再依赖 MATLAB。

项目中的 VS Code Python 解释器和 PlotRead Makefile 默认使用 Conda 的 `py` 环境。首次使用时，在项目根目录安装命令：

```bash
make -C test/PlotRead setup-python
```

Makefile 实际执行 `conda run -n py python`，不要求提前激活环境；如需在终端直接调用 `bellhop-shd`，仍应先执行 `conda activate py`。

生成一个标准结果后无需设置 `PYTHONPATH`：

```bash
make -C test/standard_cases test \
  VERSION=origin CASE=munk_cerveny_cc PROFILE=single

bellhop-shd info \
  test/standard_cases/results/origin/munk_cerveny_cc/single/f000_50Hz/munk_cerveny_cc_f000_50Hz.shd

bellhop-shd plot \
  test/standard_cases/results/origin/munk_cerveny_cc/single/f000_50Hz/munk_cerveny_cc_f000_50Hz.shd \
  -o /tmp/munk.png
```

Python 的 `--frequency-index` 从 **0** 开始；`--frequency` 按最接近的 Hz 值选择。完整安装、CLI、API、测试和项目边界见 [PlotRead 使用说明](test/PlotRead/README.md)。

## 回归测试

完成一次 `make -C test/PlotRead setup-python` 后：

```bash
make -C test/PlotRead test
```

兼容显式名称 `make -C test/PlotRead test-plotread`；它与 `test` 执行相同的 PlotRead 回归。

标准 Bellhop 算例的运行方法见 [test/standard_cases/README.md](test/standard_cases/README.md)。
