# Bellhop 工程展示

展示内容按 `cases/codes/results/figures` 管理输入、程序、数值结果和图片：

```text
demo/
├── cases/
│   └── reliability/                 # 单频一致性与多频（16 频）执行路线输入
├── codes/
│   ├── reliability.py               # 三版本运行对比；routes 子命令 2×2 路线对比
│   ├── speed.py                     # 执行模式路线运行与速度对比
│   └── tests/                        # 展示代码单元测试
├── results/
│   ├── reliability/                 # 三版本单频结果
│   └── speed/                       # 四条路线的 SHD 与计时（唯一多频结果）
├── figures/
│   ├── reliability/                 # 一致性图与 2×2 路线对比图
│   └── speed/                       # 速度对比图
└── Makefile
```

输入文件保留在 Git 中；`results/` 和 `figures/` 都是可重新生成的展示产物，
不进入 Git。Python 默认通过仓库根目录的 uv 环境运行。

## 1. 一致性（可靠性）展示

基础环境：
[`cases/reliability/munk_cerveny_cc.env`](./cases/reliability/munk_cerveny_cc.env)

同一个 50 Hz Munk `.env` 分别交给原版 Bellhop、F2CPP 和 Bellhop Broadband，确认三者
都可直接生成 `.prt/.shd`，再对传播损失和复压力误差进行横向比较。

```bash
# 检查输入和三套程序
uv run make -C demo check

# 一键计算三套程序并绘图
uv run make -C demo all

# 单独计算并展示某个版本
uv run make -C demo origin
uv run make -C demo f2cpp
uv run make -C demo broadband

# 将计算与绘图拆开
uv run make -C demo run VERSIONS=origin,f2cpp,broadband
uv run make -C demo plot VERSIONS=origin,f2cpp,broadband
```

默认可执行文件：

```text
Bellhop_origin/bin/bellhop
Bellhop_F2CPP/build/release/bellhop_f2cpp
Bellhop_Broadband/build/release/bellhop_broadband
```

以 F2CPP 为例，展示脚本对应的原生调用是：

```bash
mkdir -p demo/results/reliability/f2cpp
cp demo/cases/reliability/munk_cerveny_cc.env \
  demo/results/reliability/f2cpp/
cd demo/results/reliability/f2cpp
../../../../Bellhop_F2CPP/build/release/bellhop_f2cpp munk_cerveny_cc
```

输出分类：

```text
demo/results/reliability/
├── origin/munk_cerveny_cc.env|prt|shd
├── f2cpp/munk_cerveny_cc.env|prt|shd
├── broadband/munk_cerveny_cc.env|prt|shd
└── run_summary.json

demo/figures/reliability/
├── munk_cerveny_cc_50Hz_comparison.png
├── munk_cerveny_cc_50Hz_difference.png
└── munk_cerveny_cc_50Hz_summary.json
```

## 2. 执行模式路线展示（一致性与速度）

多频环境：
[`cases/reliability/munk_rayreuse_multifrequency.env`](./cases/reliability/munk_rayreuse_multifrequency.env)

其频率记录为 50–800 Hz 共 16 个频率：

```text
50 100 150 200 250 300 350 400 450 500 550 600 650 700 750 800 / ! FREQS (Hz), RayReuse extension
```

同一个多频 `.env` 分别按四条执行模式路线计算（`speed.py` 负责运行并记录
计时）：

```text
nonreuse   → --execution-mode nonreuse                 （逐频重新追踪）
serial     → --execution-mode reuse --reuse-mode serial（trace once 逐频投影）
frequency  → --execution-mode reuse --reuse-mode frequency --reuse-workers 4
range      → --execution-mode reuse --reuse-mode range      --reuse-workers 4
```

一次运行产出两个视图：

- **数值一致性**（`reliability.py routes`）：2×2 传播损失对比图；summary
  JSON 记录三条 reuse 路线对 nonreuse 的逐频 `max |ΔTL|` 与
  `max |Δpressure|`（应为 0）。
- **运行速度**（`speed.py plot`）：墙钟总时长与 Trace 相位双面板对比——
  reuse 路线 trace-once 与 nonreuse 逐频重追踪的差异、并行 reuse 路线的
  墙钟收益。更细的 `Project/Influence/Scale/SHD` 分相数据在
  `run_summary.json` 中（注意 frequency 路线的分相行是跨 worker 的 CPU
  汇总，大于墙钟值属正常）。

```bash
# 一键计算四条路线并出两张图（2×2 一致性 + 速度对比）
uv run make -C demo speed

# 自定义路线与并行 worker 数
uv run make -C demo speed SPEED_ROUTES=nonreuse,serial,frequency SPEED_WORKERS=4

# 将计算与绘图拆开
uv run make -C demo speed-run
uv run make -C demo speed-plot SPEED_FREQUENCY_INDEX=2
```

以 frequency 路线为例，展示脚本对应的原生调用是：

```bash
mkdir -p demo/results/speed/reuse_frequency_w4
cp demo/cases/reliability/munk_rayreuse_multifrequency.env \
  demo/results/speed/reuse_frequency_w4/
cd demo/results/speed/reuse_frequency_w4
../../../../Bellhop_Broadband/build/release/bellhop_broadband \
  munk_rayreuse_multifrequency --execution-mode reuse \
  --reuse-mode frequency --reuse-workers 4
```

输出分类：

```text
demo/results/speed/
├── nonreuse/munk_rayreuse_multifrequency.env|prt|shd
├── reuse_serial/munk_rayreuse_multifrequency.env|prt|shd
├── reuse_frequency_w4/munk_rayreuse_multifrequency.env|prt|shd
├── reuse_range_w4/munk_rayreuse_multifrequency.env|prt|shd
└── run_summary.json          # 每条路线的 CLI 参数、耗时与 PRT 分相计时

demo/figures/speed/
├── munk_rayreuse_multifrequency_speed_comparison.png   # 墙钟总时长 + Trace 相位
└── munk_rayreuse_multifrequency_speed_summary.json     # 各路线计时

demo/figures/reliability/
├── munk_rayreuse_multifrequency_150Hz_routes_comparison.png   # 2×2 TL 一致性
└── munk_rayreuse_multifrequency_150Hz_routes_summary.json     # 逐频一致性指标
```

## 验证

```bash
uv run make -C demo test
uv run make -C test/PlotRead test
```
