# Bellhop F2CPP

本目录用于实现裁剪后的优化版 C++ 单频 Bellhop，负责项目里程碑 M1/M2：

1. 组件级复刻 SSP、中心射线、动态射线、边界反射和 Cartesian Cerveny Influence；
2. 端到端生成与可重现 Fortran 单频 oracle 一致的 coherent complex pressure 和 SHD；
3. 在数值一致性通过后建立单线程性能与峰值内存基线；
4. 产出可独立运行的 `bellhop_f2cpp` 程序；如内部拆分 `bellhop_f2cpp_core`，该目标只服务本工程的程序和测试；
5. 在单频阶段即验证完整轨迹缓存能够脱离追踪器供后续声场计算只读使用。

## 设计边界

- 只实现二维、固定环境/收发位置、Cartesian Cerveny coherent pressure；
- 不移植原 Bellhop 全部运行模式；
- 单频阶段仍使用宽带就绪的 `SimulationCase::frequencies`，但要求其恰含一个频率；
- 必须直接采用 RayReuse 最终需要的数据模型，不允许先使用只够单频计算的临时变量布局；
- `RayPath` 完整保存 `position/slowness/dynamicP/dynamicQ/soundSpeed/realTravelTime`、逐步 `StepQuadrature`、`ReflectionEvent` 和终止原因；
- `RayPathCache` 在追踪完成后冻结为只读；单频 F2CPP 也通过“轨迹缓存 → 单频投影 → Influence”计算链使用它；
- 逐频复走时、吸收、反射幅相、active 状态和压力只进入 `RayFrequencyState/FrequencyWorkspace`，不得回写 `RayPath`；
- 数值正确性优先于并行优化，不在 M1/M2 阶段实现 Ray-Reuse 调度；
- 完成单频验收后，其代码作为创建 `Bellhop_RayReuse/` 数值核心的重构起点；
- RayReuse 从基线代码派生后维护独立源码副本和构建系统，不反向影响 F2CPP，也不在运行时链接 F2CPP；
- 二者继续通过共同标准算例、变量规范、中间状态和最终结果互相参照。

计划工程结构见 [`doc/01-Bellhop源码分析与宽带复用设计.md`](../doc/01-Bellhop源码分析与宽带复用设计.md) 第 21.2 节，实施任务见 [`doc/02-项目实施待办.md`](../doc/02-项目实施待办.md)。

面向实际施工的工作包、依赖顺序、验收门和首组任务见
[`BUILD_PLAN.md`](./BUILD_PLAN.md)。

## 命令行契约

首版程序使用与原 Bellhop 相同的 file-root 调用方式：

```bash
bellhop_f2cpp <file-root>
```

程序从当前工作目录读取 `<file-root>.env`，并写出 `<file-root>.prt` 和
`<file-root>.shd`。只接受本工程支持范围内的输入；其他模式必须明确报错。

## 当前构建状态

截至 2026-07-27，M2-01～M2-12 已完成：

- 裁剪版 `.env` parser、单频求解编排、PRT/SHD writer 和标准算例
  `f2cpp` adapter 已接通；
- 完整计算链为 `RayPathCache → FrequencyProjector → Cartesian Cerveny
  Influence → pressure scaling → SHD`，轨迹缓存冻结后只读；
- parser 保留原始 degree 端点，并按 Fortran `SubTab` 的度数域运算顺序
  生成角度；几何初始化强制独立调用 `sin`/`cos`，避免 Clang 合并为
  `sincos` 后的 1 ULP 差触发不同近边界反射序列；
- Debug 的 ASan/UBSan 与 Release 各 20 个 CTest 全部通过；求解器测试在
  sanitizer 下覆盖六类物理路径和异常终止，标准算例 Python 测试 21 个
  全部通过；
- 六个共享单频算例均通过原版 SHD 完整复压力和 TL 比较。

最终 Release 六例指标如下。三列依次为最大复压力绝对误差、最大相对误差
和最大 TL 差：

| 算例 | 绝对误差 | 相对误差 | TL 差 / dB |
|---|---:|---:|---:|
| `constant_speed_direct` | `9.33e-10` | `8.10e-7` | `7.63e-6` |
| `constant_speed_vacuum_rigid` | `7.27e-8` | `7.84e-5` | `3.13e-4` |
| `constant_speed_acoustic_bottom` | `5.03e-8` | `1.24e-5` | `9.16e-5` |
| `constant_speed_no_attenuation_5khz` | `2.33e-9` | `2.75e-6` | `2.29e-5` |
| `constant_speed_thorp` | `1.52e-9` | `1.91e-6` | `1.53e-5` |
| `munk_cerveny_cc` | `2.45e-9` | `3.08e-5` | `2.52e-4` |

所有结果满足 `abs=1e-7`、`rel=1e-5` 的组合压力判据和 `1e-3 dB`
TL 判据。5 kHz 用例缓存 `10,129,596` 个轨迹点；角度感知 reserve 将
报告缓存量从约 `3.64 GB` 降至约 `1.23 GB`。生产 Influence 与单点 oracle
诊断路径分离，并加入无损/空间均匀投影快路径、零 taper 复指数短路和步长
限制器复用后，Munk 的 Project+Influence 约为 `2.24 s`，三轮分项总计
中位数为 `2.300 s`，原版总 CPU 时间为 `5.26 s`。5 kHz 与 Munk 的实测
峰值 RSS 分别为约 `1.32 GB` 和 `66.1 MB`；其他五例仍未达到“相对原版
不慢于 20%”的严格跨实现性能门，属于 M2-14。

在本目录运行：

```bash
# Debug：警告即错误，启用 ASan/UBSan
cmake --preset debug
cmake --build --preset debug --parallel
ctest --preset debug

# Release
cmake --preset release
cmake --build --preset release --parallel
ctest --preset release
```

标准算例 Python 工具要求 Python 3.11 或更高版本。

运行一个已生成的环境：

```bash
cd /path/to/case
/path/to/Bellhop_F2CPP/build/release/bellhop_f2cpp case_root
```

运行六例结构校验：

```bash
python3 test/standard_cases/codes/standard_cases.py test \
  --version f2cpp --case all --profile single \
  --executable Bellhop_F2CPP/build/release/bellhop_f2cpp
```

与原版结果做完整场比较：

```bash
python3 test/standard_cases/codes/compare_fields.py \
  /path/to/origin.shd /path/to/f2cpp.shd
```
