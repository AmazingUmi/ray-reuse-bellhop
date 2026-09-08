VERDICT: CHANGES_REQUIRED
源码确认当前 production 数据流本身没有把 attenuation、reflection、complex travel time、active prefix 或接收贡献错误写回 frozen cache；问题主要出在报告的状态分类、候选缓存设计、收益估算和验收依据不足。
数据流核查
实际链路为：
Frozen RayPath/RayPathCache
→ FrequencyProjector 为每条射线、每个频率生成新的 RayFrequencyState
→ Influence 按活动前缀遍历
→ receiver/image 匹配
→ 写入该 source/frequency 独占的 pressure/intensity/arrival/eigenray workspace。
证据：
- Frozen geometry 定义及构建：[ray_path.hpp](/Volumes/exDateDisk/projects/ray-reuse bellhop/Bellhop_RayReuse/include/rayreuse/ray/ray_path.hpp)、[ray_path_cache.cpp](/Volumes/exDateDisk/projects/ray-reuse bellhop/Bellhop_RayReuse/src/cache/ray_path_cache.cpp)
- 频率投影：[frequency_projector.cpp](/Volumes/exDateDisk/projects/ray-reuse bellhop/Bellhop_RayReuse/src/field/frequency_projector.cpp)
- 每频率局部状态和 workspace：[frequency_workspace.hpp](/Volumes/exDateDisk/projects/ray-reuse bellhop/Bellhop_RayReuse/include/rayreuse/field/frequency_workspace.hpp)
- solver 中的局部生命周期：[single_frequency_solver.cpp](/Volumes/exDateDisk/projects/ray-reuse bellhop/Bellhop_RayReuse/src/solver/single_frequency_solver.cpp)
- CC 遍历及累加顺序：[cartesian_cerveny_influence.cpp](/Volumes/exDateDisk/projects/ray-reuse bellhop/Bellhop_RayReuse/src/field/cartesian_cerveny_influence.cpp)
正确分类应为：
状态	分类
position、slowness、dynamic P/Q、real c、quadrature、reflection geometry、原始材料输入	frequency-independent
evaluated attenuation、cImag、projected complex travel time、reflection amplitude/phase	frequency-dependent
active/terminal prefix	frequency-/threshold-dependent
epsilon、p/q、gamma、KMAH、wavelength、beam width、window/taper	mixed 或 frequency-/threshold-dependent
receiver contribution、pressure/intensity、arrival/eigenray workspace	frequency-local，禁止共享


Critical findings
1. Candidate 1/2 的索引和浮点 schema 不满足仓库支持范围，也不能支持报告宣称的 bitwise parity。
   - Candidate 1 使用 uint16 点索引并把 weight/z/tz/c 降为 float；Candidate 2 也使用 uint16 depth index：[报告](/Volumes/exDateDisk/projects/ray-reuse bellhop/Bellhop_RayReuse/doc/reports/REPORT_IGR0_INFLUENCE_GEOMETRY_REUSE_AUDIT.md)
   - 仓库允许最多 2,000,000 个 ray points：[environment_parser.cpp](/Volumes/exDateDisk/projects/ray-reuse bellhop/Bellhop_RayReuse/src/io/environment_parser.cpp)
   - 当前 CC 热路径使用 double：[cartesian_cerveny_influence.cpp](/Volumes/exDateDisk/projects/ray-reuse bellhop/Bellhop_RayReuse/src/field/cartesian_cerveny_influence.cpp)
   - 因而报告中的 32-byte entry、约 80 MB、zero-risk 和 100% byte-identical 声明均不能成立。报告后文要求 binary64，也与前述 schema 自相矛盾：[报告](/Volumes/exDateDisk/projects/ray-reuse bellhop/Bellhop_RayReuse/doc/reports/REPORT_IGR0_INFLUENCE_GEOMETRY_REUSE_AUDIT.md)
Major findings
1. complexTravelTime 分类错误。
   报告将其实部归为 G、虚部归为 F，并称 attenuation 只影响虚部。实际上 lossy projector 计算：
   quadrature / (c + i*cImag(f))
   因而投影后 travel time 的实部和虚部都可能随频率变化：[frequency_projector.cpp](/Volumes/exDateDisk/projects/ray-reuse bellhop/Bellhop_RayReuse/src/field/frequency_projector.cpp)。组件测试也明确要求 lossy 实部不同于 frozen real travel time：[frequency_projector_test.cpp](/Volumes/exDateDisk/projects/ray-reuse bellhop/Bellhop_RayReuse/tests/component/frequency_projector_test.cpp)。
2. active/terminal prefix 的描述不准确。
   attenuation 本身不改变 active；终止主要来自 source amplitude 或累计 reflection multiplier 的阈值。报告所称 “attenuation/reflection truncation” 会误导 Candidate 2/4 的失效条件。
3. 热点与收益结论缺少证据。
   - fortranUpperRangeIndex 是截断加 clamp 的 O(1) 计算，不是报告所说的 binary search：[cartesian_cerveny_influence.cpp](/Volumes/exDateDisk/projects/ray-reuse bellhop/Bellhop_RayReuse/src/field/cartesian_cerveny_influence.cpp)
   - 既有 profile 显示 depth/image 循环次数远高于 range 查找：[F1 baseline](/Volumes/exDateDisk/projects/ray-reuse bellhop/Bellhop_RayReuse/doc/archive/benchmarks/REPORT_F1_BASELINE_96F23F8_2026-07-30.md)
   - Candidate 1 的 3–6x、Candidate 2 的 5–10x，以及构建 <0.05s、L3≈100% 等数字没有测量依据。
4. Munk 维度写反，Candidate 2 的收益估算无效。
   算例实际是 201 个 receiver depths、501 个 ranges：[origin.env.in](/Volumes/exDateDisk/projects/ray-reuse bellhop/test/standard_cases/cases/munk_cerveny_cc/origin.env.in)、[case.toml](/Volumes/exDateDisk/projects/ray-reuse bellhop/test/standard_cases/cases/munk_cerveny_cc/case.toml)。报告却按 501 depths 推导候选数缩减和 4–6x 收益。
5. Candidate 1 的跨 Influence-family 适用范围被明显夸大。
   CC 使用水平 range crossing；GeoHat/GeoGaussian 的投影权重依赖完整 receiver point/depth；Simple Gaussian 的 range cursor 和边界规则也不同。不能声称同一 stencil 对 CC/S/G/A/E 具有 100% 适用性。参见：
   - [Cartesian CC](/Volumes/exDateDisk/projects/ray-reuse bellhop/Bellhop_RayReuse/src/field/cartesian_cerveny_influence.cpp)
   - [Geometric Hat](/Volumes/exDateDisk/projects/ray-reuse bellhop/Bellhop_RayReuse/src/field/geometric_hat_influence.cpp)
   - [Simple Gaussian](/Volumes/exDateDisk/projects/ray-reuse bellhop/Bellhop_RayReuse/src/field/simple_gaussian_influence.cpp)
   - [Eigenray family selection](/Volumes/exDateDisk/projects/ray-reuse bellhop/Bellhop_RayReuse/src/solver/eigenray_solver.cpp)
6. ownership、cache key、invalidation 和生命周期设计不完整。
   现有 cache 是 per-source，并由 serial/parallel solver orchestration 持有：[serial solver](/Volumes/exDateDisk/projects/ray-reuse bellhop/Bellhop_RayReuse/src/solver/serial_ray_reuse_solver.cpp)、[parallel solver](/Volumes/exDateDisk/projects/ray-reuse bellhop/Bellhop_RayReuse/src/solver/parallel_ray_reuse_solver.cpp)。
   Candidate 1 至少需要绑定对应 per-source RayPathCache、完整 receiver-range layout、family/coordinate/traversal semantics、schema version 和 checked offsets。Candidate 2 还必须包含完整 frequency set、receiver depths、beam/window/epsilon 参数，因此报告“frequency change never invalidates”的结论不成立。
7. Candidate 4 的最终 crossing/flip topology 不能跨频率共享。
   activePointCount 是频率局部状态，而 ray-centered image normal flip 会沿 step/depth/image 持续传播：[ray_centered_cerveny_influence.cpp](/Volumes/exDateDisk/projects/ray-reuse bellhop/Bellhop_RayReuse/src/field/ray_centered_cerveny_influence.cpp)。不同 active prefix 可以改变最终 flip parity。最多只能缓存原始 geometry primitives；那已经是另一个候选设计。
8. 报告遗漏明显的 geometry reuse candidates。
   至少包括：
   - CC per-point tangent/normal、c²、SSP-gradient projection bundle：[cartesian_cerveny_influence.cpp](/Volumes/exDateDisk/projects/ray-reuse bellhop/Bellhop_RayReuse/src/field/cartesian_cerveny_influence.cpp)
   - GeoHat frozen event-prefix bounce counts：[geometric_hat_influence.cpp](/Volumes/exDateDisk/projects/ray-reuse bellhop/Bellhop_RayReuse/src/field/geometric_hat_influence.cpp)
   - Cartesian GeoHat receiver-cell exact geometry stencil
   - Simple Gaussian receiver-depth geometry kernel
9. 缺少 IGR-0 Worklist 和可执行的 Batch Acceptance gates。
   Bellhop_RayReuse/doc/worklists/ 中没有 IGR-0 Worklist，但报告已经自行声明 ACCEPTED。没有冻结 lossy/lossless、reflection threshold、terminal prefix、serial/parallel、fallback、memory budget、benchmark 和 oracle gates，不满足 Batch Final Review 的关闭条件。
Minor findings
1. 报告以旧 baseline 213581b 为主要依据，部分行号与当前源码已经漂移。
2. 报告相对基线的 git diff --check 存在 8 处 trailing whitespace。
3. rayOffsets_ 使用 uint32 时还需验证累计 entry 数上界，而不仅是单条射线的点数。
Reuse candidate 判断
Candidate	判断	结论
1. Segment–Range Crossing Stencil	QUESTIONABLE	CC-only 的 topology 思路合理；当前 float/u16 schema、跨 family 声明、key、内存和收益结论均不成立。
2. Bounded Receiver-Depth Candidate Set	QUESTIONABLE	保守 envelope 可以研究，但依赖频率集合、depth grid、beam/window 参数；当前索引、Munk 算术和 no-miss 证明不足。
3. Segment Cartesian Projection Bundle	QUESTIONABLE	geometry 概念成立且边界较清楚；当前 float length/AABB、16 MB 和 zero-risk 声明未经 parity 与 benchmark 验证。
4. Ray-Centered Crossing Stencil	INVALID	最终 crossing/flip topology 受 frequency-local active prefix 影响，不能按报告方案冻结共享。
5. Universal Multi-Family Cache	INVALID	各 family 匹配语义不同，缺乏统一 owner/key/schema/parity 契约，也没有收益证据。


IGR-1 首个 prototype 建议
当前报告不能直接授权 Candidate 1 进入 IGR-1。
报告修订并通过重新验收后，首个实验原型仍可选择范围严格收缩的：
CC-only、per-source Segment–Range Crossing Topology Cache
必要边界：
- 与一个 RayPathCache 一一对应、同生命周期、solver-owned，并计入 parallel memory budget。
- 第一版仅缓存安全宽度的整数 topology；如缓存 W/z/tz/c，必须使用 binary64 并保持现有运算顺序。
- key 包含 source cache identity/fingerprint、完整 receiver-range layout、CC/coordinate/traversal semantic version 和 checked offsets。
- 每个频率仍执行 left-point active 检查，并保留“首个 inactive terminal point 的 incoming segment 仍可参与”的现有语义。
- attenuation、完整 projected complex tau、reflection amplitude/phase、active prefix、epsilon/p/q/gamma/KMAH、window/taper 和 receiver contribution 全部保持 frequency-local。
- 实现前先测量当前 crossing 计算占比；若没有明确 wall-time 收益，应优先考虑 exact-double 的 CC point-geometry bundle。
- 验收必须覆盖 lossy tau 实部变化、reflection/source threshold、terminal prefix、规则与非规则 receiver grid、C/I/S、multi-source、serial/parallel/fallback byte parity、memory budget、Origin/F2CPP oracle 和 Munk benchmark。
本次仅完成只读复核；未修改文件，也未进入 IGR-1。
