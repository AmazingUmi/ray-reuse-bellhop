# Bellhop F2CPP 性能阶段

> 当前状态：P1 baseline/profile 已完成；尚未实施性能优化。
> 基线日期：2026-08-16。

## P1 目标与边界

P1 只回答当前二维单频 Release 实现的时间、峰值内存和主要热点，不修改生产
代码、不增加 benchmark 框架，也不改变 I0～I8 与 B1～B4 的冻结语义。分配
次数和 cache miss 尚未测量；只有后续优化需要区分内存候选时才使用系统工具
做一次性诊断。

## 基线身份与协议

- Git：`531f5f60372d6c3a560ab6178062030d8e7134e8`，采样时 working tree clean；
- AppleClang Release 可执行文件 SHA-256：
  `52600a909bd38bfd762d4f20dcc46102a29fd14a31542c9eb0ff44b71ab96600`；
- 机器：MacBook Air `Mac16,12`，Apple M4，10 cores，16 GB；
- 工具：Apple clang 21.0.0、CMake 4.0.2、Conda `py` Python 3.12.9；
- 每个 workload 先 warmup 1 次，再测量 5 次；每轮旋转 workload 顺序；
- 每个样本使用独立 helper/solver 子进程；外部 `perf_counter` 记录 wall，
  `resource.RUSAGE_CHILDREN.ru_maxrss` 记录 peak RSS；
- 固定 `OMP_NUM_THREADS=1`、`OPENBLAS_NUM_THREADS=1`、
  `VECLIB_MAXIMUM_THREADS=1`；
- 原始 JSON 位于可再生成的
  `Bellhop_F2CPP/build/benchmarks/f2cpp_p1_baseline.json`，本轮文件 SHA-256 为
  `1c8228a3d017d28f9fb67d4e1d573ed2c96532f6430e5ef86e8a5dff9a1f84e9`，不进入
  Git。

## P1 结果

以下均为 5 次计量中位数；括号内是 wall 的 min～max。RSS 是独立 solver
子进程峰值。

| Workload | 规模 | Wall (s) | Peak RSS | PRT 阶段中位数 (s) |
|---|---|---:|---:|---|
| `munk_cerveny_cc` TL | 1000 rays；337079 points；201×501 receivers | 2.6761 (2.6690～2.6800) | 64.27 MiB | Trace 0.05918；Project 0.00371；Influence 2.60300；Scale 0.00400；SHD 0.00112 |
| `ray_trace_vacuum_rigid` R | 2 sources × 5 rays；5934 points | 0.00656 (0.00624～0.00692) | 2.31 MiB | Trace 0.00088；RAY 0.00277 |
| `arrival_line_directional_multisource` A | 2 sources × 300 rays；390640 points；480 candidates | 0.06750 (0.06680～0.06989) | 28.81 MiB | Trace 0.05725；Project 0.00191；Influence 0.00476；ARR 0.00030 |
| `eigenray_geometric_hat` E | 2 sources × 300 rays；316 hits；125494 prefix points | 0.12841 (0.12763～0.15220) | 25.11 MiB | Trace 0.04590；Project 0.00156；Influence 0.00367；RAY 0.07381 |

冻结 ray cache 分别为 63,514,512、368,056、24,120,912 和 19,485,760
bytes；这是 solver 报告的单 source 峰值 capacity footprint，不是所有 source
的累计分配量。

5 次计量及独立标准案例验证的产品 SHA-256 完全一致：

| Workload | Product SHA-256 |
|---|---|
| `munk_cerveny_cc` | `be3e6257bee54a021a0be5c983e2dd495fe2f1d0b5109ed32dc3e9636a8ba033` |
| `ray_trace_vacuum_rigid` | `04a966cadd65e298d4f2cb910948f8b5f96169807d53cdd103f0e26b7c877ce8` |
| `arrival_line_directional_multisource` | `0899cf03da4107ca4c6ff415111cc795e81e9aceafa23db0ec3c6b497b49d2f3` |
| `eigenray_geometric_hat` | `852cfd6de35c4d32e8d81b22a3dd1ee6ff6d8fdb26e0ff55a265b785f031bbb9` |

## 热点结论与 P2 入口

Munk TL 的 `Influence` 占外部 wall `97.27%`，Trace 只占 `2.21%`；PRT 已解释
`99.81%` 的 wall。一次 macOS `sample`（2 s、2 ms interval）中，主线程
648 个样本有 644 个位于
`CartesianCervenyInfluence::accumulate/accumulateImpl`。热点叶调用包含
boundary depth/environment getter、有限性检查、`sincos/exp`、receiver depth
和 workspace 访问；这些是采样线索，不单独视为优化收益证明。

A 的绝对时间很小且主要由 Trace 决定；E 主要由 4.7 MB ray-prefix 产品写出
决定；普通 R 低于 7 ms，进程启动和文件开销已不可忽略。因此 P2 只选择
Cartesian Cerveny Influence 热循环，先审查并实测循环不变量、receiver/
workspace 访问和 Release 校验边界。每个候选必须保持产品 SHA-256/Origin
容差，通过相关组件测试和 `f2cpp-regression`，并以同轮 1 warmup + 5 repeats
证明 wall 改善；不在首批直接引入 SIMD/OpenMP。

Peak RSS 的最大样本是 Munk `64.27 MiB`，与 `60.57 MiB` 的冻结 ray cache
footprint 同量级，说明后续仍需关注 cache layout；当前没有容量或 correctness
问题，P2 首批不以改数据布局为前置条件。

## 验收

- 四个 workload 的独立 `standard_cases.py test` 均通过，manifest 产品哈希与
  正式计量一致；
- `make -C test/standard_cases f2cpp-regression`：CTest 37/37、标准案例
  14/14；
- 本阶段未修改生产数值路径，因此未运行 `f2cpp-full`。
