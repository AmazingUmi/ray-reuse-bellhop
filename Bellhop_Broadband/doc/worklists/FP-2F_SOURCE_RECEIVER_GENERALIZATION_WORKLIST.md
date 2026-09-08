# FP-2F Worklist — Source / Receiver Generalization

> Batch: FP-2F（multisource + Cartesian irregular receiver，含 paired A/a/E）
> 状态：DESIGN（本文件为 frozen scope 与执行期权威状态源）
> 参考证据：`doc/reports/REPORT_F2CPP_RAYREUSE_PARITY_2026-08-25.md` §4.2/§4.3/§4.7
> （SRC-04/PRD-07、REC-02/REC-03/PRD-06 均为 `ARCHITECTURAL_CONFLICT`）

## 0. 冻结 Scope

### 做

1. **Multisource / multiple source depths（SRC-04、PRD-07）**：`.env`
   source-depth count `NSz ≥ 1`；point source；同一 launch fan 对每个 source
   depth 独立 trace；产品覆盖当前已支持面：TL（各已支持 beam family）、
   R（保持单频）、A/a、E。per-source 产品 sequencing/header 与
   F2CPP/Origin 一致（source 按 depth 升序）。
2. **Cartesian irregular receiver（REC-02、PRD-06）**：run type 第 5 位
   `I`；paired 语义 = `NRz == NRr`、`receiversPerRange() == 1`、
   `depthAt(depthIndex, rangeIndex) == depths[rangeIndex]`。适用面与 F2CPP
   相同：Cartesian Cerveny `CC/IC/SC`、Cartesian GeoHat `CG/IG/SG`、
   Cartesian GeoGaussian `CB/IB/SB` TL，及 Cartesian `G/B` 的 A/a/E；
   SHD `PlotType = "irregular "`。
3. **三执行模式适配**：multisource 与 irregular 在 `nonreuse/reuse/parallel`
   下逐频产品一致（与既有 single/rectilinear 相同的 byte-identity 门槛）。

### 不做（及原因）

1. **Line source（SRC-02/PRD-08）**：P1 非 P0；不改变 trajectory/cache
   schema（逐 ray `sqrt(|cos α)|` ratio + frequency-local spreading），但
   触及全部 6 个 influence 家族 + scaling + writers；与 multisource/irregular
   合批会使验证矩阵失控。parser 继续显式拒绝 `X`。留作独立后续候选批次。
2. **3D / N×2D / Sx/Sy 多坐标**：Origin 2D 中 `ReadSxSy` 固定
   `Sx=Sy=(0,0)` 单值；二维 RayReuse 不可达。
3. **Ray-centered irregular（REC-05）**：F2CPP 自身
   `F2CPP_OUT_OF_SCOPE`；parser/model 保持拒绝。
4. **Broadband R / frequency 向量 R**：R 保持单频产品语义。
5. **frozen `RayPathCache` schema / `contentFingerprint` 算法变更**：本批
   不改 cache 字段、布局与指纹算法（见 §1 论证）。
6. **未 oracle 验证的组合不声明 parity**（沿用 FP-2E 纪律）：例如
   multisource × Q、multisource × ray-centered、irregular × Q 等
   机制可达但无独立 oracle 的组合只按机制处理，不写入支持矩阵。

## 1. Ownership / Lifetime / Cache 契约论证（冻结决定）

**事实基线**（代码证据）：

- F2CPP `single_frequency_solver.cpp`：multisource = 外层
  `for sourceIndex` → 每 source 独立 `RayPathCache`（reserve → append →
  freeze，用后即弃）→ 每 source 独立 `FrequencyWorkspace`；sources 由
  `SimulationCase` 构造时 `stable_sort` 按 depth 升序。
- Origin `bellhop.f90`：`SourceDepth: DO is = 1, NSz` 同构；SHD 记录
  `IRec = 10 + NRz_per_range*(is-1)` 逐 source 堆叠；A/a/E 逐 source 写块。
- RayReuse `RayPath` = `launchAngle + points`；source depth 只存在于轨迹
  初始点；`contentFingerprint()` 只覆盖 traced geometry。

**冻结设计**：

1. `RayPathCache` 语义收紧为 **one cache = one source 的 launch fan**。
   cache 本身不加 source 字段、不改 schema。source depth 是
   frequency-independent，per-source frozen cache 在契约内。
2. **Cache identity**：multisource = solver 层持有
   `vector<RayPathCache>`（每 source 一个，各自 freeze、各自
   fingerprint）。reuse 模式的复用单位从 "case" 细化为
   "(source, frozen fan)"，跨全部频率复用。`NSz == 1` 时必须逐字节复现
   现有行为与 fingerprint 值（回归 gate）。
3. **Ownership/lifetime**：cache vector 由 solver 编排层独占；parallel
   频率 worker 只拿 `const` 引用；逐频 product state（workspace /
   ArrivalWorkspace / Eigenray hits）仍为 frequency-local，per-frequency
   结果从 "单 workspace" 变为 "per-source workspace 序列"；serial ordered
   consumer 发布模型不变。不引入 global current frequency，不引入新的
   shared mutable state。
4. **Memory 语义**：reuse/parallel 峰值 = Σ(每 source cache) +
   active workers × NSz × workspace。沿用 F2CPP 的
   `sources × receiversPerRange × rangeCount ≤ kMaximumReceiverGridValues`
   上限检查以约束规模；在统计中报告 per-source cache bytes。
5. **Statistics 冻结语义**：`tracePassCount` = per-source fan trace 次数
   （`nonreuse = Nfreq × NSz`；`reuse/parallel = NSz`）。单源两频的既有
   `2/1/1` 语义不变。

## 2. 任务拆解

### F01 [ADVANCED]
Status: DONE
Reviewer: PASS
Depends: —

Acceptance:
- `ReceiverGrid` 增加 `ReceiverGridLayout{Rectilinear,Irregular}`、
  `receiversPerRange()`、`depthAt(depthIndex, rangeIndex)`、
  `isIrregular()`，语义逐条对齐 F2CPP（irregular 要求
  depths.size()==ranges.size()；ranges 非负；strictly increasing 校验保持）。
- `SimulationCase` 持有 `vector<Source>`（含 `sourceCount()/sources()`），
  构造时按 depth `stable_sort`；保留现有单源构造路径或等价迁移，全部
  现有 caller 语义不变。
- 校验矩阵复刻 F2CPP `simulation_case.cpp`：每 source 严格在水体内；
  receiver 在水体或边界上；ray-count / source×receiver workspace 上限；
  ray-centered 家族 + irregular 拒绝；SimpleGaussian 仅 coherent
  point-source rectilinear。
- 组件测试覆盖：排序稳定性、irregular 计数约束、各拒绝分支、
  单源行为回归。

Evidence:
- `tests/unit/core_types_test.cpp`（`rayreuse.unit.core_types`）新增 6 个
  model 测试函数：rectilinear/irregular 寻址与计数约束、多源排序与
  stable_sort 稳定性、多源 fan planning 1500 m/s 参考速度（单源仍取
  local speed）、TL workspace 上限（单源 2000×1001 拒绝 / RayTrace 豁免 /
  双源 2×1001×1000 拒绝 / irregular 1M pairs 接受）、run ray-count 上限
  （2000001 拒绝 / 2000000 接受 / 双源×1000001 拒绝）、ray-centered 与
  SimpleGaussian 的 irregular 拒绝分支；单源回归断言
  `sourceCount()==1`。
- `Bellhop_RayReuse/build/fp2f-clean`（Release，
  `RAYREUSE_WARNINGS_AS_ERRORS=ON`）全量 CTest 37/37 通过
  （含既有 solver/writer/parser 全部回归，`NSz==1` 无 production 行为
  变化；单源 fan planning 与校验行为 bit-level 等价）。

### F02 [ADVANCED]
Status: DONE
Reviewer: PASS
Depends: F01

Acceptance:
- parser：source-depth count 接受 `≥1`；读入 depth 向量并传入 model。
- run type 第 5 位 `I` 接受；施加 F01/F2CPP 同构约束（ray-centered
  家族、`CS` 拒绝；`R` run type 第 5 位仅 ` `/`R` 保持）；`X`（line）
  继续显式拒绝、错误信息不变。
- `R-PARSER` 既有拒绝矩阵全部保持（新增 accept 分支只来自
  multisource count 与 `I` layout）。
- PRT（`app/main.cpp` summary）：多源时逐行列出 source depths；
  irregular 时打印 Origin 兼容 marker（`Irregular grid`），rectilinear
  打印保持现状；`Point source (cylindrical coordinates)` 文本按实际
  geometry 输出。
- parser/PRT 组件测试；`irregular_receiver_pairs` case 的
  `prt_markers` 校验通过。

Evidence:
- `src/io/environment_parser.cpp`：移除 `NSz != 1` 拒绝（count 复用
  `parseCount` ≥1 上限 2M，与 F2CPP 同构），source depths 读取顺序与
  legacy single-precision subtabulation 不变；`SimulationCase` 改用 F01
  `vector<Source>` 构造（每 source `amplitude = 1.0`，depth 升序由 model
  stable_sort 保证）。run type 第 5 位按 F2CPP 矩阵接受：TL（C/I/S 全
  family）、A/a、E 接受 `I`；`R` ray trace 的 mode 布尔加入
  `(runType[4] == ' ' || 'R')`（`R...I` 落入既有 generic 拒绝）；新增
  parser 级 F2CPP 同文拒绝：ray-centered 家族 + `I`
  （"ray-centered beam families do not support irregular receiver grids"）、
  `NRz != NRr`（"irregular receiver grid requires equal depth and range
  counts"）；`CS`+`I` 由 F01 model 同文拒绝；`X` 拒绝与错误信息不变。
  `ReceiverGrid` 构造传入 `runType.receiverLayout`。
- `app/main.cpp` PRT：`sourceCount > 1` 时输出 `source depths = N` +
  逐行 `source depth = <z>`（升序，对照 F2CPP 计数行与 Origin Sz echo）；
  irregular 打印 `Irregular grid: paired receiver ranges and depths`
  （Origin 兼容 `Irregular grid` marker，与 F2CPP summary 同文），rectilinear
  行不变；`Point source (cylindrical coordinates)` 保持（RayReuse 无 line
  source，即实际 geometry）。
- 组件测试（`tests/component/environment_parser_test.cpp`，新增
  `testMultiSourceDepths`/`testIrregularReceiverLayouts`）：多源
  explicit/subtab/排序/单位幅度、NSz=0 与 count 不匹配与出水体拒绝、
  双源 R 接受；`CG/IG/SG/CB/AG/aG/EB + I` paired 接受（layout/计数/
  `depthAt` 配对寻址）、`CC + I` 均匀 paired 接受、paired 计数不匹配拒绝、
  `CR/Cg/Ag + I` 与 `CS + I` 与 `R...I` 拒绝、`CG X` line-source 拒绝不
  变、无 `I` 时 rectilinear 回归。
- 本地 PRT 验证：F01 基线二进制 vs F02 二进制同单源 case，PRT 除计时行
  外 byte-identical、SHD byte-identical；共享 case
  `irregular_receiver_pairs`（`CC RI2`）本地运行 PRT 含全部声明
  markers（`Irregular grid`/`VACUUM`/`Perfectly RIGID`）且无
  `Rectilinear receiver grid`；`multi_source_depths`（NSz=3 乱序输入）
  解析并输出升序 `source depths = 3` + 3 行深度（三方 validator 留待
  F07 启用）。
- `Bellhop_RayReuse/build/fp2f-clean`（Release，
  `RAYREUSE_WARNINGS_AS_ERRORS=ON`）全量 CTest 37/37 通过（既有
  parser/solver/writer 回归零变化）。

### F03 [ADVANCED]
Status: DONE
Reviewer: PASS
Depends: F01

Acceptance:
- `SingleFrequencySolver::traceRayFan` 泛化为 per-source trace（或新增
  per-source 入口），产出 `NSz` 个独立 frozen `RayPathCache`；
  `RayPathCache`/`RayPath`/`RayState` 字段与 `contentFingerprint()`
  算法零改动。
- `NSz==1`：输出、fingerprint、timings 与改动前完全一致。
- 每 source 的 trace 失败诊断携带 source index（对齐 F2CPP 错误语义）。

Evidence:
- `include/rayreuse/solver/single_frequency_solver.hpp` +
  `src/solver/single_frequency_solver.cpp`：新增
  `traceSourceFan(simulation, sourceIndex)`（单 source 独立 reserve →
  append → freeze，共享 `launchFanPlan()` 角度集——对照 F2CPP
  `single_frequency_solver.cpp` 确认：外层 `for sourceIndex` 使用同一个
  `simulation.launchFanPlan()`，全源共享同一 fan，无 per-source 角度规划）
  与 `traceAllSourceFans(simulation)`（返回 `vector<RayFanTraceResult>`，
  每 source 一个独立 frozen cache，各自 `contentFingerprint()`，由 solver
  编排层持有，为 F04 留接口）。`traceRayFan` 保留为首源（最浅 source）
  过渡入口 = `traceSourceFan(simulation, 0)`，serial/parallel reuse solver
  现 caller 零改动。诊断对齐 F2CPP 文本
  `"...normally (source index N, launch index M, angle A, reason R)"`
  （RayReuse `RayPath` 无 `terminationDetail` 字段，schema 冻结故无
  `, detail: ...` 尾部）；`sourceIndex` 越界 → ValidationError。
- `RayPathCache`/`RayPath`/`RayState` 与 `contentFingerprint()` 零改动：
  `git diff -- src/cache include/rayreuse/cache include/rayreuse/ray` 为空。
- solver 组件测试
  （`tests/component/single_frequency_solver_test.cpp` 新增
  `testPerSourceTraceProducesIndependentFrozenCaches`/
  `testSingleSourcePerSourceTraceEquivalence`/
  `testPerSourceTraceDiagnostics`）：3 sources → 3 trace results、各
  cache 尺寸 = 共享 fan `launchAngleCount`、各自 `frozen()`、
  `cache.at(0).points.front().position.depth == sources()[i].depth`
  （cache 索引 ↔ depth 升序 source 索引）、3 个 fingerprint 互异、重复
  trace pass fingerprint 稳定、`traceRayFan` == entry 0；NSz==1 时
  per-source trace 与 legacy `traceRayFan` fingerprint/point-count 等价；
  越界 index 与 point-limit 诊断（含 `source index 1`）覆盖。
- `NSz==1` 等价回归：munk_spline broadband（50/250 Hz）改动前亲跑记录
  fingerprint `1526667602348633172`（before==after）、SHD SHA
  `74028065178ff80d`；改动后三模式重跑：reuse/parallel fingerprint
  `1526667602348633172` before==after、nonreuse/reuse/parallel 三模式
  SHD 均 `74028065178ff80d`、Trace passes `2/1/1`（单源两频冻结语义不变）。
- `Bellhop_RayReuse/build/fp2f-clean`（Release，
  `RAYREUSE_WARNINGS_AS_ERRORS=ON`）全量 CTest 37/37 通过（含新增
  per-source 断言）。

### F04 [ADVANCED]
Status: DONE
Reviewer: PASS
Depends: F03

Acceptance:
- 逐频 product state 变为 per-(frequency, source)：`solveFrequency`
  （TL/A/a/E 各 solver）以 `(sourceIndex, cache)` 配对消费；per-source
  的 `sourceSoundSpeed`、Lloyd/semicoherent 源项、epsilon 输入、
  `.sbp` pattern amplitude 全部取自当前 source（F2CPP 同构）。
- 三模式适配：`nonreuse` 每频每源独立 trace；
  `reuse` 全部 source 各 trace 一次后跨频复用；
  `parallel` 频率 worker 只读共享 cache vector，serial ordered consumer
  按 frequency index 发布 per-source workspace 序列。
- `ArrivalWorkspace`/Eigenray hits 为 frequency-local 不变；无逐频状态
  写回 cache（`--verify-cache` 语义扩展为 per-source fingerprint 校验）。
- statistics 按冻结语义（§1.5）输出 `tracePassCount`。
- 组件测试：单源三模式 byte-identity 不回归；双源 case 三模式
  per-frequency 产品一致。

Evidence:
- `single_frequency_solver.hpp/.cpp`：新增
  `solveFrequencyFromSourceCache(simulation, frequency, rayCache,
  sourceIndex, ...)`——per-source `sourceSample`（depth 取当前 source）、
  `source.amplitude × pattern`、Lloyd `source.depth/sourceSoundSpeed`、
  `pickBeamEpsilon` 的 `sourceSoundSpeed/gradient` 全部取
  `sources()[sourceIndex]`（对照 F2CPP `single_frequency_solver.cpp`
  per-source 外层循环逐处确认）；`(sourceIndex, cache)` 配对结构性校验
  （cache 首 ray 首点 depth 必须等于当前 source depth，cache schema
  零改动）。`solveFrequencyFromCache` 保留为首源等价入口。
  `SingleFrequencyResult` 增加 F2CPP 同构 `additionalSourceWorkspaces`/
  `sourceCount()`/`sourceWorkspace(i)`；`solveAtFrequency` 改为
  `traceAllSourceFans` 聚合（rayCount=Σ、totalRayPointCount=Σ、
  rayCacheBytes=peak per-source、timings=Σ）。
- serial/parallel reuse solver：`traceAllSourceFans` 一次（cache vector
  归编排层所有）；`RayReuseFrequencyConsumer` 变为
  `(frequencyIndex, vector<FrequencyWorkspace>&&, timings)` per-source
  序列发布（parallel 为 serial ordered consumer，worker 只读 const
  cache vector）；statistics `tracePassCount=NSz`、`rayCount/totalRayPointCount=Σ`、
  `rayCacheBytes=Σ per-source`、parallel 内存估计
  `estimatedWorkspaceBytes=NSz×per-source`；
  `--verify-cache` per-source fingerprint vectors（scalar 字段保留
  source-0 值，NSz==1 输出不变）。
- arrival/eigenray solver：consumer 变为
  `(frequencyIndex, vector<RayPathCache>&, vector<ArrivalWorkspace>&)` /
  `(..., vector<EigenraySourceHits>&)`；三入口（solve/solveNonReuse/
  solveParallel）全部 per-source（nonreuse 每频重 trace 全部 source，
  per-source fingerprint 跨频一致性检查保持）；diagnostic 对齐 F2CPP
  "at source N, launch M"。ArrivalWorkspace/hits 保持 frequency-local。
- `ray_trace_product`：新增 `traceRayProducts`（NSz 个 frozen cache，
  per-source 诊断含 source index）；`traceRayProduct` 保留首源入口。
  R 单源行为 byte-identical。
- `app/main.cpp`：TL/A/a/E/R 全部分支消费 per-source 数据；NSz==1
  输出与改动前逐字节一致（writer 层 per-source 块为 F06：多源时
  产品暂写首源块，per-source 数据已在 solver 层就绪）；PRT 多源时
  追加 per-source fingerprint 行与 `Trace passes = Nfreq×NSz / NSz`
  （§1.5 冻结语义）。
- 组件测试：新增 `tests/component/multi_source_product_test.cpp`
  （`rayreuse.component.multi_source_product`）：双源（乱序输入→升序）
  TL（Cerveny Cartesian SemiCoherent + directional SBP，覆盖 per-source
  Lloyd/epsilon/pattern）三模式 per-(frequency, source) bitwise 一致、
  trace passes 4/2/2、per-source fingerprint before==after 且互异、
  每源 workspace == 同深度单源 run（fan 角度集相同性显式断言）；
  双源 A（GeoHat+SBP）/E（GeoGaussian）三模式一致 + 每源等于单源
  参照；R 双源 per-source cache == 单源 R trace fingerprint，NSz==1
  legacy 入口 == entry 0。既有 serial/parallel/arrival/eigenray/
  broadband/single-frequency 测试适配新 consumer 后全部通过
  （单源三模式 byte-identity 不回归）。
- NSz==1 冻结基线：munk_spline broadband（50/250 Hz，5000-ray fan）
  三模式亲跑：reuse/parallel fingerprint before==after
  `1526667602348633172`；nonreuse/reuse/parallel SHD 两两 byte-identical，
  SHA-256 前缀 `74028065178ff80d`（与 F03 基线一致）；Trace passes
  `2/1/1`。双源端到端 smoke（munk 双源 900/1000 m）：CC/AG/EG 三模式
  产品 byte-identical、Trace passes `4/2/2`、ray count `10000`
  （NSz×fan）、per-source fingerprint 行输出、R 双源运行通过
  （1000 m source 恰复单源同深度 fingerprint）。
- `Bellhop_RayReuse/build/fp2f-clean`（Release，
  `RAYREUSE_WARNINGS_AS_ERRORS=ON`）全量 CTest 38/38 通过（37 既有 +
  1 新增）；`git diff --check` 干净；`src/cache`/`include/rayreuse/cache`/
  `include/rayreuse/ray`/`src/ray` 零改动（fingerprint 算法不变）。

### F05 [ADVANCED]
Status: DONE
Reviewer: PASS
Depends: F01

Acceptance:
- Cartesian 影响族（`cartesian_cerveny_influence`、
  `geometric_hat_influence` Cartesian 分支、
  `geometric_gaussian_influence`）与 Cartesian G/B 的 Arrival/Eigenray
  traversal：receiver 深度取值从 `depths()[depthIndex]` 改为
  `depthAt(depthIndex, rangeIndex)`；workspace 维度按
  `receiversPerRange() × rangeCount` 构造。
- ray-centered 路径与 SimpleGaussian 在 irregular 下按 F02 拒绝，
  数值路径零改动。
- 组件测试：paired 寻址（每 range 取对应 depth）；rectilinear 行为
  逐字节不变（现有 influence 组件测试全绿）。

Evidence:
- **Frozen-decision deviation（CC，对齐 reference 语义）**：施工前核对
  reference 发现 Origin `InfluenceCervenyCart`（`Bellhop_origin/Bellhop/
  influence.f90:318-319`）irregular 时 `RcvrDepths: DO iz = 1,
  NRz_per_range` 读 `Pos%Rz(iz)`，即恒取 `Rz(1)` 而非 paired `Rz(ir)`；
  F2CPP `cartesian_cerveny_influence.cpp:596-603` 显式保留该 legacy
  （`irregularReceiverDepth = receiverDepths.front()`，含同源注释）。
  Origin `InfluenceGeoHatCart`（influence.f90:601-605）与
  `InfluenceGeoGaussianCart`（influence.f90:719-723）则真用 paired
  `Rz(ir)`。按 §0 优先级（Origin/F2CPP 科学语义优先于 Worklist 措辞），
  RayReuse CC 采用与 F2CPP/Origin 逐字节同构的 `Rz(1)` 语义
  （含 F2CPP 同源注释）；GeoHat/GeoGaussian Cartesian 按 paired
  `depthAt(depthIndex, rangeIndex)` 施工。若 CC 也用 paired，F07 的
  `irregular_receiver_pairs`（TL CC）三方 0-diff oracle 必然失败。
- `src/field/frequency_workspace.cpp`（`FrequencyWorkspace`/
  `IntensityWorkspace`）与 `src/field/arrival_workspace.cpp`
  （`ArrivalWorkspace`）：`depthCount_` 构造从 `receivers.depthCount()`
  改为 `receivers.receiversPerRange()`（F2CPP `frequency_workspace.cpp`/
  `arrival_workspace.cpp` 同构；rectilinear 时二者相等，行为不变；
  F04 的 per-(freq, source) 维度逻辑不受影响——构造入口不变）。
- `src/field/pressure_scaling.cpp`：两处 workspace↔receiver 维度校验改
  `receiversPerRange()`（F2CPP `pressure_scaling.cpp:43/146` 同构）。
- `src/field/cartesian_cerveny_influence.cpp`：
  `validateAccumulateInput`/`validatePrevalidatedInput` 的 workspace 维度
  与 diagnostic depth-index 上限改 `receiversPerRange()`（F2CPP
  `:164/:222` 同构）；`accumulateImpl` 热循环 depth 界改
  `receiversPerRange`，receiver 深度 = irregular ? `Rz(1)` :
  `receiverDepths[depthIndex]`。
- `src/field/geometric_hat_influence.cpp`：`validateField` 维度/diagnostic
  校验改 `receiversPerRange()`；Cartesian 分支
  `accumulateField`/`accumulateArrivals`/`collectEigenrayHits` 三处
  depth 界改 `receiversPerRange()`、深度取
  `depthAt(depthIndex, receiverIndex)`（F2CPP
  `geometric_hat_influence.cpp:501-504` 同构）；ray-centered 路径
  （`forEachRayCenteredEvaluation` 与 RayCentered 分支）零改动。
- `src/field/geometric_gaussian_influence.cpp`：`validateField` 校验 +
  Cartesian `accumulateField`/`accumulateArrivals`/`collectEigenrayHits`
  三处同上（F2CPP `geometric_gaussian_influence.cpp:515-518` 同构）。
- `src/solver/parallel_ray_reuse_solver.cpp` `workspaceBytes` 内存估计改
  `receiversPerRange()`（irregular 下估计不失真）。
- ray-centered cerveny / SimpleGaussian influence 零改动（irregular 由
  F01 model + F02 parser 拒绝；`git diff` 不含这两个文件）。
- 组件测试（新增 `tests/component/irregular_receiver_influence_test.cpp`，
  `rayreuse.component.irregular_receiver_influence`，CMake 注册）：
  workspace 维度（irregular 1×N / rectilinear depths×N）、GeoHat/GeoGaussian
  paired 寻址（可区分深度 {400,500,600}，仅 on-axis paired 列非零，
  且与 rectilinear 对角线逐位相等）、GeoHat irregular 下 diagnostic
  depth-index≥receiversPerRange 拒绝、GeoHat A/E paired traversal
  （arrival candidate 与 eigenray hit 仅落 paired 列）、CC `Rz(1)` 语义
  （irregular 51 对 == 首深度单行 rectilinear 逐位相等，且 ≠ 中位深度行）、
  solver 级 TL parity（`solveAtFrequency` 全链路 trace→project→influence→
  scale：CG/CB irregular == rectilinear 对角线逐位相等；CC irregular ==
  `Rz(1)` 单深度 rectilinear 逐位相等且对角线相异）。
- **Rectilinear 逐字节不变（硬 gate）**：`build/fp2f-clean` clean
  rebuild（Release，`RAYREUSE_WARNINGS_AS_ERRORS=ON`）全量 CTest
  39/39（38 既有 + 1 新增，含全部既有 influence/solver/writer/parser
  回归）；munk_spline broadband（50/250 Hz，5000-ray fan）三模式亲跑：
  nonreuse/reuse/parallel SHD 两两 byte-identical、SHA-256 前缀
  `74028065178ff80d`（= F03/F04 冻结基线）、Trace passes `2/1/1`、
  reuse/parallel `--verify-cache` fingerprint before==after
  `1526667602348633172`（冻结值不变）。
- **F02 finding 过渡窗口关闭验证**：`irregular_receiver_pairs`（CC RI）
  与 `arrival_geometric_gaussian_irregular`（AB RI）端到端亲跑：PRT 打
  `Irregular grid: paired receiver ranges and depths`，数值层按本 task
  语义计算；SHD/ARR writer 显式拒绝（"SHD workspace dimensions must
  match the receiver grid" / "ARR workspace metadata does not match
  simulation"）而非静默写出 rectilinear 交叉积——writer 布局属 F06。
  均匀 paired irregular == rectilinear 的数值等价已由上述 solver 级
  组件测试逐位证明；非均匀 paired 与 Origin 抽验留 F07 oracle。

### F06 [ADVANCED]
Status: DONE
Reviewer: PASS
Depends: F04, F05

Acceptance:
- SHD：header 写 `NSz` 与 `Sz` 向量；`PlotType` 按 layout 写
  `irregular `/`rectilin  `；NRz header = depths 向量长度（对齐
  F2CPP/Origin）；record 布局 = header + frequencyIndex ×
  (NSz × receiversPerRange) + sourceIndex × receiversPerRange +
  depthIndex。单频 `NSz>1` SHD 与 F2CPP byte-identical。
- ARR（A/a，per-frequency 产品文件）：header 写 source count + 每 source
  depth（binary record / ASCII 行，对齐 F2CPP `writeBinaryHeader`/
  ASCII header）；文件体内 per-source 块顺序 = source depth 升序；
  per-source 块内 cell 遍历 = receiversPerRange × rangeCount。
- E RAY（per-frequency 产品文件）：header/source count 与 per-source
  段落对齐 F2CPP `eigenray_writer`（ASCII `1 1 NSz`；binary count
  record）。
- R RAY（单频）：per-source 块 + header `1 1 NSz`，对齐 F2CPP
  `ray_writer` 与 Origin `WriteRay`。
- 全部 writer：`NSz==1` 且 rectilinear 时输出与改动前 byte-identical
  （冻结回归 gate）。

Evidence:
- **SHD**（`src/io/shd_writer.cpp`/`include/rayreuse/io/shd_writer.hpp`）：
  header record 3 offset 16 写 `NSz`（原硬编码 1），record 8 写
  `Sz(1:NSz)` float32 向量；`PlotType`（record 2）按
  `receivers.isIrregular()` 写 `irregular `/`rectilin  `；NRz header 与
  receiver-depth record 保持 `depths()` 向量全长（F2CPP/Origin 同构，
  irregular 时 NRz==NRr）。pressure record 寻址 = header +
  freqIndex × (NSz × receiversPerRange) + sourceIndex ×
  receiversPerRange + depthIndex（Origin
  `IRec = 10 + NRz_per_range*(is-1)` 的多频推广）；每频率 record 数 =
  NSz × receiversPerRange（irregular 时 = NSz）。workspace 维度校验改
  `receiversPerRange()`（F05 临时拒绝移除）。recordWords 取
  max(41, 2Nfreq, 2, 2, 2, NSz, NRz, 2NRr)（Origin `WriteHeader`
  LRecl 公式同构）。API：`ShdFrequencyWriter::writeFrequency` 新增
  F2CPP 同构 `(first, additional)` 与 span 双入口，legacy 单 workspace
  入口对 NSz>1 显式拒绝（"SHD source workspace count must match the
  simulation sources"）；`ShdWriter::writeSingleFrequency` 增加 5 参
  F2CPP 同构重载，`writeFrequencies` 增加 per-frequency×per-source
  vector 重载（broadband nonreuse 路径）。
- **ARR**（`src/io/arrival_writer.cpp`）：ASCII header 第 3 行 =
  `NSz Sz(1) ... Sz(NSz)`（Origin `Pos%NSz, Pos%Sz(1:NSz)` 单行）；
  binary header source record = count(int32) + NSz×float32 depths
  （F2CPP `writeBinaryHeader` 同构，NSz==1 时与原 8 字节 record
  byte-identical）；文件体 per-source 块（depth 升序 = workspace 索引
  序），块内 cell 遍历 `receiversPerRange × rangeCount`，每块一个
  maximum-count record（binary）/行（ASCII）。`ArrivalWriter::write`
  新增 span per-source 入口；legacy 单 workspace 入口对 NSz>1 拒绝。
- **E**（`src/io/eigenray_writer.cpp`）：header 第 3 行 `1 1 NSz`
  （Origin `Pos%NSx, Pos%NSy, Pos%NSz`）；body per-source hit 段落
  （source 升序，段内 launch 序非降）；receiver index 上限校验改
  `receiversPerRange()`（F2CPP `appendHit` 同构）。F2CPP
  `eigenray_writer` 为 ASCII-only（Origin E 产品 = FORMATTED .ray），
  无 binary E schema，故 "binary count record" 语义落在 ARR binary
  per-source count record（上条已实现）。
- **R**（`src/io/ray_writer.cpp`）：header `1 1 NSz`；新增
  `appendSource(sourceIndex, cache)`（顺序强制 = sources() 升序，
  finalize 要求全部 source；per-source amplitude 取
  `sources()[i].amplitude × pattern`）；`append` 保留为 legacy
  = `appendSource(0, ·)`。
- `app/main.cpp`：R/ARR/E/SHD 全部分支改 per-source 写出，移除
  "只写首源" 过渡逻辑；E 的 per-frequency hit count 改跨 source 求和
  （NSz==1 输出不变）。
- **Worklist 措辞注记（自包含表述）**：`PlotType` 字段宽度以 reference
  为准——Origin `PlotType` 为 `CHARACTER (LEN=10)`
  （`ReadEnvironmentBell.f90:28`，值 `'rectilin  '`/`'irregular '`），
  F2CPP `storeText(record, 0U, 10U, ...)` 同为 10 字符；据此 SHD header
  的 `PlotType` 采用 10 字符字段（`irregular `/`rectilin  `），padding
  与 F2CPP 逐字节对齐。
- 组件测试（新增 `tests/component/multi_source_writer_test.cpp`，
  `rayreuse.component.multi_source_writer`，11 个测试函数）：SHD 双源
  header/record 布局（NSz=2、Sz 向量、source-major 寻址逐字段）、双频×
  双源频率主序堆叠、irregular（PlotType=`irregular `、NRz=3、每源 1
  条 paired record、paired 值逐字段）、NSz==1 byte-identity（legacy
  4 参 vs 5 参空 additional vs streaming 单/span 入口互为 byte-equal，
  及单源 sim + additional 的拒绝）；ARR ASCII（`2 30 70` header 行、
  双块 cell 计数）与 binary（record 布局 344 字节逐字段、source record
  count+depths）、NSz==1 legacy vs span byte-equal、source 数不匹配
  拒绝；R 双源（header `1 1 2`、首源块与同深度单源产品逐行一致）、
  appendSource 顺序/重复/未完成 finalize 拒绝、NSz==1 append vs
  appendSource byte-equal；E 双源（header `1 1 2`、source-major hit
  段落、cache 数不匹配拒绝）、NSz==1 legacy vs span byte-equal。
- `Bellhop_RayReuse/build/fp2f-clean`（Release，
  `RAYREUSE_WARNINGS_AS_ERRORS=ON`）全量 CTest 40/40 通过（39 既有 +
  1 新增，全部既有 writer/solver/parser 回归零变化）。
- **NSz==1 rectilinear 冻结基线（硬 gate）**：munk_spline broadband
  （50/250 Hz，5000-ray fan）三模式亲跑：nonreuse/reuse/parallel SHD
  两两 byte-identical 且与冻结基线文件 byte-identical，SHA-256 前缀
  `74028065178ff80d`；Trace passes `2/1/1`；reuse/parallel
  `--verify-cache` fingerprint before==after
  `1526667602348633172`。
- **本地 A/B vs F2CPP（`Bellhop_F2CPP/build/release/bellhop_f2cpp`，
  2026-08-27 构建）单频双源逐字节对比**（F2CPP/Origin 兼容 env，
  Munk CC / isovelocity R/AG/aG/EG，源深 900/1000 与 35/65）：
  `tl.shd` 331,248 B、`r.ray` 302,895 B、`a.arr` 9,246 B、
  `abin.arr` 6,744 B、`e.ray` 4,734,108 B、双源 paired irregular
  （`CC RI`，5 对）`irr.shd` 1,968 B —— 六个产品全部 0 差异。
  （仓库 `Bellhop_F2CPP/build/bellhop_f2cpp` 为 8 月 14 日旧二进制，
  不含 F2CPP A/E 支持，A/B 使用 `build/release`。）
- **多源宽带三模式一致**（RayReuse，50/250 Hz 双源）：TL SHD 三模式
  byte-identical（654,456 B）；A/E 两频各 3 文件三模式 byte-identical；
  Trace passes `4/2/2`（§1.5 冻结语义）。

### F07 [STANDARD]
Status: DONE
Reviewer: N/A
Depends: F06

Acceptance:
- 启用既有共享 case 的 RayReuse allow-list（compatibility 加
  `rayreuse`）：`multi_source_depths`（TL CC NSz=3）、
  `irregular_receiver_pairs`（TL CC paired）、`ray_trace_vacuum_rigid`
  （R 双源）、`eigenray_geometric_hat`（E 双源）、
  `arrival_geometric_gaussian_irregular`（A paired）。
- 新增最小共享 case（Origin/F2CPP/RayReuse 三方，沿用 FP-2A minimal
  companion 模式）：`arrival_multi_source`（A ASCII + 最小 a binary
  companion；GeoHat Cartesian、双源）；`eigenray_irregular_pairs`
  （E + paired irregular；GeoHat 或 GeoGaussian Cartesian）。
- 三方 validator 通过：RayReuse↔F2CPP 0 差异；Origin↔RayReuse 沿用
  既有 tolerance（不放宽）；`shd_dimensions` /
  `arrival_receiver_cell_count` / PRT markers 按 case.toml 校验。

Evidence:
- **Allow-list 启用（5 个既有 case）**：`multi_source_depths`、
  `irregular_receiver_pairs`、`ray_trace_vacuum_rigid`、
  `eigenray_geometric_hat`、`arrival_geometric_gaussian_irregular` 的
  `case.toml` `compatibility.versions` 均加 `rayreuse`（五者均为既有
  目录，特性与 Worklist 假设一致，无调整）。
- **新增共享 case（3 个）**：`arrival_multi_source`（`AG RR`，GeoHat
  Cartesian ASCII A，双源 35/65 m，3×4 rectilinear receiver grid）、
  `arrival_multi_source_binary`（`aG RR`，binary a 双源 twin）、
  `eigenray_irregular_pairs`（`EG RI`，GeoHat Cartesian E + paired
  irregular 4 ranges×4 depths，单源 50 m）。均含 `origin.env.in` +
  `case.toml`，coverage.toml 注册（`arrival`/`eigenray` set），i8
  validator 矩阵纳入。
- **PRT marker 处理（reviewer LOW finding 闭环）**：
  `multi_source_depths` 的 `source depths = 3` 以
  `version_prt_markers = { f2cpp = [...], rayreuse = [...] }` 声明——
  亲验 Origin PRT 无该 marker（Origin 以
  `Number of Source z-coordinates, Sz` echo 指针输出），放公共
  `prt_markers` 会破坏 origin leg；`irregular_receiver_pairs` 的
  `prt_markers` 已含 `Irregular grid`，rayreuse PRT 亲验命中。
  `standard_cases.py` 新增 `validate_print_output(..., version)`
  version-scoped marker 校验 + 单元测试覆盖。
- **F06 "12 字符字段" doc nit**：worklist F06 Evidence 已为自包含
  表述（`PlotType` `CHARACTER (LEN=10)`，Origin
  `ReadEnvironmentBell.f90:28` + F2CPP `storeText(..., 10U, ...)`
  双 reference 引用），全仓 doc/standard_cases grep 无残留悬空
  "12 字符" 字样，技术裁定不变。
- **三方 validator（全部 PASSED；rayreuse =
  `build/fp2f-clean/bellhop_rayreuse`，sha256 `fe3e6153…`）**：
  - `validate_i6_multi_source`（multi_source_depths，NSz=3）：origin↔
    rayreuse single/1000Hz max |Δp| 2.049e-08、max TL diff 3.81e-05 dB
    （与 origin↔f2cpp 同 metric 同 tolerance，未放宽）；f2cpp↔rayreuse
    decoded payload exact（13464 B == 13464 B）；source depths 向量
    (20,50,80) exact、pressure shape (1,3,11,51) exact；per-source
    slice guards 7 项。
  - `validate_i6_ray_trace`（ray_trace_vacuum_rigid，R 双源）：f2cpp↔
    rayreuse 与 origin↔rayreuse max coordinate error 均 0.0 m（10
    rays、5934 points、tol 1e-07 m/1e-10），三方 semantic sha256
    一致（`4f6878eb…`）。
  - `validate_i6_irregular_receivers`（irregular_receiver_pairs，
    `CC RI`）：f2cpp↔rayreuse decoded payload exact（40 B，single +
    broadband 两切片）；origin↔rayreuse max |Δp| 3.79e-09、max TL
    diff 3.81e-06 dB；irregular header axes/record shape exact。
  - `validate_i8_arrivals`：origin↔rayreuse 与 f2cpp↔rayreuse 8 个
    case（含 `arrival_multi_source` 162 records、
    `arrival_multi_source_binary` 162、
    `arrival_geometric_gaussian_irregular` 335）全字段 0 ULP；三实现
    A/a 与 Ag/ag、A/a 多源 encoding pair 对比全过。
  - `validate_i8_eigenrays`：origin↔rayreuse 与 f2cpp↔rayreuse 5 个
    case（含 `eigenray_irregular_pairs`）max coordinate error 0.0 m；
    双源 header `multi_source_header_count = 2` guard 通过。
- **case.toml 维度/cell 校验**：runner 对 rayreuse leg 强制
  `shd_dimensions`（multi_source_depths `[1,1,1,1,3,11,51]`、
  irregular_receiver_pairs `[1,1,1,1,1,5,5]`）与
  `arrival_receiver_cell_count`（irregular A case = 3，既有约定仅
  irregular 声明）；PRT markers（含 version-scoped）同被 runner 与
  单元测试强制。
- **测试**：`make -C test/standard_cases test-unit` 159 tests OK；
  `uv run pytest`（配置内全部 suite）174 passed + 374 subtests；
  `eigenray_irregular_pairs` broadband_smoke 三方 runner 冒烟通过
  （该 profile 三方均可运行，F08 三模式一致性仍属 F08 scope）。
- **施工期机械修复（共享测试基建，无 production 改动）**：
  1. `arrival_multi_source_binary/origin.env.in` run type
     `'ag RR'`→`'aG RR'`（本 Origin fork `g`=GeoHat ray-centered、
     `G`=GeoHat Cartesian；原值与声明的 beam family/PRT marker/
     A-vs-a encoding pair 矛盾）。
  2. `validate_i6_multi_source.py`：rayreuse leg 的 reader 循环与
     single/broadband 切片对照检查按 `RAYREUSE_PROFILES`（=single）
     收口，修复 `broadband_smoke` KeyError（broadband 三方对照属
     F08）；`generation_commands` 对 rayreuse 仅输出
     `RAYREUSE_PROFILES` 的命令。

### F08 [STANDARD]
Status: DONE
Reviewer: N/A
Depends: F07

Acceptance:
- 新启/新增 case 的两频 broadband profile：`nonreuse/reuse/parallel`
  每频产品（SHD/A/a/E）逐字节一致；R 保持单频。
- `reuse/parallel` per-source cache fingerprint before/after 相同；
  PRT Trace passes 符合冻结语义（两频双源 = `4/2/2`）。
- 既有冻结基线不变：C/P/N/S probe SHA、C/P/N/S broadband SHD 基线、
  `munk_spline` fingerprint `1526667602348633172`、Q case fingerprint
  `2879552213476552188`、既有 single-source 三模式 SHD 基线。

Evidence:
- 全部亲跑使用 `build/fp2f-clean/bellhop_rayreuse`（CTest 40/40 复核通过）；
  环境由 `standard_cases.py generate`（共享 fan）生成，三模式在独立目录
  运行（reuse/parallel 加 `--verify-cache`），逐频产品两两 `cmp` +
  SHA-256 对比。
- **broadband profile 补齐（3 个 case.toml）**：`arrival_multi_source`、
  `arrival_multi_source_binary`（500/1000 Hz，对齐既有 A twin 惯例）、
  `eigenray_geometric_hat`（500/1000 Hz，对齐
  `eigenray_geometric_gaussian`/`eigenray_irregular_pairs`）；
  `multi_source_depths`（1000/2000）、`irregular_receiver_pairs`
  （1000/2000）、`eigenray_irregular_pairs`（500/1000）沿用既有
  `broadband_smoke`。
- **三模式逐字节一致（6 case，10 个产品文件全 PASS）**：
  - `multi_source_depths`（TL CC NSz=3）：SHD
    `0e62bbe0cd0082ce…8635d2a`；
  - `irregular_receiver_pairs`（TL CC paired）：SHD
    `09c2a03ced7baa32…c39ec5`；
  - `arrival_multi_source`（A ASCII NSz=2）：f000_500Hz
    `687ea7c07a77e858…053d661`、f001_1000Hz
    `bcbcb2663b0e5931…dc26288`；
  - `arrival_multi_source_binary`（a binary NSz=2）：f000_500Hz
    `26e21008e212cf75…848c17f`、f001_1000Hz
    `e571697b8221df0f…8778461`；
  - `eigenray_irregular_pairs`（E paired NSz=1）：f000_500Hz
    `1a7e6d6900e92c2a…72583666`、f001_1000Hz
    `89d99664981b9b92…c27a93f`；
  - `eigenray_geometric_hat`（E NSz=2）：f000_500Hz
    `70f123d2f0ba7fdf…02a6c3f9`、f001_1000Hz
    `852cfd6de35c4d32…031bbb9`。
- **Trace passes 冻结语义（§1.5）**：两频双源 case
  （`arrival_multi_source`/`arrival_multi_source_binary`/
  `eigenray_geometric_hat`）= `4/2/2`；两频三源 `multi_source_depths` =
  `6/3/3`；两频单源（`irregular_receiver_pairs`/`eigenray_irregular_pairs`）
  = `2/1/1`；单源 `munk_spline` = `2/1/1`。
- **per-source fingerprint before==after（reuse/parallel 均
  `--verify-cache`）**：`multi_source_depths` 三源
  `3625827730432409847`/`12792067103283003378`/`5610685649423155114`
  （互异）；三个双源 case 同环境共享 fan，双源 fingerprint
  `14945116410443199622`/`11432133048555655010`（互异、跨 case 一致）；
  单源 irregular `18405272883869031402`、`eigenray_irregular_pairs`
  `17096228715590727446`。全部 before == after。
- **R 保持单频**：`ray_trace_vacuum_rigid` 单频 250 Hz 双源
  （25/75 m）亲跑通过，R 产品 SHA-256
  `04a966cadd65e298d4f2cb910948f8b5f96169807d53cdd103f0e26b7c877ce8`；
  默认调用与 `--execution-mode nonreuse` byte-identical；
  `--execution-mode reuse/parallel` 被 CLI 显式拒绝
  （"--execution-mode reuse/parallel is not defined for R products"）。
- **runner 适配（共享测试基建）**：`standard_cases.py` 两个 broadband
  PRT 校验的期望 Trace passes 按 §1.5 冻结统计改为
  `Nfreq×NSz / NSz`；`case.toml` 新增可选
  `validation.source_depth_count`（默认 1；四个多源 case 声明
  3/2/2/2，0/负值拒绝）。runner `test --version rayreuse --profile
  broadband_smoke` 六 case 全部 PASSED；单源 munk 路径回归 PASSED。
- **测试**：`make -C test/standard_cases test-unit` 163 tests OK
  （159 + 4 新增）；`uv run pytest` 178 passed（174 + 4 新增，含
  subtests）；新增断言覆盖多源 Trace statistics 接受/拒绝与
  `source_depth_count` 解析。
- **冻结基线对照（硬 gate，全部不变）**：
  - C/P/N/S probe SHA（`geometry_oracle_probe`，0.0125 rad 亲跑）：
    C `809b126d4b2657b8…98492e2`、P `eb51ced19656a772…1c646a0`、
    N `360dda437550e396…ddbcc00be8`、S
    `1fd0e4f84391aa24…a7faad63` — 全 match；
  - C/P/N/S broadband SHD（runner `test` 亲跑 product SHA-256）：
    C `cf1f9711aefcab08…5fd126bc`、P `fd5b2e2cf77a524e…0d85cde`、
    N `18817c6788b6e7a4…c27d29c4`、S `74028065178ff80d…eef1596` —
    全 match；
  - `munk_spline` reuse/parallel `--verify-cache` fingerprint
    `1526667602348633172` before==after；三模式 SHD 均
    `74028065…`、Trace passes `2/1/1`（single-source 三模式基线不变）；
  - Q case（`q_range_dependent_cross_gradient` 1000/2000 Hz）
    reuse/parallel fingerprint `2879552213476552188` before==after；
  - 无三模式不一致，无冻结基线漂移，无 blocker。

### F09 [SIMPLE]
Status: DONE
Reviewer: N/A
Depends: F08

Acceptance:
- `REFERENCE_FEATURE_SUPPORT_MATRIX.md`（RayReuse）与 parity report
  增量更新：SRC-04/REC-02/REC-03/PRD-06/PRD-07 关闭范围 = 本 Worklist
  §0 声明的 oracle-validated 组合；line source 仍 GAP；
  机制可达但未验证组合（multisource × Q / ray-centered 等）明确不声明。
- `STATUS_PROGRESS.md` 更新批次状态。

Evidence:
- `doc/reference/REFERENCE_FEATURE_SUPPORT_MATRIX.md`：头部更新日期加
  "FP-2F 更新：2026-08-29"、适用范围行改为 point-source（含多 source
  depths）/rectilinear 与 Cartesian paired-irregular；Fully supported 表新增
  「多源（multisource / NSz ≥ 1）」与「Cartesian irregular receiver」两行
  （含 CC/IC/SC 恒取 `Rz(1)` legacy 说明、trace passes `Nfreq×NSz / NSz / NSz`、
  multisource × Q / multisource × ray-centered / irregular × Q 不声明）；
  R/Arrivals/Eigenray 行补 per-source 块与 paired 范围（ray-centered `g`
  保持 single-source/rectilinear）；SSP 行尾注改为 line source、3D 与
  N×2D 不属范围 + `Q` × multisource/irregular 不声明；状态所有权行改
  per-source frozen fan cache；并行行补 worker 只读 per-source cache
  vector 与 per-source workspace 序列发布；Deferred 移除 irregular
  receiver 与 multisource parity（line source 保留）。
- `doc/reports/REPORT_F2CPP_RAYREUSE_PARITY_2026-08-25.md`（沿用 FP-2E
  增量更新模式，见 `74f32c6`）：头部加 "FP-2F 实现验证更新：2026-08-29"
  与实施前基线 `5a71221d2a10ee68b8ea7666b9c11a65e718fe7f`；导语与 §1 结论
  摘要/SimulationCase 边界 bullet 改写；§3 `R-PARSER`/`R-MODEL`/`R-TL`/
  `R-PRODUCT`/`R-CLI`/`MATRIX`/`TEST` 标签同步并新增 `FP2F-ORACLE`
  （八 oracle case 数字全部取自 F07/F08）；§4 SRC-04、REC-02、REC-03、
  PRD-06、PRD-07 由 `ARCHITECTURAL_CONFLICT` 改 `PARITY`（注明关闭范围、
  `Rz(1)` legacy 语义与 oracle case），PRD-01/02/03/04 范围注记与 TL-01/
  TL-07 排除注记、DOC-01 同步；§5 三执行模式表补 FP-2F validated slice
  与 per-source fingerprint；§6 标题改 FP-1A～FP-2F 并追记 FP-2F 验证
  记录（5 条 bullet）；§7 GAP 列表移除 SRC-04/REC-02/REC-03/PRD-06/
  PRD-07（line source 保留为第 1 项）、P0 清空、后续阶段状态与更新状态
  追记 FP-2F 段。
- `doc/status/STATUS_PROGRESS.md`：头部「当前状态」改为 FP-2F 完成施工
  与批次验证、待独立 Final Review（不写 ACCEPTED）；已完成范围表新增
  FP-2F 行；「下一步」指向 FP-2F worklist 并归位 FP-2E/FP-2D 历史状态。
- 声明与 Worklist evidence 一一对应核对：oracle case 名（8 个）、三方
  validator 数字（`2.049e-08`/`3.81e-05 dB`、`3.79e-09`/`3.81e-06 dB`、
  `0.0 m`、0 ULP、162/335 records、SHD dims）、trace passes `6/3/3`、
  `4/2/2`、`2/1/1`、CTest 40/40、pytest 178、unittest 163、冻结基线
  （C/P/N/S probe/broadband SHD、`1526667602348633172`、
  `2879552213476552188`、`74028065…`）均出自 F01–F08 evidence 与本
  Worklist §0/F05 `Rz(1)` 冻结决定；未新增 evidence 之外的声明。

## 3. Batch Acceptance Gate（冻结清单）

1. 隔离 Release clean build + 全量 CTest 通过。
2. `uv run pytest`、`make -C test/standard_cases test-unit` 全量通过。
3. §F07 三方 validator 全部通过（tolerance 未放宽）。
4. §F08 三模式 byte-identity + per-source fingerprint + 既有冻结基线
   全部不变。
5. 所有 ADVANCED task reviewer PASS；remediation 闭环完成。
6. final-reviewer `ACCEPTED`。
7. `git diff --check` 干净；`Bellhop_origin/`、`Bellhop_F2CPP/` 零改动；
   无生成产品入库。

## 4. Blockers / Findings

- 无（DESIGN 阶段）。
