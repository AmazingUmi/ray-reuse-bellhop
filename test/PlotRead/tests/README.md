# PlotRead 回归测试

PlotRead 的安装、CLI、Python API 和仓库关系见
[`../README.md`](../README.md)。

测试按职责拆分：

- `test_binary_reader.py`：二进制 SHD 头、坐标、复压力、多频选择、字节序和异常文件；
- `test_ascii_reader.py`：旧式 ASCII shade 文件；
- `test_plotting_cli.py`：TL 转换、绘图和 NPZ 导出命令；
- `support.py`：测试时生成小型单频/多频 SHD fixture，不依赖任何求解器输出。

从仓库根目录运行：

```bash
make -C test/PlotRead test
```

该 Makefile 默认使用 Conda 环境 `py`（`conda run -n py python`）；可用 `PYTHON=...` 显式覆盖。

新增测试应放入对应文件；只有出现新的独立职责时才新增 `test_*.py`。
