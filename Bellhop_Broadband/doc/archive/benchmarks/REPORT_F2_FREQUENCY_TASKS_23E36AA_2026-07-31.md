# Bellhop RayReuse F2 逐频任务分布：23e36aa

## 结论

提交 `23e36aa` 增加默认关闭的 `--profile-frequency-tasks`。parallel solver
原本已保存每个频率的 Project/Influence/Scale 计时，本提交只在显式请求时
将其写入 PRT 和 benchmark JSON；默认热路径没有新增时钟或同步。

在干净提交上对 Munk 16频 p8/p10 运行 1 次预热、3 次计量：

| 配置 | wall 中位数 | 三轮范围 | 逐频总 CPU 时间中位数 | 最长任务 | 最短任务 |
|---|---:|---:|---:|---:|---:|
| p8 | 26.422 s | 25.934–27.563 s | 184.424 s | 19.607 s | 6.380 s |
| p10 | 24.123 s | 22.675–24.863 s | 212.773 s | 21.877 s | 9.314 s |

最长任务均为 50 Hz，任务耗时总体随频率上升而下降；三轮 Pearson 相关系数
范围为 p8 `-0.86～-0.90`、p10 `-0.93～-0.94`。当前按升频顺序取任务，
因此已经近似最长任务优先。

用每轮实测任务时长模拟动态列表调度，当前 FIFO 与将任务按耗时降序排列的
LPT 完工时间逐轮完全相同。加权频率队列不能改善这组负载分配，不应实施。

p10 的逐频总 CPU 时间中位数比 p8 高 `15.37%`，但16频 wall 仍低
`8.70%`；更多 worker 带来资源争用，同时在16个任务上仍能缩短关键路径。
结合64频正式矩阵中 p10 比 p8 慢 `0.83%`，最优 worker 数随工作量和资源
争用变化，不能由队列顺序解释。

三配置历史与本轮 SHD 均保持
`f01ee48119549a82e79798322bf5227d8fc95054be82de955de5ccadef057c2c`。

## 验收

提交前完整运行：

```bash
RAYREUSE_BUILD_JOBS=4 Bellhop_RayReuse/scripts/quality_gate.sh
```

Debug 24/24、Release 24/24、Python 50/50、独立性扫描和无 F2CPP 隔离
Release 构建 24/24 全部通过。另以 direct 2频完成 CLI → PRT → benchmark
JSON 端到端验证。

## 原始报告

- `Bellhop_RayReuse/build/benchmarks/munk_frequency_tasks_23e36aa.json`
- JSON SHA-256：
  `c15def9b6b930a5ca06d5ad21c49059e03d1fe2c263ae45d63c9e8119e945f97`

JSON 是本机构建产物，不进入 Git。

## 下一步

1. 不实施加权频率队列，也不扩大 receiver/ray 调度重构。
2. 将 worker 选择视为资源争用问题：以 p8 为 64频主验收基线，p10 作为
   16频吞吐候选。
3. F2 局部优化与调度诊断至此关闭；下一阶段优先补工程化出口（CI 首次远端
   运行、格式化/静态分析、版本化发布），或在新增硬件基线后再研究自适应
   worker 策略。
