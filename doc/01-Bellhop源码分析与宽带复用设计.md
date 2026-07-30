# 01 Bellhop 源码分析与宽带 Ray-Reuse 设计

> 具体变量名、坐标方向、内部单位、数值类型、索引和初始容差以 [04 基础变量、单位与数值规范](./04-基础变量单位与数值规范.md) 为准；本文件侧重算法和架构。

## 1. 文档目标与分析范围

本文从功能职责和实际调用链两个角度，对仓库中的 Bellhop 源码进行模块化分类，重点回答以下问题：

1. 射线方法在源码中如何拆分为轨迹跟踪、边界交互和声场计算；
2. 各功能分别由哪些文件、模块和子程序实现；
3. 数据如何从环境输入流向射线状态，再流向接收场和输出文件；
4. 当前真正参与构建的二维代码与仓库中的三维扩展代码有何区别；
5. 后续进行 ray-reuse 重构时，应如何划分模块边界。

分析结论以当前工作区源码为准。项目代码基线只认定 `Bellhop_origin/` 中的原始 Bellhop 模型；此前多频尝试拷入的测试文件和代码痕迹属于实验材料，不作为原模型已经支持宽带的依据。当前 `Bellhop_origin/Makefile` 只编译二维程序 `Bellhop/Bellhop.f90` 及其依赖；`Bellhop3D.f90`、`Step2DMod.f90`、`Step3DMod.f90`、`Reflect2DMod.f90`、`Reflect3DMod.f90`、`influence3D.f90` 等三维相关文件存在于仓库，但不属于当前二维可执行文件的构建链。`Bellhop_F2CPP/` 已建立职责 README、尚无 C++ 实现；`Bellhop_RayReuse/` 目前只有职责说明。工程演进关系为：先完成 F2CPP 优化单频实现，再复制/派生其已验证代码形成 RayReuse 并进行宽带轨迹复用改造；派生后两者独立构建和运行。

## 2. 总体功能分层

从程序职责看，当前二维 Bellhop 可以划分为六层：

```text
1. 主控与运行模式
   Bellhop.f90 / BellhopCore
          |
2. 环境、源、接收器与边界输入
   ReadEnvironmentBell + SourceReceiverPositions + sspMod + bdryMod
          |
3. 射线初始化与轨迹推进
   TraceRay2D + Step2D + ReduceStep2D
          |
4. 边界和介质界面交互
   Distances2D + Reflect2D + RefCoef + Step2D 中的跳跃条件
          |
5. 波束影响与声场叠加
   PickEpsilon + Influence* + ApplyContribution + ScalePressure
          |
6. 结果表达与文件输出
   WriteRay + ArrMod + RWSHDFile
```

这六层又可以归并为两个主要计算阶段：

| 主要阶段 | 子功能 | 核心产物 |
|---|---|---|
| 轨迹跟踪 | 声速查询、数值积分、步长控制、界面跳跃、边界检测和反射 | 离散射线/波束状态 `ray2D(:)` |
| 声场计算 | 波束参数、接收点匹配、复振幅计算、相干或非相干叠加、归一化 | 接收网格复压力 `U(:,:)` 或到达量 `Arr` |

需要特别指出：源码中的 `TraceRay2D` 不只是几何中心线跟踪。每个 `ray2DPt` 还同时携带动态射线量 `p/q`、复走时 `tau`、幅度 `Amp` 和相位 `Phase`。因此当前实现把“可复用的几何状态”和“频率相关的声学状态”耦合在同一条轨迹数组中。

## 3. 顶层调用链

二维入口为 `Bellhop_origin/Bellhop/bellhop.f90` 中的 `PROGRAM BELLHOP`，关键调用顺序如下：

```text
PROGRAM BELLHOP
  |
  +-- ReadEnvironment       读取 .env、SSP、收发位置、发射角、运行模式
  +-- ReadATI / ReadBTY     读取海面/海底几何
  +-- ReadReflectionCoefficient
  +-- ReadPat               读取源波束方向图
  +-- OpenOutputFiles       打开 .ray / .arr / .shd
  |
  `-- BellhopCore
       |
       +-- 遍历源深度
       |    `-- 遍历发射角
       |         +-- 计算初始幅度 Amp0
       |         +-- TraceRay2D
       |         |    +-- Step2D
       |         |    |    `-- ReduceStep2D
       |         |    +-- Distances2D
       |         |    `-- Reflect2D（发生边界穿越时）
       |         |
       |         +-- R 模式：WriteRay2D
       |         `-- 其他模式：Influence* / 到达量累计
       |
       +-- ScalePressure（TL 模式）
       `-- 写 SHD / ARR / RAY
```

对应的关键源码位置为：

- 环境及辅助文件读取：`bellhop.f90:133-138`；
- 输出初始化：`bellhop.f90:146`；
- 主计算入口：`BellhopCore`，`bellhop.f90:153`；
- 单条射线跟踪：`bellhop.f90:288`、`431-592`；
- 不同波束影响算法选择：`bellhop.f90:297-313`；
- 声压缩放：`bellhop.f90:324`；
- 到达量输出：`bellhop.f90:331-333`。

## 4. 模块一：主控、配置与任务调度

### 4.1 `bellhop.f90`：程序入口和工作流编排

该文件承担 orchestration 职责，而不是单一数值算法：

- 解析命令行文件根名；
- 读取环境与辅助输入；
- 设置二维模式 `ThreeD = .FALSE.`；
- 根据源深度和发射角构造射线扇；
- 对每条射线先跟踪，再按运行模式输出轨迹或计算接收场；
- 管理压力矩阵、到达量矩阵和输出文件生命周期。

`BellhopCore` 是二维程序的真正业务主循环。其计算粒度为：

```text
一个环境
  × 多个源深度
  × 多个发射角
  × 一次轨迹跟踪
  × 一次波束对接收场的贡献计算
```

### 4.2 `bellhopMod.f90`：共享状态与核心数据类型

该模块定义全局运行状态、波束配置和射线点类型。

二维射线点 `ray2DPt` 的字段可按职责重新解释为：

| 字段 | 功能分类 | 含义 |
|---|---|---|
| `x(2)` | 几何轨迹 | 射线点坐标 `(r,z)` |
| `t(2)` | 几何轨迹 | 缩放切向/慢度方向，`c*t` 为单位切向 |
| `c` | 环境采样 | 当前点实声速 |
| `p(2), q(2)` | 动态射线 | 波束横向扩展、焦散和几何扩散所需状态 |
| `tau` | 传播相位与损耗 | 复走时；实部对应传播时间，虚部承载体吸收 |
| `Amp, Phase` | 边界声学 | 源幅度及历次边界反射造成的幅相变化 |
| `NumTopBnc, NumBotBnc` | 事件统计 | 海面、海底反射累计次数 |

`BeamStructure` 则保存运行模式、基本步长、计算盒、波束窗口和波束类型等配置。

### 4.3 `ReadEnvironmentBell.f90`：运行参数解析

主要过程包括：

- `ReadEnvironment`：环境总读取入口；
- `ReadTopOpt`、`TopBot`：上下边界和半空间参数；
- `ReadRunType`：解析计算类型及波束类型；
- `OpenOutputFiles`：按任务类型打开输出。

`RunType` 的主要字符位控制如下：

| 字符位 | 主要选项 | 控制内容 |
|---|---|---|
| 1 | `R/E/C/S/I/A/a` | 射线、特征射线、相干/半相干/非相干 TL、到达量 |
| 2 | `C/R/S/B/b/g/G` | Cartesian、ray-centered、Gaussian、hat 等波束类型 |
| 3 | 由源波束配置使用 | 源波束方向图开关 |
| 4 | `R/X` | 点源或线源 |
| 5 | `R/I` | 规则或不规则接收网格 |
| 6 | `2/3` | N×2D 或 3D |
| 7 | 映射到 `Beam%Type(4:4)` | beam shift 选项 |

## 5. 模块二：环境与几何建模

### 5.1 `sspMod.f90`：声速场及导数

轨迹微分方程和动态射线方程都通过 `EvaluateSSP` 获取局部介质参数：

- 实声速 `c`；
- 衰减对应的虚部 `cimag`；
- 一阶梯度 `gradc`；
- 二阶导数 `crr/crz/czz`；
- 密度 `rho`。

模块支持线性、N² 线性、PCHIP、三次样条、二维四边形和解析声速剖面等分支。二维当前构建使用 `Bellhop/sspMod.f90`，不是同名的 `misc/sspMod.f90`。

从功能边界上看：

- `c` 和 `gradc` 决定中心射线路径；
- 声速二阶导数决定 `p/q` 的演化；
- `cimag` 进入复走时，影响频率相关吸收；
- 因此 SSP 模块同时服务于几何层和声学层。

### 5.2 `bdryMod.f90`：海面/海底几何预处理

主要职责为：

| 过程 | 职责 |
|---|---|
| `ReadATI` | 读取海面起伏 `.ati`，或生成平海面 |
| `ReadBTY` | 读取海底地形 `.bty`，或生成平海底 |
| `ComputeBdryTangentNormal` | 预计算分段长度、切向、外法向、节点法向和曲率 |
| `GetTopSeg` | 根据射线水平位置选择当前海面分段 |
| `GetBotSeg` | 根据射线水平位置选择当前海底分段 |

边界点类型 `BdryPt` 同时保存：

- 几何量：`x/t/n/Nodet/Noden/Len/Kappa`；
- 分段导数量：`Dx/Dxx/Dss`；
- 局部半空间声学参数：`HS`。

因此边界模块本质上包含两个子域：边界几何，以及沿程变化的地声参数。

### 5.3 `SourceReceiverPositions.f90` 与 `angleMod.f90`

这两个模块共同定义计算采样：

- `ReadSzRz`：源深度和接收深度；
- `ReadRcvrRanges`：接收距离；
- `ReadfreqVec`：频率向量；
- `ReadRayElevationAngles`：二维发射仰角；
- 三维代码还使用 `ReadRayBearingAngles` 读取方位角。

注意：源码中出现的 `freqVec` 并不是本仓库原始 Bellhop 基线已经具备宽带能力的证据。该部分来自此前多频计算尝试时拷入的实验文件；当前 `BellhopCore` 仍围绕全局标量 `freq/omega` 运行。后续实现应把这些代码视为实验参考，不能直接视为已验证基础设施。

## 6. 模块三：射线轨迹跟踪

### 6.1 `TraceRay2D`：单条射线生命周期

`TraceRay2D` 位于 `bellhop.f90:431-592`，其职责不是执行某一种积分公式，而是管理一条射线从初始化到终止的完整生命周期：

1. 在源点查询 SSP；
2. 初始化 `x/t/p/q/tau/Amp/Phase` 和反射次数；
3. 定位初始海面、海底分段；
4. 循环调用 `Step2D` 推进一步；
5. 更新海面/海底活动分段及沿程地声参数；
6. 用 `Distances2D` 判断是否从水体内部穿出边界；
7. 发生穿越时调用 `Reflect2D`；
8. 根据空间范围、幅度、越界、`q` 溢出或存储上限终止。

终止条件包括：

- 超出 `Beam%Box%r` 或 `Beam%Box%z`；
- `Amp < 0.005`；
- 连续两个点均处于边界外；
- `abs(q(1)) > 1D100`；
- 轨迹点数接近 `MaxN`。

其中幅度阈值属于声学状态，却会改变几何轨迹的实际保存长度，是 ray-reuse 时需要拆开的耦合点。

### 6.2 `Step.f90::Step2D`：中心射线与动态射线积分

`Step2D` 使用修改的二阶 polygon/midpoint/leapfrog 方法：

1. 在当前点查询 SSP，做半步预测；
2. 在预测点再次查询 SSP；
3. 用混合权重完成整个步长的校正；
4. 同时更新中心射线和动态射线状态。

它更新的状态可分为：

| 方程组 | 更新字段 | 作用 |
|---|---|---|
| 中心射线方程 | `x`, `t` | 得到传播路径和方向 |
| 走时方程 | `tau` | 累积传播时间和吸收 |
| 动态射线方程 | `p`, `q` | 描述邻近射线束展开、聚焦与焦散 |
| 环境采样 | `c` | 保存新点声速 |

`Step2D` 还会在穿过 SSP 分层界面时，根据声速梯度跳变修正 `p`。这属于“介质内部界面交互”，与海面/海底反射不同，但仍属于轨迹事件处理。

### 6.3 `ReduceStep2D`：事件对齐和自适应截步

该过程不估计积分误差，而是把候选步长缩短到最近的环境事件，使射线点尽量落在事件位置。比较的候选距离包括：

- SSP 深度层界面；
- 海面；
- 海底；
- 海面/海底几何分段端点；
- 距离相关 SSP 分段；
- 计算盒边界。

最终执行：

```fortran
h = MIN(h, hInt, hTop, hBot, hSeg, hBoxr, hBoxz)
```

并用 `1.0D-3 * Beam%deltas` 作为最小步长，避免射线停滞在界面附近。

## 7. 模块四：边界和界面交互

边界交互可以进一步拆成五个连续步骤。

### 7.1 当前分段定位

`GetTopSeg/GetBotSeg` 根据水平位置选出活动边界段，并维护 `rTopSeg/rBotSeg`。当射线跨过分段端点时，`TraceRay2D` 更新活动段以及长格式边界文件中携带的局部地声参数。

### 7.2 交点对齐

`ReduceStep2D` 用射线方向与边界外法向计算截短步长，使新轨迹点落到海面或海底附近。

### 7.3 穿越判定

`Distances2D` 计算射线点相对上下边界的有符号法向距离。`TraceRay2D` 只在以下状态变化时触发反射：

```text
上一步在水体内：DistBeg > 0
当前步在边界上或边界外：DistEnd <= 0
```

这样可以避免对“由外向内”的点重复反射。

### 7.4 几何反射与动态射线修正

`ReflectMod.f90::Reflect2D` 首先分解入射慢度在边界切向和法向上的分量：

```text
Tg = t · t_boundary
Th = t · n_boundary
```

然后按镜面反射修改方向：

```text
t_out = t_in - 2 Th n_boundary
```

`CurvatureCorrection2` 再根据边界曲率、声速梯度跳变和入射角修改动态射线量 `p`，`q` 通常连续。这一步影响聚焦和散焦，不能只把边界处理理解为方向翻转。

### 7.5 反射系数、幅度和相位

`Reflect2D` 根据 `HS%BC` 选择边界声学模型：

| `HS%BC` | 类型 | 幅相处理 |
|---|---|---|
| `R` | 刚性边界 | 幅度和相位不变 |
| `V` | 真空/压力释放边界 | 幅度不变，相位加 π |
| `F` | 文件给定反射系数 | 按入射角插值幅度 `R` 与相位 `phi` |
| `A` | 声学/弹性半空间 | 由水体和半空间阻抗计算复反射系数 |
| `G` | 粒径地声模型 | 先由粒径参数生成沉积层参数，再计算反射 |

半空间反射中显式使用 `omega` 和复声速，因此反射点与镜面方向可以是几何量，而反射幅度/相位通常是频率相关量。

原始 Bellhop 还包含 `Beam%Type(4:4) == 'S'` 的 beam shift 分支，它会修改位置 `x`、走时 `tau` 和波束宽度状态 `q`。本项目本次设计和实现整体排除 beam shift：不移植该分支、不为其设计数据结构，也不把它纳入测试矩阵。

## 8. 模块五：波束影响、声强与声压计算

### 8.1 严格说是“复声压场计算”

用户通常把第二阶段概括为“声强计算”，但当前 Bellhop 的主变量 `U` 是复压力矩阵。不同运行模式的累计规则如下：

- `C`：相干叠加复声压；
- `S`：半相干处理；
- `I`：累计强度型贡献，最终再开方转换成压力幅值；
- `A/a`：不形成完整压力网格，而是记录离散到达量。

因此更准确的模块名称是“波束影响与接收声场计算”，声强只是其中一种叠加方式。

### 8.2 `PickEpsilon`：初始波束宽度参数

该函数根据波束类型、角频率、声速、发射角间隔和循环距离等参数计算复波束常数 `epsilon`。它决定初始波束宽度和后续 Cerveny 波束表达式。

由于 `epsilon` 显式依赖 `omega`，即使几何轨迹复用，该参数也必须逐频计算。

### 8.3 `influence.f90`：单条波束对接收场的贡献

当前二维代码按 `Beam%Type` 选择以下算法：

| 过程 | 坐标/波束形式 | 主要职责 |
|---|---|---|
| `InfluenceCervenyRayCen` | 射线中心坐标 Cerveny | 在射线法向坐标中计算旁轴波束 |
| `InfluenceCervenyCart` | Cartesian Cerveny | 在规则接收网格上计算旁轴波束 |
| `InfluenceGeoHatRayCen` | 射线中心坐标 hat | 几何波束窗口贡献 |
| `InfluenceGeoHatCart` | Cartesian hat | 默认几何波束贡献 |
| `InfluenceGeoGaussianCart` | Cartesian Gaussian | 高斯权重几何波束 |
| `InfluenceSGB` | Simple Gaussian Beam | 简单高斯波束贡献 |

这些过程的共同工作模式为：

1. 遍历相邻轨迹点形成的射线段；
2. 筛选可能受该段影响的接收距离/深度；
3. 在射线段上插值几何量、动态射线量和复走时；
4. 计算波束横向距离、窗函数、几何扩散和相位；
5. 按 `RunType` 将贡献加入 `U`，或调用 `AddArr` 记录到达量。

### 8.4 `BranchCut` 和焦散处理

`BranchCut` 追踪复平方根的分支和 KMAH 指数，避免射线经过焦散时相位发生错误跳变。动态射线量 `q` 的过零/相位变化不是单纯的数值细节，而是声场相位正确性的组成部分。

### 8.5 `ApplyContribution`：场分量选择

该过程把波束贡献写入压力或位移分量。`Beam%Component` 可控制压力以及水平/垂直位移等输出形式；当前常见用法为压力 `P`。

### 8.6 `ScalePressure`：射线扇积分后的整体归一化

所有射线贡献累计完毕后，`ScalePressure` 根据以下因素统一缩放：

- 发射角步长 `Dalpha`；
- 初始声速；
- 频率；
- 点源/线源几何；
- 所选波束类型；
- 相干或非相干运行模式。

对于非相干运行，源码对累计强度取平方根，把结果转换为压力幅值表示。因此输出 SHD 中仍是压力形式，而不是直接保存声强。

## 9. 模块六：结果输出

| 文件/模块 | 输出类型 | 功能 |
|---|---|---|
| `WriteRay.f90` | `.ray` | 写射线轨迹点及反射信息 |
| `ArrMod.f90` | `.arr` | 累计、排序并写 ASCII/二进制到达量 |
| `RWSHDFile.f90` | `.shd` | 读写声场文件头和压力记录 |
| `bellhop.f90` | `.prt` 及 SHD 数据记录 | 运行日志、进度、警告和二维压力矩阵写入 |

输出路径由 `RunType(1:1)` 决定：

```text
R/E       -> RAYFile
A/a       -> ARRFile
C/S/I     -> SHDFile
```

当前二维主程序写 SHD 时，记录号只按源深度和接收深度推进。仓库中能够读取 `freqVec` 或描述多频 SHD 头的片段来自此前多频实验文件的拷贝，不代表原始 Bellhop 模型已形成宽带输入输出链路；当前主计算和数据记录仍是单频结构。

## 10. 支撑工具模块

下列 `misc/` 文件一般不直接表达 Bellhop 的业务阶段，但为上述模块提供数值和 I/O 支撑：

| 模块 | 用途 |
|---|---|
| `AttenMod.f90` | 衰减单位转换、复声速、Francois-Garrison 等吸收模型 |
| `RefCoef.f90` | 读取和插值外部反射系数表 |
| `beampattern.f90` | 读取源波束方向图 |
| `RWSHDFile.f90` | SHD 格式读写 |
| `pchipMod.f90`, `splinec.f90` | SSP 插值 |
| `SourceReceiverPositions.f90` | 收发位置、接收网格和频率向量 |
| `FatalError.f90` | 统一错误退出 |
| `MathConstants.f90` | π、虚数单位、角度转换等常数 |
| `SortMod.f90`, `monotonicMod.f90`, `subtabulate.f90` | 输入向量排序、单调性检查和等距展开 |
| `PolyMod.f90` | 反射系数插值等使用的多项式工具 |

当前 Makefile 是判断“哪些源码真正参与二维运行”的可靠依据；`misc/` 中未列入 `MISC_OBJECTS` 的其他通用算法，不应默认视为当前执行链的一部分。

## 11. 三维扩展代码分类

仓库中的三维代码可与二维功能一一对应：

| 二维职责 | 三维/宽环境对应文件 |
|---|---|
| 三维主控 | `bellhop3D.f90` |
| 三维边界面 | `bdry3DMod.f90` |
| N×2D 步进 | `Step2DMod.f90` |
| 真三维步进 | `Step3DMod.f90` |
| N×2D 边界反射 | `Reflect2DMod.f90` |
| 真三维边界反射 | `Reflect3DMod.f90` |
| 三维接收场 | `influence3D.f90` |
| 三维射线法向 | `RayNormals.f90` |
| 曲面局部几何 | `Cone.f90`, `Parabot.f90` |

`bellhop3D.f90` 内含 `TraceRay2D` 和 `TraceRay3D` 两条路径：前者用于忽略水平折射的 N×2D 计算，后者使用完整三维状态。这些文件可作为未来扩展参考，但不应与当前 Makefile 构建出的二维基线混在一起验证。

另外，`influence3D copy*.f90` 是多个同模块名的副本，不能同时参与构建。它们更像历史实验版本，应在正式模块化时清理、归档或明确版本来源。

## 12. 按数据流重新划分模块边界

如果后续进行 C++ 或 Fortran ray-reuse 重构，建议把当前共享状态拆成以下明确的数据对象：

| 建议模块 | 只负责 | 建议输入/输出 |
|---|---|---|
| `EnvironmentModel` | SSP、密度、吸收原始参数 | `sample(position, frequency)` |
| `BoundaryGeometry` | 海面/海底坐标、分段、切法向、曲率 | 几何查询和交点事件 |
| `BoundaryAcoustics` | 边界类型、半空间、反射系数 | `reflection(event, frequency)` |
| `RayIntegrator` | `x/t` 中心射线积分 | 几何轨迹点 |
| `DynamicRayIntegrator` | `p/q` 演化和界面跳跃 | 动态射线状态 |
| `RayEventDetector` | 分层、边界、计算盒事件 | 有序事件流 |
| `RayGeometryCache` | 可跨频复用的轨迹和边界事件 | 不可变轨迹缓存 |
| `FrequencyReweighter` | 吸收、反射幅相、频率有效终点 | 某频率下的轨迹权重 |
| `BeamInfluence` | 单波束对接收器的复贡献 | 局部复压力/强度贡献 |
| `FieldAccumulator` | 相干、半相干、非相干叠加 | 接收场矩阵 |
| `OutputWriter` | RAY/ARR/SHD 序列化 | 文件记录 |

建议的 ray-reuse 数据流为：

```text
固定环境几何 + 源位置 + 发射角
              |
              v
      RayIntegrator（仅一次）
              |
              v
  RayGeometryCache + BoundaryEvent 列表
              |
       +------+------+------+
       | f1   | f2   | ...  | fn
       v      v      v      v
  逐频吸收/反射/波束参数重着色
       |      |      |      |
       v      v      v      v
  BeamInfluence + FieldAccumulator
              |
              v
          多频 SHD
```

## 13. 可复用状态与逐频状态

本项目已整体排除 beam shift。在实声速场和边界几何不随频率变化的目标范围内，可按下表划分：

| 状态 | 是否可跨频复用 | 原因 |
|---|---|---|
| `x`, `t`, 实声速 `c` | 是 | 由实声速及其梯度决定 |
| `p`, `q` | 通常是 | 由实声速二阶导数、边界曲率和几何跳跃决定 |
| 边界交点、法向、入射角 | 是 | 几何量 |
| 上下边界反射次数 | 是 | 由几何路径决定 |
| `tau` 实部 | 是 | 几何传播时间 |
| `tau` 虚部 | 否 | 吸收一般依赖频率 |
| 反射 `Amp/Phase` | 否 | 半空间或表格反射系数可能依赖频率 |
| `epsilon`、波束宽度和窗口 | 否 | 显式依赖波长/角频率 |
| `exp(i*omega*tau)` | 否 | 相位显式依赖频率 |
| 压力场 `U` | 否 | 每个频率都需重新叠加 |
| 幅度阈值产生的终止点 | 否/需特殊处理 | 当前终止逻辑受逐频损耗控制 |

beam shift 不进入本次数据模型、实现和测试范围。

## 14. 现有源码中的主要耦合点

1. **全局状态耦合**：`freq`、`omega`、`Beam`、`ray2D`、SSP 分段索引等由模块全局共享，不利于并行、多频和单元测试。
2. **轨迹与损耗耦合**：`Step2D` 同时推进 `x/t/p/q` 和复 `tau`。
3. **反射几何与反射声学耦合**：`Reflect2D` 同时做镜面反射、曲率修正和复反射系数；原代码中的 beam shift 分支不进入本次重构。
4. **频率状态影响轨迹长度**：`TraceRay2D` 以 `Amp < 0.005` 提前终止。
5. **任务分支集中**：`RunType` 的多个字符位穿透主程序、影响函数和输出模块。
6. **实验文件与原始基线混杂**：此前多频尝试带入了 `freqVec` 和部分 SHD 头代码，但当前原始模型核心仍为标量频率，不能把实验痕迹当作已完成能力。
7. **二维/三维源码并置**：同名过程和历史 copy 文件增加了错误编译或误读风险。

### 14.1 已确认的源码与历史制品不一致

1. 测试环境曾使用第六个 SSP 选项字符 `B` 表示宽带，但当前 `ReadEnvironmentBell.f90` 对应分支只接受既有选项，直接使用会报错。
2. 整理前的未知来源可执行文件曾输出 `Broadband calculation enabled`，并生成完整多频 SHD；当前可重现源码不包含相同主循环，因此历史二进制和结果只能作为观察样本，不能作为唯一 oracle。
3. 仓库同时存在 `Bellhop/sspMod.f90` 与 `misc/sspMod.f90`。当前二维 Makefile 明确编译前者，后者的 `UpdateSSPLoss/UpdateHSLoss` 不会自动进入当前执行链。
4. `Bellhop/sspMod.f90` 读取衰减参数后立即转换成复声速，没有完整保留逐节点原始 `alphaR/alphaI`。逐频重建前必须先保留这些原始输入或建立不可变衰减模型。

### 14.2 SSP 与半空间的逐频接口要求

重构后的环境模型应分开提供频率无关和频率相关查询：

```cpp
SoundSpeedSample SoundSpeedProfile::evaluate(Vec2 x) const;
double SoundSpeedProfile::imaginarySpeed(Vec2 x, double frequency) const;
HalfspaceSample BoundaryModel::evaluate(double range, double frequency) const;
```

输入层保存用户给定的实声速、衰减参数、衰减单位、幂律参数和半空间材料参数，不以某个参考频率转换后的复声速覆盖原始数据。

## 15. 源码分类结论

从算法上，Bellhop 不是简单的“先算轨迹、再算声强”两段式程序，而是以下四类功能的组合：

1. **环境和几何建模**：提供声速场、边界形状、收发位置和运行配置；
2. **中心射线与动态射线跟踪**：同时推进 `x/t` 和 `p/q`，并处理介质分层事件；
3. **边界交互**：完成穿越检测、镜面反射、曲率修正和反射幅相；
4. **波束到声场的映射**：把离散射线转换为接收点复声压、强度型累计或到达量。

当前二维源码的关键业务链可以压缩为：

```text
ReadEnvironment
  -> Boundary/SSP preprocessing
  -> TraceRay2D
       -> Step2D
       -> interface jump
       -> boundary detection
       -> Reflect2D
  -> PickEpsilon
  -> Influence*
  -> ScalePressure
  -> RAY / ARR / SHD output
```

对 ray-reuse 而言，最重要的重构不是简单地在最外层增加频率循环，而是先把“几何轨迹与事件”和“逐频吸收、反射及波束贡献”从 `ray2DPt`、`Step2D`、`Reflect2D` 和 `TraceRay2D` 中解耦。只有完成这一层模块边界划分，多频计算才能在保持原 Bellhop 数值行为的同时真正复用轨迹。

## 16. 射线理论与源码映射

### 16.1 理论来源和实现优先级

本项目以 [03 射线轨迹方程以及动态射线追踪方程推导](./03-射线轨迹方程以及动态射线追踪方程推导.html) 作为二维射线理论参考。该文档已经修订竖直慢度导数和动态方程符号问题。

实现和验收采用以下优先级：

1. 本节统一后的规范方程；
2. 原 Bellhop `Step.f90` 的数值离散行为；
3. 解析算例和可重现单频回归结果；
4. 理论推导文档的中间推导与文字说明。

不能只按公式外观逐行翻译；规范方程、原代码离散行为和数值测试三者必须一致。

### 16.2 规范符号

| 符号 | 含义 | C++ 建议命名 |
|---|---|---|
| `x=(r,z)` | 二维位置 | `position` |
| `s` | 中心射线弧长 | `arcLength` |
| `tau` | 实几何传播时间 | `realTravelTime` |
| `c(r,z)` | 实声速 | `soundSpeed` |
| `u=grad(tau)=(xi,zeta)` | 慢度向量 | `slowness` |
| `e_s=c*u` | 单位切向 | `tangent` |
| `e_n=(c*zeta,-c*xi)` | 单位法向 | `normal` |
| `q` | 法向几何偏移基本解 | `q` |
| `p` | 法向慢度扰动基本解 | `p` 或 `dynamicSlowness` |
| `c_nn` | 声速沿射线法向的二阶导数 | `soundSpeedNormal2` |

为避免歧义，声压统一命名为 `pressure`；动态射线变量才使用 `p`。

### 16.3 中心射线方程

由程函方程

```math
|\nabla \tau|^2 = \frac{1}{c^2}, \qquad \mathbf{u}=\nabla\tau
```

可得以弧长 `s` 为参数的中心射线方程：

```math
\frac{d\mathbf{x}}{ds}=c\mathbf{u}, \qquad
\frac{d\mathbf{u}}{ds}=-\frac{\nabla c}{c^2}
```

以及实传播时间：

```math
\frac{d\tau}{ds}=\frac{1}{c}
```

分量形式为：

```math
\frac{dr}{ds}=c\xi, \quad
\frac{dz}{ds}=c\zeta, \quad
\frac{d\xi}{ds}=-\frac{c_r}{c^2}, \quad
\frac{d\zeta}{ds}=-\frac{c_z}{c^2}
```

这些方程只依赖实声速及其空间导数，不显式依赖频率，是几何轨迹可复用的理论基础。

### 16.4 动态射线方程

沿中心射线的近轴系统为：

```math
\frac{dq}{ds}=cp, \qquad
\frac{dp}{ds}=-\frac{c_{nn}}{c^2}q
```

其中：

```math
c_{nn}=\mathbf{e}_n^T\nabla^2 c\,\mathbf{e}_n
```

原 Bellhop 同时积分两组基本解，所以 `ray2D%p` 和 `ray2D%q` 是长度为 2 的基本解数组，初值为：

```text
p = [1, 0]
q = [0, 1]
```

它们不是二维空间坐标，不能在重构中误当作普通 `Vec2`。

### 16.5 `Step.f90` 逐项映射

| 理论量 | Fortran 表达 | 说明 |
|---|---|---|
| `x` | `ray%x` | 二维位置 `(r,z)` |
| `u` | `ray%t` | 注释称 scaled tangent，实际为慢度向量 |
| `e_s` | `c * ray%t` | 单位射线切向 |
| `p,q` | `ray%p`, `ray%q` | 两组动态基本解 |
| `d x/ds` | `urayt = c * ray%t` | 位置推进 |
| `d u/ds` | `-gradc/csq` | 慢度推进 |
| `d p/ds` | `-cnn_csq*q` | 动态慢度推进 |
| `d q/ds` | `c*p` | 动态位移推进 |

设慢度为 `u=(xi,zeta)`，则代码中的法向二阶导数满足：

```math
\frac{c_{nn}}{c^2}
=c_{rr}\zeta^2-2c_{rz}\xi\zeta+c_{zz}\xi^2
```

对应 `Step.f90`：

```fortran
cnn_csq = crr * t(2)**2 - 2*crz*t(1)*t(2) + czz*t(1)**2
```

该表达式应在重构中封装成独立、可单元测试的函数。

### 16.6 理论验收测试

- 等声速：中心射线为直线，动态 `p/q` 满足解析解；
- 线性声速梯度：验证 Snell 不变量和弯曲方向；
- 二次声速场：验证 Hessian 和 `c_nn`；
- 邻近发射角有限差分：验证动态 `q`；
- 步长减半：验证 modified Heun/box 的二阶收敛趋势；
- 无色散多频环境：所有频率的几何路径必须一致。

## 17. 宽带复用的物理边界

### 17.1 可跨频复用的状态

在本项目排除 beam shift、实声速无色散且环境几何固定的范围内，可以复用：

- 中心轨迹 `position/slowness`；
- 动态射线基本解 `p/q`；
- 实传播时间；
- 边界交点、活动分段、切法向和入射方向；
- 几何反射方向、曲率修正和反射次数。

### 17.2 必须逐频重算的状态

- 水体吸收和复传播时间虚部；
- 半空间或表格边界的反射幅度与相位；
- `PickEpsilon`、波束半径、窗口和波长相关参数；
- `exp(i*omega*tau)` 传播相位；
- coherent、semi-coherent 或 incoherent 的场贡献；
- 最终 `ScalePressure`；
- 受 `Amp < 0.005` 规则影响的逐频有效终止位置。

原复杂度近似为：

```text
Nfreq * Nray * (TraceCost + InfluenceCost)
```

复用后为：

```text
Nray * TraceCost
+ Nfreq * Nray * (ProjectionCost + InfluenceCost)
```

因此 ray reuse 只消除重复 Trace；接收网格很大时，Influence 仍可能成为主要瓶颈，加速比通常小于频率数。

2026-07-30 在提交 `c77ff60` 上完成的正式 Munk 16频基准验证了这个边界。
该 profile 以最高频率生成 10,000 条共享射线，接收网格为
`201×501=100,701` 点。nonreuse 的 Trace 中位数为 `6.114 s`，只占外部
wall 的 `2.13%`；Influence 为 `279.429 s`，占 `97.42%`。串行 reuse 将
总射线点从约 5,389 万降到约 337 万，但 Influence 仍逐频执行，因此加速仅
`1.023×`，与只消除重复 Trace 的 Amdahl 上限 `1.020×` 一致。频率并行将
端到端加速提高到 8-worker `4.424×`、10-worker `4.643×`，达到 `≥4×`
项目门槛，但尚未接近 worker 数。

由此，后续性能优化顺序调整为：

1. 先量化并移出 Influence 逐射线入口的重复全工作区/冻结几何校验；
2. 消除热循环中的重复边界检查和索引计算；
3. 在不改变每个接收点贡献累加顺序的前提下匹配工作区布局与 depth 内循环；
4. 再评估 receiver tile、AoS/SoA、向量化和更大范围的调度变化；
5. 每一步以 SHD 逐字节一致和分级 benchmark 为门，不直接用更昂贵的
   64频全矩阵探索实现方向。

### 17.3 范围外条件

以下条件整体不进入本次修改，配置中出现时直接报告“不支持”：

- 实声速随频率色散；
- 任何 beam shift 选项；
- 不同频率使用不同环境几何或发射角集合；
- 需要 3D/N×2D、arrivals、eigenray 或其他未支持模式。

## 18. 首版范围与数据模型

本章的数据模型不是 RayReuse 阶段才追加的设计，而是 `Bellhop_F2CPP` 的强制基础设计。F2CPP 虽然只计算一个频率，也必须生成并消费完整、可冻结的 `RayPathCache`，严格分离频率无关轨迹与逐频声学状态。若 M1/M2 仍依赖追踪器局部数组、边追踪边累加且不保留求积/反射信息，则不能视为 F2CPP 验收通过，也不能据此派生 RayReuse。

### 18.1 首版支持矩阵

| 能力 | 首版选择 |
|---|---|
| 空间 | 二维 `(r,z)` |
| 声场 | coherent complex pressure / TL |
| 波束 | Cartesian Cerveny，对应原版 `CC` 主路径 |
| 接收器 | rectilinear 网格 |
| 环境 | 固定实声速和固定边界几何 |
| SSP | 首先 C-linear，再按回归需求扩展 |
| 边界 | 真空海面；刚性和声学半空间海底 |
| 衰减 | 水体及海底逐频计算 |
| 输出 | 多频 SHD 和诊断日志 |

首版不实现 ray plot、eigenray、arrivals、incoherent/semi-coherent TL、3D/N×2D、不规则接收网格、速度分量和多种波束类型运行时切换。beam shift 在本次修改中完全排除，不为其保留兼容分支。

### 18.2 不可变输入模型

```cpp
struct SimulationCase {
    Environment environment;
    Source source;
    ReceiverGrid receivers;
    std::vector<double> frequencies;
    LaunchFan launchFan;
    IntegratorSettings integrator;
};
```

构造完成后环境不再修改。频率必须作为显式参数传递，不能继续依赖全局 `freq/omega`。

### 18.3 几何轨迹与求积信息

```cpp
struct RayState {
    Vec2 position;
    Vec2 slowness;
    std::array<double, 2> dynamicP;
    std::array<double, 2> dynamicQ;
    double soundSpeed;
    double realTravelTime;
};

struct StepQuadrature {
    double stepLength;
    double startWeight;
    double midpointWeight;
    Vec2 midpoint;
};
```

只保存端点可能不足以重建原 `Step2D` 的复走时积分，因此 `RayPath` 还应保存每步等价求积数据和明确的终止原因。该结构及其完整填充逻辑必须在 F2CPP 中实现和测试，不能推迟到 RayReuse。

### 18.4 反射事件

```cpp
enum class ReflectionBoundary {
    SeaSurface,
    Seabed
};

struct ReflectionEvent {
    std::size_t rayPointIndex;
    ReflectionBoundary boundary;
    std::size_t boundarySegment;
    Vec2 position;
    Vec2 boundaryTangent;
    Vec2 outwardNormal;
    Vec2 incidentSlowness;
    Vec2 reflectedSlowness;
    double tangentSlowness;
    double normalSlowness;
};
```

`boundary` 必须显式标明该事件是碰撞海面 `SeaSurface` 还是海底 `Seabed`，不能再依赖累计次数或当前活动分段反推。几何追踪阶段记录碰撞位置、边界段、切法向和入/反射慢度；频率投影阶段据此选择海面或海底声学模型并重算反射幅相。`NumTopBnc/NumBotBnc` 可由事件序列统计得到，不再作为事件的权威表示。

### 18.5 单频临时状态

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
```

该状态由几何轨迹和频率生成，在完成一次 Influence 累加后即可释放，不需要永久保存所有射线、所有频率的完整状态。

## 19. 核心接口和主循环

### 19.1 几何追踪器

```cpp
RayPath GeometryTracer::trace(
    const Source& source, double launchAngle) const;
```

职责是推进 `x/t/p/q`、处理事件和几何反射、记录求积信息，并追踪到空间盒或数值异常；不得用逐频幅度作为几何终止条件。

### 19.2 频率投影器

```cpp
RayFrequencyState FrequencyProjector::project(
    const RayPath& path,
    double frequency,
    double sourceAmplitude) const;
```

职责包括：

1. 根据频率计算水体和半空间复声速；
2. 使用缓存的二阶求积信息重建复传播时间；
3. 在反射事件处计算该频率的反射幅相；
4. 累计幅相并维护 active mask；
5. 为 Influence 提供只读逐频状态。

### 19.3 Influence 接口

```cpp
influence.accumulate(
    field.at(frequencyIndex),
    path,
    frequencyState,
    frequency,
    epsilon);
```

显式传入几何轨迹和逐频状态，可以在类型和所有权层面防止不同频率互相污染。

### 19.4 先复刻 C++ 单频核心，再扩展宽带

正式实施顺序确定为：

1. **`Bellhop_F2CPP` 单频复刻**：只实现目标范围内的 2D Cartesian Cerveny coherent pressure 路径，逐组件和端到端对照可重现 Fortran 单频 oracle；
2. **派生 `Bellhop_RayReuse` 单频/宽带非复用基线**：复制 F2CPP 已验收的必要代码形成独立工程，先确认派生后的单频结果未发生漂移，再让每个频率完整追踪一次，验证频率循环、多频记录号和输出；
3. **串行 Ray-Reuse**：启用既有 `RayPathCache` 的全频共享遍历和重复 `FrequencyProjector` 调用，比较复用结果与宽带非复用结果；
4. **有界频率并行**：在串行结果稳定后加入频率所有权、内存预算和单 writer 输出；
5. **性能与大规模 I/O 优化**：最后评估 receiver tile、磁盘轨迹缓存和 HDF5。

F2CPP 虽然只运行一个频率，仍必须直接采用 RayReuse 所需的数据模型，而不是等派生后再扩展：`SimulationCase` 保留 `frequencies` 集合并要求 `frequencies.size()==1`；追踪器完整生成 `RayPathCache`；频率通过接口显式传入；`RayPath` 不保存逐频幅相；单频投影状态和压力使用独立 `RayFrequencyState/FrequencyWorkspace`。F2CPP 的端到端主循环也应遵循“完整追踪并冻结缓存 → 对唯一频率投影 → Influence 累加”的边界。RayReuse 派生后只扩展频率调度、共享缓存遍历和并行所有权，不再重做基础变量与轨迹保存设计。

不得从 Fortran 全模式逐行翻译，也不得在 C++ 单频轨迹、反射和 Influence 尚未通过回归前直接实现 Ray-Reuse。该顺序可以依次隔离：

```text
语言/离散误差
    → 宽带循环与 I/O 错误
    → 轨迹复用误差
    → 并行与归约误差
```

### 19.5 先缓存射线扇，再逐频投影

当前目标场景中，发射角数量和单条轨迹点数预期可控，宽带计算的目标点也通常不多。因此正式的 ray-reuse 数据流确定为：先追踪并缓存全部几何射线，冻结为只读 `RayPathCache`，再对不同频率逐个或有界并行计算复压力。

```cpp
RayPathCache paths;
paths.reserve(launchAngles.size());
for (double launchAngle : launchAngles) {
    paths.push_back(tracer.trace(source, launchAngle));
}
paths.freeze();

for (std::size_t fi = 0; fi < frequencies.size(); ++fi) {
    FrequencyWorkspace workspace(receiverGrid);
    for (const RayPath& path : paths) {
        auto state = projector.project(path, frequencies[fi], sourceAmplitude);
        influence.accumulate(
            workspace.pressure, path, state, frequencies[fi]);
    }
    scalePressure(workspace.pressure);
    outputWriter.writeFrequency(fi, workspace.pressure);
}
```

`RayPathCache` 不能只保存 `(r,z)` 折线。每条轨迹必须同时保存 `position/slowness/dynamicP/dynamicQ/realTravelTime`、每步 `StepQuadrature`、`ReflectionEvent` 和几何终止原因，否则无法在不重新积分几何轨迹的前提下严格重建逐频复走时、反射幅相和 Influence。

这一选择用轨迹缓存换取更简单的频率所有权和更低的压力场内存。运行前必须估算实际轨迹点总数；如果未来长距离、超小步长或极大发射角集合使缓存超出预算，再增加磁盘轨迹缓存，而不在首版中预先复杂化。

### 19.6 由最高频率确定发射角数目

在二维程序中，一个发射角对应一条被跟踪的中心射线/波束，因此这里统一使用源码变量名“发射角数目 `Nalpha`”。相关源码实际上包含两套经验判据，必须区分其作用。

#### 19.6.1 原版自动估算公式

`angleMod.f90::ReadRayElevationAngles` 在输入 `Nalpha == 0` 时自动设置实际使用的发射角数目（`angleMod.f90:44-50`）。源码注释明确说明它是基于等声速海洋的经验想法，并非严格理论公式：

```fortran
Nalpha = MAX(INT(0.3 * R_max * freq / 1500.0), 300)

d_theta_recommended = ATAN(Depth / (10.0 * R_max))
Nalpha = MAX(INT(pi / d_theta_recommended), Nalpha)
```

可整理为：

```text
N_phase = max(floor(0.3 * R_max * f / 1500), 300)
delta_alpha_depth = atan(Depth / (10 * R_max))
N_depth = floor(pi / delta_alpha_depth)
Nalpha_auto = max(N_phase, N_depth)
```

其中第一项限制相邻波束在最大距离处的相位差，第二项要求波束相对水深足够窄。常数 `0.3`、`10`、最低数量 `300` 和参考声速 `1500 m/s` 都属于原 Bellhop 的经验参数。

#### 19.6.2 `BellhopCore` 的数量检查

轨迹开始前，`BellhopCore` 还计算另一套建议值（`bellhop.f90:251-257`）：

```text
delta_alpha_check = sqrt(c_source / (6 * f * R_max))
Nalpha_check = 2 + floor((alpha_max - alpha_min) / delta_alpha_check)
```

原版只在 coherent 模式下检查 `Nalpha < Nalpha_check`，然后输出 `Too few beams` 警告；它不会用 `Nalpha_check` 覆盖已经读取或自动估算的 `Angles%Nalpha`。因此该平方根公式应称为“发射角数量充分性检查”，不能写成原版实际的自动选取公式。

#### 19.6.3 本项目的宽带规则

宽带运行使用：

```text
f_design = max(frequencies)
```

把 `f_design` 代入原版 `ReadRayElevationAngles` 的经验公式，得到本次运行实际采用的 `Nalpha_auto`；再用同一最高频率执行 `BellhopCore` 的 `Nalpha_check` 充分性检查。为避免自动模式生成后仍出现 “Too few beams”，自动模式最终采用：

```text
Nalpha_final = max(Nalpha_auto(f_design), Nalpha_check(f_design))
```

随后在输入的发射角上下限之间生成 `Nalpha_final` 个角度。这个数目就是最终的轨迹跟踪射线总数，所有频率共享同一发射角集合。必须在日志中同时输出 `f_design`、`N_phase`、`N_depth`、`Nalpha_check` 和 `Nalpha_final`，便于确认经验参数对计算规模的影响。

## 20. 压力场、SHD 与并行设计

### 20.1 压力场布局

逻辑结果布局仍是 frequency-major：

```text
field[frequency][receiverDepth][receiverRange]
```

其中 range 连续，以匹配 Influence 对接收距离的遍历。但这是文件和逻辑维度，不表示必须在内存中同时保留全部频率。实际工作区使用：

```text
pressure[activeFrequency][receiverDepth][receiverRange]
```

首版串行模式中 `activeFrequencyCount=1`；并行时只为当前正在计算的有界频率任务分配独立压力切片。某频率累加完全部 `RayPath`、缩放并写出后，立即清空并复用该 `FrequencyWorkspace`。

内部使用 `std::complex<double>`，单频压力缓冲区内存为：

```text
M_frequency = Nsd * Nrd * Nrr * sizeof(complex<double>)
M_pressure  ≈ (N_active + N_output_queue) * M_frequency
```

其中 `N_active` 不大于实际频率工作线程数，`N_output_queue` 是有界输出队列容量，建议初值为 1–2。内存不再随总频率数 `Nfreq` 线性增长。

例如 `Nsd=1`、`Nrd=500`、`Nrr=10000` 时，一个双精度复数频率切片约为 80 MB；8 个并行频率约需 640 MB 活动压力工作区，再加有界输出队列的 80–160 MB，而不是为全部上千个频率分配数十 GB。目标点较少时，这部分通常可控。

### 20.2 SHD 记录号

二维规则网格记录号必须包含频率、源深度和接收深度：

```fortran
IRec = 10 + Irz1 + NRz_per_range * &
       ( ( is - 1 ) + Pos%NSz * ( ifreq - 1 ) )
```

建议封装为独立 `SHDRecord2D(ifreq, isz, irz)` 并对维度边界进行单元测试；写出顺序必须用 `test/PlotRead/bellhop_io_py/` 独立验证。

### 20.3 无竞争 CPU 并行

正确性版本稳定后，采用“全射线扇只读缓存 + 有界频率所有权”：

```cpp
parallel_for(launchAngles, [&](std::size_t ai) {
    paths[ai] = tracer.trace(source, launchAngles[ai]);
});
paths.freeze();

bounded_parallel_for(frequencyIndices, activeFrequencyLimit,
    [&](std::size_t fi) {
        FrequencyWorkspace workspace(receiverGrid);
        for (const RayPath& path : paths) { // 固定角度顺序
            auto state = projector.project(path, frequencies[fi]);
            influence.accumulate(
                workspace.pressure, path, state, frequencies[fi]);
        }
        scalePressure(workspace.pressure);
        outputQueue.push(fi, std::move(workspace.pressure));
    });
```

每个任务独占一个单频压力缓冲区，所有任务只读 `RayPathCache`，无需复数原子加法，也不需要“线程数 × 全宽带压力场”的私有副本。同一频率保持固定角度累加顺序，结果更易复现。

`activeFrequencyLimit` 应取下列三者最小值：

```text
min(workerCount, frequencyCount, memoryLimitedFrequencyCount)
```

给定用户内存预算 `M_budget`、轨迹缓存 `M_paths`、其他固定工作区 `M_fixed` 和输出队列容量 `N_output_queue` 时，可按下式估算：

```text
memoryLimitedFrequencyCount = floor(
    (M_budget - M_paths - M_fixed
              - N_output_queue * M_frequency) / M_frequency)
```

若括号内结果小于一个 `M_frequency`，程序不应强行运行，而应降低输出队列、启用 receiver tile 或报告内存预算不足。

输出队列必须有容量上限；写盘落后计算线程会被反压，不能无限积压已完成的压力切片。如果单频切片本身仍超出内存预算，再引入 receiver tile；该模式需对同一只读轨迹缓存重复遍历，但不重新追踪射线。

### 20.4 宽带输入输出格式策略

`.env` 输入体积很小，多频计算时不会成为性能瓶颈，但其位置字段、字符选项和多个关联文件不适合渗透到新求解器内部。首版保留 `.env` 作为 Bellhop 兼容输入，由 `EnvReader` 一次转换为不可变 `SimulationCase`；核心计算不读取文件位置，也不保留 Fortran 全局输入状态。后续可增加 TOML/JSON 配置，但不与数值重构同时强制切换。

SHD 在语义上能表达多频复压力，且是与 Acoustic Toolbox 格式和 Fortran 基线对比的必要格式，因此首版继续支持。读取、绘图和数值导出统一由 `test/PlotRead/bellhop_io_py/` 完成。但它不适合让多个计算线程直接随机写入。建议由单一 `OutputWriter` 按频率序号写入已完成切片，并使用有界队列隔离计算与磁盘 I/O。

对大规模正式计算，后续可将 HDF5 作为内部主结果格式，使用维度 `[frequency][sourceDepth][receiverDepth][range]`、坐标数据集、计算元数据、分块和频率完成标记；SHD 则保留为兼容导出。该扩展属于算法正确性闭环之后的工程任务，不阻塞首个 SHD 里程碑。

## 21. C++ 重构与性能决策

### 21.1 采用结论

C++ 不会仅因语言选择就天然快于 Fortran。项目的主要性能收益依次来自：

1. ray reuse，消除逐频重复追踪；
2. 无竞争的任务划分；
3. 移除模块级共享可变状态；
4. 数据布局、批处理、向量化和缓存优化；
5. 最后才是语言层面的常数优化。

当前结论是**有条件采用 C++20/CMake 重构**：项目本身需要裁剪模式、重构数据流并建立线程安全架构，且必须通过数值回归和性能门槛。若目标只是最短时间得到加速，现代 Fortran + OpenMP 同样可实现 ray reuse。

### 21.2 推荐工程结构

```text
Bellhop_F2CPP/
  CMakeLists.txt
  include/bellhop/
    model/          Environment, SSP, Boundary, Grid
    numerics/       Vec2, interpolation, intersection
    ray/            GeometryTracer, RayPath, ReflectionEvent
    cache/          RayPathCache
    acoustics/      Attenuation, ReflectionAcoustics
    field/          FrequencyProjector, CervenyInfluence, FrequencyWorkspace
    io/             EnvReader, ShdWriter
  src/
  app/              bellhop_f2cpp 单频程序
  tests/
    unit/
    regression/
    golden/

Bellhop_RayReuse/
  CMakeLists.txt
  include/rayreuse/
    model/          Environment, SSP, Boundary, Grid
    numerics/       Vec2, interpolation, intersection
    ray/            GeometryTracer, RayPath, ReflectionEvent
    acoustics/      Attenuation, ReflectionAcoustics
    influence/      CartesianCerveny
    broadband/      FrequencyScheduler, FrequencyProjector
    cache/          RayPathCache, optional disk cache
    field/          FrequencyWorkspace, broadband orchestration
    io/             MultiFrequencyWriter
  src/
  app/              bellhop_rayreuse 宽带程序
  tests/
    broadband/
    reuse/
    performance/
```

`Bellhop_RayReuse` 初始代码由已验收的 `Bellhop_F2CPP` 复制/派生，这是有意保留的源码演进关系。派生后，两者各自产出独立可执行程序并维护自己的源码副本；不通过 `add_subdirectory`、静态/动态库或跨目录头文件/源码包含建立持续依赖。共同变量规范、相同标准算例、中间状态导出和复压力/TL 对比用于防止后续改造产生数值漂移。两者首版都只有一个明确计算模式，不设计庞大的运行模式继承树。

### 21.3 性能 go/no-go 门槛

- F2CPP 保留完整冻结 `RayPathCache`；单频相对优化 Fortran 的比值作为
  诊断数据，不再作为 M2 阻断门；
- F2CPP M2 使用 16 频点 trace-once 摊销模型：六个标准算例相对“每频
  重复一次完整 F2CPP”均至少节省 1%，并固定分阶段时间、缓存字节和峰值
  RSS；
- 8 个物理核心上代表性宽带案例端到端加速至少 4 倍；
- 64 频案例中几何追踪次数不随频率数增长；
- 并行与单线程复压力差异满足预设误差标准；
- 峰值内存处于目标机器可接受范围。

前两项关闭 F2CPP 派生门；后三项在 RayReuse 实际多频和并行阶段关闭。
摊销模型只证明完整缓存具有可量化的 trace-once 收益，不替代 RayReuse
宽带实测。若实际宽带原型未通过，先检查任务划分和数据布局；仍不通过时，
应保留 Fortran 并实施同样的算法模块化，而不是为语言选择继续扩大风险。

当前阶段 A～E 已关闭，正式 Munk 16频五轮基准的 8/10-worker 中位加速为
`4.424×/4.643×`，因此 go/no-go 门已通过。后续 Influence 优化属于在数值
契约冻结后的性能工程，不回滚 C++/RayReuse 采用结论；如果优化不能稳定改善
wall，则保留显式 execution mode/worker 配置，不为追求线程数线性加速而
改变数值语义。

### 21.4 主要风险

| 风险 | 影响 | 控制方式 |
|---|---|---|
| 缺少中间状态 oracle | 无法定位轨迹、反射或 Influence 误差 | 先增强可重现 Fortran 基线 |
| Cerveny Influence 迁移困难 | 焦散附近幅相敏感 | 单条射线贡献、KMAH 和复根专项回归 |
| 只缓存轨迹端点 | 逐频复走时与单跑不一致 | 保存 `StepQuadrature` |
| 幅度终止影响路径长度 | 不同频率有效轨迹不同 | 几何追踪到空间终止，逐频 active mask |
| 过早按射线并行 | 场累加需要原子或巨量私有内存 | 先串行，再用频率切片独占 |
| 完整轨迹缓存超预算 | 长距离/小步长时 `RayPathCache` 增大 | 运行前估算轨迹点和字节数；超限时改用顺序磁盘缓存 |
| 输出速度低于计算 | 已完成单频场在内存积压 | 单 writer + 容量 1–2 的有界队列和反压 |
| 历史 3D/copy 文件混入 | 构建链和行为不明确 | 以 Makefile 为二维基线，历史副本不参与首版 |

### 21.5 验证金字塔

```text
解析解
  等声速直线、动态 q、简单反射
        ↓
组件对比
  SSP / Step / Reflect / Influence 中间状态
        ↓
单频端到端
  C++ vs 可重现 Fortran
        ↓
宽带非复用
  一次运行逐频完整追踪
        ↓
宽带 Ray-Reuse
  reuse vs 非复用复压力
```

### 21.6 分阶段验收里程碑

| 里程碑 | 验收内容 | 主要对照 |
|---|---|---|
| **M1：F2CPP 组件级单频** | C++ SSP、中心射线、动态 `p/q`、反射；完整构造 `RayPath/StepQuadrature/ReflectionEvent` | Fortran 中间状态与解析解 |
| **M2：F2CPP 端到端单频** | 冻结 `RayPathCache` 后通过单频投影和 Influence 生成 coherent complex pressure/SHD；逐频量不回写轨迹 | 可重现 Fortran 单频结果 |
| **M3：RayReuse 单频/宽带非复用** | 从 M2 代码派生独立工程；派生后单频结果保持一致，一次运行逐频完整追踪等于多个独立单频运行 | Fortran 与 M2 参照结果 |
| **M4：串行 Ray-Reuse** | 射线扇只追踪一次，复压力等于 M3 | 宽带非复用基线 |
| **M5：并行 Ray-Reuse** | 有界频率并行结果满足确定性容差和内存预算 | M4 串行结果 |

首个必须完成的里程碑是 **M1/M2 单频复刻**，不是直接得到多频 Ray-Reuse。最小环境仍采用等声速、真空海面、刚性平底、一个源和小型规则接收网格；复杂 SSP、真实海底损失和大规模 I/O 在单频链路稳定后逐步加入。

## 22. 当前基线事实与最终决策摘要

当前仓库状态为：

- `Bellhop_origin/Makefile` 已能可重现构建二维 release/static `Bellhop_origin/bin/bellhop.exe`；
- `Bellhop_F2CPP/` 已建立职责 README，`Bellhop_RayReuse/` 已建立空目录，C++ 实现尚未开始；
- 当前代码基线应视为原始、单频 Bellhop 模型；
- `freqVec`、部分多频 SHD 头处理和测试目录中的宽带结果，是此前尝试多频计算时直接拷入的实验文件，不代表原始模型已经具备或验证了宽带基础设施；
- 后续实现可以参考这些实验文件，但验证链必须依次使用可重现的原始单频 Bellhop、C++ 单频复刻和宽带非复用基线；
- 小型单频标准算例和独立 Python SHD 读取回归已建立；当前下一步是冻结指定点复压力/TL 容差、导出中间状态并分阶段计时，而不是直接移植完整 Influence。

最终架构决策如下：

- 使用双精度保存轨迹、动态状态和复相位；
- 环境不可变，频率显式传参；
- 首版复现原 modified Heun/box 积分器；
- `RayPath` 显式保存反射事件和求积信息；
- `ReflectionEvent::boundary` 显式区分海面和海底；
- 上述 RayReuse 数据模型和“追踪缓存 → 单频投影 → 声场累加”边界必须在 F2CPP 阶段完成，不得推迟到宽带工程；
- 最终发射角数目 `Nalpha_final` 由输入最高频率、原 Bellhop 自动估算经验式和数量充分性检查共同确定；
- 先实现唯一的二维 Cartesian Cerveny coherent pressure 路径；
- 先在 `Bellhop_F2CPP/` 完成优化版 C++ 单频实现并通过验收，再复制/派生代码形成 `Bellhop_RayReuse/`，在独立副本中实现宽带非复用和轨迹复用；两工程互不链接；
- 本次修改完全不考虑 beam shift；
- 先完成串行正确性；复用版先缓存完整只读 `RayPathCache`，再进行有界频率切片独占并行；
- 压力缓冲区只为活动频率分配，通过单 writer 和有界队列写出；
- `.env/.shd` 是首版兼容边界，不是数值核心的内部状态模型；
- 所有不支持选项明确报错，不做静默近似。
