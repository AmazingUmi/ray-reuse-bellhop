# Bellhop 工程展示

展示内容按用途分为两类，同时按 `cases/codes/results/figures` 管理输入、程序、
数值结果和图片：

```text
demo/
├── cases/
│   ├── reliability/                 # 三版本单频一致性输入
│   └── rayreuse_multifrequency/      # RayReuse 多频输入
├── codes/
│   ├── reliability.py               # 三版本运行、对比和绘图
│   ├── rayreuse_multifrequency.py    # 多频运行和选频绘图
│   ├── execution_modes.py            # 四条执行模式路线运行、对比和计时绘图
│   └── tests/                        # 展示代码单元测试
├── results/
│   ├── reliability/                 # origin/F2CPP/Broadband 单频结果
│   ├── rayreuse_multifrequency/      # 一个包含全部频率的 SHD
│   └── execution_modes/             # 每条路线一个目录的 SHD 与计时
├── figures/
│   ├── reliability/                 # 一致性图和差值图
│   ├── rayreuse_multifrequency/      # 多频传播损失图
│   └── execution_modes/             # 路线对比、差值和计时图
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

## 2. RayReuse 多频效果展示

多频环境：
[`cases/rayreuse_multifrequency/munk_rayreuse_multifrequency.env`](./cases/rayreuse_multifrequency/munk_rayreuse_multifrequency.env)

其频率记录为：

```text
50 100 150 200 250 / ! FREQS (Hz), RayReuse extension
```

这是 RayReuse 向后兼容的 `.env` 扩展；原版 Bellhop 和 F2CPP 仍使用标准单频
格式。展示命令不另外传频率参数，频率直接由 `.env` 提供。

```bash
# 一键计算全部频率并绘制 50、150、250 Hz
uv run make -C demo rayreuse-multifrequency

# 自定义要绘制的频率索引
uv run make -C demo rayreuse-multifrequency MULTI_INDEXES=0,1,3,4

# 将计算与绘图拆开
uv run make -C demo multifrequency-run
uv run make -C demo multifrequency-plot MULTI_INDEXES=0,2,4
```

对应的原生调用是：

```bash
mkdir -p demo/results/rayreuse_multifrequency
cp demo/cases/rayreuse_multifrequency/munk_rayreuse_multifrequency.env \
  demo/results/rayreuse_multifrequency/
cd demo/results/rayreuse_multifrequency
../../../Bellhop_Broadband/build/release/bellhop_broadband \
  munk_rayreuse_multifrequency --execution-mode reuse --reuse-mode serial
```

输出分类：

```text
demo/results/rayreuse_multifrequency/
├── munk_rayreuse_multifrequency.env
├── munk_rayreuse_multifrequency.prt
├── munk_rayreuse_multifrequency.shd
└── run_summary.json

demo/figures/rayreuse_multifrequency/
├── munk_rayreuse_multifrequency_selected_tl.png
└── munk_rayreuse_multifrequency_summary.json
```

## 3. 执行模式路线对比展示

复用多频环境：
[`cases/rayreuse_multifrequency/munk_rayreuse_multifrequency.env`](./cases/rayreuse_multifrequency/munk_rayreuse_multifrequency.env)

同一个多频 `.env` 分别按四条执行模式路线计算：

```text
nonreuse   → --execution-mode nonreuse                 （逐频重新追踪）
serial     → --execution-mode reuse --reuse-mode serial（trace once 逐频投影）
frequency  → --execution-mode reuse --reuse-mode frequency --reuse-workers 2
range      → --execution-mode reuse --reuse-mode range      --reuse-workers 2
```

展示三件事：四条路线的传播损失一致性（对 nonreuse 的逐频 max |ΔTL|）、
reuse 路线 trace-once 与 nonreuse 逐频重追踪的 Trace 相位计时差异，以及并行
reuse 路线的墙钟收益。计时图只对比墙钟总时长和 Trace 相位这两个在所有路线
语义一致的量；更细的 `Project/Influence/Scale/SHD` 分相数据在各路线的
`run_summary.json` 中（注意 frequency 路线的分相行是跨 worker 的 CPU 汇总，
大于墙钟值属正常）。

```bash
# 一键计算四条路线并出三张图
uv run make -C demo execution-modes

# 自定义路线、并行 worker 数与绘图频率索引
uv run make -C demo execution-modes \
  EXECUTION_ROUTES=nonreuse,serial,frequency EXECUTION_WORKERS=4 \
  EXECUTION_INDEXES=0,2,4

# 将计算与绘图拆开
uv run make -C demo execution-modes-run
uv run make -C demo execution-modes-plot EXECUTION_INDEXES=2
```

以 frequency 路线为例，展示脚本对应的原生调用是：

```bash
mkdir -p demo/results/execution_modes/reuse_frequency_w2
cp demo/cases/rayreuse_multifrequency/munk_rayreuse_multifrequency.env \
  demo/results/execution_modes/reuse_frequency_w2/
cd demo/results/execution_modes/reuse_frequency_w2
../../../../Bellhop_Broadband/build/release/bellhop_broadband \
  munk_rayreuse_multifrequency --execution-mode reuse \
  --reuse-mode frequency --reuse-workers 2
```

输出分类：

```text
demo/results/execution_modes/
├── nonreuse/munk_rayreuse_multifrequency.env|prt|shd
├── reuse_serial/munk_rayreuse_multifrequency.env|prt|shd
├── reuse_frequency_w2/munk_rayreuse_multifrequency.env|prt|shd
├── reuse_range_w2/munk_rayreuse_multifrequency.env|prt|shd
└── run_summary.json          # 每条路线的 CLI 参数、耗时与 PRT 分相计时

demo/figures/execution_modes/
├── munk_rayreuse_multifrequency_tl_comparison.png   # 选定频率 × 四路线
├── munk_rayreuse_multifrequency_tl_difference.png   # reuse 路线 − nonreuse
├── munk_rayreuse_multifrequency_timings.png         # 墙钟总时长 + Trace 相位
└── munk_rayreuse_multifrequency_summary.json        # 一致性指标与图清单
```

## 验证

```bash
uv run make -C demo test
uv run make -C test/PlotRead test
```
