# 外部参考实现

`acoustic_toolbox_matlab/` 保存从 Acoustic Toolbox 提取的原始 `plotshd` / `read_shd` MATLAB 函数，仅用于核对 SHD 格式来源。

项目运行、绘图和回归统一使用同级的 `../bellhop_io_py/` 与 `../tests/`，不要在各算例目录复制或修改这些 MATLAB 文件。若发现格式差异，应为 Python reader 增加测试并集中修复。
