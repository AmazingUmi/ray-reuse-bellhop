# F2CPP 数值接口与中间状态契约 v1

## 1. 契约边界

本契约冻结数值语义、字段类型、单位、索引和导出格式，不冻结 C++ ABI、对象
大小、padding、private 成员或源文件组织。`Bellhop_F2CPP` 与
`Bellhop_RayReuse` 继续各自编译和链接，不建立跨工程运行时依赖。

`tests/unit/numerical_contract_test.cpp` 以 compile-time assertions 固定公开
字段和关键返回类型。任何有意变更必须同时更新本文、contract test、probe
schema 版本和三模型中间状态门。

## 2. 模块接口与单位

| 模块 | 冻结类型/入口 | v1 语义与单位 |
|---|---|---|
| `numerics` | `Vec2` | 分量顺序固定为 `range/depth`，单位由字段决定 |
| `ray` | `RayState` | 位置 m、慢度 s/m、动态 `p/q`、声速 m/s、实走时 s |
| `ray` | `StepQuadrature` | 实际步长 `h`、`hw0/hw1` 和 predictor midpoint，长度为 m |
| `ray` | `ReflectionEvent` | 0-based 内部点/段索引、边界、切/法向、反射前后慢度 |
| `ray` | `RayPath` | `launchAngle` 为 rad；`points/steps/events` 频率无关且只读使用 |
| `field` | `RayFrequencyPoint/State` | complex128 复走时、幅度、未包裹累计反射相位、active 状态 |
| `field` | `FrequencyProjector::project` | 只读 `RayPath`，逐频重建声学状态，不回写几何缓存 |
| `field` | `CartesianCervenyInfluence::accumulate` | 固定 true→surface→bottom 图像顺序和射线顺序 |
| `field` | `FrequencyWorkspace` | complex128；逻辑 `[depth,range]`，range 连续 |
| `io` | `ShdWriter` | 仅在 SHD 写出边界量化为 complex64 |

`RayPath` 禁止保存复走时、逐频吸收、反射幅相、active mask 或压力。
`FrequencyWorkspace` 禁止降为 complex64。RayReuse 可以增加私有快路径和统计，
但不得改变上述数值语义或公开状态含义。

## 3. C++ geometry probe schema v1

F2CPP 和 RayReuse 各自提供仅在 `BUILD_TESTING` 下构建的
`*_geometry_oracle_probe`。它们写出：

- `<output>.csv`；
- `<output>.csv.manifest.json`。

Manifest 固定字段：

```json
{
  "schema": "bellhop.cpp.ray_path_probe",
  "schema_version": 1,
  "contract_version": 1,
  "producer": "f2cpp|rayreuse",
  "status": "complete",
  "configuration": "direct|flat-boundary-custom|munk",
  "launch_angle_rad": 0.0,
  "points_file": "ray_points.csv",
  "point_count": 0,
  "integrated_step_count": 0,
  "reflection_event_count": 0,
  "termination": "ExitedDomain|NumericalFailure|PointLimit",
  "index_base": 1,
  "numeric_precision": "binary64",
  "units": "SI",
  "columns": []
}
```

CSV v1 固定为以下顺序：

```text
point_index,point_kind,step_valid,incoming_step_index,
r_m,z_m,t_r_s_per_m,t_z_s_per_m,p1,p2,q1,q2,c_m_per_s,tau_real_s,
num_top_bounces,num_bottom_bounces,h_m,hw0_m,hw1_m,mid_r_m,mid_z_m
```

外部索引一律 1-based。`point_kind` 为 `source/integrated/top_reflection/
bottom_reflection`。只有 `step_valid=1` 的行可解释求积字段；其他行使用有限零
占位。probe 不导出 private 临时变量，也不把对象布局当作协议。

## 4. 三模型归一化与当前范围

`intermediate_state_gate.sh` 运行原版 Fortran ray-step oracle schema v2，
再将 F2CPP/RayReuse geometry probe v1 与其公共字段逐点比较。v1 覆盖：

- 位置、慢度、动态 `p/q`、声速和实走时；
- modified-box 的 `h/hw0/hw1` 与 predictor midpoint；
- 反射点顺序、bounce counters 和 reflection-event count；
- 点数、积分步数和终止类型。

v1 是“中间几何状态”契约，不是完整中间状态 bundle。Fortran 独有的
`halfh/c0/c1`、完整反射声学、`RayFrequencyState` 和 Influence image 表仍由
既有 oracle schema 验证，待 geometry v1 稳定后以新 schema 版本扩展，不能
静默向 v1 增加列。

当前自动矩阵选择 source 1、launch angle index 150，并覆盖 direct、
vacuum/rigid 和 Munk。F2CPP 与 RayReuse 的 probe CSV 要求逐字节一致；两者
相对 Fortran 使用 `compare_f2cpp_geometry_oracle.py` 中冻结的逐字段组合容差。
