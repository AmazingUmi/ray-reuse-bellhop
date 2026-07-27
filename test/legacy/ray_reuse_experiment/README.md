# 历史多频实验材料

本目录不是原版 Bellhop 的可信 oracle。

- `MunkB_Coh_CervenyC_MultiFreq.env/.prt/.shd` 对应 64 个频率（100–1000 Hz）的历史宽带实验；
- `MunkB_Coh_CervenyC_16Freq_experimental.shd` 是原 `plotshd/` 子目录中同名文件，实际只有 16 个频率，现已改名以避免与 64 频结果混淆；
- 结果读取和绘图统一使用 `test/PlotRead/bellhop_io_py/`。

可信单频和宽带逐频 oracle 位于 `test/standard_cases/`。本目录已经退出活动测试路径。
