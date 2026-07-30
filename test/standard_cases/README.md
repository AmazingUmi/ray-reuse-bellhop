# Bellhop 共享标准算例

这里是原版 Bellhop、Bellhop_F2CPP 和 Bellhop_RayReuse 共用的标准测试入口。
物理环境只定义一次；单频与多频是同一环境的不同 frequency profile。SHD
格式读取和人工绘图入口见 [`../PlotRead/README.md`](../PlotRead/README.md)。

## 目录

```text
standard_cases/
├── cases/                 不随求解器重复的基础环境
│   └── <case_id>/
│       ├── case.toml      元数据、频率 profiles、验收项
│       └── origin.env.in  原版 Bellhop 输入模板
├── codes/                 生成、运行、校验、比较及单元测试代码
│   ├── standard_cases.py  唯一命令行入口
│   ├── case_model.py
│   ├── compare_fields.py
│   ├── tolerances.toml
│   └── tests/
└── results/               生成输入、求解输出和运行清单
```

根目录只保留说明和 Makefile；业务内容严格归入 `cases/`、`codes/`、
`results/`。`results/` 中除说明文件外均可重新生成，不进入 Git。

## 算例与 profiles

| 算例 | 主要覆盖 |
|---|---|
| `constant_speed_direct` | 等声速直达场、无边界碰撞 |
| `constant_speed_vacuum_rigid` | 真空海面、刚性平底、多次反射 |
| `constant_speed_acoustic_bottom` | 有损声学半空间反射 |
| `constant_speed_no_attenuation_5khz` | 无体吸收参考 |
| `constant_speed_thorp` | 水体频率相关 Thorp 吸收 |
| `munk_cerveny_cc` | Munk SSP、折射、焦散区域和远程传播 |

算例参考自 `/Volumes/LYY/user_projects/Acoustics Toolbox to read/at/tests`，
但这里保存的是面向本项目回归目标的裁剪 fixture，不是逐字节副本。

各算例至少提供：

- `single`：单频组件和端到端回归；
- `broadband_smoke`：两频快速验证宽带数据流。

部分算例还提供 16 频 `broadband_regression` 和 64 频
`broadband_stress`。多频 profile 统一以最高频率计算发射角数目，然后
让原版 Bellhop 使用同一角度网格逐频运行，作为 RayReuse 的独立 oracle。

## 统一用法

默认 Python 为 `conda` 的 `py` 环境。先查看版本、算例与 profiles：

```bash
make -C test/standard_cases list
```

针对一个版本、一个算例和一个 profile，可独立执行任意环节：

```bash
# 只生成输入
make -C test/standard_cases generate \
  VERSION=origin CASE=munk_cerveny_cc PROFILE=single

# 只运行已经生成的输入
make -C test/standard_cases run \
  VERSION=origin CASE=munk_cerveny_cc PROFILE=single

# 只校验已有 PRT/SHD
make -C test/standard_cases validate \
  VERSION=origin CASE=munk_cerveny_cc PROFILE=single

# 从生成到校验的一体化测试
make -C test/standard_cases test \
  VERSION=origin CASE=munk_cerveny_cc PROFILE=single
```

`CASE=all` 可对指定版本/profile 运行全部适用算例。只有 `run` 和 `test`
需要求解器；若可执行文件不在默认位置，可增加
`EXECUTABLE=/absolute/path/to/bellhop`。

整体批量测试会先检查算例定义，再测试所有已有可执行程序支持的版本，并
默认覆盖单频和两频 smoke：

```bash
make -C test/standard_cases batch
```

也可缩小或扩大批量范围：

```bash
make -C test/standard_cases batch \
  VERSIONS=origin PROFILES=single,broadband_smoke
```

Makefile 只是轻量入口；脚本也可直接调用：

```bash
conda run -n py python test/standard_cases/codes/standard_cases.py \
  test --version origin --case munk_cerveny_cc --profile single
```

## 结果和比较

输出固定写入：

```text
results/<version>/<case>/<profile>/
├── run_manifest.json
└── fNNN_<frequency>Hz/
    ├── <root>.env
    ├── <root>.prt
    └── <root>.shd
```

运行清单记录频率向量、最高设计频率、共享发射角数、来源和各频率状态。
校验环节检查 PRT 运行模式、SHD 维度和频率、复压力有限性及非全零场。

比较两个 SHD 频率切片：

```bash
make -C test/standard_cases compare \
  REFERENCE=/path/reference.shd CANDIDATE=/path/candidate.shd
```

默认容差在 `codes/tolerances.toml`，程序会报告复压力绝对/相对误差与最大
TL 差异。

## 版本职责

| 版本 | 单频 profile | 多频 profile |
|---|---|---|
| `origin` | 一次原版 Bellhop | 共享 `fmax` 角度网格后逐频运行 |
| `f2cpp` | 已启用；生成兼容 PRT/SHD 并与原版逐场比较 | 可按共享 `fmax` 角度网格逐频运行，形成非复用参考 |
| `rayreuse` | 单元素频率向量 | 一次运行完整频率向量 |

`origin` 和 `f2cpp` 的执行/输入适配均已启用；`f2cpp` 默认可执行文件为
`Bellhop_F2CPP/build/release/bellhop_f2cpp`。RayReuse 的多频 CLI 契约完成后
再启用其适配器，无需改变 `cases/` 或调用方式。

迁移前的 `test_origin_bellhop` 和 `test_ray_reuse` 位于 `test/legacy/`，
仅作历史材料，不参与测试。PlotRead 使用独立生成的小型 fixture，不依赖
本目录结果。

## 后续补充

1. 冻结六个算例的复压力、TL、相位容差与紧凑参考采样。
2. 导出每步 `x/t/p/q/c/tau`、求积状态和终止原因。
3. 导出反射事件及单条射线 Influence 贡献。
4. 冻结 RayReuse CLI 后启用对应版本适配。
5. 为运行清单补充提交、编译器、编译选项、平台、线程和峰值内存。
