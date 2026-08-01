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
│   ├── benchmark_rayreuse.py
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

conda run -n py python test/standard_cases/codes/standard_cases.py \
  test --version rayreuse --case munk_cerveny_cc \
  --profile broadband_smoke --rayreuse-execution-mode nonreuse

# 验证一次追踪、多频投影的复用路径
conda run -n py python test/standard_cases/codes/standard_cases.py \
  test --version rayreuse --case munk_cerveny_cc \
  --profile broadband_smoke --rayreuse-execution-mode reuse
```

## 结果和比较

`origin`、`f2cpp` 以及 RayReuse 的 `single` profile 保持逐频目录：

```text
results/<version>/<case>/<profile>/
├── run_manifest.json
└── fNNN_<frequency>Hz/
    ├── <root>.env
    ├── <root>.prt
    └── <root>.shd
```

RayReuse 多频 profile 由一次求解产生完整频率轴，因此使用单一宽带目录：

```text
results/rayreuse/<case>/<profile>/
├── run_manifest.json
└── broadband/
    ├── <case>_<profile>_broadband.env
    ├── <case>_<profile>_broadband.prt
    └── <case>_<profile>_broadband.shd
```

宽带 `.env` 的频率字段使用 profile 首频，发射角数仍按 profile 的最高频率
统一计算。运行时适配器只调用一次 `bellhop_rayreuse`，并传入 `<root>`、
`--frequencies-hz <严格升序逗号列表>` 以及
`--execution-mode <nonreuse|reuse|parallel>`。标准 runner 的
`--rayreuse-execution-mode` 默认是 `nonreuse`，只对 RayReuse 多频运行生效；
`origin`、`f2cpp` 和 RayReuse 单频调用不传此参数。清单中的每个频率记录都
映射到同一个 PRT/SHD，并通过 `execution_model`、`execution_mode` 和
`broadband_run.expected_solver_invocations` 明确运行方式。

运行清单记录频率向量、最高设计频率、共享发射角数、来源和各频率状态。
宽带校验要求 SHD 第一维等于频率数、完整频率轴与 profile 一致、其余维度
与算例定义一致，并逐频检查复压力有限且非全零。PRT 必须报告所选 execution
mode；`nonreuse` 的 `Trace passes` 必须等于频率数，`reuse` 和
`parallel` 则必须等于 1。

比较两个 SHD 频率切片：

```bash
make -C test/standard_cases compare \
  REFERENCE=/path/reference.shd CANDIDATE=/path/candidate.shd
```

默认容差在 `codes/tolerances.toml`，程序会报告复压力绝对/相对误差与最大
TL 差异。

## RayReuse 性能基准

`codes/benchmark_rayreuse.py` 复用相同 case/profile 和输出校验，直接比较
`nonreuse`、`reuse`、`parallel`。它支持固定 workers、队列和内存预算，按
轮次旋转配置顺序，并将外部 wall、隔离 max RSS、PRT 阶段计时、输入/SHD
哈希及运行元数据写入 JSON。正式基准默认拒绝脏工作区；协议和推荐命令见
[`../../Bellhop_RayReuse/doc/BENCHMARKING.md`](../../Bellhop_RayReuse/doc/BENCHMARKING.md)。

## 版本职责

| 版本 | 单频 profile | 多频 profile |
|---|---|---|
| `origin` | 一次原版 Bellhop | 共享 `fmax` 角度网格后逐频运行 |
| `f2cpp` | 已启用；生成兼容 PRT/SHD 并与原版逐场比较 | D-02 会按当前单频重新规划，仅 `fmax` 切片与共享扇语义等价 |
| `rayreuse` | 单元素频率向量 | 一次运行完整频率向量 |

`origin` 和 `f2cpp` 的执行/输入适配均已启用；`f2cpp` 默认可执行文件为
`Bellhop_F2CPP/build/release/bellhop_f2cpp`。RayReuse 适配器已启用，默认
可执行文件为 `Bellhop_RayReuse/build/release/bellhop_rayreuse`；single
profile 不传频率参数，多频 profile 使用一次 `--frequencies-hz` 调用。
多频调用同时显式传递 `--execution-mode`；可由 runner 的
`--rayreuse-execution-mode nonreuse|reuse|parallel` 选择，默认
`nonreuse`。

三模型本地矩阵使用原版作为 broadband 主 oracle；F2CPP 在 `single` 全频
门控，在 broadband 仅 `fmax` 切片门控，低频差异仍进入报告但不作为失败，
因为 F2CPP 按设计忽略 ENV 显式 NAlpha。运行入口为：

```bash
conda run -n py python test/standard_cases/codes/model_matrix.py
```

迁移前的 `test_origin_bellhop` 和 `test_ray_reuse` 位于 `test/legacy/`，
仅作历史材料，不参与测试。PlotRead 使用独立生成的小型 fixture，不依赖
本目录结果。

## 后续补充

1. 按 [`REFERENCE_SNAPSHOTS.md`](./REFERENCE_SNAPSHOTS.md) 冻结并校验六个
   算例的复压力、TL、相位规则与紧凑参考采样。
2. 导出每步 `x/t/p/q/c/tau`、求积状态和终止原因。
3. 导出反射事件及单条射线 Influence 贡献。
4. 将 benchmark 元数据中已具备的提交、工具链、平台、线程和峰值内存按需
   回填到通用运行清单。
