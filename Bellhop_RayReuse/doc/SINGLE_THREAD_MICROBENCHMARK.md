# 三模型单线程同工作量微基准

## 目的

该基准在相同标准 ENV、相同发射扇、相同单频场公式和单线程约束下运行原版
Fortran、F2CPP 与 RayReuse，记录 direct 与 Munk 的生产求解路径阶段时间。
它用于定位 Trace/Influence 主成本，不设置“某语言必须更快”的阻断阈值。

从仓库根目录运行：

```bash
Bellhop_RayReuse/scripts/single_thread_microbenchmark.sh
```

默认每个 case/model 先预热 1 次，再计量 3 次；每轮旋转模型顺序。可覆盖：

```bash
RAYREUSE_MICROBENCH_WARMUPS=2 \
RAYREUSE_MICROBENCH_REPETITIONS=7 \
Bellhop_RayReuse/scripts/single_thread_microbenchmark.sh
```

JSON 默认写入忽略目录
`test/standard_cases/results/single_thread_microbenchmark.json`，包含 Git 状态、
平台、三个可执行文件 SHA-256、每次原始计时、中位数和相对原版比值。

## 单线程与数值合同

每次子进程显式设置：

```text
OMP_NUM_THREADS=1
OPENBLAS_NUM_THREADS=1
VECLIB_MAXIMUM_THREADS=1
```

三模型运行 single profile；标准算例门同时验证 PRT/SHD，因此性能样本不会
绕过数值正确性。Fortran 额外启用 `BELLHOP_PROFILE_STAGES=1`，该开关已由
字节一致性门证明不改变 SHD。

## 阶段映射限制

三套生产实现的阶段所有权并不完全相同：

- Fortran 的逐频声学状态部分保留在 Trace/Influence 的原始数据流中；
- F2CPP/RayReuse 显式拆出 `Project`；
- Fortran 使用 `CPU_TIME`，C++ 使用 `steady_clock`；
- F2CPP public Influence 包含防御性校验，RayReuse solver 使用预验证快路径。

因此跨模型主指标固定为：

```text
origin formula core = Trace + Influence
C++ formula core    = Trace + Project + Influence
```

原始 Trace、Project、Influence 仍逐项报告，但只用于实现内诊断，不能单独
解释成纯语言速度差。若需要严格的单算式微内核，应另行提取三端共同生产函数，
不能在 benchmark 中复制公式或用当前阶段值冒充。
