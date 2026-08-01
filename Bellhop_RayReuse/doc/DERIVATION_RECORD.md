# Bellhop RayReuse 派生记录

本文保存 `Bellhop_RayReuse` 的来源身份、独立工程边界、输入契约决策和阶段
验收状态。构建步骤及 A～E 阶段出口见 [`BUILD_PLAN.md`](./BUILD_PLAN.md)。

## 1. 来源身份

| 项目 | 记录 |
|---|---|
| 派生来源 | 已验收的 `Bellhop_F2CPP` 单频实现 |
| 来源提交 | `5889682` |
| F2CPP 源码树 SHA-256 | `d0916a0b4dfe67d90b67e20f3ca47221de5a410b367960ada0d8c1d16c781c79` |
| 派生目标 | 独立的 `Bellhop_RayReuse` C++20/CMake 工程 |
| 阶段 E 构建输入树 SHA-256 | `99d7bca60d444b75f5601800188a4650d9bfd2315d22d6930b0e61d0daac5256` |

提交标识用于定位仓库历史；源码树 SHA-256 用于识别实际派生内容。后续若
重新从 F2CPP 同步文件，必须记录新来源及新树哈希，不能静默覆盖本记录。
阶段 E 哈希由 CMake 文件以及 `app/cmake/include/src/tests` 下 80 个普通文件
的排序逐文件 SHA-256 再摘要得到，不包含构建产物和文档。

## 2. 独立工程身份

| 类别 | RayReuse 身份 |
|---|---|
| CMake project | `BellhopRayReuse` |
| C++ namespace | `rayreuse` |
| 公共头文件前缀 | `include/rayreuse/` |
| 核心静态库目标 | `bellhop_rayreuse_core` |
| 工程选项目标 | `bellhop_rayreuse_project_options` |
| 可执行目标/文件 | `bellhop_rayreuse` |
| Debug 构建目录 | `Bellhop_RayReuse/build/debug` |
| Release 构建目录 | `Bellhop_RayReuse/build/release` |

独立性的约束如下：

1. 不链接 `bellhop_f2cpp_core` 或任何其他 F2CPP 构建目标；
2. 不从 RayReuse 源码/CMake 跨目录包含 F2CPP 头文件或源码；
3. 不用 `add_subdirectory` 引入 `Bellhop_F2CPP`；
4. 测试可共享仓库级算例和数值 oracle，但 RayReuse 自身的编译、链接和运行
   不依赖 F2CPP 目录；
5. F2CPP 是派生来源和回归参照，不是运行时依赖。

## 3. 多频 CLI 契约

阶段 C 已冻结以下首版契约：

```text
bellhop_rayreuse <file-root>
bellhop_rayreuse <file-root> --frequencies-hz <f0,f1,...>
bellhop_rayreuse <file-root> --frequencies-hz <f0,f1,...> \
  --execution-mode parallel --workers 8 \
  --output-queue-capacity 2 --memory-budget-mib 4096
```

Bellhop `.env` 文件继续保存环境、边界、收发位置和传统单频值，不修改其
格式。未提供 `--frequencies-hz` 时使用 `.env` 中的单个频率并保持 F2CPP
兼容行为；提供该选项时，其完整向量覆盖 `.env` 的频率值。

- 频率列表必须非空、有限、为正且严格升序，重复项和降序均拒绝；
- `max(frequencies)` 用于一次规划共享发射角集合；
- 文件根仍指向现有 `.env` 及其关联 Bellhop 输入；
- 一次宽带调用生成一个包含完整频率轴的多频 SHD；
- `nonreuse`、`reuse`、`parallel` 分别保留逐频全追踪、串行复用和有界频率
  并行入口；
- 并行模式默认使用硬件并发数，完成队列容量限制为 1 或 2；可选内存预算按
  `RayPathCache + 活动工作区 + 排队工作区 + writer 消费工作区` 模型限制
  活动频率数；
- 逗号列表是首版 smoke/regression 契约；超大频率集合的清单文件属于后续
  兼容扩展，不改变数值核心接口。

## 4. 阶段 A 构建记录

| 检查项 | 状态 | 命令/证据 |
|---|---|---|
| Debug configure/build | PASS | Apple Clang 21，C++20，ASan/UBSan，`-Werror` |
| Debug CTest | PASS | 20/20 |
| Release configure/build | PASS | Apple Clang 21，C++20，`-Werror` |
| Release CTest | PASS | 20/20 |
| F2CPP 跨目录依赖扫描 | PASS | 源码/CMake及生成的依赖、链接文件零命中 |
| 标准算例工具测试 | PASS | Conda `py`，23/23 |
| 独立单频运行 | PASS | 六个 RayReuse single profile 均生成并校验 PRT/SHD |

以上是进入阶段 C 之前的阶段 A/B 冻结结果；阶段 C 新增测试后总数会增长，
不得用新总数回写或覆盖本基线。

## 5. 阶段 B 单频零漂移记录

2026-07-30 使用 Release RayReuse 对六个共享单频算例重新计算。相对派生
来源 F2CPP 的复压力和 TL 六例均为逐位零误差；相对 Fortran oracle 的结果
如下：

| 算例 | 最大压力绝对误差 | 最大压力相对误差 | 最大 TL 差 / dB |
|---|---:|---:|---:|
| `constant_speed_direct` | `9.33139788e-10` | `8.10358301e-7` | `7.62939453e-6` |
| `constant_speed_vacuum_rigid` | `7.27386151e-8` | `7.83636569e-5` | `3.12805176e-4` |
| `constant_speed_acoustic_bottom` | `5.03259017e-8` | `1.23803393e-5` | `9.15527344e-5` |
| `constant_speed_no_attenuation_5khz` | `2.33484565e-9` | `2.74698209e-6` | `2.28881836e-5` |
| `constant_speed_thorp` | `1.51682333e-9` | `1.90880564e-6` | `1.52587891e-5` |
| `munk_cerveny_cc` | `2.45444687e-9` | `3.08017406e-5` | `2.51770020e-4` |

六例均通过共享的复压力组合容差和 `1e-3 dB` TL 门。表中最大相对误差是
独立统计值，不是脱离绝对误差项单独使用的判据。

## 6. 阶段 C 宽带非复用记录

阶段 C 冻结的命令行为：

```text
bellhop_rayreuse <root> --frequencies-hz <f0,f1,...>
```

默认执行模式为 `nonreuse`。六例 `broadband_smoke`（2 频）和六例
`broadband_regression`（16 频）均由一次 RayReuse 进程生成一个多频 SHD，
并通过完整频率轴、维度、逐频有限值和非零压力校验。每份 PRT 分别记录
`Trace passes = 2` 和 `Trace passes = 16`，确认本阶段仍逐频完整追踪。

2 频结果相对共享 `fmax` 发射扇的 Fortran oracle 为 12/12 通过。相对 F2CPP
逐频运行是 9/12；三个低频差异来自 F2CPP 的单频 D-02 规划器按当前低频
重新生成较小射线扇，而 RayReuse/Fortran 宽带按最高频率使用共享射线扇：

| 算例低频 | F2CPP 射线数 | RayReuse/Fortran 共享射线数 |
|---|---:|---:|
| 5 kHz 无损案例的 1 kHz slice | 2,000 | 10,000 |
| 5 kHz Thorp 案例的 1 kHz slice | 2,000 | 10,000 |
| Munk 案例的 50 Hz slice | 1,000 | 5,000 |

对应高频 slice 与 F2CPP 均为零误差，三个低频 slice 相对 Fortran 也全部通过，
因此 F2CPP 多频逐跑仅作诊断，不作为共享 `fmax` 宽带主 oracle。

16 频 Release 非复用墙钟基线如下，供阶段 D trace-once 直接对照：

| 算例 | Trace / s | Project / s | Influence / s | 非复用墙钟 / s |
|---|---:|---:|---:|---:|
| `constant_speed_direct` | 0.394 | 0.018 | 0.399 | 0.813 |
| `constant_speed_vacuum_rigid` | 4.201 | 0.311 | 12.169 | 16.694 |
| `constant_speed_acoustic_bottom` | 4.424 | 0.495 | 3.851 | 8.783 |
| `constant_speed_no_attenuation_5khz` | 31.016 | 1.292 | 22.026 | 54.384 |
| `constant_speed_thorp` | 31.100 | 4.393 | 22.225 | 57.771 |
| `munk_cerveny_cc` | 5.937 | 0.473 | 264.650 | 271.163 |

这些计时是单次实际运行记录，不视为跨机器性能承诺。

## 7. 阶段 D 串行 Ray-Reuse 记录

串行复用模式只构建一次共享 `RayPathCache`，并按固定
`frequency → all cached paths` 顺序逐频完成投影、Influence 和缩放。六例
2 频 smoke 和六例 16 频 regression 均通过；相对阶段 C 非复用多频 SHD，
全部文件逐字节一致，PRT 均记录 `Trace passes = 1`。

`--verify-cache` 会在全部频率投影前后计算完整缓存语义指纹。真实 smoke 和
regression 验收中，六例指纹均保持不变；该选项默认关闭，避免把诊断哈希
成本计入常规性能数据。

16 频 Release 串行复用的单次实测如下：

| 算例 | Trace / s | 复用墙钟 / s | 相对非复用加速 |
|---|---:|---:|---:|
| `constant_speed_direct` | 0.025 | 0.438 | 1.856× |
| `constant_speed_vacuum_rigid` | 0.297 | 13.066 | 1.278× |
| `constant_speed_acoustic_bottom` | 0.296 | 4.738 | 1.854× |
| `constant_speed_no_attenuation_5khz` | 2.113 | 25.765 | 2.111× |
| `constant_speed_thorp` | 2.079 | 28.968 | 1.994× |
| `munk_cerveny_cc` | 0.397 | 271.484 | 0.999× |

Munk 算例的 Influence 占绝对主导，串行 trace-once 因此没有形成端到端收益，
这也是阶段 E 采用频率级有界并行的主要代表性性能目标。上述数据与阶段 C
一样是单次实测，不是跨机器性能承诺。

## 8. 阶段 E 有界频率并行记录

阶段 E 使用只读冻结缓存、频率独占 `FrequencyWorkspace`、容量 1～2 的完成
队列和主线程单 writer。worker 不直接写 SHD，也没有使用复压力原子累加。
流式 writer 支持乱序完成后按固定频率槽写入，并拒绝越界、重复和漏写。

2026-07-30 的正确性矩阵如下：

- Debug ASan/UBSan 与 Release 全量 CTest 均为 25/25；`parallel` 专项覆盖
  1、2、16 频、逐复数零差异、重复确定性、回调恰一次、异常传播及
  worker/队列/预算边界；
- 六例 2 频 smoke 和六例 16 频 regression 的并行 SHD 均与阶段 C 非复用
  基线逐字节一致，且全部 `Trace passes = 1`；
- `constant_speed_direct` 的 64 频 stress 并行/非复用结果逐字节一致，
  并行只追踪 1 次，非复用追踪 64 次；
- 447 MiB 的真实 CLI 预算把 acoustic-bottom 16 频的活动频率从 10 限制为
  6，估算峰值 `468,612,656` bytes 不超过预算 `468,713,472` bytes；
  开启指纹校验时前后均为 `11944258463452329496`。446 MiB 因连一个活动
  频率、队列和 consumer 工作区都无法容纳而按设计拒绝。

机器为 Apple M4 MacBook Air，10 核（4 性能核 + 6 能效核）、16 GB 内存；
Release 使用 Apple Clang 21.0.0、CMake 4.0.2。16 频单次实测如下：

| 算例 | workers | 并行墙钟 / s | 相对阶段 C 非复用加速 |
|---|---:|---:|---:|
| `constant_speed_direct` | 10 | 0.115 | 7.080× |
| `constant_speed_vacuum_rigid` | 10 | 3.056 | 5.464× |
| `constant_speed_acoustic_bottom` | 10 | 1.141 | 7.694× |
| `constant_speed_no_attenuation_5khz` | 10 | 8.067 | 6.742× |
| `constant_speed_thorp` | 10 | 8.512 | 6.787× |
| `munk_cerveny_cc` | 8 | 59.459 | 4.560× |

代表性 8-worker Munk 运行达到项目 `≥ 4×` 目标；zsh 计时记录为
`59.47 s real / 431.42 s user / 0.99 s sys / 632,560 KiB max RSS`，PRT 的
有界内存模型估算为 `644,299,288` bytes。64 频 direct 的默认 10-worker
并行墙钟为 `0.665 s`，非复用为 `6.448 s`，作为长频率轴调度与输出验证，
不替代 Munk 的 8-worker 代表性结论。

内存预算是可解释的缓存/工作区上界，不承诺限制运行库、线程栈和分配器的
全部 RSS；因此验收同时保留了实测 max RSS。所有计时均为本机单次/复测数据，
不视为跨机器性能承诺。

## 9. 可重复正式基准与阶段 F 决策

提交 `c77ff60` 增加可重复 benchmark 后，在干净工作区对 direct/Munk 16频
执行 1 次预热 + 5 次计量、逐轮轮换配置的正式矩阵。完整身份、样本范围、
ENV/SHD 哈希和报告 SHA-256 见
[`BENCHMARK_RESULTS_C77FF60.md`](./BENCHMARK_RESULTS_C77FF60.md)。

Munk 四配置每组 wall 中位数合计约 `693.7 s`，六组预计 `69.4 min`，实际
约 67 分钟；这是 24 次独立 solver 的总成本，不是单次 16频运行卡住。
正式结果如下：

| 配置 | wall 中位数 / s | 相对 nonreuse | RSS 中位数 / MiB |
|---|---:|---:|---:|
| nonreuse | 286.823 | 1.000× | 685.75 |
| reuse | 280.284 | 1.023× | 597.12 |
| parallel-8 | 64.829 | 4.424× | 616.03 |
| parallel-10 | 61.776 | 4.643× | 618.20 |

nonreuse Trace 只占 wall 的 `2.13%`，Influence 占 `97.42%`；串行 reuse
结果与只消除重复 Trace 的 Amdahl 上限一致。阶段 E 的 `≥4×` 门已关闭，
但继续增加 worker 不能解决 Influence 热路径本身的成本，且 10 workers
出现一次 76.432 s 抖动。

因此后续阶段 F 不先运行更昂贵的 Munk 64频全矩阵，而按以下顺序推进：

1. 默认关闭的 Influence 计数和分层计时；
2. 将冻结输入和完整压力工作区校验移出逐射线热路径；
3. 消除重复索引/边界检查并改善 range/depth 访问局部性；
4. 再独立评估 receiver tile、AoS/SoA 和向量化；
5. 用 2频 smoke → 16频三轮 tuning → 64频精选配置逐级验收。

所有 F 阶段优化默认不得改变每个接收点的复数贡献累加顺序；若确需改变，
必须单独定义数值误差门，不能沿用“逐字节一致”的验收表述。

## 10. F1 重复校验移出

提交 `96f23f8` 完成默认关闭的 Influence 工作计数、三段细分计时、跨频率
统计聚合和 `--profile-influence` 诊断入口。公共 `accumulate` 继续完整校验
外部输入；solver 仅在 `RayPathCache::freeze()` 和逐频投影校验完成后调用
私有预验证入口，最终缩放仍统一检查完整压力工作区。

干净 Munk 2频 reuse 在 1 次预热 + 3 次计量下，wall 中位数由
`20.1310 s` 降至 `18.7690 s`（`-6.77%`），Influence 由 `19.8713 s`
降至 `18.5109 s`（`-6.85%`），所有 SHD 继续逐字节一致。诊断运行确认
10,000 次射线累积的完整射线点/工作区重复扫描计数均为 0，剩余约
9.996 亿次 depth 和 29.987 亿次 image 评估集中在热循环。完整记录见
[`BENCHMARK_RESULTS_96F23F8.md`](./BENCHMARK_RESULTS_96F23F8.md)。

提交 `4af3f7f` 随后以一次 depth-major 线性索引取得压力单元引用，替代同一
贡献的两次 `workspace.at()`。Munk 2频 reuse 相对 `96f23f8` 再下降
`10.17%`，F1 累计下降 `16.25%`。16频 reuse、parallel-8、parallel-10
相对 `c77ff60` 分别下降 `18.04%`、`13.95%`、`10.68%`；三模式 SHD 与
旧基线逐字节一致。完整记录见
[`BENCHMARK_RESULTS_4AF3F7F.md`](./BENCHMARK_RESULTS_4AF3F7F.md)。

至此 F1 关闭，后续进入 F2 的 range-major 临时布局、receiver depth tile、
SoA 和向量化独立实验。

## 11. F2 布局筛选与图像专化

F2 首先完成两个保持接收点贡献顺序不变的 2频独立 screen。range-major
临时压力累加器需在结束时转回 depth-major，wall 为 `16.8910 s`，相对 F1
慢 `0.19%` 且增加约 1.6 MiB RSS；segment 内 range-batch 后按 depth →
range 累加为 `17.7544 s`，慢 `5.31%`。两者 SHD 均不变，但因无收益而完整
回滚，未运行16频矩阵。

提交 `eedc790` 随后将 `imageCount=1/2/3` 和 true/surface/bottom 图像类型
专化为模板实例，消除默认三图像热路径中的运行时循环、映射和分支，同时保持
图像及射线贡献顺序。相对 F1，Munk 2频 reuse wall 下降 `26.17%`；16频
reuse、parallel-8、parallel-10 分别下降 `29.62%`、`23.71%`、`27.05%`。
三配置 SHD 逐字节一致，完整质量门通过。记录见
[`BENCHMARK_RESULTS_EEDC790.md`](./BENCHMARK_RESULTS_EEDC790.md)。

F2 下一步先取得编译器向量化报告并审计已按字段分离的
`PrecomputedRayValues`，再选择不改变复压力累加顺序的独立候选。

AppleClang 21 向量化报告随后确认 `PrecomputedRayValues` 已是四个独立
vector；depth 循环主要受诊断控制流、复指数/有限性检查调用和压力依赖约束。
额外专化诊断路径使模板组合倍增，并令 2频 wall 回退 `46.34%`，已回滚。
提交 `fe6b33f` 改为保留公共 Hermite API 校验，仅让内部已验证 Influence
调用无重复参数校验。相对 `eedc790`，2频下降 `16.69%`；16频
reuse/p8/p10 下降 `16.15%/14.98%/23.43%`，SHD 不变，完整质量门通过。
记录见
[`BENCHMARK_RESULTS_FE6B33F.md`](./BENCHMARK_RESULTS_FE6B33F.md)。

提交 `f1511b9` 进一步明确有限性检查所有权：Release solver 热路径不再
逐图像/逐贡献检查，而由每频缩放入口扫描完整未缩放场；公共 API 返回前
另行扫描，Debug 和详细诊断保留即时检查。有限输入触发计算溢出的回归确认
公共 API 仍拒绝非有限结果。相对 `fe6b33f`，2频下降 `12.56%`；16频
reuse/p8/p10 下降 `15.99%/17.93%/9.15%`，SHD 不变。记录见
[`BENCHMARK_RESULTS_F1511B9.md`](./BENCHMARK_RESULTS_F1511B9.md)。

提交 `7ce9c7d` 将环境边界深度和当前 segment 右端幅度/相位显式提升出
depth 热循环。AppleClang 21 在当前对象、span 与压力写入组合下未完成等价
提升；局部标量使 2频相对 `f1511b9` 下降 `18.99%`，16频
reuse/p8/p10 下降 `23.10%/14.05%/15.31%`。SHD 不变，完整质量门通过。
记录见
[`BENCHMARK_RESULTS_7CE9C7D.md`](./BENCHMARK_RESULTS_7CE9C7D.md)。

后续 segment 端点引用与 position/slowness/sound speed/q/tau/gamma 差值
缓存使 2频 wall 回退 `3.69%`、Influence 回退 `3.71%`。这组额外局部对象
和复数差值未降低有效加载成本，反而增加寄存器压力；SHD 不变，源码已回滚，
未进入16频矩阵。

receiver-depth 数量缓存、接收深度标量和压力连续数据指针候选使 2频 wall
回退 `1.35%`、Influence 回退 `1.20%`。编译器已能等价提升这些简单访问，
显式局部量没有形成收益；SHD 不变，源码已回滚，未进入16频矩阵。下一项仅
比较 receiver depth tile。

64-depth receiver tile 保持原 depth 与压力累加顺序，但使 2频 wall 回退
`1.95%`、Influence 回退 `1.90%`。当前 range 内已顺序扫描 201 个接收深度，
额外 tile 边界没有改善工作集，只增加控制开销；SHD 不变，源码已回滚。
至此安全局部性候选收敛，进入 Munk 64频精选矩阵。

提交 `fdaaf56` 的干净工作区上完成 Munk 64频精选矩阵。reuse、p8、p10
wall 中位数为 `626.852/176.951/178.422 s`，相对 reuse 加速为
`3.543×/3.513×`；三配置 SHD 逐字节一致。64频 profile 将最高频率由
16频档的 500 Hz 提高到 1000 Hz，最终共享发射角数由 5,000 增至 20,000，
因此 wall 相对16频增长约 `6.93×–7.52×`，不是单纯4倍。p10 没有优于
p8，后续先测固定开销和逐频负载分布，不直接扩大调度重构。

提交 `4f8b227` 上的紧预算梯度表明，外部 wall 与 solver wall 的固定边界在
direct 中约 `3–4 ms`、acoustic-bottom 中约 `7 ms`。2频 direct 仅约
`40 ms`，并行差异落在波动范围；16频 direct/acoustic-bottom 已达到
`3.34×–3.61×`，64频 direct 达到 `4.09×–4.26×`。因此 Munk 的百秒级
耗时不由进程或线程固定成本主导，下一步只测逐频任务负载和长尾。

提交 `23e36aa` 将 parallel solver 已有逐频计时按显式开关导出，不增加默认
热路径计时。Munk 16频三轮显示任务耗时与频率的相关系数为
`-0.86～-0.94`，当前升频 FIFO 已先派发最慢的低频任务；按实测任务时长
模拟时，FIFO 与 LPT 完工时间相同。p10 逐频总 CPU 时间比 p8 高 `15.37%`，
说明限制来自资源争用而非队列不均。因此不实施加权队列，不扩大
receiver/ray 调度重构，F2 局部优化与调度诊断关闭。

## 12. 工程化与内部发布收口

2026-08-01 完成本地工程化出口：全量 C++ 采用项目 `.clang-format` 并以
`--dry-run --Werror` 验证；CMake compilation database 驱动 Clang static
analyzer；`bellhop_rayreuse --version` 输出 `Bellhop RayReuse 0.1.0`；CPack
生成只包含可执行程序和 README 的 TGZ，并在临时安装目录执行版本烟测。

扩展质量门最终覆盖 Debug/Release 25/25 CTest、Conda `py` 标准工具
50/50、PlotRead 9/9、独立性扫描和无 F2CPP 隔离构建 25/25。HDF5 只冻结
候选 schema v1，SHD 继续作为默认结果格式；顺序磁盘轨迹缓存因 64频代表性
RSS 未超过 2 GiB 触发预算而延后。

当前 TGZ 是内部验证构建，不是公开发行包：仓库尚无 LICENSE/NOTICE，也未
冻结最低 macOS 或跨平台矩阵；仓库无远端，因此 GitHub Actions 首次运行和
主分支必需检查仍需外部仓库配置后完成。

## 13. G0/G1 紧凑数值参考

2026-08-01 冻结三模型职责：原版 Fortran 为场结果主要 oracle，F2CPP 为
单频 C++ 派生一致性参考，RayReuse 为被验收对象。紧凑快照 schema v1 对
bearing/source depth/receiver depth 最多各取 3 点、range 最多取 5 点，
并加入全场最大复压力幅值点；首批六例 `single` 均为 16 点。

六个原版单频算例重新生成并通过既有 PRT/SHD 校验。快照总计约 62 KiB，
记录 ENV、SHD、可执行文件 SHA-256 和来源提交 `f35bbdd`；共同的原版
Bellhop 可执行文件 SHA-256 为
`f77b7bb60509fdb5e0f22b03a71c27ad4998718ba864e5206e5e48bd461ddcee`。
完整 SHD 仍被忽略，参考更新只能显式执行。

新增生成器、manifest 相对路径、采样策略、参考内部一致性、候选 SHD、
压力/TL/相位 floor 和矩阵切片测试后，标准工具测试为 62/62。G2 校验器会输出逐样本
失败原因及全局最大复压力绝对/相对误差、TL 差和包裹相位差；六个原版 SHD
自校验均通过。

G3 六例 single screen 全部通过：全场最大复压力绝对误差 `7.27e-8`、最大
TL 差 `3.13e-4 dB`，紧凑点最大相位差 `1.11e-6 rad`。六例两频 smoke
中，RayReuse 相对原版最大复压力绝对误差 `6.42e-8`、最大 TL 差
`7.93e-4 dB`，三个模式每例均逐字节一致。F2CPP D-02 忽略 ENV 显式
NAlpha 并按当前单频规划，因此 broadband 只在 `fmax` 门控；12 个低频失败
比较作为诊断保留，不影响原版→RayReuse 主门。Fortran 分阶段计时属于 G4。
干净提交 `9f254bd` 的最终身份、二进制哈希和误差汇总见
[`MODEL_MATRIX_RESULTS_9F254BD.md`](./MODEL_MATRIX_RESULTS_9F254BD.md)。

G4 为原版二维 Fortran 增加 `BELLHOP_PROFILE_STAGES=1` 显式开关，默认 PRT
和求解路径保持原状。direct single 的 Trace/Influence/Scale/Output 为
`0.00498/0.01167/0.000001/0.000198 s`；Munk single 为
`0.01580/2.63189/0.000030/0.000721 s`，总 CPU `2.65 s`。Munk Influence
占已分类阶段约 99%。direct 开启/关闭及 Munk profiled SHD 与冻结 oracle
逐字节一致；使用方法见
[`../../Bellhop_origin/STAGE_PROFILING.md`](../../Bellhop_origin/STAGE_PROFILING.md)。

G5 在干净提交 `06e390fc9338e2b94c29b9492027c3a59391dd5d` 关闭本地出口。
完整质量门通过 Debug/Release/隔离构建各 25/25 CTest、标准 Python 62/62、
PlotRead 9/9 和独立性扫描；工程门通过格式、Clang static analyzer、Release
安装烟测与 CPack。内部 TGZ SHA-256 为
`9b5e512ffe73c1e12f5da642e291dbbf2886d8b60ef288f747df705ae3b4ea08`。

G4 插桩后的原版可执行文件 SHA-256 为
`0b728f0879adc684adcdd1d87c077cadabaef11ddb570d763ff8ef2ebb813b0b`；由于
默认输出及 profiled SHD 与冻结 oracle 逐字节一致，G0/G1 快照继续保留其
生成时的 `f35bbdd` 来源和 `f77b7bb...` 可执行文件身份，不重新生成。最终
三模型矩阵仍为 12/12 通过、门控失败 0，误差上限与 `9f254bd` 轮一致；详细
身份见 [`MODEL_MATRIX_RESULTS_06E390F.md`](./MODEL_MATRIX_RESULTS_06E390F.md)。

本机工具链为 Apple clang/clang-format 21.0.0、GNU Fortran 14.2.0、CMake
4.0.2、Conda `py` Python 3.12.9 和 NumPy 2.2.6。`/usr/bin/time -l` 对 Munk
50 Hz 原版单频算例记录最大 RSS `3,342,336 B`（约 `3.19 MiB`）；同轮阶段
时间为 Trace `0.015576 s`、Influence `2.631201 s`、Scale `0.000029 s`、
Output `0.000635 s`。该 RSS 是本机构建的单次进程基线，不外推为跨平台上限。

## 14. 后续验收记录规则

每次关闭阶段出口时至少记录：

- 日期、Git 提交、编译器、CMake preset 和构建类型；
- 完整命令及退出状态；
- CTest/标准算例的通过总数与失败项；
- 数值比较的最大复压力绝对误差、组合相对误差和最大 TL 差；
- 宽带阶段的频率数、共享发射角数和追踪调用次数；
- 复用/并行阶段的线程数、活动频率上限、分阶段计时和峰值 RSS；
- 任何跳过项及原因。

尚未执行或尚未实现的项目只能记为“待验证”或“未实现”，不能记为通过。
