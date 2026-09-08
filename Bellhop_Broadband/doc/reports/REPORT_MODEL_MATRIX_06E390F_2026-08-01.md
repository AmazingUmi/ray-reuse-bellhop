# 三模型本地数值矩阵与 G5 关闭：06e390f

## 身份与范围

2026-08-01 在干净提交 `06e390fc9338e2b94c29b9492027c3a59391dd5d`
运行：

```bash
Bellhop_RayReuse/scripts/model_matrix_gate.sh
```

平台为 `macOS-26.5.2-arm64-arm-64bit`。本轮覆盖六例 `single`、六例两频
`broadband_smoke`，RayReuse 多频分别运行 nonreuse、reuse、parallel。

| 模型 | 可执行文件 SHA-256 |
|---|---|
| 原版 Bellhop（G4 插桩后） | `0b728f0879adc684adcdd1d87c077cadabaef11ddb570d763ff8ef2ebb813b0b` |
| Bellhop_F2CPP | `496157df5bfd881f710b25f50d05f2dc96faede17a62438e1f7438e3200b8153` |
| Bellhop_RayReuse | `7bd6b5d2f90b459ab25121cc7ac8fb43777daaf0a3e5e617782b48ffca563821` |

G0/G1 快照仍记录其生成时的原版可执行文件
`f77b7bb60509fdb5e0f22b03a71c27ad4998718ba864e5206e5e48bd461ddcee`
和来源提交 `f35bbdd`。G4 只增加显式启用的 PRT 计时；默认输出及开启计时后的
SHD 均与冻结 oracle 逐字节一致，因此未更新参考数据。

## 数值结果

- 12 个 case/profile 结果全部通过，门控比较失败数为 0；
- single 全场最大复压力绝对误差 `7.2738615e-8`，最大 TL 差
  `3.1280518e-4 dB`；
- single 紧凑点最大包裹相位差 `1.1083813e-6 rad`；
- broadband 中 RayReuse 相对原版最大复压力绝对误差 `6.4229198e-8`，
  最大 TL 差 `7.9345703e-4 dB`；
- 六个 broadband 算例的 RayReuse 三模式 SHD 均逐字节一致。

F2CPP D-02 按当前单频重新规划而不采用 ENV 显式 NAlpha，因此 broadband
只在 `fmax` 门控。12 个低频非门控失败（原版→F2CPP 3 个、F2CPP→RayReuse
9 个）保留为诊断；所有原版→RayReuse 主比较均通过。

## G5 工程与资源证据

本机工具链：

- Apple clang 21.0.0 与 Apple clang-format 21.0.0；
- GNU Fortran 14.2.0，`-O2 -g -std=gnu -ffree-line-length-none`；
- CMake 4.0.2；
- Conda 环境 `py`：Python 3.12.9、NumPy 2.2.6。

完整质量门通过 Debug/Release/隔离构建各 25/25 CTest、标准 Python 62/62、
PlotRead 9/9 和独立性扫描。工程门通过格式、Clang static analyzer、Release
安装烟测与 CPack；TGZ SHA-256 为
`9b5e512ffe73c1e12f5da642e291dbbf2886d8b60ef288f747df705ae3b4ea08`。

`/usr/bin/time -l` 对 Munk 50 Hz 原版单频算例测得最大 RSS `3,342,336 B`
（约 `3.19 MiB`），real/user/sys 为 `2.72/2.63/0.01 s`。同轮 PRT 阶段为
Trace `0.015576 s`、Influence `2.631201 s`、Scale `0.000029 s`、Output
`0.000635 s`。该数据是单次本机资源基线，不作为跨平台性能承诺。

完整 JSON 位于忽略目录 `test/standard_cases/results/model_matrix.json`，可由
矩阵命令重新生成；远端 CI、签名、公证和公开发布未执行。
