# Bellhop F2CPP 最终派生清单

> 最终快照日期：2026-07-29
> 状态：M2-12～M2-15 已通过；R-15 性能门保留完整冻结缓存并按 16 频
> trace-once 摊销收益验收。本清单是正式“允许派生 RayReuse”标记。
> 这是历史里程碑清单；I0～I4 后续扩展的当前状态以
> [`STATUS_PROGRESS.md`](../status/STATUS_PROGRESS.md) 为准，本文件中的哈希和测试数不得解释为
> 当前工作树身份。

## 1. 可追溯身份

| 项目 | 值 |
|---|---|
| Git 基线提交 | `ce2b8f7cb2f78cb8f703ce03fd121ad69b02375a` |
| F2CPP 源码树 SHA-256 | `d0916a0b4dfe67d90b67e20f3ca47221de5a410b367960ada0d8c1d16c781c79` |
| CMake | `4.0.2` |
| C++ 编译器 | Apple Clang `21.0.0`，arm64 |
| Fortran 编译器 | GNU Fortran `14.2.0_1` |
| 系统 | Darwin `25.5.0`，arm64，`Mac16,12` |
| 硬件 | 10 CPU cores，16 GiB physical memory |
| 数学模式 | 单线程、禁止 fast-math、内部 binary64/complex128 |

当前 F2CPP 文件尚位于未提交工作区；因此 Git 提交只表示项目基线，源码树
SHA-256 才表示本最终快照的精确 F2CPP 内容。校验和不含 `build/`、`doc/`
和根 `README.md`，可用下列命令重算：

```bash
cd Bellhop_F2CPP
{
  find CMakeLists.txt CMakePresets.json app cmake include src tests \
    -type f ! -path '*/__pycache__/*' ! -name '*.pyc' -print |
    LC_ALL=C sort | xargs shasum -a 256
} | shasum -a 256
```

## 2. 构建与验证命令

```bash
cd Bellhop_F2CPP
cmake --preset debug
cmake --build --preset debug --parallel
ctest --preset debug

cmake --preset release
cmake --build --preset release --parallel
ctest --preset release

cd ..
python3 -m unittest discover \
  -s test/standard_cases/codes/tests -p 'test_*.py'

python3 test/standard_cases/codes/standard_cases.py test \
  --version f2cpp --case all --profile single \
  --executable Bellhop_F2CPP/build/release/bellhop_f2cpp

cd Bellhop_F2CPP
python3 tests/tools/test_evaluate_amortized_performance.py
python3 tests/tools/evaluate_amortized_performance.py \
  --frequency-count 16 --minimum-savings-percent 1 \
  ../test/standard_cases/results/f2cpp/*/single/f000_*/*.prt
```

Python 标准算例工具要求 Python 3.11 或更高版本。

本最终状态的结果为：

- Debug ASan/UBSan：20/20 CTest；
- Release：20/20 CTest；
- 标准算例 Python：21/21；
- 六例 F2CPP PRT/SHD 结构校验：6/6；
- 六例相对 Fortran 的完整复压力/TL 比较：6/6；
- 摊销性能工具单元测试：3/3；
- R-15 六例 16 频 trace-once 门：6/6。

## 3. 六例数值证据

| 算例 | 最大压力绝对误差 | 最大压力相对误差 | 最大 TL 差 / dB |
|---|---:|---:|---:|
| `constant_speed_direct` | `9.33139788e-10` | `8.10358301e-7` | `7.62939453e-6` |
| `constant_speed_vacuum_rigid` | `7.27386151e-8` | `7.83636569e-5` | `3.12805176e-4` |
| `constant_speed_acoustic_bottom` | `5.03259017e-8` | `1.23803393e-5` | `9.15527344e-5` |
| `constant_speed_no_attenuation_5khz` | `2.33484565e-9` | `2.74698209e-6` | `2.28881836e-5` |
| `constant_speed_thorp` | `1.51682333e-9` | `1.90880564e-6` | `1.52587891e-5` |
| `munk_cerveny_cc` | `2.45444687e-9` | `3.08017406e-5` | `2.51770020e-4` |

压力使用 `difference <= 1e-7 + 1e-5*abs(reference)` 的组合判据；TL 门为
`1e-3 dB`。

## 4. 模块与公共契约

RayReuse 派生时应复制并保持语义的数值模块：

| 模块 | 关键接口/类型 | 派生要求 |
|---|---|---|
| `model` | `Environment`、`SimulationCase`、`LaunchFanPlan` | 保持 SI 单位、原 degree provenance 和共享 `fmax` 规划 |
| `numerics` | `Vec2` | 保持分量顺序为 range/depth |
| `ray` | `RayState`、`StepQuadrature`、`ReflectionEvent`、`RayPath`、`GeometryTracer` | 频率无关；保持独立 sin/cos 和 Fortran reduced-step 语义 |
| `cache` | `RayPathCache` | 追踪完成后冻结；宽带阶段不得写回逐频状态 |
| `acoustics` | 衰减转换、`evaluateBoundaryAcoustics` | 每频计算，不缓存到几何事件 |
| `field` | `FrequencyProjector`、`CartesianCervenyInfluence`、压力缩放 | 每频独立工作区；固定射线顺序累加 |
| `solver` | `SingleFrequencySolver` | 作为单频 oracle；RayReuse 需改为“一次追踪、逐频投影” |
| `io` | `.env` parser、`ShdWriter` | parser 可扩展频率集合；writer 仍只在边界量化到 complex64 |

以下规则不得在派生时改变：

1. `RayPath` 不包含复走时、吸收、反射幅相、active mask 或压力；
2. `FrequencyProjector` 只读使用 `StepQuadrature` 和 `ReflectionEvent`；
3. 每个频率独立计算 epsilon、边界声学、Influence 和缩放；
4. `FrequencyWorkspace` 内部为 `complex<double>`；
5. SHD writer 才是 `complex<float>` 量化边界；
6. 不链接 `bellhop_f2cpp_core`，也不跨目录包含 F2CPP 头文件；RayReuse
   必须拥有可独立构建的源码副本。

## 5. 派生文件分组

应复制到 RayReuse 数值核心：

```text
include/bellhop/{acoustics,cache,field,model,numerics,ray}/
src/{acoustics,cache,field,model,ray}/
include/bellhop/error.hpp
```

应复制后改造：

```text
include/bellhop/solver/
src/solver/
include/bellhop/io/
src/io/
app/
CMakeLists.txt
CMakePresets.json
cmake/
```

应作为独立回归种子复制，而不是与 F2CPP 共用运行时文件：

```text
tests/unit/
tests/component/
tests/support/
```

`tests/tools/geometry_oracle_probe.cpp` 和 `Bellhop_origin` 诊断仍由共同项目
测试入口使用，不应成为 RayReuse 运行时依赖。

## 6. 已关闭的性能与派生门

单频 Fortran 对比继续作为诊断数据：

| 算例 | Fortran CPU / s | F2CPP 分项总计 / s | 比值 |
|---|---:|---:|---:|
| direct | `0.0183` | `0.0358` | `1.95x` |
| vacuum/rigid | `0.804` | `1.138` | `1.41x` |
| acoustic bottom | `0.252` | `0.592` | `2.35x` |
| 5 kHz lossless | `0.609` | `1.831` | `3.01x` |
| 5 kHz Thorp | `0.609` | `1.913` | `3.14x` |
| Munk | `5.26` | `2.300` | `0.44x` |

5 kHz 用例缓存 10,129,596 点，报告缓存量 `1,225,680,032 B`，实测峰值
RSS `1,321,304,064 B`。Munk 报告缓存量 `62,718,712 B`，峰值 RSS
`66,125,824 B`。

表中 F2CPP 数值为同一 Release 二进制三轮运行的分项总计中位数。无损和
空间均匀投影快路径、零 taper 复指数短路、独立 Project/Influence 计时均已
纳入。除 Munk 外的单频比值包含完整轨迹缓存成本，按 R-15 不再作为
M2 阻断门。

R-15 使用：

```text
T_repeat(16) = 16 × (T_trace + T_freq)
T_reuse(16)  = T_trace + 16 × T_freq
T_freq       = T_project + T_influence + T_scale + T_shd
```

2026-07-29 串行专用运行的结果为：

| 算例 | 重复 F2CPP / s | trace-once / s | 摊销 / s·频点⁻¹ | 节省率 |
|---|---:|---:|---:|---:|
| direct | `0.578` | `0.343` | `0.021` | `40.73%` |
| vacuum/rigid | `17.908` | `13.569` | `0.848` | `24.23%` |
| acoustic bottom | `9.578` | `4.882` | `0.305` | `49.03%` |
| 5 kHz lossless | `29.425` | `12.993` | `0.812` | `55.84%` |
| 5 kHz Thorp | `30.140` | `14.533` | `0.908` | `51.78%` |
| Munk | `36.517` | `35.941` | `2.246` | `1.58%` |

六例均超过 `1%` 门槛。该模型不声称 F2CPP 已实现多频运行；它关闭的是
“完整冻结缓存是否具有可量化摊销价值”的派生门。实际宽带非复用、串行
复用和并行性能必须在独立 RayReuse 工程继续实测。M2-14/M2-15 至此关闭，
允许按第 4、5 节清单派生。
