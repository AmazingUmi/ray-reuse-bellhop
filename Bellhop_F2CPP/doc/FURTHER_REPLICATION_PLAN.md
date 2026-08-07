# Bellhop F2CPP 二维功能进一步复刻计划

> 规划日期：2026-08-07  
> 参考：会话 `019fb20a-5efc-7c60-aa72-32d4cd270224` 的最新计划  
> 基线：F2CPP 已完成 C-linear、平边界、Cartesian Cerveny coherent
> pressure 单频链和本地双 C++ 编译器验证  
> 默认前提：不实现 3D，也不把 N×2D 作为二维功能纳入

## 1. 目标

在不破坏现有 `RayPathCache → FrequencyProjector → Influence` 分层的前提下，
逐步复刻原版 `Bellhop_origin/Bellhop/bellhop.f90` 二维可执行程序中有实际
使用价值的功能。每项功能必须先在 F2CPP 形成单频、可审计的数值基线，再
作为独立变更同步到 RayReuse；不能在两个工程中同时试错。

本计划不是“把所有源文件翻译成 C++”。复刻对象是可验证的二维运行行为：

```text
ENV/附属文件解析
  → 不可变二维环境
  → 中心射线和动态射线
  → 冻结轨迹与事件
  → 单频声学投影
  → Influence/arrival/eigenray 产品
  → Bellhop 兼容输出
```

## 2. 已冻结的边界

### 2.1 默认纳入

- 原版二维 `bellhop.f90` 构建链中的范围无关和范围相关二维环境；
- 单频 F2CPP；
- PCHIP、N²-linear、Cubic Spline 等一维 SSP 插值；
- 二维 `.ati/.bty` 边界、二维 `Q` 型范围相关 SSP；
- 二维 source/receiver、field、ray、arrival 和 eigenray 产品；
- 原版二维支持的主要边界声学、衰减单位和 beam family；
- AppleClang/GCC C++ 构建验证，以及 GNU Fortran/gfortran oracle。

### 2.2 默认排除

- `Bellhop3D.f90`、真 3D、N×2D、方位角和 3D receiver surface；
- `H` 型 hexahedral SSP 以及所有 3D 边界/Influence 模块；
- F2CPP 多频调度、Ray-Reuse 和频率并行；这些仍只属于
  `Bellhop_RayReuse`；
- beam shift。它已由 R-09 明确排除，因为会使几何轨迹依赖频率；
- HDF5、磁盘轨迹缓存、发布系统和与 Bellhop 数值复刻无关的工程功能；
- `A` 型 analytic SSP 的通用化。原版该分支依赖编译期定制公式，如有明确
  模型需求再单独立项，不作为文件格式兼容功能；
- 为追求逐字节一致而移植未定义行为或明显的历史缺陷。

任何 3D/N×2D 输入都必须继续明确报错；后续阶段不再重复询问是否顺带支持
3D。

## 3. 实施原则

1. **唯一 Fortran oracle**：GNU Fortran/gfortran 是项目唯一支持的原版
   工具链，不再等待或规划第二套 Fortran 编译器。
2. **先接口、后公式**：先解除 `GeometryTracer/RayStepper/FrequencyProjector`
   对 `CLinearSsp` 的具体类型依赖，旧六例零回归后再加入新插值器。
3. **先 F2CPP、后 RayReuse**：一个功能在 F2CPP 完成组件、轨迹、最终场和
   双 C++ 编译器验收后，才允许按单独提交同步到 RayReuse。
4. **几何与逐频状态继续分离**：实声速、边界形状和动态 `p/q` 属于几何；
   虚声速、吸收、复反射系数和场贡献属于逐频状态。
5. **纵向小步提交**：parser/model → 数值组件 → tracer/projector → writer →
   oracle/端到端，不能把多个 Bellhop 选项塞入同一不可诊断提交。
6. **不以最终 SHD 掩盖中间误差**：优先对照 `c/cz/czz`、轨迹、事件、
   `p/q` 和复走时，最后才比较 pressure/TL。
7. **性能只防退化**：功能首轮不设加速目标，但系数预计算不得落入逐射线点
   热循环；记录 Trace、Project、Influence、输出和 RSS。

## 4. 子程序依赖与阶段顺序

主要原版来源与 F2CPP 落点如下：

| 原版二维来源 | 功能 | F2CPP 主要落点 |
|---|---|---|
| `ReadEnvironmentBell.f90`、`SourceReceiverPositions.f90` | ENV、source/receiver、run type | `io/model` |
| `sspMod.f90`、`pchipMod.f90`、`spline*.f90` | SSP 插值与导数 | `model/numerics` |
| `Step.f90`、`bellhop.f90::TraceRay2D` | 中心/动态射线和 limiter | `ray/cache` |
| `bdryMod.f90`、`ReflectMod.f90`、`RefCoef.f90` | 边界几何与反射声学 | `ray/acoustics` |
| `influence.f90`、`bellhop.f90::PickEpsilon` | beam 与场贡献 | `field` |
| `WriteRay.f90`、`ArrMod.f90`、`RWSHDFile.f90` | ray/arrival/SHD 输出 | `io` |

硬依赖关系为：

```text
I0 通用二维数值接口与 oracle 基础
 ├─→ I1 PCHIP SSP
 │    └─→ I2 N²-linear / Cubic Spline
 ├─→ I3 非平坦二维边界 .ati/.bty
 ├─→ I4 边界声学与衰减扩展
 │     └─→ I3-06 长格式沿程地声参数
 ├─→ I5 Q 型范围相关二维 SSP
 └─→ I6 多源/接收网格/射线输出
       ├─→ I7 场分量、相干类型与 beam family
       └─→ I8 arrivals / eigenray
              └─→ 复用 I7 中所需的 beam/接收器求交原语

I1～I8 全部通过
 └─→ I9 二维兼容矩阵与文档收口
```

I2～I6 在 I0 冻结后大多可以独立开发；图中只画真实代码依赖。默认合入顺序
仍按编号执行，这是为了先收敛常用、可诊断的环境功能，不是假定 I5 的 `Q`
型 SSP 依赖 I3 的非平坦边界。I3-06 的长格式边界必须等待 I4 的材料表示；
I7 不早于 I6，以免在 field workspace 稳定后再次改 source/receiver 维度；
I8 最后关闭，并只依赖其实际使用的 I7 beam/求交原语，不要求先实现全部
低优先级 beam family。

## 5. 工作包

### I0：通用二维数值接口与 oracle 基础

目的：为后续 SSP 和范围相关环境消除当前的具体类型耦合，同时证明重构不
改变 C-linear 基线。

- [ ] I0-01 定义 `SspInterpolationKind`，parser 显式区分 `C/P/N/S/Q`；
  未实现值解析后仍返回“已识别但不支持”，不能回退到 C-linear。
- [ ] I0-02 建立只读二维 SSP evaluator 契约，统一返回
  `c/imaginaryC/gradient/Hessian/density/segment`；可用 variant、concept 或
  type-erasure，但热循环不得引入逐点堆分配。
- [ ] I0-03 将 `RayStepper`、`GeometryTracer` 和 `FrequencyProjector` 从
  `CLinearSsp/CLinearFrequencySsp` 具体类型解耦。
- [ ] I0-04 把“仅 C/N/Q/H 在跨段时应用一阶导数跳跃，P/S 不应用”编码为
  SSP 连续性属性，而不是函数名判断。
- [ ] I0-05 扩展 SSP oracle，能导出指定深度和到达侧 segment 下的
  `c/cz/czz/rho`；保持诊断默认关闭且不改变 SHD。
- [ ] I0-06 运行现有六例、三组中间状态和 AppleClang/GCC CTest；F2CPP
  C-linear SHD、probe 计数和错误语义必须保持不变。

出口：这是纯重构提交。若旧结果有数值漂移，不得进入 I1。

### I1：PCHIP SSP（当前第一优先级）

目的：落实参考会话的下一阶段决定，并覆盖现有 C-linear 未真正使用的非零
`czz` 动态射线路径。

- [ ] I1-01 以 `pchipMod.f90`、`sspMod.f90::cPCHIP` 和
  `Step.f90` 为权威，冻结端点导数、单调性限制、节点侧选择和外推规则。
- [ ] I1-02 新增能明显区分 C-linear 与 PCHIP 的 `munk_pchip` 标准算例；
  保存 ENV、gfortran 可执行文件、oracle 输出和提交哈希。
- [ ] I1-03 实现不可变 PCHIP 实系数预计算，查询返回 `c/cz/czz`；密度仍按
  原版逐段线性插值。
- [ ] I1-04 实现逐频复声速 PCHIP 系数。原版先把各节点转换成复声速再求
  PCHIP 系数，不能插值原始 attenuation，也不能只给实系数附加虚部。
- [ ] I1-05 ENV parser 接受 `P`，完整验证节点数量、严格递增深度、有限值、
  极短 segment 和单调/非单调剖面。
- [ ] I1-06 增加节点、端点、峰谷、常值、两点/三点剖面、外推、复系数和
  非有限输入的单元/组件测试。
- [ ] I1-07 对照 gfortran 的 `c/cz/czz/rho`、单步 `p/q`、完整射线和最终
  SHD；报告最大误差字段、射线、点和接收器位置。
- [ ] I1-08 运行旧六例无回归、新 PCHIP 单频矩阵、AppleClang/GCC CTest、
  sanitizer 和 RSS/分阶段计时。

出口：形成一个独立的 F2CPP PCHIP 提交。完成前不修改 RayReuse；完成后才
按独立同步任务复制接口、实现和测试，并验证 nonreuse/reuse/parallel 一致。

### I2：其余范围无关一维 SSP

按两个独立纵切实施，不能一次合并：

1. **I2-A N²-linear (`N`)**
   - 预计算复 `1/c²` 和斜率；
   - 复现 `c`、`cz=-0.5*c³*n2z`、`czz=3*cz²/c`；
   - 保留一阶导数跨段跳跃；
   - 增加强梯度和节点穿越案例。
2. **I2-B Cubic Spline (`S`)**
   - 复现 `CSpline/SplineALL` 边界条件和复系数顺序；
   - 返回连续 `c/cz/czz`，不得触发 C-linear jump；
   - 增加端点曲率和焦散敏感案例。

每个纵切都必须重复 I1 的组件→单步→轨迹→最终场验收。PCHIP 通过不自动
代表 Spline 通过。

### I3：非平坦二维海面与海底

先线性、后曲线，先短格式、后长格式：

- [ ] I3-01 建立 `BoundaryGeometry` 和活动 segment 查询接口，保留现有 flat
  specialization 的行为和性能。
- [ ] I3-02 解析 `.ati/.bty` 的 piecewise-linear short format；范围 km 只在
  parser 边界转换为 m，并按原版向左右延拓。
- [ ] I3-03 将两阶段 step limiter 扩展到边界交点和边界 segment endpoint；
  `ReflectionEvent` 保存真实 segment、切向、法向、曲率和前/后点索引。
- [ ] I3-04 对照斜坡、多折线、segment 端点、掠射和多次反射轨迹。
- [ ] I3-05 实现 curvilinear 边界及非零曲率动态跳跃；不得用折线近似冒充。
- [ ] I3-06 最后支持 long format 的沿程 geoacoustic 参数，并验证事件所记录
  材料与碰撞 segment 一致。

出口：flat 六例逐字节/数值无回归；新增坡底和起伏海面案例的交点、法向、
曲率、`p/q`、反射事件及最终场通过。

### I4：边界声学与衰减扩展

依次实施，逐项保持频率状态不写回轨迹：

- [ ] I4-01 打通 `N/F/M/W/Q/L` 衰减单位的 parser 和端到端用例；已有转换
  纯函数不等于输入功能已支持。
- [ ] I4-02 增加 Francois–Garrison 和 biological volume attenuation，参数
  进入不可变环境，逐频转换进入 projector。
- [ ] I4-03 将声学半空间由当前无剪切流体扩展到含 shear 的弹性半空间；
  先用反射系数组件 oracle，再进入轨迹投影。
- [ ] I4-04 支持 grain-size (`G`) 地声模型。
- [ ] I4-05 最后评估/实现 tabulated reflection coefficient (`F/P/W`) 文件
  路径；读表、写表和预计算表分别单独验收。

出口：每一种材料/衰减选项都有独立小案例和角度×频率反射系数矩阵，不以
单个最终场覆盖全部分支。

### I5：`Q` 型范围相关二维 SSP

这是二维功能，但复杂度显著高于一维插值，必须在通用 SSP 和活动边界段稳定
后实施。

- [ ] I5-01 解析 `.ssp` 的二维 quadrilateral 网格，验证范围/深度单调性和
  维度溢出。
- [ ] I5-02 实现 range/depth segment 定位及 `c/cr/cz/crr/crz/czz`。
- [ ] I5-03 step limiter 同时对齐 range cell、depth cell 和物理边界事件。
- [ ] I5-04 为动态射线补齐非零 `crr/crz/czz` 组合测试和二维 Snell/有限差分
  诊断。
- [ ] I5-05 对照 gfortran 全轨迹、焦散和最终场；确认实 SSP 不随频率变化时
  仍满足 RayReuse 的几何可复用条件。

`H` 型 hexahedral SSP 不随 I5 纳入。

### I6：source、receiver 与射线产品

- [ ] I6-01 支持多个 source depth，按 source 独立生成/冻结轨迹缓存，并按
  SHD source record 顺序写出。
- [ ] I6-02 支持 irregular receiver grid，显式区分笛卡尔积网格与
  `(range,depth)` 配对列表。
- [ ] I6-03 支持 source beam-pattern (`.sbp`) 读取与幅度插值。
- [ ] I6-04 实现 `R` ray-trace 模式和二维 `.ray` writer；它直接使用冻结的
  `RayPathCache`，不另写第二套 tracer。
- [ ] I6-05 扩展 CLI/PRT/SHD 维度验证和大维度乘法溢出检查。

出口：多源 SHD、irregular SHD 和 `.ray` 均能被独立 reader 解析；单源规则
网格结果不变。

### I7：场分量、相干类型与 beam family

按“复用现有 CC 最多”到“新增 Influence 最多”的顺序实施：

1. Cartesian Cerveny 的 `V/H` 分量；
2. `F/M/W` beam width 与 `D/S/Z` curvature condition；
3. point/line source 缩放；
4. incoherent (`I`) 与 semi-coherent (`S`) 累加和缩放；
5. ray-centered Cerveny (`R`)；
6. simple Gaussian、geometric Gaussian、geometric hat 等其余二维 beam。

每个新 Influence 实现必须有单射线贡献 oracle、KMAH/branch-cut 专项测试和
最终场案例。不同相干类型不得复用同一压力容器而隐含改变数值含义，应在类型
或 workspace 层区分 complex pressure 与 intensity accumulation。

### I8：arrivals 与 eigenray

- [ ] I8-01 定义独立的 `Arrival` 数据结构和 receiver 有界存储策略；不得把
  arrival 状态塞入 `FrequencyWorkspace::pressure`。
- [ ] I8-02 实现 ASCII `A` 和 binary `a` writer，并用独立 reader 对照头、
  数量、走时、幅相、出射/入射角和反射次数。
- [ ] I8-03 建立 eigenray receiver 命中/插值规则，再实现 `E` 模式和
  `.ray` 输出。
- [ ] I8-04 覆盖直达、多径、焦散、重复到达、arrival 上限和零到达案例。

出口：arrivals/eigenray 与 TL 模式共享已验收 tracer，但拥有独立的数据和
writer；任何容量截断都必须在 PRT 中可见。

### I9：二维兼容收口

- [ ] 发布机器可读的二维 feature matrix：`supported/rejected/deferred`；
- [ ] 运行所有旧例和新增例的 AppleClang/GCC CTest、gfortran 单频 oracle、
  中间状态和最终产品矩阵；
- [ ] 检查 F2CPP 不链接、不包含 RayReuse，且可在隔离副本独立构建；
- [ ] 更新 `USAGE.md` 的输入语法、附属文件、输出和限制；
- [ ] 记录新的派生清单和源码树哈希，供 RayReuse 分批同步；
- [ ] 对所有仍未支持的二维原版选项保留明确拒绝测试。

## 6. 每项功能的统一验收门

| 层级 | 必须通过的证据 |
|---|---|
| Parser | 正例、边界值、错误选项、缺失附属文件、非有限值和单位转换 |
| 数值组件 | 解析解或 gfortran 组件 oracle；记录最大误差和位置 |
| 中间状态 | `c/gradient/Hessian`、step、`p/q`、事件、复走时逐字段比较 |
| 端到端 | 结构、坐标轴、复压力/强度或 arrival 字段比较，不只比较 TL 图 |
| 回归 | 现有六例和所有已完成的新案例全部通过 |
| 工具链 | AppleClang 和 GCC warning-as-error；Debug sanitizer |
| 性能资源 | Release 分阶段时间、缓存字节数和峰值 RSS；无数量级退化 |
| 文档 | feature matrix、CLI、输入文件、限制和可复现命令同步更新 |

最终场沿用现有组合压力门和 `1e-3 dB` TL 门；中间状态沿用 D-07。若某新
分支需要不同容差，必须基于误差分布单独记录，不能全局放宽旧门。

## 7. 计划优先级

| 优先级 | 阶段 | 理由 |
|---|---|---|
| P0 | I0～I1 | 已由参考会话确定；PCHIP 是当前明确需求和 Hessian 覆盖缺口 |
| P1 | I2～I3 | 扩大常用二维环境，同时复用 I0 的通用接口 |
| P2 | I4～I6 | 补真实边界材料、范围相关环境和常见输入/输出 |
| P3 | I7～I8 | 算法分支最多，必须建立在环境、网格和 writer 稳定之后 |
| 收口 | I9 | 形成可声明的二维兼容范围和新派生基线 |

当前第一施工批只包含 I0 和 I1。I2 之后的顺序已经规划，但每个阶段开始前
应根据实际算例需求复核优先级；该复核只能调整尚未开始阶段的先后，不能绕过
依赖门。

## 8. 第一施工批的建议提交

```text
1. docs: freeze 2D replication scope and gfortran oracle policy
2. refactor: generalize SSP evaluator without C-linear drift
3. test: add PCHIP component oracle and standard case
4. feat: implement geometry-side PCHIP c/cz/czz
5. feat: implement frequency-side complex PCHIP projection
6. test: close PCHIP intermediate/final-field matrices
7. docs: record PCHIP results and RayReuse handoff manifest
```

在第 6 项通过前，不开始 RayReuse PCHIP，也不并入 Spline、非平坦边界或
其他 beam 模型。
