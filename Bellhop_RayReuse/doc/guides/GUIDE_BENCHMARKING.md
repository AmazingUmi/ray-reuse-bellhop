# Bellhop RayReuse 可重复性能基准

## 目标与适用范围

`test/standard_cases/codes/benchmark_rayreuse.py` 用同一标准算例输入比较
`nonreuse`、`reuse-serial`、`reuse-frequency` 与 `reuse-range`（默认
`nonreuse,reuse-serial,reuse-frequency`）。它直接调用 Release 可执行程序
`bellhop_broadband`，并记录 reuse workers、输出队列容量和内存预算。

跨模式的主性能指标是外部测得的 `real_seconds`。PRT 内的 Trace、Project、
Influence、Scale、solver wall 和 SHD 时间用于定位热点；不同模式的 solver
wall 覆盖范围并不完全相同，不应用它计算跨模式加速比。

## 正式运行前提

1. 使用根目录 uv 环境和 Release 可执行程序。
2. Git 工作区必须干净；默认会拒绝未提交更改。
3. 固定算例 profile、workers、队列和内存预算。
4. 每个配置至少预热一次、计量五次，以中位数为主要结果。
5. 保存生成的 JSON，不只抄录加速比。

示例：

```bash
uv run python test/standard_cases/codes/benchmark_rayreuse.py \
  --case constant_speed_direct \
  --case munk_cerveny_cc \
  --profile broadband_regression \
  --modes nonreuse,reuse-serial,reuse-frequency \
  --parallel-workers 8,10 \
  --queue 2 \
  --memory-budget-mib 2048 \
  --machine-label "Apple M4 MacBook Air, 10 cores, 16 GiB" \
  --warmups 1 \
  --repeats 5 \
  --executable Bellhop_RayReuse/build/release/bellhop_broadband \
  --output Bellhop_RayReuse/build/benchmarks/regression.json
```

`--parallel-workers` 与 `--fused-range-workers` 是保留的历史旗标名，语义已
按 BB-1 执行模型改为 reuse workers 轴：前者把 `reuse-frequency` 展开为一组
`--reuse-workers` 配置，后者把 `reuse-range` 展开为静态 receiver-range
worker 配置，并使用相同的外部 wall/RSS/PRT/SHD 协议。不要把
frequency-worker 与 range-worker 数据混在一列。

开发中的非正式烟测可显式增加 `--allow-dirty`。报告会保留
`git.dirty = true`，此类结果不得作为发布性能记录。

需要诊断 reuse-frequency 逐频任务分布时，将模式限制为 `reuse-frequency`
并增加 `--profile-frequency-tasks`（该开关要求本次运行只含 reuse-frequency
模式）。runner 会把每个频率已有的
Project/Influence/Scale/total 计时保存到样本 JSON；该开关默认关闭：

```bash
uv run python test/standard_cases/codes/benchmark_rayreuse.py \
  --case munk_cerveny_cc \
  --profile broadband_regression \
  --modes reuse-frequency \
  --parallel-workers 8,10 \
  --queue 2 \
  --memory-budget-mib 2048 \
  --profile-frequency-tasks \
  --warmups 1 \
  --repeats 3 \
  --executable Bellhop_RayReuse/build/release/bellhop_broadband \
  --output Bellhop_RayReuse/build/benchmarks/frequency_tasks.json
```

## 分级运行策略

不要在每次修改后运行包含 nonreuse 的五轮 Munk 全矩阵。按用途分三级：

| 等级 | 用途 | 推荐配置 | 重复 |
|---|---|---|---:|
| smoke | 验证 runner、PRT、RSS 和哈希门 | direct 2频，所改模式 | 0 预热 + 1 计量 |
| tuning | 比较单项优化或 workers | Munk 2频；必要时16频 reuse-frequency/reuse-range | 1 预热 + 3 计量 |
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
- PRT 必须包含成功标记（`Bellhop Broadband completed successfully`）、正确
  的 execution mode 与 reuse mode 路线行（`execution mode = broadband
  nonreuse` / `execution mode = broadband reuse` + `reuse mode = …`）、
  Trace passes，以及与请求一致的 workers、队列和预算回显；
- 输出采用原子替换，JSON 禁止 NaN/Infinity。

## 报告内容

JSON 记录 Git commit/tree/dirty 状态、可执行文件 SHA-256、平台、CPU/内存、
Python、NumPy、环境信息、CMake/C++ 工具版本、完整配置和轮换顺序。
在 macOS 等无法可靠查询具体芯片型号的平台，应通过 `--machine-label` 补充
可读硬件身份；系统探测字段仍会独立保留。
每个配置保留预热与原始计量样本，并汇总 wall、RSS 和 PRT 阶段时间的
median/min/max；存在 `nonreuse` 时，按外部 wall 中位数计算
`speedup_vs_nonreuse`。

## 报告 schema 版本（v3）

BB-1 之后报告写入 `schema_version = 3`：路线词汇换为两层模型
（`execution_mode` ∈ `nonreuse` / `reuse-serial` / `reuse-frequency` /
`reuse-range`；identifier 语法 `reuse-frequency-w< N >-q< Q >-m< M >`、
`reuse-range-w< N >`；PRT 指标字段 `requested/effective reuse worker
count`）。BB-M1 之前的 v2 报告使用旧词汇（`reuse`/`parallel`/`fused`；
identifier `parallel-w*` / `fused-range-w*`，裸 `fused` 配置无后缀；PRT
字段 `requested worker count` / `requested/effective range worker count`）。
两组词汇不可混用：跨 BB-M1 边界比较时，
按版本号区分并先做下表映射，不同版本的报告不得直接合并或对比。

| v2（legacy） | v3（BB-1 起） |
|---|---|
| `execution_mode: reuse` | `reuse-serial` |
| `execution_mode: parallel` | `reuse-frequency` |
| `execution_mode: fused` | `reuse-range` |
| identifier `parallel-w<N>-q<Q>-m<M>` | `reuse-frequency-w<N>-q<Q>-m<M>` |
| identifier `fused-range-w<N>` | `reuse-range-w<N>` |
| identifier 裸 `fused`（未展开 range workers） | `reuse-range-w1`（BB-1 冻结映射：未请求按显式 1 worker） |
| PRT `requested worker count` | `requested reuse worker count` |
| PRT `requested/effective range worker count` | `requested/effective reuse worker count` |

有意保留的旧名（不随 v3 改动）：配置字段 `parallel_workers` /
`fused_range_workers` 与 CLI 旗标 `--parallel-workers` /
`--fused-range-workers`——它们是 reuse workers 轴的展开参数，语义已在
runner 中固定，彻底更名需要另行 bump schema 并迁移历史报告。

## PRT 标签的保留边界与消费者

PRT 中以下行保留 legacy 措辞，属于路线诊断的冻结输出，不随命名里程碑改动：

| PRT 行 | 所属路线 | 保留理由 | 消费者 |
|---|---|---|---|
| `fused reuse wall seconds` | reuse-range | 描述底层跨频 fused 实现的 solver wall | benchmark `parse_prt_metrics`（wall 字段按路线映射）、[REFERENCE_FEATURE_SUPPORT_MATRIX](../reference/REFERENCE_FEATURE_SUPPORT_MATRIX.md) IGR-3A A08 节的 serial/range PRT wall-seconds 差异描述（该文件不含这些行名的逐字引用）、本指南诊断说明 |
| `parallel reuse wall seconds` | reuse-frequency | 同上（frequency-parallel 实现） | 同上 |
| `non-reuse wall seconds` / `reuse wall seconds` | nonreuse / reuse-serial | 历史 solver wall 行名 | benchmark wall 映射、历史报告引用 |
| `single-frequency non-reuse` / `single-frequency reuse` | 单频 ARR/E 路线行前缀 | 单频产品的历史行措辞 | 当前无代码消费者（生产者事实：`app/main.cpp` 的 `writeProductExecutionMode`；runner 的 marker 校验只覆盖 broadband 路线标记） |

规范路线身份以两层新标签为准（`execution mode = broadband reuse` +
`reuse mode = …`、`requested/effective reuse worker count`）；上表 legacy 行
仅是 wall/前缀措辞。任何新增 PRT 解析方必须按路线映射 wall 字段，而不是
按字符串全局匹配 "reuse wall"。

报告默认写入 `Bellhop_RayReuse/build/benchmarks/`，该目录属于可再生成构建
产物，不进入 Git。需要归档时应将 JSON 连同对应提交或发布附件一起保存。
提交 `c77ff60` 的首轮正式结果与结论见
[`REPORT_OFFICIAL_BENCHMARK_C77FF60_2026-07-30.md`](../archive/benchmarks/REPORT_OFFICIAL_BENCHMARK_C77FF60_2026-07-30.md)。
阶段 F1 提交 `96f23f8` 的 Munk 2频前后对照和诊断计数见
[`REPORT_F1_BASELINE_96F23F8_2026-07-30.md`](../archive/benchmarks/REPORT_F1_BASELINE_96F23F8_2026-07-30.md)。
F1 关闭提交 `4af3f7f` 的线性压力访问及 2/16频确认见
[`REPORT_F1_PRESSURE_ACCESS_4AF3F7F_2026-07-30.md`](../archive/benchmarks/REPORT_F1_PRESSURE_ACCESS_4AF3F7F_2026-07-30.md)。
F2 提交 `eedc790` 的两个布局回滚实验、图像专化及 2/16频确认见
[`REPORT_F2_IMAGE_SPECIALIZATION_EEDC790_2026-07-31.md`](../archive/benchmarks/REPORT_F2_IMAGE_SPECIALIZATION_EEDC790_2026-07-31.md)。
F2 提交 `fe6b33f` 的向量化审计、诊断专化回滚、Hermite 快路径及
2/16频确认见
[`REPORT_F2_HERMITE_FAST_PATH_FE6B33F_2026-07-31.md`](../archive/benchmarks/REPORT_F2_HERMITE_FAST_PATH_FE6B33F_2026-07-31.md)。
F2 提交 `f1511b9` 的 Release 末端有限性校验边界及 2/16频确认见
[`REPORT_F2_FINITE_CHECKS_F1511B9_2026-07-31.md`](../archive/benchmarks/REPORT_F2_FINITE_CHECKS_F1511B9_2026-07-31.md)。
F2 提交 `7ce9c7d` 的环境/segment 循环不变量提升及 2/16频确认见
[`REPORT_F2_LOOP_INVARIANTS_7CE9C7D_2026-07-31.md`](../archive/benchmarks/REPORT_F2_LOOP_INVARIANTS_7CE9C7D_2026-07-31.md)。
