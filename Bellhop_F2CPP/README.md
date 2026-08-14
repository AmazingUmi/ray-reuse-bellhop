# Bellhop F2CPP

`Bellhop_F2CPP` 是可独立配置、构建和运行的 C++20 单频二维 Bellhop
实现。它支持目标范围内的 Cartesian/ray-centered Cerveny coherent、
incoherent 与 semi-coherent 二维声场和二维几何射线输出，从 Bellhop
`.env` 读取环境；场模式输出 PRT 与单频 SHD，ray-trace 模式输出 PRT 与 RAY。

核心数据流：

```text
.env
  → SimulationCase
  → GeometryTracer
  → frozen RayPathCache
  ├─ C/I/S TL  → FrequencyProjector → Cartesian/RayCenteredCervenyInfluence
  │            → pressure/intensity workspace → .prt + .shd
  └─ ray trace    → RayWriter → .prt + .ray
```

程序保留完整、频率无关、冻结只读的射线轨迹缓存；复走时、衰减、反射
幅相和压力只存在于逐频临时状态。F2CPP 一次运行只计算一个频率，实际多频
调度和轨迹复用由后续独立的 `Bellhop_RayReuse` 工程实现。

## 快速开始

从本目录编译并测试 Release：

```bash
cmake --preset release
cmake --build --preset release --parallel
ctest --preset release
```

可执行文件：

```text
build/release/bellhop_f2cpp
```

运行：

```bash
./build/release/bellhop_f2cpp <file-root>
```

参数必须省略 `.env` 扩展名。程序读取 `<file-root>.env`，并在相同位置
写出 `<file-root>.prt`；C/I/S TL 模式另写 `<file-root>.shd`，ray-trace
模式另写 `<file-root>.ray`。

## 文档

- [完整使用说明](./doc/USAGE.md)：环境、编译、测试、CLI、输入输出和排错；
- [当前进度](./doc/PROGRESS.md)：已完成范围、最新验证基线和当前施工入口；
- [F2CPP 文档索引](./doc/README.md)；
- [构建与验收计划](./doc/BUILD_PLAN.md)；
- [二维功能进一步复刻计划](./doc/FURTHER_REPLICATION_PLAN.md)；
- [最终派生清单](./doc/DERIVATION_MANIFEST.md)。
- [数值接口与中间状态契约 v1](./doc/INTERMEDIATE_STATE_CONTRACT.md)。

全项目设计与数值契约：

- [Bellhop 源码分析与宽带复用设计](../doc/01-Bellhop源码分析与宽带复用设计.md)；
- [项目实施待办](../doc/02-项目实施待办.md)；
- [基础变量、单位与数值规范](../doc/04-基础变量单位与数值规范.md)。

## 当前状态

G0、M1、M2、I0～I5、I6-01～I6-05 及 I7-01～I7-05 已完成；当前进入
I7-06 其余二维 beam：

- Debug ASan/UBSan、Release 与 GCC 14 Release/Werror 当前各 29/29 CTest；
- 标准算例 Python 测试当前 112/112；
- 原有六个单频案例无回归，新增 `munk_pchip`、`munk_n2` 和
  `munk_spline` 均通过；I3 新增的 `i3_piecewise_boundaries` 已通过
  CLI 的 PRT/SHD 检查、497 发射角 gfortran 逐点轨迹矩阵和最终场数值门；
  `i3_curvilinear_oracle` 的 459 条射线和最终场也已闭环；
- I4-01 的 `N/F/M/W/Q/L` 六种衰减输入单位已通过 parser、双频 projector
  矩阵和六个独立 Origin/F2CPP 最终场案例；I4-02 的 Francois–Garrison
  与 biological 体积衰减也已完成参数解析、逐频投影和最终场闭环；I4-03
  已支持普通 ENV `A` 型弹性海床的 P/S 波逐频反射；I4-04 已支持 bottom
  `G` grain-size 流体地声模型及逐事件当地水声速展开；I4-05 已支持 bottom
  `F` 与 `.brc` 的幅相反射表；I5 已接入 `Q + .ssp` 范围相关二维 SSP 的
  双线性几何、逐频损耗与跨 range/depth cell 限步；I6-01 已支持多个
  source depth 的逐源缓存生命周期、独立场切片和 SHD source-major 写出，
  并已接入 irregular receiver 的输入与 SHD 布局；I6-03 已支持共享只读
  `.sbp` source beam-pattern、线性幅度插值和逐射线投影；I6-04 已支持安全
  子集内的 `R/RG/RGO` 二维 ray-trace、双声源 source-major RAY 写出及反射
  前后重复点；当前单频标准案例共 47 个（46 个 SHD、1 个 RAY）
  （含弹性、grain-size、tabulated reflection、Q SSP、multi-source、
  irregular receiver、source beam-pattern、ray trace 及各自 control）；其中
  ray-trace 案例验证 PRT/RAY，不是 SHD 声场案例；其 10 条射线与 Origin
  坐标最大误差为 0、语义哈希一致，详见
  [`i6_ray_trace_report.json`](./doc/validation/i6_ray_trace_report.json)；
- I6-05 已加入 SHD 二维布局的集中式溢出预检、CC 压力工作区与总射线资源门、
  SHD/RAY 临时文件原子发布、同根 CC/R 模式切换清理及标准案例陈旧结果防护；
  失败运行保留旧的有效产品并清除临时文件，详见
  [`i6_output_safety_report.json`](./doc/validation/i6_output_safety_report.json)；
- I7-01 已接受并保存 Cartesian Cerveny 的 `P/V/H` component，PRT 明确
  回显选择。源码审计与真实 Origin 对照确认 Cartesian 路径当前忽略该选择，
  因此 F2CPP 有意冻结 P/V/H 三场逐位相同的 legacy 行为；真正 V/H 公式由
  I7-05 ray-centered Cerveny 单独实现。证据见
  [`i7_cartesian_components_report.json`](./doc/validation/i7_cartesian_components_report.json)；
- I7-02 已贯通 Cartesian Cerveny 的 `F/M/W` beam width 与 `D/S/Z`
  reflection-curvature condition。WKB epsilon 按发射角逐射线计算并采用
  独立的实数 q 过零 KMAH 规则；D/Z 对完整反射动态跳变量加倍/清零。五例
  最小矩阵的最大 TL 差为 `1.1444091796875e-05 dB`，证据见
  [`i7_beam_options_report.json`](./doc/validation/i7_beam_options_report.json)；
- I7-03 已支持 run-type 第 4 字符的 point `R` 与 line `X` source geometry。
  默认空白与显式 `R` 的场逐位相同；`X` 仅改变 Influence 入射角权重与最终
  压力扩散缩放，不改变冻结射线几何。证据见
  [`i7_source_geometry_report.json`](./doc/validation/i7_source_geometry_report.json)；
- I7-04 已支持 coherent `C`、incoherent `I` 与 semi-coherent `S`。三种
  模式共享同一冻结几何；I 对每条 beam 的贡献取强度后累加，S 先在逐频投影
  中施加 Lloyd mirror，再走同一强度路径。I/S 最终取强度平方根并执行
  point/line 扩散缩放，写入与 C 相同布局的 SHD；其复数槽虚部严格为零。
  证据见
  [`i7_coherence_modes_report.json`](./doc/validation/i7_coherence_modes_report.json)；
- I7-05 已支持 ray-centered Cerveny `CR/IR/SR`，并在规则接收网格上消费
  与 Cartesian 相同的冻结射线几何。`P/V/H` 分别按 Origin 的 ray-centered
  压力、切向投影和法向投影公式计算；CC/P、CR/P、CR/V、CR/H 四例的跨实现
  最大复压力绝对误差为 `4.485843874135753e-08`、最大相对误差为
  `8.186953891708981e-06`、最大 TL 差为 `6.103515625e-05 dB`。首个纵切
  明确拒绝 ray-centered irregular receiver grid；证据见
  [`i7_ray_centered_components_report.json`](./doc/validation/i7_ray_centered_components_report.json)；
- 已支持二维 `.ati/.bty` 的 piecewise-linear `LS` 与 canonical
  curvilinear `C` short format，以及 piecewise-linear `LL` long format
  的逐段流体材料；已实现水平端点延拓、活动 segment 步长限制、未归一化
  插值反射帧、非零曲率动态跳跃和原始材料随反射事件冻结；
- R-15 的 16 频 trace-once 摊销性能门 6/6；
- 最终快照已允许派生 `Bellhop_RayReuse`。

支持范围和已知限制以[完整使用说明](./doc/USAGE.md)为准。后续二维功能按
[进一步复刻计划](./doc/FURTHER_REPLICATION_PLAN.md)推进；3D、N×2D 和
beam shift 不在该路线内。
