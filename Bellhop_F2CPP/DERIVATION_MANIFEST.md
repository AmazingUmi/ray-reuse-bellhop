# Bellhop F2CPP 派生清单

> 候选快照日期：2026-07-27  
> 状态：M2-12/M2-13 已通过；M2-14 性能门尚未全部关闭，因此本清单可用于
> 审计和准备 RayReuse 派生，但不是最终“允许派生”标记。

## 1. 可追溯身份

| 项目 | 值 |
|---|---|
| Git 基线提交 | `ce2b8f7cb2f78cb8f703ce03fd121ad69b02375a` |
| F2CPP 源码树 SHA-256 | `3dc22f1b1b17b1dede0cab0710d9af15f3cddce033bec8d692b0c729747e1f56` |
| CMake | `4.0.2` |
| C++ 编译器 | Apple Clang `21.0.0`，arm64 |
| Fortran 编译器 | GNU Fortran `14.2.0_1` |
| 系统 | Darwin `25.5.0`，arm64，`Mac16,12` |
| 硬件 | 10 CPU cores，16 GiB physical memory |
| 数学模式 | 单线程、禁止 fast-math、内部 binary64/complex128 |

当前 F2CPP 文件尚位于未提交工作区；因此 Git 提交只表示项目基线，源码树
SHA-256 才表示本候选快照的精确 F2CPP 内容。校验和不含 `build/`、本文件、
README 和 BUILD_PLAN，可用下列命令重算：

```bash
cd Bellhop_F2CPP
{
  find CMakeLists.txt CMakePresets.json app cmake include src tests \
    -type f -print | LC_ALL=C sort | xargs shasum -a 256
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
```

Python 标准算例工具要求 Python 3.11 或更高版本。

本候选状态的结果为：

- Debug ASan/UBSan：20/20 CTest；
- Release：20/20 CTest；
- 标准算例 Python：21/21；
- 六例 F2CPP PRT/SHD 结构校验：6/6；
- 六例相对 Fortran 的完整复压力/TL 比较：6/6。

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

## 6. 尚未关闭的派生门

M2-14 当前数据：

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
纳入。Munk 已满足性能门；其他用例仍受完整轨迹缓存和通用 binary64 积分
状态成本影响，尚未达到“相对 Fortran 不慢于 20%”。在明确接受新性能门或
完成缓存布局/算法重构前，不应把本候选清单升级为最终 RayReuse 派生批准。
