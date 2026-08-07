# H4 本地跨编译器验证结果

## 结论

H4 的 C++ AppleClang↔GCC 本地矩阵已于 2026-08-07 关闭。正式证据基于干净
提交 `37290d68f001aba784b12f76bc9cef1316f20328`：F2CPP 和 RayReuse 均能由
两套 C++ 工具链以警告即错误构建，CTest、最终场和中间几何状态全部通过。

本机只有 GNU Fortran 14.2.0，没有真正独立的第二套 Fortran 编译器，因此
H4-5 保持“等待工具链”。这不阻塞本地 C++ 可移植性结论，但当前不能宣称完整
跨编译器或跨平台发布支持。全过程未推送远端。

## 工具链与构建契约

| 项目 | AppleClang | GCC |
|---|---|---|
| C++ 编译器 | Apple clang 21.0.0 | Homebrew GCC 14.2.0 |
| Release flags | `-O3 -DNDEBUG` | `-O3 -DNDEBUG` |
| fast-math | 关闭 | 关闭 |
| SDK/部署目标 | SDK 26.4；min macOS 26.0 | 固定 MacOSX15.sdk；产物 min macOS 16.0 |
| F2CPP CTest | 21/21 | 21/21 |
| RayReuse CTest | 25/25 | 25/25 |

CMake 为 4.0.2，Python 固定使用 Conda `py`（Python 3.12.9）。GCC 在 macOS
26 上调用系统汇编器时会提示将 deployment version 从 16.0 覆盖为 26.0；
构建和测试均成功。这是 Homebrew GCC、旧 SDK 目标与当前系统汇编器组合的
工具链限制，未发现源代码 warning，但 GCC 产物的兼容下限暂不作发布承诺。

为通过 GCC 警告即错误构建，本阶段在 F2CPP/RayReuse 对称补齐聚合初始化，
修正 RayReuse help/version 的返回构造，并把刚性反射组件测试的 binary64
诊断容差改为跨编译器可移植容差；legacy binary32 位模式仍要求逐位一致。
微基准汇总新增 min、max、MAD 和变异系数。源码修正已单独提交为 `37290d6`。

## 正确性矩阵

| 检查 | AppleClang | GCC | 跨工具链 |
|---|---:|---:|---:|
| 三模型 single + broadband smoke | 12/12 | 12/12 | 36/36 结果组 |
| RayReuse nonreuse/reuse/parallel | 全部 SHD 一致 | 全部 SHD 一致 | 60/60 频率切片通过 |
| 中间状态 direct/vacuum-rigid/Munk | 3/3 | 3/3 | 6/6 producer/case |

最终场跨工具链最坏复压力绝对差为 `5.820766091346741e-11`，出现在
`constant_speed_vacuum_rigid` broadband 500 Hz；最坏组合相对差为
`8.125297767946904e-08`，出现在 Munk broadband 250 Hz；最大 TL 差为
`0 dB`。两套工具链内 RayReuse 三种宽带模式的完整多频 SHD 均逐字节一致。

中间状态 direct 和 vacuum/rigid 跨工具链逐字节一致。Munk 的 F2CPP 与
RayReuse 均在 `q1`、point 164 出现同一最坏差：绝对误差
`3.296881914138794e-07`，scaled error `0.0005236339084020488`，即只使用
约 `0.05236%` 的 D-07 容差预算。同一工具链内 F2CPP↔RayReuse CSV 均逐字节
一致。

## 单线程性能与资源

每轮预热 2 次、计量 7 次，固定 solver/OMP/OpenBLAS/vecLib 单线程。下表均为
formula core 秒数；GCC/Clang 大于 1 表示 GCC 较慢。

| 算例/模型/编译器 | 中位数 | min～max | MAD | CV | GCC/Clang |
|---|---:|---:|---:|---:|---:|
| direct F2CPP/AppleClang | 0.034700 | 0.033763～0.034839 | 0.000138 | 1.27% | — |
| direct F2CPP/GCC | 0.045444 | 0.044705～0.046577 | 0.000113 | 1.14% | 1.310× |
| direct RayReuse/AppleClang | 0.030012 | 0.028969～0.030204 | 0.000176 | 1.55% | — |
| direct RayReuse/GCC | 0.040681 | 0.040461～0.043341 | 0.000211 | 2.29% | 1.356× |
| Munk F2CPP/AppleClang | 2.300370 | 2.283771～2.356795 | 0.005083 | 1.13% | — |
| Munk F2CPP/GCC | 2.716760 | 2.688370～2.753381 | 0.023066 | 0.83% | 1.181× |
| Munk RayReuse/AppleClang | 0.978355 | 0.970902～1.017477 | 0.006082 | 1.55% | — |
| Munk RayReuse/GCC | 1.549587 | 1.547851～1.556537 | 0.001736 | 0.23% | 1.584× |

C++ 样本变异系数均低于 5%，但结果只用于识别工具链性能差异，不构成 H4
阻断门，也不据此调整 RayReuse 默认 worker。GCC 轮的原版 direct 样本因一个
进程级离群点使变异系数达到 15.32%；原版可执行文件在两轮完全相同，因此不
用于比较 C++ 编译器。

`/usr/bin/time -l` 的代表性最大 RSS（字节）如下：

| 模型 | origin | AppleClang F2CPP | AppleClang RayReuse | GCC F2CPP | GCC RayReuse |
|---|---:|---:|---:|---:|---:|
| direct | 2,359,296 | 24,788,992 | 24,854,528 | 24,084,480 | 24,248,320 |
| Munk | 3,260,416 | 67,092,480 | 66,404,352 | 64,995,328 | 64,995,328 |

正式可执行文件 SHA-256：

- AppleClang F2CPP：`496157df5bfd881f710b25f50d05f2dc96faede17a62438e1f7438e3200b8153`；
- GCC F2CPP：`8d8fbc7a849a268ad2873ce678175aea11242b8119089ac3effb96b6c7abf1d4`；
- AppleClang RayReuse：`73e4fcd6810764a40ad56ffe45aed8dbbb2c0d52b68ab149f1512e452fa55cea`；
- GCC RayReuse：`24e4bd56e2372c036d9acff1b41a7d5725f12fddd31d763f6da406c37e914ed9`。

## 验收与证据边界

除四套 toolchain CTest 外，阶段还通过 RayReuse 完整质量门：Debug、Release
和隔离构建各 25/25 CTest，69/69 标准工具测试、9/9 PlotRead、独立性扫描
和格式门均通过。正式 JSON 与运行目录保存在被 Git 忽略的
`test/standard_cases/results/toolchain/`，本报告是进入版本库的阶段摘要。

下一步不继续扩大本机编译器别名矩阵。获得独立 LLVM Flang 或另一目标平台
Fortran 编译器后，再按
[`CROSS_COMPILER_PLAN.md`](../plans/CROSS_COMPILER_PLAN.md) 的 H4-5 执行
原版 single、schema v2 中间状态和资源矩阵；远端 CI 与发布平台矩阵仍单独
等待用户决定。
