# Bellhop F2CPP 当前进度

> 更新日期：2026-08-16
> 当前主路线：二维单频复刻；3D、N×2D 和 beam shift 默认排除。
> 当前施工状态：I0～I8 与 I9-B1～B4 已完成并冻结；二维单频复刻正式封板。

## 1. 已完成范围

| 阶段 | 状态 | 已冻结能力 |
|---|---|---|
| G0、M1、M2 | 完成 | C++20/CMake 工程、二维中心/动态射线、冻结轨迹缓存、Cartesian Cerveny 声场、CLI/PRT/SHD |
| I0 | 完成 | 通用 SSP evaluator 与中间状态 oracle 接口 |
| I1 | 完成 | PCHIP 实/复 SSP、梯度与 Hessian |
| I2 | 完成 | N²-linear 与 not-a-knot Cubic Spline SSP |
| I3-01～I3-04 | 完成 | `LS` 折线 ATI/BTY、活动 segment、斜面求交与多次反射 |
| I3-05 | 完成 | canonical `C`、未归一化反射帧、曲率动态跳跃 |
| I3-06 | 完成 | `LL` 分段流体材料、左节点归属、事件材料冻结与 `1e20 m` 衰减深度 |
| I4-01 | 完成 | `N/F/M/W/Q/L` 六种衰减输入单位 |
| I4-02 | 完成 | Francois–Garrison 与 biological 体积衰减 |
| I4-03 | 完成 | 普通 ENV `A` 型弹性海床、P/S 衰减与逐频反射 |
| I4-04 | 完成 | bottom `G` grain-size 地声模型、当地水声速逐事件展开与 `L` 损耗 |
| I4-05 | 完成 | bottom `F`、共享只读 `.brc` 幅相表、掠射角插值与逐频只读投影 |
| I5-01 | 完成 | `Q` 型 `.ssp` 二维网格解析、维度/资源上限与共享只读存储 |
| I5-02 | 完成 | range/depth 双单元定位、双线性 `c/cr/cz/crz` 及逐频参考 SSP 虚部 |
| I5-03～I5-04 | 完成 | Q range-cell 限步、双维梯度跳跃、动态 `p/q` 与交叉导数有限差分门 |
| I5-05 | 完成 | 715 点代表射线逐点 oracle、双频最终场及冻结几何跨频复用门 |
| I6-01 | 完成 | 多 source depth 排序、逐源缓存生命周期、独立 workspace 与 SHD source-major 写出 |
| I6-02 | 完成 | irregular receiver 输入、单压力行 workspace、SHD irregular 布局与 CC legacy 深度语义 |
| I6-03 | 完成 | `CC* + .sbp`、dB 到线性幅度转换、逐角插值/外推及只读投影 |
| I6-04 | 完成 | `R/RG/RGO` 二维 ray-trace、逐 source 冻结缓存消费与 Origin 兼容 `.ray` 写出 |
| I6-05 | 完成 | SHD/RAY 布局与资源预检、原子发布、CLI 模式切换和标准结果防陈旧门 |
| I7-01 | 完成 | Cartesian `P/V/H` 解析/PRT 与 Origin legacy component no-op 兼容门 |
| I7-02 | 完成 | Cartesian Cerveny `F/M/W` 宽度、WKB KMAH 与 `D/S/Z` 反射曲率模式 |
| I7-03 | 完成 | point/line source geometry、逐射线 Ratio1 与最终场扩散缩放 |
| I7-04 | 完成 | `C/I/S` 相干模式、逐 beam 强度累加、逐频 Lloyd mirror、最终强度平方根与 SHD |
| I7-05 | 完成 | ray-centered Cerveny、规则接收网格求交与物理 `P/V/H` 分量变换 |
| I7-06 | 完成 | Cartesian simple Gaussian、geometric Gaussian，以及 Cartesian/ray-centered geometric hat |
| I8-01 | 完成 | Origin-compatible Arrival 模型、checked 容量、真实 AddArr 语义、G/g/B accumulation 与 source-streamed solver |
| I8-02 | 完成 | ASCII `A`、GNU sequential-unformatted binary `a`、多 source ARR writer 与原子发布 |
| I8-03 | 完成 | G/g/B eigenray 命中、冻结 ray prefix、EOF `.ray` writer 与 E-mode CLI |
| I8-04 | 完成 | 独立 ARR/E reader、真实 ArrMod oracle、Origin/F2CPP 矩阵、输出安全与全回归报告 |
| I9-B1 | 完成 | top `R/A/G/F`、top `.trc`、bottom `V` 与方向无关的共享边界声学/事件投影 |
| I9-B2 | 完成 | General R 的方向性 `.sbp`、有损边界逐频 terminal prefix 与显式单 ray |
| I9-B3 | 完成 | top/bottom acoustic `LL` elastic P/S、事件材料冻结与 `1e20 m` 逐频换算 |
| I9-B4 | 完成 | supported/divergence/deferred 矩阵、文档统一、双编译器与隔离构建封板 |

## 2. 当前验证基线

- AppleClang Debug ASan/UBSan：37/37 CTest；
- AppleClang Release：37/37 CTest；
- GCC 14 Release/Werror：37/37 CTest，项目 C++ 源码无 GCC warning/error；
- Python 标准算例工具：145/145；
- F2CPP 单频端到端案例：65/65，其中包含 6 个 ARR、4 个 E，以及 B1～B3 三个收口案例；既有 SHD/RAY
  案例全部保持通过；
- I3-06 `LL` 最终场：最大复压力绝对误差 `4.66e-11`、最大相对误差
  `4.57e-7`、最大 TL 差 `7.63e-6 dB`。
- I4-03 弹性海床最终场（1/2 kHz）：最大复压力绝对误差 `1.69e-8`、
  最大 TL 差 `2.59e-4 dB`；两端均通过 nonzero-shear 对 fluid control 的
  非空操作门。
- I4-04 grain-size 最终场（1/2 kHz）：最大复压力绝对误差 `1.68e-8`、
  最大相对误差 `2.56e-6`、最大 TL 差 `2.29e-5 dB`；两端的 `G` 场均与
  等价普通流体 `A` control 压力逐位一致。
- I4-05 tabulated reflection 最终场（1/2 kHz）：最大复压力绝对误差
  `1.68e-8`、最大相对误差 `6.35e-6`、最大 TL 差 `3.82e-5 dB`；两端均
  通过相对 rigid control 的非空操作门。
- I5 Q 型代表射线：715 点、714 个积分步与 Origin 离散序列一致，最坏连续量
  绝对误差 `1.06e-22`；范围相关主案例 1/2 kHz 最大复压力绝对误差
  `2.05e-8`、最大相对误差 `2.29e-6`、最大 TL 差 `1.53e-5 dB`。常速 Q
  control 的相干近零点采用单独冻结的 `1e-6` 绝对压力/`0.02 dB` TL 门，
  实测最大值分别为 `5.80e-7` 与 `0.0113 dB`；两端主例相对 control 的
  最大压力差均大于 `0.0296`，排除 Q range 依赖被静默忽略。
- I6 多源深度案例：输入 `80/20/50 m` 后按 Origin 排为 `20/50/80 m`，
  SHD 压力维度为 `[1,3,11,51]`；1/2 kHz 最大复压力绝对误差
  `2.24e-8`、最大相对误差 `4.98e-6`、最大 TL 差 `3.82e-5 dB`。
  三个源切片的最小两两最大压力差大于 `0.0501`，排除切片串写或复用。
- I6 irregular receiver 案例：SHD 保留 5 个 depth/range 坐标并以
  `irregular` plot type 写一条压力记录；1/2 kHz 最大复压力绝对误差
  `3.80e-9`、最大相对误差 `5.03e-7`、最大 TL 差 `3.82e-6 dB`。
- I6 source beam-pattern 案例：`.sbp` 的 6 个 dB 节点先转线性压力幅度，
  再按发射角插值；1/2 kHz 最大复压力绝对误差 `1.68e-8`、最大相对误差
  `4.06e-6`、最大 TL 差 `2.29e-5 dB`。两端相对 omni control 的最大压力
  差均大于 `0.0214`，排除方向图未生效。
- I6 ray-trace 案例：2 source × 5 angles 共 10 条射线、5934 个写出点，
  top/bottom bounce 各 19 次；相对 Origin 的坐标最大绝对误差为 `0 m`，
  source-major/launch-angle-major 语义哈希一致。冻结报告为
  [`validation/i6_ray_trace_report.json`](./validation/i6_ray_trace_report.json)。
- I6 输出安全门：规则与 irregular SHD 均在分配/写文件前冻结 record 数、
  record bytes、最终 record 号和总文件字节；SHD/RAY 采用临时文件完整关闭后
  发布。CLI 的 CC→R→CC 同根切换不会留下异类产品或 `.tmp`，失败输入保留
  旧有效 SHD 并在 PRT 记录 FATAL；报告为
  [`validation/i6_output_safety_report.json`](./validation/i6_output_safety_report.json)。
- I7 Cartesian component 案例：Origin 与 F2CPP 各自的 P/V/H SHD 均逐字节
  相同且场非零；三组跨实现比较的最大复压力绝对误差 `1.50e-8`、最大相对
  误差 `2.86e-6`、最大 TL 差 `7.63e-6 dB`。报告为
  [`validation/i7_cartesian_components_report.json`](./validation/i7_cartesian_components_report.json)。
- I7 beam-option 五例矩阵：FS/MS/WS 的首射线 HalfWidth 与 epsilon 均和
  Origin 精确一致；MD/MS/MZ 的曲率效果在两实现中均非空。五组跨实现比较的
  最大复压力绝对误差 `1.32e-9`、最大相对误差 `1.64e-6`、最大 TL 差
  `1.1444091796875e-05 dB`。报告为
  [`validation/i7_beam_options_report.json`](./validation/i7_beam_options_report.json)。
- I7 source-geometry 三例矩阵：默认空白 point 与显式 `R` point 在 Origin、
  F2CPP 内部分别逐位相同；`X` line 与 point 的最大复压力差均为
  `0.2273859679698944`，TL 差中位数分别为 `51.082218 dB` 与
  `51.082214 dB`。三组跨实现最大复压力绝对误差为 `1.64e-7`、最大 TL
  差为 `9.54e-6 dB`。报告为
  [`validation/i7_source_geometry_report.json`](./validation/i7_source_geometry_report.json)。
- I7 coherence-mode 三例矩阵：C/I/S 共用相同的 300 条冻结射线几何；I 对
  每条 beam 的复贡献取强度后累加，S 在逐频投影中先施加 Lloyd mirror，
  两者最终取累积强度平方根并执行 point/line 扩散缩放。I/S SHD 与 C 使用
  相同布局，但最终复数槽虚部严格为零。三组跨实现最大复压力绝对误差
  `9.33139787662185e-10`、最大相对误差 `8.103583013507887e-7`、最大 TL 差
  `1.52587890625e-5 dB`；C/I 最大压力差为 `0.0022908477`、TL 差中位数约
  `23.8819 dB`，I/S Lloyd effect 最大压力差为 `1.7811398720368743e-6`、
  TL 差中位数为 Origin `0.0492249 dB`、F2CPP `0.0492172 dB`。报告为
  [`validation/i7_coherence_modes_report.json`](./validation/i7_coherence_modes_report.json)。
- I7 ray-centered 四例矩阵：CC/P control 与 CR/P、CR/V、CR/H 使用除此
  之外相同的非零场输入；ray-centered 路径消费 Origin 的局部法向、插值
  `p/q`、KMAH 与 `P/V/H` 投影公式。四组跨实现最大复压力绝对误差
  `4.485843874135753e-08`、最大相对误差 `8.186953891708981e-06`、最大 TL
  差 `6.103515625e-05 dB`。CC/P↔CR/P、CR/P↔CR/V、CR/P↔CR/H、
  CR/V↔CR/H 的最大压力效果分别为 `0.0115372753`、`0.0872854739`、
  `0.1365610510`、`0.1562754959`，均通过独立非空门。首个纵切仅支持规则
  接收网格并明确拒绝 ray-centered irregular receiver grid。报告为
  [`validation/i7_ray_centered_components_report.json`](./validation/i7_ray_centered_components_report.json)。
- I7 Gaussian-family 三例矩阵：Cartesian geometric hat (`G`)、geometric
  Gaussian (`B`) 和 simple Gaussian (`S`) 使用相同的 300 条发射射线及
  1 kHz 场布局。三组 Origin/F2CPP 最大复压力绝对误差为
  `1.30385160446167e-08`、最大相对误差 `2.107042291754624e-06`、最大 TL 差
  `2.288818359375e-05 dB`；两实现内部的 G/B/S 三组两两 family effect 均
  通过独立非空门。Origin 明确不提供 ray-centered geometric Gaussian，
  F2CPP 在 parser 边界保持相同拒绝。报告为
  [`validation/i7_gaussian_beams_report.json`](./validation/i7_gaussian_beams_report.json)。
- I8 Arrival accumulator：直接链接真实 `ArrMod::AddArr` 的 15 个固定场景
  产生 24 个存储到达，计数、顺序、bounce 和全部 144 个 float 字段逐位一致；
  覆盖严格 delay/phase 边界、last-only duplicate、weighted merge、signed
  axial-cusp guard、first-minimum tie、容量替换/丢弃和零到达。报告为
  [`validation/i8_arrival_accumulator_report.json`](./validation/i8_arrival_accumulator_report.json)。
- I8 ARR 六例矩阵覆盖 `A/a`、G/g/B、规则/不规则接收、多 source、反射多径、
  unwrapped phase 和零到达；Origin/F2CPP 的 source/cell/arrival 顺序、计数、
  bounce 以及全部存储字段一致，最坏误差 `0 ULP`，并包含 984 个反射到达和
  单 cell 最大 123 个到达。报告为
  [`validation/i8_arrivals_report.json`](./validation/i8_arrivals_report.json)。
- I8 E 四例矩阵覆盖 G/g/B、多 source、重复 launch angle、反射 prefix 与零命中；
  2200 个 EOF block、876191 个 prefix 点、2830/2830 次 top/bottom bounce
  的结构和坐标全部一致，最大坐标误差 `0 m`。报告为
  [`validation/i8_eigenrays_report.json`](./validation/i8_eigenrays_report.json)。
- I8 输出安全门验证 `CC -> R -> A -> a -> E -> CC`、header-only 零命中 E、
  陈旧 SHD/RAY/ARR 和 `.tmp` 清理，以及 parse、solver 和 publish 三阶段失败
  对旧正式产品的保留。报告为
  [`validation/i8_output_safety_report.json`](./validation/i8_output_safety_report.json)。
- I9-B1 top-F/bottom-V 对照：top `.trc` 和 bottom vacuum 均由 Origin/F2CPP
  端到端消费；最大复压力绝对误差 `1.73985413e-8`、最大相对误差
  `1.40428056e-5`、最大 TL 差 `1.14440918e-4 dB`。B1 checkpoint 的
  `f2cpp-regression`、Python 145/145、CTest 37/37 和单频案例 63/63 均通过。
- I9-B2 General R 对照：方向性 `.sbp`、top `.trc`、bottom rigid 与显式
  `Nalpha=1` 组合产生 1 条射线、1107 个写出点和 3/3 次 top/bottom bounce；
  Origin/F2CPP 的结构、terminal prefix 和坐标精确一致。
- I9-B3 elastic LL 对照：top/bottom acoustic `LL` 同时使用非零 P/S 参数；
  最大复压力绝对误差 `4.38070691e-11`、最大相对误差 `4.26289063e-7`、
  最大 TL 差 `1.52587891e-5 dB`。最终 regression 为 CTest 37/37、案例
  14/14；full 为 Python 145/145、CTest 37/37、单频案例 65/65。
- I9-B4 closure：AppleClang Debug ASan/UBSan、AppleClang Release 与 GNU
  C++ 14.2 Release/Werror 均为 37/37；`f2cpp-full` 为 Python 145/145、
  Release CTest 37/37、单频案例 65/65。排除既有 build 与 RayReuse 后的
  AppleClang clean Release 完整编译通过；补入仓库级共享标准案例 fixture
  后隔离 CTest 37/37。生产源码无 TODO/FIXME，未支持输入在 parser/model/
  solver 边界显式报错，未发现 correctness blocker 或 silent fallback。

I3/I4/I5/I6 的逐项输入、可执行文件与场结果哈希位于
[`validation/`](./validation/)。I3-06 和 I4-03 的验证器输出均与对应冻结
报告逐字节一致；I4-04、I4-05 报告均由各自验证器直接生成。

## 3. 缓存与逐频状态

`RayPathCache` 继续保存完整、频率无关、冻结只读的射线状态。SSP 虚部、
体积衰减、边界复反射系数、active mask 和声压只存在于逐频临时状态。
`LL` 材料在几何追踪时按活动 segment 冻结原始参数，各频率只换算复材料，
不会重新定位边界或改写缓存。`.brc` 表作为环境级共享只读数据保留，逐频
投影只读取事件慢度和表格，不把插值结果写回 `RayPathCache`。Q 实声速矩阵
同样是 Environment 级共享只读几何；`.env` 参考节点的虚声速按目标频率
换算后仅进入临时 `RayFrequencyState`，不会进入或修改 `RayPathCache`。

## 4. 文档角色

- 本文件记录当前施工进度和最新验证基线；
- [`FURTHER_REPLICATION_PLAN.md`](./FURTHER_REPLICATION_PLAN.md) 记录后续二维
  功能的依赖顺序和逐项完成证据；
- [`USAGE.md`](./USAGE.md) 记录当前可实际使用的输入范围；
- [`FEATURE_SUPPORT_MATRIX.md`](./FEATURE_SUPPORT_MATRIX.md) 是复刻封板后的
  supported、intentional divergence 与 deferred/out-of-scope 唯一分类表；
- [`DERIVATION_MANIFEST.md`](./DERIVATION_MANIFEST.md) 是 2026-07-29 的 M2
  历史派生快照，继续用于追溯当时批准 RayReuse 的源码和性能门，不代表当前
  I0～I8 扩展树的哈希。
- [`PERFORMANCE.md`](./PERFORMANCE.md) 记录复刻封板后的 P1 基线、P2
  Cartesian Cerveny 热循环优化证据和后续性能入口。

## 5. 下一步

I0～I8 与 B1～B4 已全部冻结，不再以“剩余复刻功能”继续扩张。支持范围和
延期项以 [`FEATURE_SUPPORT_MATRIX.md`](./FEATURE_SUPPORT_MATRIX.md) 为准；
`P/W`、`CS/CL`、`G/F + LL`、ray-centered irregular receiver、3D、N×2D、
beam shift、analytic SSP 和 F2CPP 多频调度均不属于本次 closure。

P1 已在 TL、R、A、E 四个代表性 workload 上完成 1 次 warmup + 5 次正式
测量。Munk TL 外部 wall 中位数为 `2.6761 s`，其中 Cartesian Cerveny
Influence 为 `2.6030 s`（`97.27%`）；peak RSS 为 `64.27 MiB`。四例产品哈希
在重复测量与独立标准案例验证之间完全一致，`f2cpp-regression` 为 CTest
37/37、案例 14/14。详细证据见 [`PERFORMANCE.md`](./PERFORMANCE.md)。

P2 已完成 Cartesian Cerveny Influence 的局部低风险优化：hoist 稳定循环量与
边界读取、缓存 receiver 布局/深度访问，并在入口维度验证后直接访问连续
pressure workspace。Munk TL 的 wall 中位数由同轮 before `2.6642 s` 降至
`1.6565 s`（`1.608×`），Influence 由 `2.5916 s` 降至 `1.5816 s`
（`1.639×`）；peak RSS 无实质变化，SHD 与 P1 baseline 逐字节一致。
`f2cpp-regression` 为 CTest 37/37、案例 14/14。

P3-01 已在 P2 clean HEAD 上重新采样。三轮 1 ms sample 的 4195 个主线程样本
表明剩余成本以 accumulateImpl 本体、sincos/exp 和 finite helper 为主；三轮
硬件统计 IPC 中位数为 `6.890`，无 swap/block I/O，仅各 1 次 hard fault，
当前没有 RayPathCache memory-bound 证据。唯一接受的即时 scalar 优化是内联
finite helper，wall `1.6536→1.5093 s`（`1.096×`），Influence
`1.5797→1.4374 s`（`1.099×`），产品继续 bitwise 一致；回归仍为 CTest
37/37、案例 14/14。下一优先级是独立的 P3-A Hermite hot-path specialization；
不直接进入数据布局、显式 SIMD 或 OpenMP，也不自动同步 RayReuse。

P3-02 已完成上述 Hermite specialization：保留公开 helper 和全部 correctness
检查，将相同实现置于 TU-local always-inline hot helper，并只让 Cartesian
两个直接路径消费。Munk wall `1.5087→1.4152 s`（`1.066×`），Influence
`1.4375→1.3441 s`（`1.069×`），SHD 继续 bitwise 一致；优化后两轮 sample
中公开 taper 叶占比由 `18.51%` 降至 `0%`。AppleClang/GCC focused CTest
均为 2/2，regression 仍为 CTest 37/37、案例 14/14。Hermite 已满足停止条件，
后续是否进入 sincos/exp 必须作为新的、单独批准的性能阶段。

P3-03 已审查 Cartesian Cerveny 的 transcendental 调用结构。Munk 中
`67,155,371` 个实际传播候选各只调用一次 `exp` 和一次由 AppleClang 已融合的
`sincos`；zero amplitude/real phase/imaginary phase 均为 0，精确重复复相位
仅 `324,785` 个（`0.484%` 上界），没有覆盖缓存成本的安全复用机会。相位
hoist/递推会改变浮点运算顺序且不能减少唯一相位的 transcendental 调用，
因此未接受生产修改。clean wall/Influence 为 `1.4180/1.3472 s`，最终使用
同一 Release 二进制，speedup `1.000×`，RSS 与 SHD hash 不变；focused、Munk
和 regression 均通过。单线程低风险 scalar 阶段至此停止，不自动进入
SIMD/OpenMP。

P4-01 已完成并行/vectorization 决策。AppleClang/GCC 均无法自动向量化
receiver/image 热循环；真实 Munk 相位上的 Accelerate vForce math-only 原型
虽为 `3.705×`，但 84.84% 的合成复指数不再 bitwise（最大相对差
`6.50e-16`），且总体理想 wall 上限约 `1.18×`，不接受为正式路线。保持每个
cell 内 ray 顺序的 receiver-depth stripe OpenMP 原型在 50 Hz/1000-ray 与
250 Hz/5000-ray Munk 上，8-thread 相对正式串行分别达到 `2.48×/2.01×`，
所有线程数 SHD 均逐字节一致，额外 RSS 小于 0.3 MiB；4 threads 后已明显
饱和。原型源码已移除，focused 2/2、Munk 与 regression 37/37+14/14 通过。
推荐下一阶段只正式化持久 team、静态 depth tiles 和异常汇合，不实施 SIMD，
也不允许与 BARR/RayReuse 外层 frequency/source 并行嵌套。
