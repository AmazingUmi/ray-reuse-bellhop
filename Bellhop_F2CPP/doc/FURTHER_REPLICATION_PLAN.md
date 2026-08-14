# Bellhop F2CPP 二维功能进一步复刻计划

> 初始规划日期：2026-08-07
> 最近更新：2026-08-14
> 状态依据：本文件、`PROGRESS.md`、`tasks/`、测试和验证报告，以 Git 内容为准
> 当前基线：F2CPP 已完成并冻结 I0～I8；正在执行剩余二维复刻 B1
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

- [x] I0-01 定义 `SspInterpolationKind`，parser 显式区分 `C/P/N/S/Q`；
  未实现值解析后仍返回“已识别但不支持”，不能回退到 C-linear。
- [x] I0-02 建立只读二维 SSP evaluator 契约，统一返回
  `c/imaginaryC/gradient/Hessian/density/segment`；可用 variant、concept 或
  type-erasure，但热循环不得引入逐点堆分配。
- [x] I0-03 将 `RayStepper`、`GeometryTracer`、`FrequencyProjector` 和
  `CartesianCervenyInfluence` 从
  `CLinearSsp/CLinearFrequencySsp` 具体类型解耦。
- [x] I0-04 把“仅 C/N/Q/H 在跨段时应用一阶导数跳跃，P/S 不应用”编码为
  SSP 连续性属性，而不是函数名判断。
- [x] I0-05 扩展 SSP oracle，能导出指定深度和到达侧 segment 下的
  `c/cz/czz/rho`；保持诊断默认关闭且不改变 SHD。
- [x] I0-06 运行现有六例、三组中间状态和 AppleClang/GCC CTest；F2CPP
  C-linear SHD、probe 计数和错误语义必须保持不变。

出口：这是纯重构提交。若旧结果有数值漂移，不得进入 I1。

I0 于 2026-08-07 完成：Debug/Release AppleClang 与 GCC warnings-as-errors
CTest 均为 22/22，Python 基础测试 69/69，六个单频案例及三组中间状态矩阵
通过。Munk 节点 oracle 验证了相同 `c/rho` 与左右侧不同 `cz`；启用和关闭
SSP 诊断的 SHD SHA-256 相同。

### I1：PCHIP SSP（当前第一优先级）

目的：落实参考会话的下一阶段决定，并覆盖现有 C-linear 未真正使用的非零
`czz` 动态射线路径。

- [x] I1-01 以 `pchipMod.f90`、`sspMod.f90::cPCHIP` 和
  `Step.f90` 为权威，冻结端点导数、单调性限制、节点侧选择和外推规则。
- [x] I1-02 新增能明显区分 C-linear 与 PCHIP 的 `munk_pchip` 标准算例；
  保存 ENV、gfortran 可执行文件、oracle 输出和提交哈希。
- [x] I1-03 实现不可变 PCHIP 实系数预计算，查询返回 `c/cz/czz`；密度仍按
  原版逐段线性插值。
- [x] I1-04 实现逐频复声速 PCHIP 系数。原版先把各节点转换成复声速再求
  PCHIP 系数，不能插值原始 attenuation，也不能只给实系数附加虚部。
- [x] I1-05 ENV parser 接受 `P`，完整验证节点数量、严格递增深度、有限值、
  极短 segment 和单调/非单调剖面。
- [x] I1-06 增加节点、端点、峰谷、常值、两点/三点剖面、外推、复系数和
  非有限输入的单元/组件测试。
- [x] I1-07 对照 gfortran 的 `c/cz/czz/rho`、单步 `p/q`、完整射线和最终
  SHD；报告最大误差字段、射线、点和接收器位置。
- [x] I1-08 运行旧六例无回归、新 PCHIP 单频矩阵、AppleClang/GCC CTest、
  sanitizer 和 RSS/分阶段计时。

出口：形成一个独立的 F2CPP PCHIP 提交。完成前不修改 RayReuse；完成后才
按独立同步任务复制接口、实现和测试，并验证 nonreuse/reuse/parallel 一致。

I1 于 2026-08-07 完成 F2CPP 侧验收：AppleClang Debug sanitizer/Release
与 GCC 14 Release/Werror CTest 均为 23/23；Python 基础测试为 70/70；旧六例
及 `munk_pchip` 均通过。PCHIP 单射线 370 点/367 步逐字段对照的最坏误差为
`h=5.82e-11 m`，最终场最大复压力绝对误差 `2.34e-9`、最大 TL 差
`0.00439453125 dB`。本阶段未修改 RayReuse。

### I2：其余范围无关一维 SSP

按两个独立纵切实施，不能一次合并：

1. **I2-A N²-linear (`N`)（已完成）**
   - [x] 预计算复 `1/c²` 和斜率；
   - [x] 复现 `c`、`cz=-0.5*c³*n2z`、`czz=3*cz²/c`；
   - [x] 保留一阶导数跨段跳跃；
   - [x] 增加强梯度和节点穿越案例。
2. **I2-B Cubic Spline (`S`)（已完成）**
   - [x] 复现 `CSpline/SplineALL` 边界条件和复系数顺序；
   - [x] 返回连续 `c/cz/czz`，不得触发 C-linear jump；
   - [x] 增加端点曲率和焦散敏感案例。

每个纵切都必须重复 I1 的组件→单步→轨迹→最终场验收。PCHIP 通过不自动
代表 Spline 通过。

I2-A 于 2026-08-07 完成 F2CPP 侧验收：AppleClang Debug sanitizer/Release
与 GCC 14 Release/Werror CTest 均为 24/24；Python 基础测试为 71/71；八个
单频案例全部通过。N² 单射线与 gfortran 同为 295 点/294 步；节点梯度跳变
在后续焦散零交叉放大微小步长差异，因此只对 `munk-n2` 的动态 `q` 使用
`1e-12 + 3e-9*|q|` 局部门，最坏为第 181 点 `q2=4.49e-10`
（门限占用 89.5%），未放宽其他配置或字段。最终场最大复压力绝对误差
`2.45e-9`、最大相对误差 `4.11e-5`、最大 TL 差 `0.000366210938 dB`。
Release 的 1000 条射线包含 337075 个轨迹点、冻结缓存 62761192 bytes；
分阶段计时为 Trace 0.0281 s、Project 0.00319 s、Influence 2.296 s、Scale
0.00422 s、SHD 0.00141 s。本阶段未修改 RayReuse。

I2-B 于 2026-08-07 完成 F2CPP 侧验收：两端 not-a-knot 条件、两/三点
退化、复系数顺序以及 `SplineALL` 中 binary32 舍入的 `SIXTH` 常量均有
gfortran 组件锚点。AppleClang Debug sanitizer/Release 与 GCC 14
Release/Werror CTest 均为 26/26；Python 基础测试为 72/72；十个单频案例
全部通过。Spline 单射线与 gfortran 同为 285 点/284 步，最坏轨迹项为
第 152 点 `h=7.01e-10 m`（D-07 门限占用 5.86%），无需专属轨迹容差。
最终场最大复压力绝对误差 `2.02e-9`、最大相对误差 `3.96e-4`、最大 TL
差 `0.00282287598 dB`。Release 的 1000 条射线包含 337961 个轨迹点、
冻结缓存 63040152 bytes；分阶段计时为 Trace 0.0271 s、Project 0.00827 s、
Influence 2.182 s、Scale 0.00426 s、SHD 0.00138 s。本阶段未修改 RayReuse。

### I3：非平坦二维海面与海底

先线性、后曲线，先短格式、后长格式：

- [x] I3-01 建立 `BoundaryGeometry` 和活动 segment 查询接口，保留现有 flat
  specialization 的行为和性能。
- [x] I3-02 解析 `.ati/.bty` 的 piecewise-linear short format；范围 km 只在
  parser 边界转换为 m，并按原版向左右延拓。
- [x] I3-03 将两阶段 step limiter 扩展到边界交点和边界 segment endpoint；
  `ReflectionEvent` 保存真实 segment、切向、法向、曲率和前/后点索引。
- [x] I3-04 对照斜坡、多折线、segment 端点、掠射和多次反射轨迹。
- [x] I3-05 实现 curvilinear 边界及非零曲率动态跳跃。按原版语义，碰撞面
  仍是分段直线弦；curvilinear 作用于未归一化节点反射帧插值和曲率跳跃。
- [x] I3-06 支持 piecewise-linear `LL` long format 的沿程 fluid geoacoustic
  参数，并验证事件所记录材料与碰撞 segment 一致；curvilinear `CL` 和
  elastic 参数继续显式拒绝。

出口：flat 六例逐字节/数值无回归；新增坡底和起伏海面案例的交点、法向、
曲率、`p/q`、反射事件及最终场通过。

I3-01～I3-04 于 2026-08-09 完成首轮实现和回归：AppleClang Debug
sanitizer/Release 与 GCC 14 Release/Werror CTest 均为 26/26，Python 基础
测试为 72/72，十个 F2CPP 单频案例的 PRT/SHD 结构和有限值检查全部通过。
折线边界标准案例的全部 497 个发射角均与 gfortran 保持相同点数、积分
步数、反射事件数和终止类型，导出的逐点位置、slowness、`p/q`、声速、
走时、步长/权重与 predictor 中点在冻结门下全部零误差。收口修复显式复刻
了 gfortran 二维 `DOT_PRODUCT` 的运算顺序、对称 box 的 legacy 限步公式，
以及 `Reflect2D` 每分量的 fused subtract；没有加入角度特判或放宽容差。
共享发射扇修正为真实最大接收距离 1900 m 后，最终场最大复压力绝对误差
`1.23751187e-9`、最大相对误差 `4.31240835e-7`、最大 TL 差 `0 dB`。
完整汇总、输入与可执行文件哈希见
[`validation/i3_piecewise_oracle_report.json`](./validation/i3_piecewise_oracle_report.json)。

I3-05 于 2026-08-09 完成：canonical `C` 短格式、节点反射帧、实际生效的
`Dss=Dxx*t_r^3` 曲率、Top/Bottom 动态跳跃、非单位 legacy 镜像、缓存冻结
契约和连续两点位于边界外的终止门均已实现。非对称曲线边界标准案例的
459/459 条射线逐点通过，共 19056 点、14600 步、3997 次反射；最坏连续量
绝对误差为 `3.33e-16`。最终场最大复声压绝对误差 `8.33e-10`、最大相对
误差 `5.70e-7`、最大 TL 差 `7.63e-6 dB`。冻结报告见
`doc/validation/i3_curvilinear_fortran_oracle_report.json`。

I3-06 于 2026-08-09 完成：`.ati/.bty` 的 `LL` 七列节点记录现在都会解析
并校验；当前真空海面的 ATI 材料按边界条件忽略，声学海底的 BTY 保留原始
流体材料。活动 segment 使用左节点材料，左右水平延拓复制首末节点材料。
声学反射事件冻结所选 raw material 与 Origin 的 `1e20 m` 衰减求值深度，
RayReuse 逐频投影不重新定位 segment，也不写回轨迹。组件门覆盖 segment→
node 映射、列数/elastic 拒绝、ENV 回退优先级、biological 深度语义和双频
投影；1000 Hz 标准场通过 Origin/F2CPP 对照，最大复压力绝对误差
`4.66e-11`、最大相对误差 `4.57e-7`、最大 TL 差 `7.63e-6 dB`。冻结报告见
`doc/validation/i3_long_format_materials_report.json`。

### I4：边界声学与衰减扩展

依次实施，逐项保持频率状态不写回轨迹：

- [x] I4-01 打通 `N/F/M/W/Q/L` 衰减单位的 parser 和端到端用例；已有转换
  纯函数不等于输入功能已支持。
- [x] I4-02 增加 Francois–Garrison 和 biological volume attenuation，参数
  进入不可变环境，逐频转换进入 projector。
- [x] I4-03 将普通 ENV 声学海床由无剪切流体扩展到含 shear 的弹性半空间；
  先用反射系数组件 oracle，再进入轨迹投影。
- [x] I4-04 支持 grain-size (`G`) 地声模型。
- [x] I4-05 评估 tabulated reflection coefficient (`F/P/W`) 路径，并完成
  当前 Origin 2D 中可观察的 bottom `F + .brc` 纵切；`P/W` 因缺少完整消费/
  写出链而明确延期，不伪装为 `F`。

出口：每一种材料/衰减选项都有独立小案例和角度×频率反射系数矩阵，不以
单个最终场覆盖全部分支。

I4-01 于 2026-08-09 完成：ENV `TopOpt` 第三字符的六种大写单位现在原样
进入不可变 SSP 节点和声学半空间材料；逐频转换仍只发生在 projector，未
写回冻结 `RayPathCache`。组件矩阵在 1/2 kHz 锚定 `N/M` 的频率无关行为和
`F/W/Q/L` 的线性频率行为。六个独立 5 kHz 标准案例均通过 Origin/F2CPP
最终场门，最大复压力绝对误差 `8.41e-9`、最大相对误差 `2.06e-6`、最大
TL 差 `1.91e-5 dB`；等效输入在各实现内部产生逐字节相同的六份场。
冻结报告见 `doc/validation/i4_attenuation_units_report.json`。小写幂律 `m`
没有提前开放。

I4-02 于 2026-08-09 完成：`TopOpt` 第四字符 `F/B` 的附加记录进入唯一的
环境级不可变体积衰减配置；Biological 层表使用共享只读存储，环境在投影器
与声场累加器之间复制时不会重复深拷贝。C/N/P/S 四种复 SSP 均在节点深度
先换算衰减再插值，逐频结果不写回冻结轨迹。Francois–Garrison 的 REAL(4)
常量、FMA 多项式和 `pow` 调用路径，以及 Biological 的闭区间、重叠层逐层
换算再累加顺序，均由独立 gfortran 锚固定。两个新案例共六个单频/多频
Origin–F2CPP 场切片全部通过，最坏复压力绝对误差 `1.01e-7`、最大相对误差
`9.98e-5`、最大 TL 差 `5.57e-4 dB`；两种实现的两种模型均显著区别于同一
无损控制场。冻结报告见 `doc/validation/i4_volume_attenuation_report.json`。

I4-03 于 2026-08-09 完成普通 ENV `A` 型弹性海床纵切：parser 保留 P/S
波速、两套 raw attenuation 和密度，projector 在每个频率分别执行 CRCI 后
按 Origin `ReflectMod` 的复根与运算顺序计算弹性反射系数。四个 gfortran
组件锚覆盖无损/有损、法向、斜入射和临界区邻域；冻结轨迹双频投影不写回
几何。弹性场及同几何 fluid control 共六个 Origin/F2CPP 场切片全部通过，
最大复压力绝对误差 `1.69e-8`、最大 TL 差 `2.59e-4 dB`，两端的 shear
非空操作门均通过。冻结报告见
`doc/validation/i4_elastic_halfspace_report.json`。顶部 `A` 半空间和弹性
`LL/CL` 仍明确延期，不属于本纵切。

I4-04 于 2026-08-11 完成 bottom `G` grain-size 纵切：不可变环境保存 `Mz`
及 `vr/rhor/alpha2_f`，逐频 projector 在冻结反射事件处结合当地水声速生成
流体半空间；沉积物强制使用 Origin 的 `L` loss parameter，不继承 ENV 的
体积衰减。组件门逐位锚定 default-REAL 常量、全部分段边界与 0/30/60 度
反射系数，双频投影不写回轨迹。原 2D Fortran 漏写粒径派生参数，会产生
NaN；oracle 仅补入与同仓库 3D 路径一致的三项初始化，并在
`Bellhop_origin/ORACLE_DIAGNOSTICS.md` 明示。grain-size 场与等价普通流体
control 共六个 Origin/F2CPP 场切片全部通过，最大复压力绝对误差
`1.68e-8`、最大相对误差 `2.56e-6`、最大 TL 差 `2.29e-5 dB`；两端 `G`
与 control 的压力均逐位一致。冻结报告见
`doc/validation/i4_grain_size_report.json`。顶部 `G` 与 `G+LL` 继续延期。

I4-05 于 2026-08-11 完成 bottom `F` tabulated-reflection 纵切：parser 从
同根 `.brc` 读取严格递增的 `grazing_angle_deg magnitude phase_deg`，将表
保存为环境级共享只读数据；projector 以冻结事件的 `Tg/Th` 复刻 Origin
角度折叠，分别线性插值幅值和已展开相位。显式相位不会从复系数反推，因此
零幅值节点仍保留相位累加；该分支也不套用 `A/G` 的 `1e-5` 系数抑制。
域判断与二分区间选择显式使用 Origin 的 default-REAL 舍入角，而插值权重
继续使用原 binary64 角度，端点半个 REAL(4) ULP 邻域由独立 gfortran 锚
固定。
组件门覆盖节点/中点/域外、左右行进折叠、零幅值相位、tiny coefficient、
short bathymetry 组合、`F+LL` 拒绝和双频缓存只读。tabulated 场与 rigid
control 共六个 Origin/F2CPP 场切片全部通过，最大复压力绝对误差
`1.68e-8`、最大相对误差 `6.35e-6`、最大 TL 差 `3.82e-5 dB`，两端的
table-effect 非空操作门均通过。冻结报告见
`doc/validation/i4_tabulated_reflection_report.json`。顶部 `F/.trc` 延期；
源码审计确认当前 2D `P` 只读未消费的 `.irc`，`W` 只打印提示却不写表，
两者首次实际反射均无完整实现，故不列为已支持能力。

### I5：`Q` 型范围相关二维 SSP

这是二维功能，但复杂度显著高于一维插值，必须在通用 SSP 和活动边界段稳定
后实施。

- [x] I5-01 解析 `.ssp` 的二维 quadrilateral 网格，验证范围/深度单调性和
  维度溢出。
- [x] I5-02 实现 range/depth segment 定位及 `c/cr/cz/crr/crz/czz`。
- [x] I5-03 step limiter 同时对齐 range cell、depth cell 和物理边界事件。
- [x] I5-04 为动态射线补齐非零 `crz`、零 pure Hessian、二维 Snell、
  range-gradient jump、双维跨界优先级和有限差分诊断。
- [x] I5-05 对照 gfortran 全轨迹、焦散和最终场；确认实 SSP 不随频率变化时
  仍满足 RayReuse 的几何可复用条件。

`H` 型 hexahedral SSP 不随 I5 纳入。

截至 2026-08-11，I5-01～I5-05 已通过 `.ssp` 解析/资源门、双线性采样、
逐频参考 SSP 损耗、range/depth 双索引、range-cell 限步、梯度跳跃和
ASan/UBSan 组件门。代表射线 715 点/714 步逐点 gfortran oracle、两个 Q
案例共六个最终场切片、范围相关 effect guard 及双频冻结缓存门均已通过；
报告见 `doc/validation/i5_q_geometry_oracle_report.json` 与
`doc/validation/i5_quadrilateral_ssp_report.json`。

### I6：source、receiver 与射线产品

- [x] I6-01 支持多个 source depth，按 source 独立生成/冻结轨迹缓存，并按
  SHD source record 顺序写出。
- [x] I6-02 支持 irregular receiver grid，显式区分笛卡尔积网格与
  irregular 坐标轴/单压力行布局，并冻结 CC 分支的 Origin legacy 深度语义。
- [x] I6-03 支持 source beam-pattern (`.sbp`) 读取与幅度插值。
- [x] I6-04 实现 `R` ray-trace 模式和二维 `.ray` writer；它直接使用冻结的
  `RayPathCache`，不另写第二套 tracer。
- [x] I6-05 扩展 CLI/PRT/SHD/RAY 维度验证、大维度乘法溢出检查与原子发布。

出口：多源 SHD、irregular SHD 和 `.ray` 均能被独立 reader 解析；单源规则
网格结果不变。

I6-01 的最小安全纵切已经锁定为：输入 source depth 按 Origin 顺序整理后
保存在有序 source 列表；所有 source 共用同一 D-02 launch fan，但按 source
重新计算起点声速、epsilon、束窗与压力缩放。求解器逐 source 建立、冻结、
消费并释放一个 `RayPathCache`，每个 source 产生独立 workspace，禁止把不同
source 的压力相干相加；这样峰值缓存取各 source 最大值而不是总和。SHD 头
写真实 `NSz`，source-depth 向量和场记录按 source-major、receiver-depth 次序
写出。多源 fan 使用 Origin 的固定 1500 m/s 参考声速自动规划；各 source
的真实局部声速只进入 epsilon、束窗与压力缩放。源深仍采用项目的严格水体内部安全子集，
不复刻 Origin 把越界源钳到边界后生成零场的宽松行为。

I6-01 于 2026-08-11 完成：parser 接受、REAL4 化并稳定排序多个 source
depth，求解器以共享 launch fan 逐源建立/释放缓存并产生独立 workspace，
SHD writer 写真实 `NSz`、深度向量和 source-major 场记录。乱序三源标准例
在 1/2 kHz 的最大复压力绝对误差为 `2.24e-8`、最大 TL 差为
`3.82e-5 dB`，冻结报告为
`doc/validation/i6_multi_source_report.json`。随后进入 I6-02。

I6-02 于 2026-08-11 完成：run-type 第 5 字符 `I` 进入独立 receiver layout，
workspace 每个 range 仅保存一个压力值，SHD 头保留等数量 depth/range 轴并
写 `irregular` plot type。真实 Origin/F2 场闭环同时确认 2D
`InfluenceCervenyCart` 实际对所有 range 使用 `Rz(1)`，而非注释所称的
`Rz(ir)` 配对；F2CPP 冻结该 legacy 行为，修正后的真正配对 CC 场不在同一
兼容选项下偷换。1/2 kHz 最大复压力绝对误差 `3.80e-9`，报告为
`doc/validation/i6_irregular_receivers_report.json`。

I6-03 于 2026-08-11 完成：run-type 第 3 字符 `*` 读取同根 `.sbp`，节点 dB
按 `10^(dB/20)` 转为线性压力幅度后，以 Origin 的严格左段选择和首末段外推
顺序逐发射角求值。方向图只进入逐频 projector，不修改 source、轨迹或冻结
缓存；1/2 kHz 最终场最大复压力绝对误差 `1.68e-8`、最大 TL 差
`2.29e-5 dB`，报告为
`doc/validation/i6_source_beam_pattern_report.json`。

I6-04 于 2026-08-11 完成：parser 接受安全范围内的 `R/RG/RGO`，限全向源、
真空海面/刚性海底、无 beam shift 与无损耗前缀；显式 `Nalpha` 原样使用，
`0` 固定自动规划为 50。R 模式不读取 coherent-TL 的 `MS`/image 行，逐 source
直接消费冻结 `RayPathCache`，输出 PRT/RAY 而不生成 SHD。标准案例的 2 source
× 5 angles 共 10 条射线、5934 个点，top/bottom bounce 各 19 次；相对 Origin
坐标最大绝对误差为 `0 m`，source/angle 顺序与语义哈希一致，报告为
`doc/validation/i6_ray_trace_report.json`。收口时 AppleClang Debug sanitizer
和 Release CTest 当时均为 27/27，Python 标准工具 82/82，单频端到端案例
33/33；其中新增 ray-trace 案例不是 SHD 场案例。

I6-05 于 2026-08-11 完成：新增无分配的 `Shd2DLayout` 规划器，在打开输出
前检查 record words/bytes、source-major pressure record、最终 Origin int32
record 号和总文件偏移；CC 压力 workspace 与两种模式总射线数均在追迹前
预检。SHD/RAY 改为临时文件完整关闭后发布，失败保留旧产品；CLI 同根
CC→R→CC 会移除异类产品和陈旧临时文件，标准案例的单频/宽带运行也不会
复用旧 manifest 或输出。最终 AppleClang Debug/Release 与 GCC14 Werror
均为 28/28 CTest，Python 标准工具 86/86，单频端到端案例保持 33/33；
冻结摘要为 `doc/validation/i6_output_safety_report.json`。随后 I7-01～I7-06
与 I8-01～I8-04 也已完成并冻结。

### I7：场分量、相干类型与 beam family

按“复用现有 CC 最多”到“新增 Influence 最多”的顺序实施：

1. [x] Cartesian Cerveny 的 `P/V/H` 输入、PRT 回显与 Origin legacy no-op
   兼容门；真正的 `V/H` 变换只属于后续 ray-centered Cerveny；
2. [x] `F/M/W` beam width 与 `D/S/Z` curvature condition；
3. [x] point/line source 缩放；
4. [x] incoherent (`I`) 与 semi-coherent (`S`) 累加和缩放；
5. [x] ray-centered Cerveny (`R`)；
6. [x] I7-06：simple Gaussian、geometric Gaussian、geometric hat 等其余
   二维 beam。

每个新 Influence 实现必须有单射线贡献 oracle、KMAH/branch-cut 专项测试和
最终场案例。不同相干类型不得复用同一压力容器而隐含改变数值含义，应在类型
或 workspace 层区分 complex pressure 与 intensity accumulation。

I7-01 于 2026-08-11 完成。模型与 ENV parser 现保存大写 `P/V/H`，PRT 明确
回显 component；未知值和小写值均拒绝。源码审计与真实 Origin 运行共同确认：
`InfluenceCervenyCart` 不读取 component，而 `V/H` 公式只存在于
`InfluenceCervenyRayCen`，所以 Cartesian P/V/H 的 SHD 在 Origin 与 F2CPP
各自均逐字节相同且非零。三个组件的 Origin↔F2CPP 最大复压力绝对误差为
`1.494685086811387e-08`、最大相对误差为 `2.8521849344542716e-06`、最大 TL
差为 `7.62939453125e-06 dB`；冻结报告为
`doc/validation/i7_cartesian_components_report.json`。最终 AppleClang
Debug/Release 与 GCC14 Werror 均为 28/28 CTest，Python 标准工具 90/90，
单频标准案例 36/36。该阶段随后进入 I7-02；不得把 ray-centered 的 `V/H` 公式提前
移植到 Cartesian influence。

I7-02 于 2026-08-11 完成。ENV parser 严格接收 `{F,M,W}×{D,S,Z}` 九种
两字符组合；F/M 使用纯正虚 epsilon，W 使用逐发射角实 epsilon，并在
Influence 的逐点和接收距离插值两处按 real(q) 过零更新 KMAH。D/S/Z 已从
配置贯通到 GeometryTracer，分别加倍、保留或清零完整反射动态跳变量，中心
轨迹与镜面反射慢度不变。FS/MS/WS 的首射线 HalfWidth/epsilon 与 Origin
精确一致；五例矩阵跨实现最大复压力绝对误差 `1.31708899342442e-09`、最大
相对误差 `1.6340760566890822e-06`、最大 TL 差
`1.1444091796875e-05 dB`，且 12 个宽度/曲率效果门全部非空。冻结报告为
`doc/validation/i7_beam_options_report.json`；最终 Debug/Release/GCC14
Werror CTest 28/28、Python 95/95、单频案例 40/40。该阶段随后进入 I7-03。

I7-03 于 2026-08-11 完成。run-type 第 4 字符现以 case 级
`SourceGeometry` 保存：空白与 `R` 为 point，`X` 为 line。两者共用同一
频率无关冻结射线；仅 Cartesian Influence 的 Ratio1 和最终 ScalePressure
分支不同。默认 point 与显式 point 在 Origin/F2CPP 内部分别逐位一致；
point/line 最大复压力差均为 `0.2273859679698944`，三组跨实现最大复压力
绝对误差 `1.6408202441198227e-7`、最大 TL 差
`9.5367431640625e-6 dB`。冻结报告为
`doc/validation/i7_source_geometry_report.json`；最终 Debug/Release/GCC14
Werror CTest 28/28、Python 100/100、单频案例 42/42。该阶段随后进入 I7-04，
并已由下一段关闭。

I7-04 于 2026-08-11 完成。`CC`、`IC`、`SC` 三例只改变 run type 首字符，
共享同一组 300 条冻结射线几何。C 继续逐 beam 累加复压力；I 对每条 beam
的图像源合成贡献取模平方后累加强度；S 先在逐频 source 投影中施加 Lloyd
mirror 幅度，再沿用 I 的强度路径。I/S 在最终缩放前取累积强度平方根，随后
继续按 source geometry 执行 point/line 扩散缩放。三种模式写相同 SHD
布局；I/S 的复数槽虚部严格为零。

三组 Origin/F2CPP 最大复压力绝对误差为 `9.33139787662185e-10`、最大相对
误差 `8.103583013507887e-7`、最大 TL 差 `1.52587890625e-5 dB`。C/I 的
最大压力差约 `0.0022908477`、TL 差中位数约 `23.8819 dB`；I/S Lloyd
effect 的最大压力差为 `1.7811398720368743e-6`，TL 差中位数为 Origin
`0.0492249 dB`、F2CPP `0.0492172 dB`。冻结报告为
`doc/validation/i7_coherence_modes_report.json`；最终 Debug/Release/GCC14
Werror CTest 28/28、Python 106/106、单频案例 44/44（43 SHD + 1 RAY）。
该阶段随后进入 I7-05，并已由下一段关闭。

I7-05 于 2026-08-11 完成。run type 第 2 字符 `R` 选择 ray-centered
Cerveny，并与 Cartesian 共享同一冻结射线几何；Influence 按局部射线法向、
插值动态 `p/q`、KMAH 与图像源累加计算压力，再按 Origin 公式得到 `P/V/H`
分量。CC/P control 与 CR/P、CR/V、CR/H 三例均为非零场；四组跨实现最大
复压力绝对误差 `4.485843874135753e-08`、最大相对误差
`8.186953891708981e-06`、最大 TL 差 `6.103515625e-05 dB`，四个 family/
component 独立效果门均非空。ENV/PRT family/component、可执行文件路径、
mtime/hash 与 Origin 公式来源均已绑定，冻结报告为
`doc/validation/i7_ray_centered_components_report.json`。首个纵切只接受规则
接收网格并明确拒绝 ray-centered irregular receiver grid；最终
Debug/Release/GCC14 Werror CTest 29/29、Python 112/112、单频案例 47/47
（46 SHD + 1 RAY）。随后进入 I7-06，并已由下一段关闭。

I7-06 于 2026-08-14 完成。ENV parser 与求解器现支持 Cartesian geometric
hat (`G`)、geometric Gaussian (`B`) 和 simple Gaussian (`S`)，并保留既有
ray-centered geometric hat (`g`)；Origin 未实现的 ray-centered geometric
Gaussian 继续在 parser 边界明确拒绝。组件测试冻结了接收网格求交、REAL4
舍入顺序、振幅/相位/时延、KMAH/branch 行为和非法 family 组合。1 kHz 的
G/B/S 三例使用相同 300 条发射射线与场布局，Origin/F2CPP 最大复压力绝对
误差 `1.30385160446167e-08`、最大相对误差
`2.107042291754624e-06`、最大 TL 差 `2.288818359375e-05 dB`；两实现内部
三组两两 family effect 均通过非空门。冻结报告为
`doc/validation/i7_gaussian_beams_report.json`。最终 AppleClang Debug
ASan/UBSan、AppleClang Release 与 GCC14 Werror CTest 均为 32/32，Python
标准工具 123/123，单频案例 52/52（51 SHD + 1 RAY）。I8 架构审查与任务
拆分随后完成，并已由下一段的 I8 完成记录关闭。

### I8：arrivals 与 eigenray

- [x] I8-01 Arrival data model & accumulation：`ArrivalCandidate`/`Arrival`
  精度边界、每 source/频率独立 workspace、Origin 容量公式、last-only
  duplicate merge、最弱到达替换，以及 G/g/B contribution sink 与流式 solver。
- [x] I8-02 Arrival writers：checked layout、ASCII `A`、GNU Fortran sequential
  unformatted binary `a`、多 source 写出、PRT 统计和 SHD/RAY/ARR 原子生命周期。
- [x] I8-03 Eigenray mode：复用 G/g/B receiver contribution 命中，保留每个
  命中的 ray prefix，按 EOF 解释变长 `.ray` block，不和 arrival 去重/容量混用。
- [x] I8-04 Validation & documentation：独立 ARR/E reader、直接 `ArrMod`
  组件 oracle、直达/多径/焦散/重复/容量/零到达矩阵、Origin↔F2CPP 报告和
  全回归收口。

I8 的冻结架构、支持/拒绝矩阵、阶段依赖和 18 个 OpenCode 原子任务见
[`tasks/I8_arrivals_eigenray/README.md`](./tasks/I8_arrivals_eigenray/README.md)。
任务文档保留了逐项设计与验收记录。I8-01～I8-04 的全部任务均为 ACCEPTED；
真实 `ArrMod` probe 的 15 个场景、六例 ARR 与四例 E 的 Origin/F2CPP parity、
产品生命周期和全回归均已形成冻结报告。最终 Debug/Release/GCC14 Werror
CTest 为 37/37，Python 为 142/142，F2CPP 单频案例为 62/62。I8 当前冻结并
暂停在 I9 之前；未启动 RayReuse 同步。

出口：arrivals/eigenray 与 TL 模式共享已验收 tracer，但拥有独立的数据和
writer；任何容量截断都必须在 PRT 中可见。

### I9：剩余二维复刻与兼容收口

I0～I8 保持冻结，不重新拆分或修改其数值语义。剩余工作按以下四个 batch
实施；每个 batch 只增加当前功能无法由既有案例验证时所必需的最小案例，
标准算例总增量控制在 2～3 个。

1. **B1 — Boundary symmetry（ACCEPTED）**
   - 支持 top `R/A/G/F`、top `F + .trc` 和 bottom `V`；
   - 统一 `BoundaryModel` 的 upper/lower 方向约束，复用已有边界声学、
     `ReflectionEvent` 和逐频投影；
   - 运行相关 parser/boundary/projector 测试、一个 top-boundary Origin 对照、
     `f2cpp-regression` 和 `f2cpp-full`。
2. **B2 — General R products（ACCEPTED）**
   - 在 B1 接口稳定后支持方向性 source pattern、已支持有损边界、逐频活动
     prefix 和显式单 ray；
   - 不加入 beam shift，不改变冻结轨迹的频率无关契约。
3. **B3 — Elastic LL materials（ACCEPTED）**
   - 在 B1 接口稳定后支持 top/bottom acoustic `LL` 的沿程 elastic P/S 材料；
   - B2 与 B3 并行实现，batch 完成后统一 review 和集成。
4. **B4 — Replication closure（TODO）**
   - 发布 `supported/rejected/deferred` feature matrix；
   - 更新 `PROGRESS.md`、`USAGE.md` 和派生清单；
   - 检查 F2CPP 与 RayReuse 的构建独立性，并执行最终 AppleClang/GCC、
     `f2cpp-full` 和 release/checkpoint 验收。

`P/W` reflection coefficient、`CS/CL` legacy 混合格式、`G/F + LL`、
ray-centered irregular receiver、3D/N×2D、beam shift、analytic SSP 和
F2CPP 多频调度继续明确延期。它们不因本轮收口而获得静默降级路径。

B1 于 2026-08-14 完成：parser 和 `BoundaryModel` 允许 top `R/A/G/F` 与
bottom `V`，top `F` 解析同根 `.trc`，上下边界共用既有反射事件和逐频声学
投影。新增的单个 top-F/bottom-V 算例相对 Origin 最大复压力绝对/相对误差
分别为 `1.73985413e-8`/`1.40428056e-5`，最大 TL 差
`1.14440918e-4 dB`；`f2cpp-regression`、145 项 Python 测试、37 项 CTest
及 63 个 F2CPP 单频案例全部通过。B1 接口已稳定，B2/B3 可以并行启动。

B2 与 B3 于 2026-08-14 并行完成。B2 的 R writer 使用临时逐频投影决定
Origin-compatible terminal prefix，支持方向性 `.sbp`、已验收的非
beam-shift 边界和显式单 ray，且不改写 `RayPathCache`；新案例与 Origin 的
1 条射线、1107 点、3/3 次上下反射及全部坐标精确一致。B3 允许 top/bottom
acoustic `LL` 保存 elastic P/S 材料，继续在几何事件处冻结单个 segment
material，并按 `1e20 m` 深度逐频换算；新案例最大复压力绝对/相对误差为
`4.38070691e-11`/`4.26289063e-7`，最大 TL 差 `1.52587891e-5 dB`。
最终 regression 为 CTest 37/37、案例 14/14；full 为 Python 145/145、
CTest 37/37、F2CPP 单频 65/65。B4 的前置条件已经满足，但尚未启动。

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
| P4 | I9-B1 | 补齐上下边界类型对称性 |
| P4 | I9-B2～B3 | B1 后并行补普通 R 产品与 elastic LL |
| 收口 | I9-B4 | 形成可声明的二维兼容范围和新派生基线 |

I0～I2、I3-01～I3-06、I4-01～I4-05、I5-01～I5-05、I6-01～I6-05 和
I7-01～I7-06、I8-01～I8-04 及 I9-B1～B3 已完成并冻结，当前暂停在 B4 前。
每个阶段开始前仍应根据实际
算例需求复核优先级；该复核只能调整尚未开始阶段的先后，不能绕过依赖门。

## 8. 已完成施工批的建议提交拆分

```text
1. docs: freeze 2D replication scope and gfortran oracle policy
2. refactor: generalize SSP evaluator without C-linear drift
3. test: add PCHIP component oracle and standard case
4. feat: implement geometry-side PCHIP c/cz/czz
5. feat: implement frequency-side complex PCHIP projection
6. test: close PCHIP intermediate/final-field matrices
7. docs: record PCHIP results and RayReuse handoff manifest
8. feat: implement N2-linear SSP and close its oracle matrix
9. feat: implement not-a-knot cubic spline SSP and close its oracle matrix
10. docs: record I2 results and advance the main route to I3
```

I0～I7-06 当前施工期间未修改 RayReuse。I8 也明确禁止修改 RayReuse；后续
同步仍是独立任务。F2CPP 主路线现处于 I8 设计完成、实现未开始状态，不顺带
并入 3D、N×2D 或 beam shift。
