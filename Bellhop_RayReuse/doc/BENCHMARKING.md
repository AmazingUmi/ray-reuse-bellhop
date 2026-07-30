# Bellhop RayReuse 可重复性能基准

## 目标与适用范围

`test/standard_cases/codes/benchmark_rayreuse.py` 用同一标准算例输入比较
`nonreuse`、`reuse` 和 `parallel`。它直接调用 Release 可执行程序，以便固定
并记录 parallel workers、输出队列容量和内存预算。

跨模式的主性能指标是外部测得的 `real_seconds`。PRT 内的 Trace、Project、
Influence、Scale、solver wall 和 SHD 时间用于定位热点；不同模式的 solver
wall 覆盖范围并不完全相同，不应用它计算跨模式加速比。

## 正式运行前提

1. 使用 Conda `py` 环境和 Release 可执行程序。
2. Git 工作区必须干净；默认会拒绝未提交更改。
3. 固定算例 profile、workers、队列和内存预算。
4. 每个配置至少预热一次、计量五次，以中位数为主要结果。
5. 保存生成的 JSON，不只抄录加速比。

示例：

```bash
conda run -n py python test/standard_cases/codes/benchmark_rayreuse.py \
  --case constant_speed_direct \
  --case munk_cerveny_cc \
  --profile broadband_regression \
  --modes nonreuse,reuse,parallel \
  --parallel-workers 8,10 \
  --queue 2 \
  --memory-budget-mib 2048 \
  --machine-label "Apple M4 MacBook Air, 10 cores, 16 GiB" \
  --warmups 1 \
  --repeats 5 \
  --executable Bellhop_RayReuse/build/release/bellhop_rayreuse \
  --output Bellhop_RayReuse/build/benchmarks/regression.json
```

开发中的非正式烟测可显式增加 `--allow-dirty`。报告会保留
`git.dirty = true`，此类结果不得作为发布性能记录。

## 分级运行策略

不要在每次修改后运行包含 nonreuse 的五轮 Munk 全矩阵。按用途分三级：

| 等级 | 用途 | 推荐配置 | 重复 |
|---|---|---|---:|
| smoke | 验证 runner、PRT、RSS 和哈希门 | direct 2频，所改模式 | 0 预热 + 1 计量 |
| tuning | 比较单项优化或 workers | Munk 2频；必要时16频 reuse/parallel | 1 预热 + 3 计量 |
| formal | 冻结发布或算法基线 | 16频全矩阵；必要时精选64频配置 | 1 预热 + 5 计量 |

正式 Munk 16频全矩阵包含 4 个配置时会执行
`4 × (1 + 5) = 24` 次 solver。提交 `c77ff60` 上四配置中位数之和约为
693.7 秒，因此整组预计约 69 分钟。运行前应先用最近中位数估算：

```text
预计总时长 =
sum(各配置单次 wall) × (warmups + repeats)
```

算法与 nonreuse 基线未变化时，tuning 阶段不重复 nonreuse；使用已经冻结的
正式报告作为历史参照，但新加速比只能表述为相对同轮 reuse。跨提交发布结论
仍须重新运行同轮基线。

## 采样协议

- 每个样本在独立目录和独立 Python helper 中运行，使用
  `resource.RUSAGE_CHILDREN.ru_maxrss` 记录唯一 solver 子进程的峰值 RSS；
- macOS 的原始 RSS 字节值归一化为 KiB，Linux 原始值按 KiB 记录，同时保留
  原始数值和单位；
- 每轮将配置顺序循环左移，降低固定顺序和温度漂移造成的偏差；
- 输入 ENV、每次 SHD 及跨配置 SHD 默认必须分别逐字节一致；
- PRT 必须包含成功标记、正确 execution mode、Trace passes，以及与请求一致
  的 workers、队列和预算回显；
- 输出采用原子替换，JSON 禁止 NaN/Infinity。

## 报告内容

JSON 记录 Git commit/tree/dirty 状态、可执行文件 SHA-256、平台、CPU/内存、
Python、NumPy、Conda 环境、CMake/C++ 工具版本、完整配置和轮换顺序。
在 macOS 等无法可靠查询具体芯片型号的平台，应通过 `--machine-label` 补充
可读硬件身份；系统探测字段仍会独立保留。
每个配置保留预热与原始计量样本，并汇总 wall、RSS 和 PRT 阶段时间的
median/min/max；存在 `nonreuse` 时，按外部 wall 中位数计算
`speedup_vs_nonreuse`。

报告默认写入 `Bellhop_RayReuse/build/benchmarks/`，该目录属于可再生成构建
产物，不进入 Git。需要归档时应将 JSON 连同对应提交或发布附件一起保存。
提交 `c77ff60` 的首轮正式结果与结论见
[`BENCHMARK_RESULTS_C77FF60.md`](./BENCHMARK_RESULTS_C77FF60.md)。
阶段 F1 提交 `96f23f8` 的 Munk 2频前后对照和诊断计数见
[`BENCHMARK_RESULTS_96F23F8.md`](./BENCHMARK_RESULTS_96F23F8.md)。
F1 关闭提交 `4af3f7f` 的线性压力访问及 2/16频确认见
[`BENCHMARK_RESULTS_4AF3F7F.md`](./BENCHMARK_RESULTS_4AF3F7F.md)。
