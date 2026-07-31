# Bellhop RayReuse F2 64频精选矩阵：fdaaf56

## 结论

F2 安全局部性候选收敛后，在干净提交 `fdaaf56` 上运行 Munk/Cerveny
64频 reuse、parallel-8 和 parallel-10 精选矩阵。每配置 1 次预热、3 次
计量，计量轮次旋转配置顺序。

| 配置 | wall 中位数 | 三轮范围 | 范围/中位数 | RSS 中位数 | 相对 reuse |
|---|---:|---:|---:|---:|---:|
| reuse | 626.852 s | 623.967–627.289 s | 0.53% | 1,218,880 KiB | 1.000× |
| parallel-8 | 176.951 s | 162.662–181.517 s | 10.66% | 1,241,696 KiB | 3.543× |
| parallel-10 | 178.422 s | 167.067–182.113 s | 8.43% | 1,247,008 KiB | 3.513× |

parallel-10 中位数比 parallel-8 慢 `0.83%`。当前没有证据将默认 worker
从 8 改为 10；8 workers 继续作为后续调度分析基线。三配置 SHD SHA-256
均为
`a56c03b7c9fb6bb35ad36b764f09b7024c21e437852f709c67b378c67b5207f8`。

## 与16频的规模关系

相对 `7ce9c7d` 的 16频结果，64频 reuse/p8/p10 wall 分别增长
`7.158×/6.932×/7.524×`，不是单纯的频率数 `4×`：

- 16频 profile 为 50–500 Hz，最终共享发射角数为 5,000；
- 64频 stress profile 为 50–1000 Hz，phase criterion 和最终共享发射角数
  为 20,000；
- 频率数和最高频率共同扩大任务量；但高频波束窗口更窄，Influence 的有效
  图像贡献不会按“频率数 × 射线数”完整线性增长。

64频 reuse 的 Influence 中位数为 `621.719 s`，占 solver wall
`99.19%`。parallel 报告中的 Influence 是各 worker CPU 时间之和，不能与
外部 wall 直接相除；跨模式加速统一使用外部 wall。

## 运行身份

- 日期：2026-07-31
- Git commit：`fdaaf563f76a1f279c79d601b7a1a4ec5e7b512f`
- Git tree：`e360d10693049a406da20c6519583f9f90256fe9`
- worktree：`dirty = false`
- Release executable SHA-256：
  `ac49ec85291b588277a076edc8b79b2018508fdebe58b87d846705a741db5d9f`
- 平台：Apple M4，macOS 26.5.2 arm64，10 logical CPUs，16 GiB
- 工具链：Apple clang 21.0.0，CMake 4.0.2
- Python：Conda `py`

## 原始报告

- `Bellhop_RayReuse/build/benchmarks/munk_stress_f2_locality_converged_fdaaf56.json`
- JSON SHA-256：
  `12191f3f01a8a664ef20eaecb61808933fce7cdf3642cb8360cd1ef3ed0a43cc`

JSON 是本机构建产物，不进入 Git。

## 下一步

1. 保留 8 workers 作为当前默认候选，不采用 10 workers。
2. 用 direct/acoustic-bottom 紧预算梯度分离固定进程、线程栈、队列与分配器
   开销。
3. 为 Munk 频率任务取得低开销的逐任务 wall/工作量分布，判断 p8/p10 的
   波动来自负载不均还是内存带宽/调度争用。
4. 只有证据显示调度负载不均，才评估加权频率队列或更大范围的
   receiver/ray 调度重构。
