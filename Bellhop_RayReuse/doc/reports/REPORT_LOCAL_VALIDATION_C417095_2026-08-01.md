# H1～H3 本地验证结果：c417095

## 身份与范围

2026-08-01 在干净提交 `c41709551f2b9e507e184fabade60347f83cecc9`
运行单线程三模型同工作量微基准和中间几何状态门。平台为
`macOS-26.5.2-arm64-arm-64bit`。

主程序 SHA-256：

| 模型 | SHA-256 |
|---|---|
| origin | `0b728f0879adc684adcdd1d87c077cadabaef11ddb570d763ff8ef2ebb813b0b` |
| F2CPP | `496157df5bfd881f710b25f50d05f2dc96faede17a62438e1f7438e3200b8153` |
| RayReuse | `7bd6b5d2f90b459ab25121cc7ac8fb43777daaf0a3e5e617782b48ffca563821` |

## 单线程微基准

执行：

```bash
Bellhop_RayReuse/scripts/single_thread_microbenchmark.sh
```

每个 case/model 预热 1 次、计量 3 次，模型顺序逐轮旋转。跨模型主指标为
统一公式核心：Fortran `Trace + Influence`，C++
`Trace + Project + Influence`。原始阶段边界并不相同，因此 Trace/Influence
单项只作诊断，不计算跨语言单项加速。

| 算例 | 模型 | Trace / s | Project / s | Influence / s | 公式核心 / s | 相对原版 |
|---|---|---:|---:|---:|---:|---:|
| direct | origin | 0.004866 | — | 0.011429 | 0.016312 | 1.000× |
| direct | F2CPP | 0.015365 | 0.000684 | 0.017644 | 0.034052 | 2.088× |
| direct | RayReuse | 0.015179 | 0.000677 | 0.013185 | 0.029014 | 1.779× |
| Munk | origin | 0.015469 | — | 2.630108 | 2.645606 | 1.000× |
| Munk | F2CPP | 0.039934 | 0.003647 | 2.215775 | 2.259697 | 0.854× |
| Munk | RayReuse | 0.041744 | 0.003810 | 0.919092 | 0.964646 | 0.365× |

direct 仍由小规模固定开销主导；RayReuse 在 Munk 公式核心中约为原版
`2.74×`、F2CPP `2.34×`。该数据是三次计量的本机中位数，不作为跨平台性能
承诺，也不设置语言间阻断阈值。

## 接口与中间几何状态

F2CPP 新增 compile-time numerical contract test，冻结公开字段类型、单位与
关键返回类型；geometry probe schema v1 同时由 F2CPP 和 RayReuse 独立实现。
契约见
[`../../../Bellhop_F2CPP/doc/reference/REFERENCE_INTERMEDIATE_STATE_CONTRACT.md`](../../../Bellhop_F2CPP/doc/reference/REFERENCE_INTERMEDIATE_STATE_CONTRACT.md)。

执行：

```bash
Bellhop_RayReuse/scripts/intermediate_state_gate.sh
```

| 算例 | 点数 | 积分步 | 反射事件 | Fortran→C++ 最坏误差 | F2CPP/RayReuse CSV |
|---|---:|---:|---:|---:|---|
| direct | 512 | 511 | 0 | 0 | 字节一致 |
| vacuum/rigid | 1,975 | 1,816 | 158 | 0 | 字节一致 |
| Munk | 366 | 363 | 2 | `h=3.5243e-12`，scaled `6.9620e-5` | 字节一致 |

三例均使用 source index 1、launch angle index 150。对照覆盖位置、慢度、
动态 `p/q`、声速、实走时、`h/hw0/hw1`、predictor midpoint、bounce 顺序、
点/步/反射计数和终止类型。

## 完整门禁

- F2CPP Debug/Release：21/21 CTest；
- RayReuse Debug/Release/隔离构建：各 25/25 CTest；
- 标准 Python：69/69；PlotRead：9/9；
- RayReuse 格式、Clang static analyzer、独立性、安装烟测和 CPack：通过；
- 中间状态门：3/3；
- 工作区在两项正式测量时均为干净状态。

生成 JSON 位于忽略目录：

- `test/standard_cases/results/single_thread_microbenchmark.json`；
- `test/standard_cases/results/intermediate_state_matrix.json`。
