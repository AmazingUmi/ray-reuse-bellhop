# Bellhop RayReuse 正式基准记录：c77ff60

## 运行身份

- 日期：2026-07-30
- Git commit：`c77ff6027fd989b21025178efc01b3842cdb21e2`
- Git tree：`6f5686ef7c4f9323deae64fded7bfcf0d09c5c20`
- 工作区：`dirty = false`
- Release 可执行文件 SHA-256：
  `63c3f6e3d23c88a40f4460e862bde87bf8351f5358a091654d4fd375539bb169`
- 平台：Apple M4，macOS 26.5.2 arm64，10 logical CPUs，16 GiB
- 工具链：Apple clang 21.0.0，CMake 4.0.2
- Python：Conda `py`，CPython 3.12.9，NumPy 2.2.6

协议为每配置 1 次预热、5 次计量，配置顺序逐轮循环左移。并行配置固定
queue 2、memory budget 2048 MiB，比较 8/10 workers。跨模式主指标为 helper
测得的端到端 `real_seconds` 中位数；RSS 是隔离 solver 子进程的
`ru_maxrss`。所有样本的 ENV 与 SHD 分别逐字节一致，PRT 完成标记和参数回显
均通过严格校验。

## 16 频 constant-speed direct

| 配置 | wall 中位数 | 5 轮范围 | 范围/中位数 | 相对 nonreuse | RSS 中位数 |
|---|---:|---:|---:|---:|---:|
| nonreuse | 0.794 s | 0.790–0.828 s | 4.8% | 1.000× | 38.64 MiB |
| reuse | 0.453 s | 0.439–0.455 s | 3.5% | 1.754× | 38.25 MiB |
| parallel, 8 workers | 0.121 s | 0.113–0.124 s | 8.7% | 6.579× | 39.25 MiB |
| parallel, 10 workers | 0.115 s | 0.112–0.135 s | 19.7% | 6.911× | 39.42 MiB |

ENV SHA-256 为
`a42fb15af7e8e5976bfd25e7f2b13654950074429156a9d2162d4e9cd9d70002`，
SHD SHA-256 为
`edc818ea763eea92c1553818e2130d4021329a787242d3a2c45e06b4766cbb47`。

## 16 频 Munk/Cerveny

| 配置 | wall 中位数 | 5 轮范围 | 范围/中位数 | 相对 nonreuse | RSS 中位数 |
|---|---:|---:|---:|---:|---:|
| nonreuse | 286.823 s | 282.044–288.241 s | 2.2% | 1.000× | 685.75 MiB |
| reuse | 280.284 s | 276.679–280.939 s | 1.5% | 1.023× | 597.12 MiB |
| parallel, 8 workers | 64.829 s | 61.373–67.908 s | 10.1% | 4.424× | 616.03 MiB |
| parallel, 10 workers | 61.776 s | 59.591–76.432 s | 27.3% | 4.643× | 618.20 MiB |

ENV SHA-256 为
`9621c7766f90eec18c0369b33f61db6e3c13c273395f789e66c9a110f57f6fdb`，
SHD SHA-256 为
`f01ee48119549a82e79798322bf5227d8fc95054be82de955de5ccadef057c2c`。

Munk 的 Influence 明显主导总时间。串行 reuse 省去了重复 Trace，但相对
nonreuse 仅快 2.3%；频率并行才提供主要端到端收益。10 workers 在五轮中有
四轮快于 8 workers，中位数再快约 4.9%，但一次 76.432 s 样本将范围扩大到
27.3%。因此 10 workers 进入下一轮吞吐候选，尚不足以设为默认；8 workers
作为稳定性对照保留。

## 内存模型观察

2 GiB 预算未限制这两个 16 频算例：8/10-worker 的 active frequency limit
分别保持 8/10。Munk 中模型估算峰值与实测 RSS 中位数只差约
1.58/0.68 MiB（0.26%/0.11%），因为大轨迹缓存占主导；direct 的实测 RSS
则比模型高约 9.22/9.36 MiB（约 31%），反映固定进程、线程栈、运行库和
分配器开销在小工作集中的占比。后续预算模型应增加固定开销项，并用紧预算
梯度验证边界，而不能把当前 2 GiB 结果当成硬 RSS 上限证明。

## 原始报告

- `Bellhop_RayReuse/build/benchmarks/direct_regression_c77ff60.json`
  （SHA-256
  `e32bdcef89a43b15c3fc4006a96680ba173c48801ed066ea73d21d92c5ac18c9`）
- `Bellhop_RayReuse/build/benchmarks/munk_regression_c77ff60.json`
  （SHA-256
  `1dd1dc263915323077909e162b01d9da7dbc58d6d9ae0e1ea9b990c9981fbab5`）

原始 JSON 是本机构建产物，不进入 Git；本文件冻结可审阅结论和报告哈希。
完整采样规则与复跑命令见 [`BENCHMARKING.md`](./BENCHMARKING.md)。

## 下一步决策

1. 以 reuse、parallel-8、parallel-10 做 Munk 64 频的 1 次预热、3 次计量，
   不再重复成本极高的 64 频 nonreuse 全矩阵。
2. 仅当 10 workers 在 64 频仍稳定领先，且复测消除大抖动后，才考虑将其设为
   当前 M4 的推荐配置；跨机器仍需重新选择。
3. 在 direct 和 acoustic-bottom 上增加紧预算梯度，把固定进程开销、线程栈
   和分配器余量纳入预算记录。
