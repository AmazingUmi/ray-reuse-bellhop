# Bellhop F2CPP 当前进度

> 更新日期：2026-08-14
> 当前主路线：二维单频复刻；3D、N×2D 和 beam shift 默认排除。
> 当前施工状态：I7-06 已完成并收口；按当前任务要求暂停，尚未进入 I8。

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

## 2. 当前验证基线

- AppleClang Debug ASan/UBSan：32/32 CTest；
- AppleClang Release：32/32 CTest；
- GCC 14 Release/Werror：32/32 CTest，项目 C++ 源码无 GCC warning/error；
- Python 标准算例工具：123/123；
- F2CPP 单频端到端案例：52/52，其中 51 个验证 SHD 声场，ray-trace
  案例验证 PRT/RAY 且明确不产生 SHD；
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
- [`DERIVATION_MANIFEST.md`](./DERIVATION_MANIFEST.md) 是 2026-07-29 的 M2
  历史派生快照，继续用于追溯当时批准 RayReuse 的源码和性能门，不代表当前
  I0～I7-06 扩展树的哈希。

## 5. 下一步

I7-06 已完成并按当前任务要求暂停，尚未开始 I8 arrivals/eigenray。I7-05
冻结的 ray-centered Cerveny 规则网格求交、`P/V/H` 分量变换和
CC/P↔CR/P family 效果门，以及 I7-06 冻结的 G/B/S family 效果门，均不得在
后续阶段反向混入已验收路径。ray-centered irregular receiver grid 继续明确
拒绝；若未来开放，必须单独设计逐 range 深度配对语义和 Origin 对照门。
I6-04 已直接消费既有冻结
`RayPathCache`，支持 `R/RG/RGO`、全向源、真空
海面/刚性海底、无 beam shift 和无损耗前缀的安全范围；显式 `Nalpha` 原样
使用，`0` 自动规划为 50。R 模式只读取到 integrator 记录，不消费 TL 的
`MS`/image 行，输出 PRT 与 RAY 而不产生 SHD。I4-05 已确认
当前 Origin 2D 中 `P/W` 没有完整、可观察的反射消费/写出链，因此没有把
它们伪装成 `F`；顶部 `F/.trc`、`F+LL`、顶部 `G`、`G+LL`、顶部声学
半空间、弹性 `LL/CL`、3D、N×2D 或 beam shift 继续延期。这些范围以
[`USAGE.md`](./USAGE.md) 的功能表为准。
