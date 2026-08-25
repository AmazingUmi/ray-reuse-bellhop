# Bellhop Ray-Reuse

本仓库包含 Acoustic Toolbox Bellhop 的 Fortran 参考实现、已封板的 C++20
二维单频复刻、独立宽带射线复用实现及可重复标准算例。

## 项目结构

```text
Bellhop_origin/     可重现 Fortran 单频 oracle
Bellhop_F2CPP/      独立的优化版 C++ 单频复刻
Bellhop_RayReuse/   独立的宽带轨迹复用实现
test/PlotRead/        独立 SHD 格式读取、绘图和自包含 fixtures
test/standard_cases/  原版、F2CPP、RayReuse 共用的单频/宽带算例矩阵
test/legacy/          不参与测试的迁移前历史材料
demo/                 按 cases/codes/results/figures 分类的可靠性与多频展示
```

实施顺序为先完成 `Bellhop_F2CPP`，再以其已验证代码为起点复制/派生 `Bellhop_RayReuse`，并在后者中改造成宽带轨迹复用实现。F2CPP 从一开始就必须采用 RayReuse 所需的变量和所有权设计：完整保存频率无关的 `RayPath`、`StepQuadrature`、`ReflectionEvent` 和终止原因，将逐频幅相与压力隔离到 `RayFrequencyState/FrequencyWorkspace`。因此 RayReuse 主要增加多频调度和复用策略，而不是重新设计声线状态。派生后两者保持独立 CMake 工程、独立源码副本和独立可执行程序，互不链接。

## SHD 结果读取

结果读取与绘图已集中到 `test/PlotRead/bellhop_io_py/`，相关测试与 MATLAB 格式参考也统一放在 `test/PlotRead/`。运行不再依赖 MATLAB。

项目使用仓库根目录的 uv 环境，Python 固定为 `.python-version` 中的版本。
首次使用和完整 Python 回归从项目根目录运行：

```bash
uv sync
uv run pytest
```

`uv sync` 会以 editable 方式安装仓库内的 PlotRead 组件。Makefile 和脚本从
`PATH` 发现 `python3`，通过 `uv run` 执行时会自动使用项目环境，不要求手动
激活，也不硬编码 `.venv` 路径。

生成一个标准结果后无需设置 `PYTHONPATH`：

```bash
uv run make -C test/standard_cases test \
  VERSION=origin CASE=munk_cerveny_cc PROFILE=single

uv run bellhop-shd info \
  test/standard_cases/results/origin/munk_cerveny_cc/single/f000_50Hz/munk_cerveny_cc_f000_50Hz.shd

uv run bellhop-shd plot \
  test/standard_cases/results/origin/munk_cerveny_cc/single/f000_50Hz/munk_cerveny_cc_f000_50Hz.shd \
  -o /tmp/munk.png
```

Python 的 `--frequency-index` 从 **0** 开始；`--frequency` 按最接近的 Hz 值选择。完整安装、CLI、API、测试和项目边界见 [PlotRead 使用说明](test/PlotRead/README.md)。

## 回归测试

完成一次 `uv sync` 后：

```bash
uv run make -C test/PlotRead test
```

兼容显式名称 `make -C test/PlotRead test-plotread`；它与 `test` 执行相同的 PlotRead 回归。

标准 Bellhop 算例的运行方法见 [test/standard_cases/README.md](test/standard_cases/README.md)。

三套求解器的工程演示和结果绘图见 [demo/README.md](demo/README.md)。

F2CPP 复刻封板后的 supported、intentional divergence 与 deferred 范围见
[二维单频支持矩阵](Bellhop_F2CPP/doc/reference/REFERENCE_FEATURE_SUPPORT_MATRIX.md)。
