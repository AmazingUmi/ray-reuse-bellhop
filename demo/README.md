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
│   └── tests/                        # 展示代码单元测试
├── results/
│   ├── reliability/                 # origin/F2CPP/RayReuse 单频结果
│   └── rayreuse_multifrequency/      # 一个包含全部频率的 SHD
├── figures/
│   ├── reliability/                 # 一致性图和差值图
│   └── rayreuse_multifrequency/      # 多频传播损失图
└── Makefile
```

输入文件保留在 Git 中；`results/` 和 `figures/` 都是可重新生成的展示产物，
不进入 Git。Python 默认通过仓库根目录的 uv 环境运行。

## 1. 一致性（可靠性）展示

基础环境：
[`cases/reliability/munk_cerveny_cc.env`](./cases/reliability/munk_cerveny_cc.env)

同一个 50 Hz Munk `.env` 分别交给原版 Bellhop、F2CPP 和 RayReuse，确认三者
都可直接生成 `.prt/.shd`，再对传播损失和复压力误差进行横向比较。

```bash
# 检查输入和三套程序
uv run make -C demo check

# 一键计算三套程序并绘图
uv run make -C demo all

# 单独计算并展示某个版本
uv run make -C demo origin
uv run make -C demo f2cpp
uv run make -C demo rayreuse

# 将计算与绘图拆开
uv run make -C demo run VERSIONS=origin,f2cpp,rayreuse
uv run make -C demo plot VERSIONS=origin,f2cpp,rayreuse
```

默认可执行文件：

```text
Bellhop_origin/bin/bellhop
Bellhop_F2CPP/build/release/bellhop_f2cpp
Bellhop_RayReuse/build/release/bellhop_rayreuse
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
├── rayreuse/munk_cerveny_cc.env|prt|shd
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
../../../Bellhop_RayReuse/build/release/bellhop_rayreuse \
  munk_rayreuse_multifrequency --execution-mode reuse
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

## 验证

```bash
uv run make -C demo test
uv run make -C test/PlotRead test
```
