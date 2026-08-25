# 基础变量、单位与数值规范

> 版本：v1.0（2026-08-25）
>
> 适用范围：已封板的 F2CPP 二维单频实现与 RayReuse 宽带实现
>
> 原则：本文是代码、测试和日志共同遵守的数据契约；未写入本文的约定不得成为稳定公共接口。

## 1. 项目决策

D-01～D-06 于 2026-07-27 确认；D-07～D-12 已随 Fortran oracle、F2CPP
封板、SHD 兼容门和 RayReuse 并行验收关闭。所有设置均为当前冻结契约。

### 1.1 基础决策：原 Bellhop 与本项目设置对照

原版已有明确值或行为时，表中先列出原值；“本项目设置”是 F2CPP 和
RayReuse 必须共同遵守的已冻结约定。

| 编号 | 决策 | 原 Bellhop 值/行为 | 本项目已确认设置 | 影响 |
|---|---|---|---|---|
| **D-01** | 内部密度单位 | `.env/.ati/.bty` 输入及内部 `rho` 数值均按 **g/cm³** 使用，没有转为 SI | **kg/m³**；解析后统一乘以 1000，所有介质一起转换 | 环境模型、反射系数、日志 |
| **D-02** | 发射角数目 | `Nalpha=0` 时按当前单频经验式自动估算；`Nalpha>0` 时尊重输入；不足只警告 | **A：按最高频率计算 `Nalpha_final`，并作为最终数目** | 输入兼容性、计算规模 |
| **D-03** | 内部压力与 SHD 精度 | 射线 `x/t/p/q/tau` 为双精度；压力矩阵 `U` 和 SHD 场为默认 `COMPLEX`，通常是单精度复数 | **内部全部 `complex<double>`；仅写 SHD 时转 `complex<float>`** | 内存、误差、文件兼容性 |
| **D-04** | 命名风格 | Fortran 大小写不敏感，存在 `x/t/p/q/U/Nalpha`、模块全局量和缩写混用 | **类型 PascalCase；变量 lowerCamelCase；文件 snake_case；公共接口使用物理名称** | 全部源码接口 |
| **D-05** | 第一阶段输入方式 | 程序从 `.env` 及可选 `.ati/.bty/.trc/.brc` 等文件读取，并写入模块全局状态 | **先在测试代码中构造 `SimulationCase`；算法闭环后再实现 `.env` parser** | 首个里程碑工作量 |
| **D-06** | 衰减权威数据 | SSP 读入 `alphaR/alphaI` 后立即按当前频率调用 `CRCI`；逐深度原始衰减没有完整保留 | **永久保留原始值、单位和参考频率；逐频生成 Np/m、复声速及复走时** | 宽带和轨迹复用正确性 |

#### D-02 方案记录

| 方案 | 行为 |
|---|---|
| **A（已采用）** | 忽略输入射线数，始终按 `f_max` 和 Bellhop 经验判据生成 `Nalpha_final` |
| B（原版行为） | `Nalpha=0` 时自动计算；`Nalpha>0` 时尊重用户输入，仅在不足时警告 |
| C | `Nalpha=0` 时自动计算；显式输入不足时直接报错，不继续计算 |

项目已确认采用方案 A；B、C 只保留为决策历史。如果以后修改，必须同步更新
总体设计、组件支持矩阵、标准算例角度生成规则和回归基线。

### 1.2 经 oracle 与实现验收关闭的决策

原版没有正式阈值时不伪造单一原值；本项目以组合容差、契约测试、验证器和
冻结报告共同确定最终行为。

| 编号 | 决策 | 原 Bellhop 值/行为 | 已冻结设置 |
|---|---|---|---|
| **D-07** | 逐步状态比较容差 | 无正式回归容差 | 使用第 9 节逐字段 absolute + relative 组合容差；整数、枚举和明确零值使用精确门，验证器记录最坏位置 |
| **D-08** | 复压力、相位和 TL 容差 | 无正式端到端误差阈值 | 内部 complex128 与 SHD complex64 分层验收；每类 oracle/案例冻结自己的压力、相位和 TL 门，近零场另设 absolute gate，不使用一个全局数字 |
| **D-09** | `dynamicP/dynamicQ` 量纲与表示 | `REAL(KIND=8) p(2),q(2)`；初值 `[1,0]`、`[0,1]`；属于两组基本解 | 保留两组基本解、初始化、归一化和逐分量语义；v1 契约不强加单一量纲 |
| **D-10** | 反射点布局 | `Reflect2D` 保留反射前边界点，并新增一个同位置的反射后点 | 保留双点布局；`ReflectionEvent::rayPointIndex` 固定指向反射前边界点，已由反射 oracle 验收 |
| **D-11** | SHD 布局 | Fortran direct-access records；压力按默认单精度复数写入 | 内部场保持 complex128，writer 边界量化为 complex64 并兼容 PlotRead；HDF5 按独立决策继续延后 |
| **D-12** | 并行确定性 | 原二维主循环串行，按发射角固定顺序累加 | RayReuse 使用有界 frequency-slice 独占和按频率序号发布；每频保持固定轨迹顺序。F2CPP receiver-depth team 保持每 cell 累加顺序与 SHD bitwise 一致 |

### 1.3 Ray-Reuse 新增冻结设定：原版无对应值

以下内容在原 Bellhop 中不存在。它们不是“原值迁移”，而是已经实现并通过
验收的轨迹复用设计。

| 编号 | 新增设定 | 已冻结值 | 原因 |
|---|---|---|---|
| **R-01** | 几何状态与逐频状态分离 | `RayPath` 保存频率无关的 `position/slowness/dynamicP/dynamicQ/realTravelTime`，并配套 R-02/R-03 的求积和事件缓存；不保存逐频幅相 | 防止某个频率污染共享轨迹 |
| **R-02** | 逐频损耗重建所需缓存 | 每步保存 `StepQuadrature` | 复现原 modified Heun/box 求积权重 |
| **R-03** | 反射信息 | 使用独立 `ReflectionEvent`，明确 `SeaSurface/Seabed` | 逐频选择正确边界声学模型 |
| **R-04** | 几何终止条件 | 只按空间盒、存储和数值异常终止 | 不让某频率的幅度阈值截断共享路径 |
| **R-05** | 单频有效终止 | 每个频率维护 `active` mask | 复现原版 `Amp<0.005` 语义 |
| **R-06** | 实施顺序 | C++ 单频复刻 → 宽带非复用 → 串行 Ray-Reuse → 有界频率并行 | 依次分离语言/离散、宽带 I/O、复用和并行误差 |
| **R-07** | 发射角集合 | 用 `f_max` 生成一次，全部频率共享 | 满足最高频率采样需求 |
| **R-08** | 轨迹处理方式 | 先缓存完整射线扇并冻结为只读 `RayPathCache`，再逐频遍历 | 当前轨迹数可控，可确保几何只追踪一次 |
| **R-09** | Beam shift | 不实现、不留兼容分支 | 它会使几何轨迹依赖频率 |
| **R-10** | 压力工作区 | 串行时一个单频 `FrequencyWorkspace`；并行时按内存预算限制活动频率数 | 避免 `Nfreq × 全接收网格` 常驻内存 |
| **R-11** | 输出所有权 | 单一 `OutputWriter` + 容量 1–2 的有界完成频率队列 | 隔离计算与 I/O，防止已完成场无限积压 |
| **R-12** | 输入输出格式 | `.env/.shd` 作为首版 Bellhop 兼容边界；核心只使用强类型对象 | 分离文件兼容、数值核心和后续 HDF5 扩展 |
| **R-13** | 派生与工程独立性 | 先验收 F2CPP，再复制/派生其代码形成 RayReuse；派生后独立构建、独立运行、互不链接 | 复用已验证实现作为重构起点，同时保持两个交付物边界清楚 |
| **R-14** | 数据模型实施阶段 | R-01～R-05、R-08 和 R-10 的类型边界必须在 F2CPP 中实现；单频也走 `RayPathCache → RayFrequencyState → FrequencyWorkspace` | 防止派生 RayReuse 时再次重构基础变量和轨迹保存逻辑 |
| **R-15** | F2CPP 性能门 | 保留完整冻结缓存；单频 Fortran 比值只作诊断，M2 采用 16 频点 trace-once 摊销模型，六例相对重复 F2CPP 均至少节省 1% | F2CPP 为 RayReuse 建立的是缓存就绪基线，不能用流式 Fortran 单频总时间否定必要缓存成本 |

R-15 由项目负责人于 2026-07-29 确认。F2CPP 不因此提前实现多频调度；
对每个单频 PRT 记 `T_trace` 和
`T_freq = T_project + T_influence + T_scale + T_shd`，性能门计算：

```text
T_repeat(N) = N × (T_trace + T_freq)
T_reuse(N)  = T_trace + N × T_freq
```

M2 使用标准 `broadband_regression` 的 `N=16`。实际多频非复用、串行复用
及并行加速仍在 RayReuse 的 P8～P10 通过实测关闭，不以该模型代替。

## 2. 一页速查

### 2.1 基础规则

| 项目 | 统一约定 |
|---|---|
| 坐标 | `(range, depth)`，即 `(r,z)` |
| 深度方向 | 向下为正 |
| 发射角正方向 | 从正 `range` 轴向下为正 |
| 核心单位 | SI |
| 实数 | `double` |
| 复数 | `std::complex<double>` |
| 数组索引 | C++ 零基 |
| 压力 | 无量纲归一化复压力，不是 Pa |
| Beam shift | 完全不支持 |

### 2.2 关键单位

| 量 | 内部单位 |
|---|---:|
| 距离、深度、步长 | m |
| 时间、走时 | s |
| 频率 | Hz |
| 角频率 | rad/s |
| 角度、相位 | rad |
| 声速、复声速虚部 | m/s |
| 慢度 | s/m |
| 密度 | kg/m³ |
| 曲率 | 1/m |
| 衰减中间量 | Np/m |
| Transmission Loss | dB |

## 3. 文档状态含义

| 标记 | 含义 |
|---|---|
| **已冻结** | 基础实现必须遵守；修改时同步改代码、测试和本文 |
| **兼容约定** | 为复现原 Bellhop 数值行为而保留 |
| **暂定** | 已给出工作默认值，等待 oracle 验证 |
| **待补充** | 当前阶段不需要，不得提前成为公共接口 |

## 4. 坐标与几何约定（已冻结）

### 4.1 坐标

```text
position = (range, depth) = (r, z)
```

- 基础源位置为 `range=0`；
- `range` 向接收器方向增大；
- `depth` 向下增大；
- 海面深度小于海底深度；
- 公共接口不使用含义不清的二维 `x/y`。

### 4.2 发射角

| `launchAngle` | 方向 |
|---:|---|
| `0` | 水平向右 |
| `> 0` | 向下 |
| `< 0` | 向上海面 |

输入和日志可以使用 degree，进入求解器前统一转换为 radian。

### 4.3 边界方向

- `boundaryTangent` 按 `range` 增大方向定向；
- `outwardNormal` 始终指向水体外；
- 海面外法向总体向上，即负 `depth`；
- 海底外法向总体向下，即正 `depth`；
- 切向与法向均为无量纲单位向量。

## 5. 类型与命名（D-03、D-04 已冻结）

### 5.1 数值类型

| 数据 | C++ 类型 |
|---|---|
| 连续实数 | `double` |
| 连续复数 | `std::complex<double>` |
| 容器数量和索引 | `std::size_t` |
| 状态分类 | `enum class` |
| 可缺失值 | `std::optional<T>` 或显式状态 |

禁止：

- 核心计算使用 `float`；
- 用 `-999.9` 等哨兵表示缺失；
- 公共接口发生隐式窄化；
- 使用 `-ffast-math`；
- 用 `long double` 掩盖算法错误。

### 5.2 命名规则

| 对象 | 规则 | 示例 |
|---|---|---|
| 类型、枚举 | `PascalCase` | `RayPath` |
| 函数、变量、字段 | `lowerCamelCase` | `soundSpeed` |
| 编译期常量 | `kPascalCase` | `kReferenceSoundSpeed` |
| 命名空间 | 小写 | `rayreuse` |
| 文件名 | `snake_case` | `ray_stepper.cpp` |

核心领域变量默认不带单位后缀，因为单位已经统一：

```cpp
double frequency;    // Hz
double launchAngle;  // rad
Vec2 position;       // m
```

只在 I/O 转换边界使用后缀：

```cpp
double receiverRangeKmInput;
double launchAngleDegInput;
float shdPressureReal;
```

## 6. 变量字典

### 6.1 环境与网格

| C++ 名称 | 含义 | 单位 | Fortran 映射 |
|---|---|---:|---|
| `frequency` | 当前频率 | Hz | `freq` |
| `frequencies` | 频率集合 | Hz | `freqVec`（实验代码映射） |
| `designFrequency` | 决定发射角数目的最高频率 | Hz | `max(frequencies)` |
| `angularFrequency` | 角频率 | rad/s | `omega` |
| `sourceDepth` | 源深度 | m | `Pos%Sz` |
| `receiverDepths` | 接收深度 | m | `Pos%Rz` |
| `receiverRanges` | 接收距离 | m | `Pos%Rr` |
| `maximumRange` | 最远接收距离 | m | `Pos%Rr(Pos%NRr)` |
| `waterDepth` | 水深 | m | `Depth` |
| `stepLength` | 基本积分步长 | m | `Beam%deltas` |
| `rangeLimit` | 计算盒距离限制 | m | `Beam%Box%r` |
| `depthLimit` | 计算盒深度限制 | m | `Beam%Box%z` |

### 6.2 发射角

| C++ 名称 | 含义 | 单位 | Fortran 映射 |
|---|---|---:|---|
| `minimumLaunchAngle` | 最小发射角 | rad | `Angles%alpha(1)` |
| `maximumLaunchAngle` | 最大发射角 | rad | `Angles%alpha(Nalpha)` |
| `launchAngleStep` | 角度间隔 | rad | `Angles%Dalpha` |
| `launchAngleCount` | 最终发射角/射线数目 | count | `Angles%Nalpha` |
| `launchAngleIndex` | 当前零基索引 | index | `ialpha-1` |
| `phaseCriterionCount` | 原经验相位判据 | count | `N_phase` |
| `depthCriterionCount` | 原经验水深判据 | count | `N_depth` |
| `minimumRecommendedAngleCount` | 数目充分性检查 | count | `NalphaOpt` |

二维基础版本中一个发射角对应一条中心射线，公共接口统一使用 `launchAngleCount`，不使用含义模糊的 `NBeams`。
`SimulationCase` 构造参数中的 `LaunchFan` 只表示角度上下界和 parser-facing
显式数目请求；构造函数必须从自身的频率、源点声速、水深和最远接收距离
调用 `LaunchFanPlanner`，并只向数值链暴露完整 `LaunchFanPlan`。禁止调用方
直接注入最终 `launchAngleCount`，从而绕过 D-02。

`.env` parser 还必须把输入文件中的原始 degree 端点保存在
`LaunchAngleDegreeBounds`。它不是核心计算单位，而是 I/O provenance：
`LaunchFanPlanner` 先按 Fortran `SubTab` 在 degree 域执行
`minimum + index * delta`，再把每个结果乘 `pi/180`，最后由首末 radian
端点计算 `launchAngleStep`。禁止从运行时 radian 反算 degree；该往返会使
负端点偏移 1 ULP，并在高反射相干场中改变结果。没有 parser provenance
的纯核心调用仍按 radian 上下界生成等距角度。

几何射线初始慢度的 `cos(launchAngle)` 和 `sin(launchAngle)` 必须是两个
独立 libm 调用。Release 编译器不得把它们合并成 `sincos`；两种路径的末位
可能不同，近边界步进会因此选择不同的反射点序列。

### 6.3 SSP 样本

| 名称 | 含义 | 单位 | Fortran 映射 |
|---|---|---:|---|
| `soundSpeed` | 实声速 | m/s | `c` |
| `imaginarySoundSpeed` | 正虚部等效声速 | m/s | `cimag` |
| `soundSpeedGradient` | `(dc/dr, dc/dz)` | 1/s | `gradc` |
| `soundSpeedHessian.rr` | `d²c/dr²` | 1/(m·s) | `crr` |
| `soundSpeedHessian.rz` | `d²c/(dr dz)` | 1/(m·s) | `crz` |
| `soundSpeedHessian.zz` | `d²c/dz²` | 1/(m·s) | `czz` |
| `density` | 介质密度 | kg/m³ | `rho` |

### 6.4 射线状态

| 名称 | 含义 | 单位/约定 | Fortran 映射 |
|---|---|---|---|
| `position` | `(range,depth)` | m | `ray%x` |
| `slowness` | 慢度 `(xi,zeta)` | s/m | `ray%t` |
| `unitTangent` | `soundSpeed*slowness` | 无量纲 | `urayt` |
| `dynamicP[2]` | 两组动态慢度基本解 | Bellhop 归一化 | `ray%p` |
| `dynamicQ[2]` | 两组动态位移基本解 | Bellhop 归一化 | `ray%q` |
| `soundSpeed` | 当前点实声速 | m/s | `ray%c` |
| `realTravelTime` | 频率无关的实几何走时 | s | `REAL(ray%tau)` |

声压只命名为 `pressure`，不能使用 `p`；`p` 只允许出现在理论公式或 Fortran 映射中。

### 6.5 逐频状态与声场

| 名称 | 含义 | 单位 |
|---|---|---:|
| `complexTravelTime` | 含体吸收的复走时 | s |
| `amplitude` | 累计相对幅度 | 无量纲 |
| `reflectionPhase` | 累计反射相位 | rad |
| `pressure` | 归一化复压力 | 无量纲复数 |
| `transmissionLoss` | `-20*log10(abs(pressure))` | dB |

相位因子兼容原 Influence：

```text
exp(-i * (angularFrequency * complexTravelTime - reflectionPhase))
```

该符号约定只能通过单条射线复压力回归修改，不能凭另一套时间谐波习惯调整。

## 7. 基础数据结构

### 7.1 SSP 查询结果

```cpp
struct SoundSpeedSample {
    double soundSpeed;
    double imaginarySoundSpeed;
    Vec2 soundSpeedGradient;
    SoundSpeedHessian soundSpeedHessian;
    double density;
    std::size_t segmentIndex;
};
```

`segmentIndex` 是零基 C-linear 层段索引，范围为
`[0, profilePointCount-2]`。查询接口接收前一步的 segment hint；当深度恰好
位于 SSP 节点且 hint 对应的层段仍包含该节点时，必须保留该 hint，使梯度取
射线到达侧的单边值。没有可沿用的相邻 hint 时，精确节点归入左侧层段。这一
规则复刻 Fortran `GetSegz` 的严格 `<`/`>` 搜索行为。
若最小步使查询点轻微越过全局海面/海底 SSP 端点，则分别沿首段/末段外推；
这同样复刻 `GetSegz`，只用于边界命中与反射前状态，不代表水体区域被扩展。

M1 的 C-linear 几何查询只计算实声速，`imaginarySoundSpeed=0`；M2 再根据
D-06 保存的原始衰减和当前频率填充逐频虚声速。

### 7.2 几何射线状态

```cpp
struct RayState {
    Vec2 position;
    Vec2 slowness;
    std::array<double, 2> dynamicP;
    std::array<double, 2> dynamicQ;
    double soundSpeed;
    double realTravelTime;
};
```

`RayState` 不保存频率相关的幅度、反射相位和复走时。

### 7.3 步进求积信息

```cpp
struct StepQuadrature {
    double stepLength;
    double startWeight;
    double midpointWeight;
    Vec2 midpoint;
};
```

与 Fortran `Step2D` 的映射为：

```text
stepLength    = h
startWeight   = hw0
midpointWeight = hw1
midpoint      = 第一次 limiter 后由 halfh 构造的 ray1.position
```

强制满足 `startWeight + midpointWeight ≈ stepLength`。`halfh/w0/w1` 仍只作为
`ray_stepper.cpp` 局部量，不进入公共接口。第二次 limiter 进一步缩短 `h`
时不得重新计算 predictor midpoint；只更新 `hw0/hw1`，以复刻原 modified
Heun/box 离散行为。

### 7.4 反射事件

```cpp
enum class ReflectionBoundary {
    SeaSurface,
    Seabed
};

struct ReflectionEvent {
    std::size_t rayPointIndex;
    ReflectionBoundary boundary;
    std::size_t boundarySegmentIndex;
    Vec2 position;
    Vec2 boundaryTangent;
    Vec2 outwardNormal;
    Vec2 incidentSlowness;
    Vec2 reflectedSlowness;
    double tangentSlowness;
    double normalSlowness;
};
```

海面/海底反射次数从事件序列统计，不在每个射线点重复保存为权威状态。
按 D-10，`rayPointIndex=i` 固定指向反射前边界点，`points[i+1]` 是同位置、
同实走时的反射后点；事件的入射/反射慢度必须分别匹配这两个点。事件按
`rayPointIndex` 严格递增且唯一。

动态射线边界曲率条件使用显式必填枚举
`BoundaryCurvatureMode::{Standard, Double, Zero}`，分别对应 Fortran
`Beam%Type(3)` 的 `S/D/Z`；它是波束模型选择，不是海面/海底材料属性。
首批六个 `MS` 标准算例由 `GeometryTracer` 明确传入 `Standard`。平边界
反射保持 `dynamicQ`，并按 `CurvatureCorrection2` 更新 `dynamicP`；M1
不在该组件中处理逐频反射幅相或 beam shift。
`1e-3*deltas` 最小步可能使 incident 点沿外法向越界至多一个最小步；调用方
必须显式传入允许的法向 miss distance，事件和反射后点保留实际坐标，禁止
静默投影到理想平面。

Fortran oracle schema v2 的 `reflection_events.csv` 使用 1-based
`pre_point_index/post_point_index/boundary_segment_index`；映射到 C++ 时分别
减一。`reflection_coefficient_real/imag` 是无量纲的原始复压力乘子：
刚性为 `+1`，真空为 `-1`，文件/声学半空间保留计算出的复数。若原版
声学半空间分支满足 `|R|<1e-5`，`coefficient_suppressed=1` 且实际传播
幅度归零，但表中仍保存原始 `R`。事件同时记录 cumulative pre/post
amplitude/phase，禁止仅由二者反推并覆盖原始系数。

### 7.5 Cartesian Cerveny Influence oracle

Fortran Influence schema v1 使用固定的 1-based
`source_index/launch_angle_index/receiver_range_index/receiver_depth_index`。
CSV 的 `left_point_index/right_point_index` 与 ray schema v2 的
`point_index` 直接连接；映射到 C++ 时统一减一。

该 schema 固定原版 `CC/MS` 路径的以下数值边界：

- `epsilon`、复 `q/gamma/tau`、复根常数和最终单射线贡献均为
  `COMPLEX(KIND=8)`；
- `KMAH` 分别记录范围段左点值和对插值 `q` 再执行 `BranchCut` 后的值；
- image 顺序固定为 true、surface、bottom，极性依次为 `+1/-1/+1`；
- window 使用严格 `< iBeamWindow²`，随后应用
  `Hermite(deltaZ,RadiusMax,2*RadiusMax)`，贡献必须按 image 顺序累加；
- `ray_contribution` 位于 `ScalePressure` 之前；`complex64_increment`
  是 `CMPLX(ray_contribution)` 实际写入默认单精度复场的值。

F2CPP 内部仍使用 `std::complex<double>` 累加；与原版比较时先验证双精度
`ray_contribution`，再单独验证 legacy complex64 量化，禁止用 SHD 的
单精度结果掩盖 `q/gamma/KMAH` 分支差异。

### 7.6 完整声线路径与缓存

```cpp
enum class RayTerminationReason {
    ExitedDomain,
    NumericalFailure,
    PointLimit
};

struct RayPath {
    double launchAngle;
    std::vector<RayState> points;
    std::vector<StepQuadrature> steps;
    std::vector<ReflectionEvent> events;
    RayTerminationReason terminationReason;
};

using RayPathCache = std::vector<RayPath>;
```

强制不变量：

- `RayPath` 在 F2CPP 追踪阶段完整构造，返回后即可脱离追踪器使用；
- 恒有 `points.size() = 1 + steps.size() + events.size()`；
- 相邻边 `k=(points[k],points[k+1])` 若 `k` 是某个事件索引，则它是没有
  求积记录的反射边；否则按 `k` 递增顺序消费下一个 `StepQuadrature`；
- `ReflectionEvent::rayPointIndex` 必须指向 D-10 约定的反射前状态；
- `RayPathCache` 冻结后只读，逐频计算不得修改其中任何成员；
- 不允许用仅含 `(range,depth)` 的折线代替完整轨迹。

### 7.7 逐频临时状态

```cpp
struct RayFrequencyPoint {
    std::complex<double> complexTravelTime;
    double amplitude;
    double reflectionPhase;
    bool active;
};

struct RayFrequencyState {
    double frequency;
    std::vector<RayFrequencyPoint> points;
};

struct FrequencyWorkspace {
    double frequency;
    std::vector<std::complex<double>> pressure;
};
```

`RayFrequencyState` 只服务当前“一个频率 × 一条射线”的投影，可在 Influence 累加后复用；`FrequencyWorkspace` 只拥有当前活动频率的接收场切片。二者都不得被 `RayPathCache` 持有。

## 8. 特殊变量说明

### 8.1 动态射线 `dynamicP/dynamicQ`（D-09 已冻结）

长度 2 表示两组线性独立基本解，不是二维空间坐标：

```text
dynamicP = [1, 0]
dynamicQ = [0, 1]
```

基础版本要求：

- 使用 `std::array<double,2>`，不使用几何 `Vec2`；
- 不对其进行长度单位换算；
- 保持原 Bellhop 初始化和更新方程；
- 测试时两个基本解逐分量比较；
- 完整量纲解释等有限差分验证后补充。

### 8.2 衰减与复声速（D-06 已冻结）

权威数据链：

```text
原始衰减值 + 原始单位
  → 当前频率 attenuationNpPerMeter
  → imaginarySoundSpeed
  → complexTravelTime
```

规则：

- 原始衰减输入不可被转换结果覆盖；
- `attenuationNpPerMeter` 单位为 1/m；
- `imaginarySoundSpeed` 单位为 m/s，沿用 Bellhop 正虚部约定；
- 等声速无损用例中二者为 0；
- 首版开放 `N`（Np/m）、`M`（dB/m）、`m`（带幂律的 dB/m）、
  `F`（Bellhop 原义 dB/(m·kHz)）、`W`（dB/波长）、`Q`（品质因子）
  和 `L`（loss parameter）；
- 体衰减开放 None 和 Thorp；Francois-Garrison 与 biological 必须明确
  报不支持，不得静默回退；
- Thorp 兼容原 Fortran 的 default-REAL 提升语义：
  `0.11 → 0.10999999940395355`，
  `8685.8896 → 8685.8896484375`；
- 正虚声速通过 `1/(c+i*cimag)` 产生负的 `Im(tau)`，在
  `exp(-i*omega*tau)` 中形成衰减；
- C-linear 必须先对每个 SSP 节点执行当前频率的转换，再线性插值完整
  复声速；禁止先插值实声速或原始衰减、再在查询点转换。

### 8.3 逐频边界声学与投影

流体声学半空间使用事件切/法向慢度
`Tg/Th`、水体局部密度和逐频半空间复纵波声速：

```text
kx = omega * Tg
kz = omega * Th
gammaP = sqrt(kx² - (omega/cp)²)
R = -(rhoWater*gammaP - i*kz*rhoHalfSpace)
     / (rhoWater*gammaP + i*kz*rhoHalfSpace)
```

`gammaP` 保留 Fortran 的辐射平方根分支修正。首版支持 vacuum、rigid 和
剪切波速为零的 fluid acoustic half-space；elastic、反射系数文件和 grain
模型明确不支持。

反射的原始复系数不写入几何轨迹。投影时按事件即时计算，并更新：

```text
amplitude *= abs(R)
reflectionPhase += atan2(imag(R), real(R))
```

累计相位不包裹。声学半空间的原始 `|R|` 严格小于
`double(float(1e-5)) = 9.9999997473787516e-6` 时，幅度置零而相位不变；
随后独立执行 legacy active 判断：幅度严格小于
`double(float(0.005)) = 0.0049999998882412910` 时，当前点标为 inactive，
且全部几何后缀保持 inactive。这里 `active` 表示“是否允许从该状态继续
向后贡献”；触发终止的反射后点保留物理幅相，但不允许后缀贡献。

每个非反射边按缓存求积重建：

```text
tauNext = tauCurrent
        + startWeight    / complex(cStart, cimagStart)
        + midpointWeight / complex(cMid,   cimagMid)
```

反射边复制复走时。投影结果只进入 `RayFrequencyState`，不得修改
`RayPath`；不同频率的状态和 `FrequencyWorkspace` 必须拥有独立存储。

### 8.4 Cartesian Cerveny minimum-width epsilon

标准环境中的 `CC/MS` 最终组装为 `RunType="CC RR2"` 和
`BeamType="CMS"`；`PickEpsilon` 实际只读取前两位 `CM`。其中 `M` 是
minimum-width，第三位 `S` 是 M1 已处理的 standard reflection curvature，
不得误作 epsilon 分支。

F2CPP 接口接收 Hz、源点实声速、已经在 parser 边界由 km 转成 m 的
`loopRange`，以及无量纲 multiplier：

```text
omega = 2*pi*frequency
halfWidth = sqrt(2*sourceSoundSpeed*loopRange/omega)
epsilonOpt = i*(0.5*omega)*(halfWidth*halfWidth)
epsilon = epsilonMultiplier*epsilonOpt
```

`halfWidth` 单位为 m，`epsilon` 单位为 m²/s。虽然代数上 epsilon 中的
频率抵消，代码必须保留 `sqrt → square → multiply` 的原运算顺序；直接
化简成 `i*c*loopRange` 会丢失 O1-04 已冻结的一位 binary64 舍入差异。
direct 与 Munk 的精确锚点分别为
`(0, 3750000.00000000047)` 和
`(0, 37534499.9999999925)`。当前裁剪版不开放 filling/WKB 等其他宽度
模式；非法、非有限、非正或产生零/溢出的输入必须明确失败。

### 8.5 Cartesian Cerveny Influence

F2CPP 首版只支持 coherent、Cartesian、minimum-width、
standard-curvature、point-source、rectilinear receiver grid。入口只读
`RayPath`、同频 `RayFrequencyState` 和纯正虚数 epsilon，唯一写对象为
该频率的 `FrequencyWorkspace`。

逐点复动态量为：

```text
P = p1 + epsilon*p2
Q = q1 + epsilon*q2
tangent = soundSpeed*slowness
normal = (tangent.depth, -tangent.range)
gamma = 0.5 * (
    P/Q * tangent.range^2
  + 2*cNormal/c^2 * tangent.depth*tangent.range
  - cAlong/c^2 * tangent.depth^2
)
```

`Q==0` 时节点 gamma 保持复零。平方必须先独立计算再进入乘法，以保持
Fortran `**2` 的舍入次序。KMAH 初值为 `+1` 且只能取 `±1`；
minimum-width BranchCut 仅在右端 `real(Q)<0` 且虚部按原版严格/包含端点
规则跨零时翻号。接收范围插值后还要从左端 KMAH 对插值 `Q` 再执行一次
BranchCut，然后修正复平方根符号。

Influence 从 C++ `rightPointIndex=2` 开始，故 source 到首积分点不贡献。
有效逐频路径包含首个 inactive 终止点；段资格只看左端 active，因此结束于
该终止点的段仍可贡献，而从 inactive 点出发的后缀禁止。source 必须 active，
且 active 一旦为 false 不得再次变 true。

接收范围至少包含两个严格等距点，并复刻原版索引：

```text
upper = clamp(trunc((rayRange-firstReceiverRange)/deltaRange)+1, 1, count)
```

每段拥有 `(leftRange,rightRange]`。循环保持 `iS=3` 起点、右端超最大接收
范围立即结束、`1000*SPACING(rightRange)` 重复点跳过和后退段不贡献等
原顺序。

每个接收深度按固定顺序计算：

```text
true    : deltaZ =  receiverDepth - rayDepth, polarity = +1
surface : deltaZ = -receiverDepth + 2*surfaceDepth - rayDepth, polarity = -1
bottom  : deltaZ = -receiverDepth + 2*bottomDepth  - rayDepth, polarity = +1
```

窗口量 `-omega*imag(gamma)*deltaZ^2` 必须严格小于 `beamWindow^2`；等号
拒绝。Hermite 的 `(1-u)^2` 也先独立平方。图像贡献以
`true → surface → bottom` 的 complex128 次序累加，再乘
`sqrt(abs(cos(alpha)))*sqrt(c*abs(epsilon)/Q)` 及 KMAH 符号。

Legacy 在每条射线写入场前量化为 complex64；D-03 要求 F2CPP 不复制此
内部量化，必须把 finite complex128 contribution 直接加入
`FrequencyWorkspace` 的 `complex<double>`，并验证旧值及累加结果有限。
complex64 仅作为 oracle 诊断和最终 SHD writer 的兼容边界。

### 8.6 Coherent Cartesian point-source 压力缩放

某个 source 的全部发射角按固定顺序完成 Influence 累加后，对
`FrequencyWorkspace` 恰好缩放一次；禁止按射线缩放或重复缩放。目标
`RunType="CC RR2"` 不执行 incoherent `sqrt(real(U))`，也不开放 line
source 分支。

保留原版运算顺序：

```text
beamScale = (-launchAngleSpacingRadians*sqrt(frequencyHz))
            / sourceSoundSpeed

rangeFactor =
    receiverRange == 0
      ? 0
      : beamScale/sqrt(abs(receiverRange))

pressure(depth,range) *= rangeFactor
```

角间距单位为 rad、频率为 Hz、源点实声速为 m/s、接收范围为 m。零范围
判断是精确比较，`+0/-0` 都通过乘零得到零列，不用 near-zero 阈值。
负实 factor 同时翻转压力实部和虚部，不能取绝对值或把负号延后。

Legacy 先以 binary64 计算 factor，再用 `SNGL(factor)` 量化并乘 complex64
场。D-03 要求 F2CPP 主路径保留 binary64 factor，直接乘
`complex<double>` 工作区；legacy float factor 仅作为独立 oracle 参考，
SHD writer 边界再量化最终压力。实现必须在写入前验证完整输入场、全部
range factor 和全部缩放结果有限，从而任何异常都不留下部分缩放场。

## 9. 精度、容差与确定性

### 9.1 浮点比较规则（已冻结）

```text
abs(actual - expected)
  <= absoluteTolerance + relativeTolerance * abs(expected)
```

除整数、枚举和构造出的明确零值外，不直接使用浮点 `==`。

复数比较至少记录：

- 复数绝对误差；
- 幅值相对误差；
- 包裹到 `[-pi,pi]` 的相位差。

### 9.2 基础组件默认容差（D-07 已冻结）

| 量 | 绝对容差 | 相对容差 |
|---|---:|---:|
| 通用无量纲量 | `1e-12` | `1e-10` |
| `position` | `1e-8 m` | `1e-10` |
| `slowness` | `1e-13 s/m` | `1e-10` |
| `soundSpeed` | `1e-9 m/s` | `1e-11` |
| `realTravelTime` | `1e-12 s` | `1e-10` |
| `dynamicP/dynamicQ` | `1e-12` | `1e-9` |
| 边界交点 | `1e-8 m` | `1e-10` |

若需放宽，必须记录原因、最大误差和所在射线点。端到端压力/TL 按 D-08 使用
各验证器和冻结报告中的 case-specific gate；近零场不得用相对误差掩盖。

### 9.3 精度和确定性规则

- 核心状态不得静默产生 NaN/Inf；
- 每步检查几何和动态状态是否有限；
- 数值异常通过 `TerminationReason` 返回；
- 串行版本固定发射角累加顺序；
- SHD 只在 writer 边界转换为单精度复数；
- 并行执行遵守 D-12：每频保持固定轨迹累加顺序，按频率索引发布；F2CPP
  receiver-depth team 保持每 cell 的累加顺序。

## 10. I/O 转换边界

| Bellhop 文件量 | 文件单位 | 内部处理 |
|---|---:|---|
| 接收距离 `Rr` | km | ×1000 → m |
| `.ati/.bty` 水平距离 | km | ×1000 → m |
| 源、接收器和 SSP 深度 | m | 不变 |
| 发射角 | degree | ×π/180 → rad |
| 边界密度 | g/cm³ | ×1000 → kg/m³ |
| 频率 | Hz | 不变 |
| SHD 压力 | 单精度复数 | writer 显式量化 |

转换只发生在 parser/writer。积分器、反射器和 Influence 内禁止出现表示输入单位转换的裸 `1000` 或 `pi/180`。

## 11. 索引与不变量

### 11.1 索引规则（已冻结）

- C++ 容器索引从 0 开始；
- count 与 index 分开命名；
- 用户显示编号和文件 record number 可以从 1 开始，但不作为数组下标；
- SHD record number 使用独立函数计算；
- 分配前检查维度乘法溢出。

### 11.2 基础不变量

| 对象 | 必须满足 |
|---|---|
| 输入 | `frequency>0`、`maximumRange>0`、`waterDepth>0` |
| 角度 | 上下限有限且最小值小于最大值；`launchAngleCount>=2` |
| SSP | `soundSpeed>0`、`density>0`、所有导数有限 |
| 射线 | `norm(soundSpeed*slowness)≈1`、走时不减、步长为正；反射/缓存入口允许 modified-box 长程累积误差 `≤1e-4` |
| 反射 | 事件明确为海面或海底；交点位于对应边界 |
| 切法向 | 单位长度且互相正交 |
| 镜面反射 | 切向慢度不变，法向慢度变号 |

`ReflectionEvent` 不包含任何 beam shift 字段。

## 12. 已关闭的规范补充记录

以下阶段均已完成。表格只保留规范随实现落地的追踪关系，不表示当前待办。

| 开发阶段 | 已关闭的规范主题 |
|---|---|
| Fortran oracle | D-07 逐步容差、D-09 动态量解释、验证 D-10 双点反射布局 |
| C-linear SSP | 节点/层索引语义、逐频虚声速和原始衰减映射 |
| 边界声学 | 半空间材料变量、密度转换验证、复反射系数 |
| Cerveny Influence | `epsilon/gamma/KMAH` 的名称、单位和分支约定 |
| 单频 SHD | D-11 记录布局和量化误差 |
| 宽带非复用 | 频率维度、小型 `BroadbandField` 基线、逐频日志 |
| F2CPP 中心/动态射线 | 完整 `RayPathCache`、求积缓存、反射事件、终止原因、索引不变量和内存统计 |
| F2CPP 单频声场 | 单频 `RayFrequencyState/FrequencyWorkspace`、反射逐频投影、确认逐频量不回写轨迹 |
| Ray-Reuse | 复用既有 `RayPathCache` 数据模型，增加多频 active mask、调度和总体内存估算 |
| 并行化 | D-12 归约顺序、有界频率工作区、输出队列和确定性阈值 |

## 13. 规范更新流程

每进入一个新阶段：

1. 先补变量名、含义、单位和类型；
2. 写明输入单位到内部单位的转换边界；
3. 增加不变量和容差；
4. 再实现或修改公共数据结构；
5. 增加解析测试或 Fortran 对照；
6. 将“暂定/待补充”更新为“已冻结”。

若代码与本文冲突，默认先视为实现缺陷；只有理论、原源码行为或回归结果证明规范不合理时，才修改本文并记录原因。
