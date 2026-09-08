# Bellhop Broadband 可重复性能基准

## 目标与适用范围

`test/standard_cases/codes/benchmark_broadband.py` 用同一标准算例输入，按
Broadband 两层执行模型展开配置：`--execution-modes nonreuse,reuse` 与
`--reuse-modes serial,frequency,range`（默认
`nonreuse,reuse` × `serial,frequency`，即 nonreuse、Serial Reuse 与
8-worker Frequency Reuse）。它直接调用 Release 可执行程序
`bellhop_broadband`，并记录 reuse workers、输出队列容量和内存预算。

跨配置的主性能指标是外部测得的 `real_seconds`。PRT 内的 Trace、Project、
Influence、Scale、`Solver wall seconds` 和 SHD 时间用于定位热点；不同路线的
solver wall 覆盖范围并不完全相同，不应用它计算跨路线加速比。

## 正式运行前提

1. 使用根目录 uv 环境和 Release 可执行程序。
2. Git 工作区必须干净；默认会拒绝未提交更改。
3. 固定算例 profile、workers、队列和内存预算。
4. 每个配置至少预热一次、计量五次，以中位数为主要结果。
5. 保存生成的 JSON，不只抄录加速比。

示例：

```bash
uv run python test/standard_cases/codes/benchmark_broadband.py \
  --case constant_speed_direct \
  --case munk_cerveny_cc \
  --profile broadband_regression \
  --execution-modes nonreuse,reuse \
  --reuse-modes serial,frequency \
  --reuse-workers 8,10 \
  --queue 2 \
  --memory-budget-mib 2048 \
  --machine-label "Apple M4 MacBook Air, 10 cores, 16 GiB" \
  --warmups 1 \
  --repeats 5 \
  --executable Bellhop_Broadband/build/release/bellhop_broadband \
  --output Bellhop_Broadband/build/benchmarks/regression.json
```

`--reuse-workers` 是唯一的 worker 展开轴，同时作用于 Frequency Reuse 与
Range Reuse：同一份 CSV 会把 `reuse-frequency` 与 `reuse-range` 各展开为一
组对应 worker 数的配置。省略该旗标时保留既有路线默认：Frequency=8、
Range=1。配置层只有 `reuse_workers` 一个字段，按路线分别表示 frequency
workers 或 range workers；不要把 frequency-worker 与 range-worker 数据混在
一列。

开发中的非正式烟测可显式增加 `--allow-dirty`。报告会保留
`git.dirty = true`，此类结果不得作为发布性能记录。

需要诊断 Frequency Reuse 逐频任务分布时，将运行限制为
`--execution-modes reuse --reuse-modes frequency` 并增加
`--profile-frequency-tasks`。runner 会把每个频率已有的
Project/Influence/Scale/total 计时保存到样本 JSON；该开关默认关闭：

```bash
uv run python test/standard_cases/codes/benchmark_broadband.py \
  --case munk_cerveny_cc \
  --profile broadband_regression \
  --execution-modes reuse \
  --reuse-modes frequency \
  --reuse-workers 8,10 \
  --queue 2 \
  --memory-budget-mib 2048 \
  --profile-frequency-tasks \
  --warmups 1 \
  --repeats 3 \
  --executable Bellhop_Broadband/build/release/bellhop_broadband \
  --output Bellhop_Broadband/build/benchmarks/frequency_tasks.json
```

## 分级运行策略

不要在每次修改后运行包含 nonreuse 的五轮 Munk 全矩阵。按用途分三级：

| 等级 | 用途 | 推荐配置 | 重复 |
|---|---|---|---:|
| smoke | 验证 runner、PRT、RSS 和哈希门 | direct 2频，所改路线 | 0 预热 + 1 计量 |
| tuning | 比较单项优化或 workers | Munk 2频；必要时16频 frequency/range | 1 预热 + 3 计量 |
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
  的两层路线行（`execution mode = broadband nonreuse` 或
  `execution mode = broadband reuse` + `reuse mode = …`）、Trace passes，
  以及与请求一致的 workers、队列和预算回显；
- 输出采用原子替换，JSON 禁止 NaN/Infinity。

## 报告内容

JSON 记录 Git commit/tree/dirty 状态、可执行文件 SHA-256、平台、CPU/内存、
Python、NumPy、环境信息、CMake/C++ 工具版本、完整配置和轮换顺序。在
macOS 等无法可靠查询具体芯片型号的平台，应通过 `--machine-label` 补充
可读硬件身份；系统探测字段仍会独立保留。
每个配置保留预热与原始计量样本，并汇总 wall、RSS 和 PRT 阶段时间的
median/min/max；存在 `nonreuse` 时，按外部 wall 中位数计算
`speedup_vs_nonreuse`。

## 报告 schema 版本（当前 v4）

BB-M1-R1 起报告写入 `schema_version = 4`。核心字段是显式两层模型：

```json
{
  "execution_mode": "reuse",
  "reuse_mode": "frequency",
  "reuse_workers": 8
}
```

四种配置的取值为：

| execution_mode | reuse_mode | reuse_workers | 对应路线 |
|---|---|---|---|
| `nonreuse` | `null` | `null` | NonReuse |
| `reuse` | `serial` | `null` | Serial Reuse |
| `reuse` | `frequency` | `<N>` | Frequency Reuse（N = frequency workers） |
| `reuse` | `range` | `<N>` | Range Reuse（N = range workers） |

派生 identifier 语法保持稳定：`nonreuse`、`reuse-serial`、
`reuse-frequency-w<N>-q<Q>-m<M>`、`reuse-range-w<N>`；`reuse-frequency` 的
队列/内存参数只在该路线出现。identifier 只是展示与目录命名，不承载
execution/reuse 分层语义，不得当作 `execution_mode` 使用。

### 历史 schema（v3 / v2）

v2/v3 JSON 是历史文件，一律不修改，当前 runner 也不读取它们。跨版本比较
时按版本号区分并先做映射，不同版本的报告不得直接合并或对比。

v3（BB-1 至 BB-M1 期间）把路线压成单一 `execution_mode`
（`nonreuse` / `reuse-serial` / `reuse-frequency` / `reuse-range`），配置
字段为 `parallel_workers` / `fused_range_workers`，CLI 旗标为
`--parallel-workers` / `--fused-range-workers`。v3 → v4 映射：

| v3 | v4 |
|---|---|
| `execution_mode: reuse-serial` | `execution_mode: reuse` + `reuse_mode: serial` + `reuse_workers: null` |
| `execution_mode: reuse-frequency` + `parallel_workers: N` | `execution_mode: reuse` + `reuse_mode: frequency` + `reuse_workers: N` |
| `execution_mode: reuse-range` + `fused_range_workers: N` | `execution_mode: reuse` + `reuse_mode: range` + `reuse_workers: N` |
| identifier `reuse-frequency-w<N>-q<Q>-m<M>` / `reuse-range-w<N>` | 不变 |

v2（BB-M1 之前）使用旧词汇（`reuse`/`parallel`/`fused`；identifier
`parallel-w*` / `fused-range-w*`，裸 `fused` 配置无后缀；PRT 字段
`requested worker count` / `requested/effective range worker count`）。
v2 → v3 映射：

| v2（legacy） | v3 |
|---|---|
| `execution_mode: reuse` | `reuse-serial` |
| `execution_mode: parallel` | `reuse-frequency` |
| `execution_mode: fused` | `reuse-range` |
| identifier `parallel-w<N>-q<Q>-m<M>` | `reuse-frequency-w<N>-q<Q>-m<M>` |
| identifier `fused-range-w<N>` | `reuse-range-w<N>` |
| identifier 裸 `fused`（未展开 range workers） | `reuse-range-w1`（BB-1 冻结映射：未请求按显式 1 worker） |
| PRT `requested worker count` | `requested reuse worker count` |
| PRT `requested/effective range worker count` | `requested/effective reuse worker count` |

## PRT 路线标识与 wall 字段

新 PRT 的路线身份完全由两层标签表达：

```text
execution mode = broadband nonreuse
execution mode = broadband reuse
reuse mode = serial | frequency | range
```

四条路线的 solver wall 统一输出单一行名：

```text
Solver wall seconds = ...
```

不再按路线区分 wall 行名（v3 及以前的 `non-reuse wall seconds` /
`reuse wall seconds` / `parallel reuse wall seconds` /
`fused reuse wall seconds` 已随 BB-M1-R1 退役，只存在于历史 PRT 文件中）。
任何新增 PRT 解析方应先读两层路线行再取 `Solver wall seconds`，不要按
wall 行名反推路线。

runtime 统计行保留 `requested reuse worker count` /
`effective reuse worker count`，由各路线分别回显其 frequency/range worker
请求与生效值。

报告默认写入 `Bellhop_Broadband/build/benchmarks/`，该目录属于可再生成构建
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
