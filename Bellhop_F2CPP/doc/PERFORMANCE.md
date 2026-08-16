# Bellhop F2CPP 性能阶段

> 当前状态：P1～P3 scalar 优化与 P4-01 SIMD/线程并行可行性评估已完成；
> 尚未进入正式 SIMD、OpenMP 或数据布局实现。
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

## P3-01 剩余热点剖析与路线选择

P3 从 P2 checkpoint `27b10d25952168a927edebb0fcbfbe93bc1806bb`
的 clean AppleClang Release 开始，继续使用同一 Munk 输入、固定单线程、1 次
warmup + 5 次独立子进程计量。新的 clean baseline 为 wall `1.6536 s`、
Influence `1.5797 s`、peak RSS `65856 KiB`；SHD SHA-256 仍为 P1/P2 的
`be3e6257bee54a021a0be5c983e2dd495fe2f1d0b5109ed32dc3e9636a8ba033`。
原始 `f2cpp_p3_baseline.json` 位于 ignored build 目录，文件 SHA-256 为
`868d73e7c99ea50b777a1e55f63a44d37ca4f1a4769e4079db41375360abd148`。

三次 macOS `sample` 以 1 ms interval 共取得 4195 个主线程样本。以下为
P2 HEAD 的前五个直接叶级归属；dyld stub 和未命名的 libsystem math 内部叶
没有重复计入函数本体：

| 叶级位置 | 样本 | 主线程占比 |
|---|---:|---:|
| `accumulateImpl` 本体（loop/branch/index/load/store/复数算术混合） | 1958 | 46.67% |
| `__sincos_stret` | 526 | 12.54% |
| `requireFinite(double)` | 479 | 11.42% |
| `requireFiniteComplex` | 279 | 6.65% |
| `exp` | 268 | 6.39% |

把可明确归属的 dyld stub 合并后，sincos 至少占 `13.85%`，exp 至少占
`8.96%`；未命名 libsystem_m 内部叶会继续提高 transcendental 实际占比。
两个 finite helper 合计占 `18.07%`。`sqrt/hypot/norm` 没有形成稳定叶级
热点；ray-point preparation 中的 SSP evaluate 和 allocator 叶均低于约
`1%`，四个每-ray vector 的 4000 次 reserve/allocation 不是当前优先项。

低成本硬件证据使用系统 `/usr/bin/time -lp` 三次采集：instructions 为
`45.993～46.089 B`，cycles 为 `6.675～6.733 B`，IPC 为
`6.831～6.901`（中位数 `6.890`）；user/real 分别约 `1.63～1.65 s` 与
`1.65～1.67 s`。每轮只有 1 次 hard page fault，且无 swap 或文件 block I/O。
当前机器没有 `perf`，`xctrace` 缺少完整 Xcode，`powermetrics` 的 process
counter 需要 superuser，因此没有伪造 branch-miss/cache-miss 数值。现有证据
支持 compute/instruction-bound，而不支持因 60 MiB cache footprint 直接判为
memory-bound。

唯一即时 accepted scalar change 是强制内联本 translation unit 的两个 finite
helper；检查位置、条件和异常语义均未改变。独立 A/B 的 wall 为
`1.6536→1.5093 s`（`1.096×`），Influence 为 `1.5797→1.4374 s`
（`1.099×`），RSS `65856→65712 KiB`，SHD bitwise 一致。候选 JSON
`f2cpp_p3_inline_finite.json` 的 SHA-256 为
`9b8e8b9b301a050d3fd8c84e11720bc121f6a4eb82cd31e32f83b2f3b8a16604`。
优化后两次 sample 中 finite helper 已退出叶级榜首，`cervenyHermiteTaper`
成为稳定的 `18.51%` 叶级热点，sincos/exp 仍紧随其后。

路线优先级如下：

| 路线 | 预期收益 | 复杂度 / 数值风险 | 对后续 BARR/RayReuse 的影响 | 决定 |
|---|---|---|---|---|
| P3-A scalar/local | 下一步约 5～15% | 低～中 / 低 | 保持逐频 kernel 与冻结轨迹接口不变 | 第一优先；先做 Hermite hot-path specialization |
| P3-E exact math/complex | 约 5～15%，需 A/B | 中 / 中 | 必须跨频保持 Origin 误差门，禁止近似 libm | 第二优先，scalar 用尽后再评估 |
| P3-B SIMD | 总体约 1.15～1.5× 的潜力，不保证 | 高 / 中～高 | 可复用 kernel，但引入 ISA/vector-math 变体与跨频一致性负担 | 暂不实施 |
| P3-C OpenMP | 大网格有多核潜力；当前 Munk 无直接安全上限 | 高 / 中～高 | 内层并行会与 BARR 外层频率/source 并行竞争并可能 oversubscribe | 先设计并行所有权；不直接 parallel ray reduction |
| P3-D cache/data layout | 当前无可测收益依据 | 很高 / 中 | 会直接改变冻结轨迹/BARR 核心接口 | 不推荐 |

编译器 vectorization report 明确指出 image loop 因异常早退、控制流和 coherent
reduction recurrence 无法自动向量化。501-depth receiver 维具有稳定独立 cell，
但只有在将 checked/diagnostic slow path 与 hot kernel 分离并解决精确 vector
sincos/exp 后才值得显式 SIMD。OpenMP 对单 source Munk 最安全的潜在切分是
持久的 receiver-range tile 且保持每个 cell 的 ray 顺序；直接并行 ray 会引入
per-thread 大 workspace 和非确定 reduction，直接并行 501-depth 小循环则粒度
过细。多 source 外层最安全但对本 workload 无收益。

验收：AppleClang 与 GCC 14/Werror 的相关 Influence/solver CTest 均为 2/2，
独立 Munk 标准案例通过且产品 bitwise 一致，`f2cpp-regression` 为 CTest
37/37、案例 14/14，`git diff --check` 通过。P3-01 至此停止，不进入显式
SIMD、OpenMP、fast-math 或数据布局重构。

## P3-02 Hermite taper hot path

P3-02 从 P3-01 checkpoint `8b66477e2ce02f3902c6f73e135d3c6b007d68a9`
的 clean Release 开始。相同 Munk 协议的 before 为 wall `1.5087 s`、
Influence `1.4375 s`、peak RSS `65648 KiB`，产品哈希保持 replication
baseline。`cervenyHermiteTaper` 的输入层级为：`offset` 随 receiver 与 image
变化，`radiusMax` 对一条 ray 固定且由 source sound speed/frequency 决定，
zero radius 固定为 `2*radiusMax`；每个通过 beam-window 的 receiver cell 最多
为 true/surface/bottom 三个 image 各调用一次。多项式本身只有 abs、两次边界
分支、一次除法和三次乘加，18.5% 叶级热点主要来自极高调用频率、外部调用
边界及每次重复的 checked-path 控制流，而不是临时返回对象。

唯一 accepted change 将现有实现原样移入 TU-local
`cervenyHermiteTaperHot` 并强制内联；Cartesian diagnostic 与普通 contribution
路径直接调用该 helper，公开 `cervenyHermiteTaper` 接口保留并委托同一实现，
ray-centered 调用不变。finite 检查、半径约束、abs/branch/divide 和 polynomial
表达式顺序均未改变。曾尝试直接标记公开定义，但 GCC 14/Werror 正确拒绝了
缺少 `inline` 的 attribute；该中间版本未接受，最终内部 helper 同时通过
AppleClang 与 GCC 14。

独立 1 warmup + 5 repeats A/B：

| 状态 | Wall median | Influence median | Peak RSS | Product SHA-256 |
|---|---:|---:|---:|---|
| before | 1.5087 s | 1.4375 s | 65648 KiB | `be3e6257...a033` |
| hot helper | 1.4152 s | 1.3441 s | 65840 KiB | `be3e6257...a033` |

P3-02 wall speedup 为 `1.066×`，Influence speedup 为 `1.069×`；RSS 增加
`192 KiB`（约 `0.3%`），无实质容量变化。P3-01 before 到 P3-02 final 的
累计 wall speedup 为 `1.168×`。ignored build 中 `f2cpp_p302_before.json` 与
`f2cpp_p302_hot_helper.json` 的 SHA-256 分别为
`0fdb68d7e40c08a703d945db58cdd0ade2cabe77ff57a3d9464a6651d0302b84` 和
`5c5e1b7eef77a347fd5ce464c9a37eaa71e1b81446e68e08b3d1e3593e9b44d4`。

最终实现的两次 1 ms sample 共 2146 个主线程样本：

| 叶级位置 | 样本占比 |
|---|---:|
| `accumulateImpl` 本体（含已内联 taper 算术） | 62.12% |
| `__sincos_stret` | 14.35% |
| `exp` | 6.85% |
| libsystem_m 未命名 leaf `+0x1154` | 2.94% |
| libsystem_m 未命名 leaf `+0x10ac` | 2.80% |

`cervenyHermiteTaper` 的直接叶占比由 `18.51%` 降为 `0%`；Clang inline report
确认 Cartesian diagnostic/non-diagnostic 调用均已内联，二进制中唯一剩余的
公开函数调用来自未修改的 ray-centered 路径。这里的 `0%` 表示调用热点消失，
不是多项式被删除；其必要算术已归入 `accumulateImpl`。

验收：AppleClang 与 GCC 14/Werror focused Influence/solver CTest 均为 2/2；
独立 Munk 标准案例通过且 SHD bitwise 一致；`f2cpp-regression` 为 CTest
37/37、案例 14/14；`git diff --check` 通过。Hermite 已不再是独立显著热点，
P3-02 按停止条件结束，不继续 hoist 检查或改写 polynomial，也不自动优化
sincos/exp。

## P3-03 sincos/exp 调用审查

P3-03 从 P3-02 checkpoint `965d9c61f30e96c311dd31ee18e72d85e6266c53`
的 clean Release 开始，继续使用相同 Munk、单线程、1 次 warmup + 5 次独立
子进程协议。clean baseline 的 wall 为 `1.4180 s`、Influence 为 `1.3472 s`、
peak RSS 为 `65824 KiB`；SHD SHA-256 仍为
`be3e6257bee54a021a0be5c983e2dd495fe2f1d0b5109ed32dc3e9636a8ba033`。
ignored build 中 `f2cpp_p303_before.json` 的 SHA-256 为
`266671e901e09f5dff96ea2baa5f7d2b2b271ba2fa6305a5cb440a3214d344ea`。

每个通过 window/taper 检查的 receiver × image 候选只构造一次完整复相位，
随后 `negativeImaginaryExponential` 精确执行一次 `exp(imag)` 和一次
sin/cos 对。AppleClang 已把源代码中的同参数 `cos` 与 `sin` 合并为单次
`__sincos_stret`，不存在可再合并的两次三角函数调用。一次性计数得到
`67,155,371` 个实际传播候选，即同数目的 `exp` 与 `sincos`；振幅为零、
实相位为零和虚相位为零的候选均为 0，因而没有可保持表达式不变的零值短路。

`angularFrequency` 是整次计算不变量；插值后的走时、复慢度、gamma、振幅和
反射相位在一个 ray segment/range 上稳定；但 image/receiver 改变
`deltaDepth`，实相位同时含线性与二次项，虚相位也含二次项。由此三个 image
不是简单固定相移，通常没有相同的 transcendental 参数。对调用流中前两个
传播候选作精确复相位比较，只找到 `324,785` 个重复值（`0.484%` 上界），
集中于边界退化位置；即使假定缓存零成本，按 P3-02 的 sincos/exp 样本占比，
其 wall 理论上限也约为 `0.1%`，不足以抵消比较、分支和缓存状态成本。

没有接受生产修改。把基相位、线性项和二次项预乘到外层，或用 receiver/image
递推生成相位，虽可能减少 scalar 乘加，却会改变现有浮点运算顺序，并不减少
每个唯一相位所需的 sincos/exp；更换 libm、近似函数和 lookup table 又超出
本阶段精度约束。因此 P3-03 的 before/final 是同一个可执行文件（SHA-256
`44c4cb2f43eb563e5266c7809d221d04eb3b8ccdfb6957b42aeca62bf91c3e7b`），
speedup 记为 `1.000×`，RSS 和产品哈希均不变。

最终具名热点继续采用 P3-02 的冻结 sample：`accumulateImpl` 本体 `62.12%`、
`__sincos_stret` `14.35%`、`exp` `6.85%`，以及两个未命名 libsystem_m leaf
`2.94%/2.80%`。AppleClang focused Influence/solver CTest、独立 Munk 标准
案例与 `f2cpp-regression` 均通过，SHD 与 replication baseline 逐字节一致。
P3-03 按停止条件关闭单线程低风险 scalar 优化；后续若继续，应单独评估精确
vector-math/SIMD 或保持 cell 内 ray 顺序的线程级切分，不在本阶段自动实施。

## P4-01 Parallel/vectorization decision

P4-01 从 `e7e09cdd3f472877e4759fb8d4882cebe976969b` 的 clean Release
开始，只保留一次性编译诊断和两个 `/tmp` 原型，未修改正式数据布局或提交
生产代码。正式 AppleClang Release 可执行文件 SHA-256 为
`44c4cb2f43eb563e5266c7809d221d04eb3b8ccdfb6957b42aeca62bf91c3e7b`。
工作负载为既有 50 Hz Munk（1000 rays、337079 points、201×501 receivers）
及同一标准案例已有 `broadband_smoke` 250 Hz 输入（5000 rays、1683973
points、201×501 receivers）；后者足以放大 ray 维度，因此没有新增 case。

### SIMD / vector math

AppleClang 的 `-Rpass{,-missed,-analysis}=loop-vectorize` 与 GCC 14 的
`-fopt-info-vec-all` 均未向量化 receiver/image 热循环。AppleClang 报告 image
loop 的 reduction/recurrence、可能抛出异常的 early exit 和不可 select 化的
控制流；GCC 对 `accumulateImpl` 报告 `0 loops`，depth loop 因包含连续内层
image loops 无法向量化。GCC 只对少量复数/Vec2 scalar 表达式做了 128-bit
SLP，不是跨 receiver 的 vector kernel。

receiver depth 对一个 ray 本可形成 201-lane 独立集合，但每个 cell 有
window/taper 分支、1～3 image 的 coherent reduction、finite/异常保护，以及
最终 workspace load/add/store。标准 C++20 和当前 GCC 工具链没有保持 scalar
libm 结果的 portable double-vector sincos/exp；本机可用的 Apple Accelerate
`vvsincos/vvexp` 又是平台专用接口。

一次性原型从真实 Munk 调用流每 64 次抽取一个复相位，共 `1,048,576` 个输入，
固定 `VECLIB_MAXIMUM_THREADS=1` 比较 scalar libm 与 Accelerate batch。9 次中位数
为 `0.0092643→0.0025008 s`（math-only `3.705×`），但结果并不 bitwise：

| 结果 | 与 scalar 不同的输入 | 最大 ULP |
|---|---:|---:|
| exp | 278726（26.58%） | 1 |
| sin | 664808（63.40%） | 3 |
| cos | 665249（63.44%） | 3 |
| 合成复指数 | 889657（84.84%） | — |

合成复指数最大绝对/相对差分别为 `4.5183e-16` / `6.4981e-16`。差异来自
vForce 与 scalar libm 不同的 range reduction/舍入，而非降精度；它已经足以
否定现有 bitwise 契约。即使忽略 phase gather、branch compaction、scatter、
临时数组和小 batch 调用开销，并把 P3-02 的 sincos+exp `21.2%` 全部按
`3.705×` 加速，Amdahl wall 上限也只有约 `1.18×`。因此不接受 SIMD 原型；
若未来单独批准非 bitwise 数值门，还必须端到端量化 SHD 累加误差和跨平台
实现，不应作为当前首选。

抽样 phase binary 与 vector probe 输出 SHA-256 分别为
`bfd34622c11461e96a289291f02fa9dd7e125e862532053b1ee995b966aac28d`、
`f96bafccaf8f87602947be849c63e661a30eab4b932368cf6cbf7b47fa21f3a5`；
AppleClang/GCC vectorization report SHA-256 分别为
`5eb09b0a8cf85f3005b1e9ec194fd31f99237f7d1b3948200047fe9192176edb`、
`f1d832c0f0f48598ace02515d084a9be3ddc32b556fa8572151b68a8cd53957d`。

### Thread-level parallelism

一次性 AppleClang+libomp 原型在每条 ray 的 `accumulateImpl` 内建立一个线程
team，按连续 receiver-depth stripe 静态分工；所有线程共享只读 ray/frequency
状态并写入互不重叠的 workspace rows，ray 返回前 join。每个 cell 仍严格按
原 ray 顺序累加，未采用 ray reduction。depth-major workspace 下每个线程拥有
完整 rows，false sharing 只可能出现在少量 stripe 边界；原型没有 per-thread
pressure workspace，仅有线程栈和运行库开销。

以下为 `OMP_DYNAMIC=FALSE`、`OMP_PROC_BIND=SPREAD`、`OMP_PLACES=CORES` 的
1 warmup + 5 repeats（50 Hz）或 1 warmup + 3 repeats（250 Hz）中位数。
Speedup/efficiency 均相对同一个 OpenMP 原型的 1-thread；RSS 单位 KiB。

| Workload | Threads | Wall (s) | Influence (s) | Peak RSS | Speedup | Efficiency |
|---|---:|---:|---:|---:|---:|---:|
| 50 Hz / 1000 rays | 1 | 1.4949 | 1.4230 | 66224 | 1.000× | 100.0% |
| | 2 | 0.9370 | 0.8632 | 66144 | 1.595× | 79.8% |
| | 4 | 0.6394 | 0.5625 | 66240 | 2.338× | 58.4% |
| | 8 | 0.5705 | 0.4898 | 66496 | 2.620× | 32.8% |
| 250 Hz / 5000 rays | 1 | 5.0670 | 4.7418 | 312336 | 1.000× | 100.0% |
| | 2 | 3.1974 | 2.8719 | 312320 | 1.585× | 79.2% |
| | 4 | 2.3624 | 2.0114 | 312416 | 2.145× | 53.6% |
| | 8 | 2.3103 | 1.9744 | 312544 | 2.193× | 27.4% |

所有线程数的 SHD 分别保持冻结 SHA-256
`be3e6257bee54a021a0be5c983e2dd495fe2f1d0b5109ed32dc3e9636a8ba033`
和 `a7fb6b38edd6f55f66cd41b117e97b14d496a17da2789fd57a4210ab4593e825`；
线程数之间及相对正式串行输出均逐字节一致。最大线程增量 RSS 只有 272 KiB
和 208 KiB，没有 per-thread field 或 RayPathCache 复制。

OpenMP 原型可执行文件 SHA-256 为
`9be86839ff1d04d2c2072deb6c5e9135d7005e5d9893ab6390083d871d5416f1`；
50/250 Hz 原型 JSON SHA-256 为
`cf1084c5b7b4b954bb1658fe75fb5bfd83e36f04b43b96edf3efe1b08f76303e`、
`d1024955220b3547898a48a7d26ac652f39d1575895f4280f4ab2843acfc7726`。
正式串行 JSON SHA-256 为
`00e40ad0dc7c098d3cb0495a078e5fbac9648a4d65065a0fd25fbec5330259ba`、
`f6a5b013d9ce85c29e6edef879805d529522499ed9ac2b772b69fa99f75e57f3`；
原始记录保存在 ignored `build/benchmarks`，不进入 Git。

同轮正式串行 wall/Influence 为 50 Hz `1.4156/1.3453 s`、250 Hz
`4.6380/4.3148 s`。原型 1-thread 因每 ray 建立 parallel region，分别慢
`5.6%/9.2%`；即便包含该开销，8-thread 相对正式串行 wall 仍为 `2.48×/2.01×`。
本机只有 4 个 performance cores，两个 workload 都在 4 threads 后明显饱和，
8 threads 的 efficiency 不足以支持默认使用全部 logical cores。

原型未保留为生产修改，因为 OpenMP region 内直接抛出异常不能满足当前错误
传播契约，且每 ray fork/join 不是正式设计。若进入 P4-02，建议拆分
prepare 与 cell accumulation，在 solver/source 生命周期建立持久 team，以
静态 depth tiles 消费共享的逐-ray prepared state；每 ray barrier 后再前进，
从而保持 cell 内累加顺序。异常应由线程局部捕获、join 后在调用线程重抛。

source-level 并行在现有架构中数值风险最低，因为各 source 已拥有独立 cache
和 workspace；但单-source Munk 无收益，且它与未来 BARR/RayReuse 的
frequency/source 外层并行占用同一核心预算。正式接口应只有一个 parallel
owner：F2CPP 单 source 可选择 receiver-depth tiles，多 source 或 BARR 可选择
更外层 task，禁止隐藏的 nested OpenMP/oversubscription。

P4-01 的结论是优先线程并行而非 SIMD：它在两个真实 workload 上已有
`2.0～2.5×` 绝对收益、保持 bitwise 且 RSS 近似不变；SIMD 的理想 wall 上限
较小并立即破坏 bitwise。正式实现以 4 threads 为首个目标并保留 1-thread
默认/回退，但需新的明确批准。本阶段恢复原始源码后，focused CTest 2/2、
Munk 标准案例、`f2cpp-regression`（CTest 37/37、案例 14/14）及
`git diff --check` 均通过。
