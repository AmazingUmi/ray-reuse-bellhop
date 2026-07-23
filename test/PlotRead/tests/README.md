# PlotRead 回归测试

测试按职责拆分：

- `test_binary_reader.py`：二进制 SHD 头、坐标、复压力、多频选择、字节序和异常文件；
- `test_ascii_reader.py`：旧式 ASCII shade 文件；
- `test_plotting_cli.py`：TL 转换、绘图和 NPZ 导出命令；
- `support.py`：共享样例路径和最小合成 SHD 构造器，不包含测试用例。

从仓库根目录运行：

```bash
make -C test/PlotRead test
```

新增测试应放入对应文件；只有出现新的独立职责时才新增 `test_*.py`。
