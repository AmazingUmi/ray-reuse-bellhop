# Bellhop F2CPP 性能阶段

> 当前状态：P1 baseline/profile 与 P2 Cartesian Cerveny 局部热循环优化已完成；
> 尚未进入数据布局、SIMD 或并行阶段。
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

## P2 Cartesian Cerveny 热循环

P2 保持 P1 的 `munk_cerveny_cc` Release、单线程、1 次 warmup + 5 次正式
测量协议。P2-before 位于 `7977fe682a929c542e08b8a7c8e153a475702b34`，
可执行文件与 P1 的 SHA-256 相同；候选版本虽在未提交 working tree 中逐项
测量，但每项均记录独立可执行文件哈希。没有修改射线缓存布局、算术累加顺序、
有限性保护、Arrival/Eigenray/Trace/writer，也没有引入 SIMD 或 OpenMP。

审查确认的热点浪费包括：receiver × image 循环内重复读取固定海面/海底深度和
设置；重复取得 receiver 布局、深度与诊断索引；相邻射线/逐频点的重复索引；
以及每个 coherent cell 两次执行已在入口验证过维度的
`FrequencyWorkspace::at` 边界检查和扁平索引。P1 sampling 中相应叶调用包括
boundary/environment getter、receiver accessor 和 workspace accessor；
`sincos/exp` 与逐贡献有限性检查仍属于剩余计算，本轮没有移动或削弱。

三个低风险优化点按顺序独立构建、测试和 A/B。下表为每一步完成后的 5 次
中位数；speedup 是相对紧邻前一步，RSS 单位为 KiB。

| 状态 / accepted change | Wall before→after (s) | Influence before→after (s) | Wall / Influence speedup | Peak RSS |
|---|---:|---:|---:|---:|
| 稳定循环量、边界深度、相邻点和 image kind hoist | 2.6642→2.0494 | 2.5916→1.9689 | 1.300× / 1.316× | 65840→65792 |
| 缓存 receiver 布局/向量并直接读取冻结深度 | 2.0494→1.9590 | 1.9689→1.8770 | 1.046× / 1.049× | 65792→65856 |
| 一次取得 pressure span，按已验证维度直接访问 cell | 1.9590→1.6565 | 1.8770→1.5816 | 1.183× / 1.187× | 65856→65824 |

相对 P2-before，最终 wall speedup 为 `1.608×`，Influence speedup 为
`1.639×`；wall 降低约 `37.8%`。最终 RSS 中位数为 `65824 KiB`，相对
before 的 `65840 KiB` 没有实质变化。四轮 SHD SHA-256 均为
`be3e6257bee54a021a0be5c983e2dd495fe2f1d0b5109ed32dc3e9636a8ba033`，
与 P1 baseline 逐字节一致。

原始 JSON 位于 ignored build 目录，不进入 Git：

| 记录 | 文件 | JSON SHA-256 |
|---|---|---|
| before | `f2cpp_p2_before.json` | `0e5c9b21ec3de0078ce6c97c5a1ee65ab7d81fba8cee1ab87f20ac9307ac9294` |
| invariant hoist | `f2cpp_p2_invariants.json` | `2cafb92d91be0eec8289fbb1a41ecce885d9793e265442d6c88d528a906212b1` |
| receiver cache | `f2cpp_p2_receivers.json` | `be6312dfb456308460d650f7d595dc07d18d0091e3650e7ebea600619fb7f74a` |
| workspace span | `f2cpp_p2_workspace.json` | `b462bd61edb12db381368060e643f4575b2a6c25b98570ee130315deb2d71a0a` |

每个 accepted change 后相关 Cartesian Influence 与 single-frequency solver
CTest 均为 2/2；最终独立 Munk 标准案例通过且产品哈希一致；
`make -C test/standard_cases f2cpp-regression` 为 CTest 37/37、标准案例
14/14；`git diff --check` 通过。由于结果 bitwise 一致且改动仅限已定位的
局部热循环，本轮未运行 `f2cpp-full`。

P2 在低风险局部收益已经明显时停止。剩余绝对热点仍是 Cartesian Cerveny
Influence（最终约 1.58 s），主要工作包含每个 receiver/image 必需的复数
传播、`sincos/exp` 和保留的 correctness 检查；另有每条 ray 的四个临时
预计算向量。是否进入 P3 应先单独批准和重新取样；数据布局、SIMD、OpenMP
及更激进的校验边界调整均不属于本阶段。
