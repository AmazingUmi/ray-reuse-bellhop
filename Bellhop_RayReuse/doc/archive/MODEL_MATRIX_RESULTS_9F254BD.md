# 三模型本地数值矩阵：9f254bd

## 身份与范围

2026-08-01 在干净提交 `9f254bddfd1ebf29ec775ecde0dd8265938c6348`
运行：

```bash
Bellhop_RayReuse/scripts/model_matrix_gate.sh
```

平台为 `macOS-26.5.2-arm64-arm-64bit`。本轮覆盖六例 `single`、六例两频
`broadband_smoke`，RayReuse 多频分别运行 nonreuse、reuse、parallel。

| 模型 | 可执行文件 SHA-256 |
|---|---|
| 原版 Bellhop | `f77b7bb60509fdb5e0f22b03a71c27ad4998718ba864e5206e5e48bd461ddcee` |
| Bellhop_F2CPP | `496157df5bfd881f710b25f50d05f2dc96faede17a62438e1f7438e3200b8153` |
| Bellhop_RayReuse | `7bd6b5d2f90b459ab25121cc7ac8fb43777daaf0a3e5e617782b48ffca563821` |

## 结果

- 12 个 case/profile 结果全部通过，门控比较失败数为 0；
- single 全场门的最大复压力绝对误差为 `7.2738615e-8`，最大 TL 差为
  `3.1280518e-4 dB`；
- single 紧凑点最大包裹相位差为 `1.1083813e-6 rad`；
- broadband 中 RayReuse 相对原版最大复压力绝对误差为 `6.4229198e-8`，
  最大 TL 差为 `7.9345703e-4 dB`；
- 六个 broadband 算例的 RayReuse 三模式 SHD 均逐字节一致。

F2CPP 的 D-02 策略忽略 ENV 显式 NAlpha 并按当前单频重新规划。因此它在
broadband 只以 `fmax` 切片门控；本轮 12 个低频非门控比较失败被保留在
生成报告中，不代表 RayReuse 回归。所有原版→RayReuse 主比较均通过。

完整 JSON 位于忽略目录 `test/standard_cases/results/model_matrix.json`，可由
上述命令重新生成；运行产物每次进入唯一 work root，避免外置卷 AppleDouble
元数据干扰旧目录覆盖。
