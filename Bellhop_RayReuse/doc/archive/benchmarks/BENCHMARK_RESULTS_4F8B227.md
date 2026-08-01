# Bellhop RayReuse F2 紧预算开销梯度：4f8b227

## 结论

在干净提交 `4f8b227` 上，以 constant-speed direct 和 acoustic-bottom
算例测量 reuse、parallel-8、parallel-10 的 2/16/64频梯度。每配置 1 次
预热、5 次计量；acoustic-bottom 当前没有 64频 profile。

| 算例 | 频率数 | reuse | p8 | p10 | p8 加速 | p10 加速 |
|---|---:|---:|---:|---:|---:|---:|
| direct | 2 | 0.0445 s | 0.0419 s | 0.0438 s | 1.063× | 1.017× |
| acoustic bottom | 2 | 0.6856 s | 0.5124 s | 0.5063 s | 1.338× | 1.354× |
| direct | 16 | 0.3377 s | 0.1011 s | 0.0954 s | 3.341× | 3.542× |
| acoustic bottom | 16 | 3.1819 s | 0.8826 s | 0.8834 s | 3.605× | 3.602× |
| direct | 64 | 2.4011 s | 0.5875 s | 0.5641 s | 4.087× | 4.257× |

三种配置在每个算例/profile 内的 SHD 均逐字节一致。

direct 的外部 wall 与 solver wall 中位数差为约 `3–4 ms`，
acoustic-bottom 为约 `7 ms`。2频 direct 的配置差异只有数毫秒且五轮范围
重叠，不能表述为稳定并行收益；工作量增至16频后固定进程、线程、队列与
分配器开销已被摊薄。Munk 64频的百秒级 wall 不是进程固定开销造成。

10 workers 在 direct 16/64频略快，在 acoustic-bottom 16频与 p8 相同，
在 Munk 64频则略慢。worker 最优值依赖每频任务成本、波动和资源争用，当前
仍以 Munk 主验收算例的 8 workers 为默认候选。

## 原始报告

- `Bellhop_RayReuse/build/benchmarks/tight_budget_smoke_f2_4f8b227.json`
  （SHA-256
  `06e30b3550593a979776016b8049795cef8f6b7d83bb6b9aa209dcb3466d576a`）
- `Bellhop_RayReuse/build/benchmarks/tight_budget_regression_f2_4f8b227.json`
  （SHA-256
  `29fbc59b029306c6714c7561acc5bd79bfce5c4a3cfecbf579d1a465996112f6`）
- `Bellhop_RayReuse/build/benchmarks/tight_budget_stress_f2_4f8b227.json`
  （SHA-256
  `81902b12c7f426e05749d53cb28d09e97afdab00fa5427202dacd0264f5c8736`）

JSON 是本机构建产物，不进入 Git。

## 下一步

1. 不再把进程启动或线程栈固定成本作为 Munk 优化重点。
2. 为并行 solver 增加默认关闭、低开销的逐频任务 wall/工作量记录。
3. 用 Munk 16频先确认任务负载分布；只有证据显示长尾或队列不均，才设计
   加权频率队列。
