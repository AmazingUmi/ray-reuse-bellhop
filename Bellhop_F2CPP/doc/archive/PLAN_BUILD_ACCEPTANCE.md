# Bellhop F2CPP 构建与验收计划

> 规划日期：2026-07-27  
> 适用范围：`Bellhop_F2CPP` 的 M1（组件级单频）与 M2（端到端单频）  
> 依据：仓库根目录的 [01 设计文档](../../../doc/architecture/ARCHITECTURE_BELLHOP_RAY_REUSE.md)、
> [02 实施待办](../../../doc/archive/PLAN_PROJECT_IMPLEMENTATION_2026-08-14.md)、
> [04 数值规范](../../../doc/reference/REFERENCE_NUMERICAL_CONVENTIONS.md)、原二维 Fortran
> 构建链和共享标准算例

## 1. 目标与完成边界

本计划只建设独立的 C++20 单频程序 `bellhop_f2cpp`，不提前实现
`Bellhop_RayReuse` 的多频调度和并行。

完成时必须同时满足：

1. `Bellhop_F2CPP/` 是可独立配置、构建和测试的 CMake 工程；
2. 只支持二维、rectilinear 接收网格、coherent complex pressure、
   Cartesian Cerveny `CC` 和 pressure 分量；
3. 一次运行只接受一个频率，但输入模型保留 `frequencies` 容器；
4. 追踪阶段构造完整 `RayPathCache`，其中包含每点几何/动态状态、每步
   `StepQuadrature`、全部 `ReflectionEvent` 和终止原因；
5. 追踪完成后才进行
   `RayPathCache → RayFrequencyState → FrequencyWorkspace`；
6. 逐频走时、吸收、反射幅相、active 状态和压力不写回 `RayPath`；
7. 六个共享单频算例均能由统一测试入口运行，并按复压力而不是只按 TL
   通过 Fortran oracle 对比；
8. 输出 SHD 可由
   [`test/PlotRead/bellhop_io_py/`](../../../test/PlotRead/README.md) 读取；
9. 建立单线程耗时、峰值内存和 `RayPathCache` 大小基线；
10. M2 验收点可作为以后复制/派生 `Bellhop_RayReuse` 的稳定快照。

明确不在本计划中实现：

- 多频一次运行、ray reuse 调度和频率并行；
- 3D、N×2D、arrivals、eigenray、ray plot；
- incoherent/semi-coherent、速度分量、不规则接收网格；
- beam shift；
- PCHIP、Spline 或所有 Bellhop 输入选项的完整兼容；
- HDF5、磁盘轨迹缓存和 receiver tiling。

## 2. 当前基线与阻塞项

| 项目 | 当前状态 | 对构建的影响 |
|---|---|---|
| F2CPP 源码 | G0、M1、M2 已完成，含完整单频链、冻结轨迹缓存和 R-15 摊销门 | 已形成允许派生 RayReuse 的最终快照 |
| Fortran oracle | 已有六例单频最终场、可重现构建链、ray schema v2 和 Cartesian Cerveny Influence schema v1 | 继续作为派生后的独立数值 oracle |
| 标准算例 | 六个环境、统一 runner、单频/宽带 profiles 已建立 | F2CPP adapter 已启用并通过六例 single profile |
| 结果比较 | 已有复压力和 TL 比较器，六例均通过 | 派生后沿用同一比较入口 |
| C++ 编译器 | Apple Clang 21，支持 C++20 | 可直接使用 |
| Fortran 编译器 | Homebrew GCC/gfortran 14.2 | 可继续生成 oracle |
| CMake | 4.0.2，CTest 同版本 | Debug/Release 已验证 |
| Ninja | 当前机器未安装 | 非必需，首版使用 CMake 默认生成器 |
| 测试框架 | 未引入 C++ 测试依赖 | 首版使用 CTest + 仓内轻量断言程序 |

### 2.1 开工决策门

项目负责人已于 2026-07-27 确认 D-01～D-06，本计划按以下值执行：

- D-01：内部密度统一为 `kg/m³`；
- D-02：选择 A，按最高设计频率确定最终发射角数；
- D-03：内部 `std::complex<double>`，SHD 边界转 `complex<float>`；
- D-04：类型 `PascalCase`、成员/函数 `lowerCamelCase`、文件
  `snake_case`；
- D-05：先由单元测试直接构造 `SimulationCase`，M1 闭环后实现裁剪版
  `.env` parser；
- D-06：永久保存原始衰减值、单位和参考频率，逐频完成转换。

D-07～D-12 使用文档中的暂定规则，不阻塞骨架和 M1；进入对应模块前再用
oracle 收紧。

### 2.2 首版 CLI 契约

为复用现有标准算例目录和运行方式，F2CPP CLI 采用与原版相同的最小调用：

```text
bellhop_f2cpp <file-root>
```

程序在当前工作目录读取 `<file-root>.env`，写出：

```text
<file-root>.prt
<file-root>.shd
```

`.env` parser 只接受首版支持矩阵需要的子集。遇到其他运行模式、波束类型、
网格或边界选项必须明确报错。数值核心不得持有文件路径或 parser 状态。

## 3. 工程结构

首版按以下结构实施：

```text
Bellhop_F2CPP/
├── CMakeLists.txt
├── CMakePresets.json
├── cmake/
├── include/bellhop/
│   ├── model/
│   ├── numerics/
│   ├── ray/
│   ├── cache/
│   ├── acoustics/
│   ├── field/
│   └── io/
├── src/
├── app/
├── tests/
│   ├── unit/
│   ├── component/
│   ├── regression/
│   └── support/
├── doc/
│   ├── README.md
│   ├── GUIDE_USAGE.md
│   ├── PLAN_BUILD_ACCEPTANCE.md
│   └── REPORT_DERIVATION_MANIFEST_2026-07-29.md
└── README.md
```

CMake 目标固定为：

```text
bellhop_f2cpp_core     本工程内部静态库
bellhop_f2cpp          单频命令行程序
bellhop_f2cpp_tests    仓内轻量测试程序，可按需要拆分
```

`bellhop_f2cpp_core` 只服务本工程的程序和测试，不安装、不导出给
`Bellhop_RayReuse` 链接。未来 RayReuse 从 M2 快照复制必要代码后建立自己的
目标。

## 4. 工作包与依赖顺序

状态标记：

- `TODO`：尚未开始；
- `DOING`：正在实施；
- `DONE`：代码、测试和验收证据均已完成；
- `BLOCKED`：存在明确外部阻塞。

### G0：开工与可构建骨架

| ID | 状态 | 任务 | 交付物与验收 | 依赖 |
|---|---|---|---|---|
| G0-01 | DONE | 确认 D-01～D-06 和 CLI | 决策写回仓库根 `doc/04`，CLI 写入 F2CPP README | 无 |
| G0-02 | DONE | 安装 CMake | CMake/CTest 4.0.2 可用 | 用户已安装 |
| G0-03 | DONE | 建立 CMake/C++20 骨架 | Debug/Release 可配置；生成三个固定目标；空 CLI 可运行 | G0-02 |
| G0-04 | DONE | 建立编译质量门 | Clang 高警告；Debug 启用 ASan/UBSan；禁止 fast-math | G0-03 |
| G0-05 | DONE | 建立轻量测试基础设施 | Debug/Release CTest 均通过；不下载第三方测试框架 | G0-03 |
| G0-06 | DONE | 实现基础强类型和不变量 | `Vec2`、网格、`SimulationCase`、频率数检查、溢出检查 | G0-05 |
| G0-07 | DONE | 定义完整轨迹/逐频类型 | `RayState`、`StepQuadrature`、`ReflectionEvent`、`RayPath`、`RayPathCache`、`RayFrequencyState`、`FrequencyWorkspace` | G0-06 |

G0 出口条件：

- 最小 `SimulationCase` 能在测试中构造；
- 空求解器对零频、多个频率、非法网格和不支持模式明确失败；
- 轨迹类型中不存在逐频幅相或压力字段；
- Debug sanitizer smoke test 通过。

### O1：补齐可信 oracle

该工作修改的是 `Bellhop_origin` 的可选诊断能力，不改变默认计算结果和
SHD 布局。

| ID | 状态 | 任务 | 交付物与验收 | 依赖 |
|---|---|---|---|---|
| O1-01 | DONE | 冻结六个单频 oracle | 六例 PRT/SHD 已由统一结果目录保存并用于完整复压力、幅值、相位和 TL 比较 | 无 |
| O1-02 | DONE | 导出逐步状态 | 可选导出 `x/t/p/q/c/tau`、实际步长、求积权重和终止原因 | 无 |
| O1-03 | DONE | 导出反射状态 | schema v2 独立导出前/后点、边界类别、切法向、慢度、动态量和原始复反射系数 | O1-02 |
| O1-04 | DONE | 导出单射线 Influence | 独立 schema v1 导出 epsilon、插值 q/gamma、KMAH、图像窗口/贡献、复根常数及 complex64 累加增量 | O1-02 |
| O1-05 | DONE | 分阶段计时 | F2CPP PRT/manifest 已分别记录 Trace、Project、Influence、Scale、SHD 和峰值内存；R-15 下 Fortran 总 CPU 时间保留为诊断基线 | 无 |
| O1-06 | DONE | 冻结诊断 schema | 已建立兼容 v1 的 ray schema v2、Influence schema v1、CSV/JSON manifest 和独立校验器 | O1-02 |

注意：原版会在 `Amp < 0.005` 时提前终止轨迹，而 F2CPP 的几何追踪必须与
频率无关。因此：

- 无损/刚性用例可比较完整几何轨迹；
- 有损声学底用例只把原版 active 前缀作为几何 oracle；
- F2CPP 额外追踪的几何后缀由解析不变量检查；
- `FrequencyProjector` 必须在与原版相同的位置将该频率标为 inactive，
  从而禁止后缀继续贡献声场。

O1-04 已固定三个 50 Hz 样本：等声速 direct、Munk 几何焦散邻域和
Munk `KMAH=-1` 分支控制点。三者均通过 ray-points 交叉检查；验证内容
覆盖接收范围插值、复 `q/gamma`、BranchCut、Hermite/window、按 image
顺序求和、`const * sum` 以及写入默认复场前的 complex64 量化。

### M1：组件级单频与完整轨迹缓存

| ID | 状态 | 任务 | 交付物与验收 | 依赖 |
|---|---|---|---|---|
| M1-01 | DONE | 数学基础 | `Vec2` 运算、C-linear 插值、层段搜索、有限值检查和解析单测 | G0 |
| M1-02 | DONE | C-linear SSP | 返回实 `c`、零虚声速、梯度、Hessian、密度和 segment index；节点双侧测试 | M1-01 |
| M1-03 | DONE | `c_nn/c²` | 独立纯函数；按 `Step.f90` 分量公式和解析 Hessian 测试 | M1-02、O1-02 |
| M1-04 | DONE | modified Heun/box 单步 | 已推进 position/slowness、两组 dynamic p/q、实走时并记录实际求积数据 | M1-03 |
| M1-05 | DONE | 无边界 `GeometryTracer` | 等声速直线、Snell 不变量、动态 q 有限差分、二阶收敛测试 | M1-04 |
| M1-06 | DONE | `RayPath` 组装与缓存冻结 | 冻结 `points/steps/events` 索引关系、终止原因、缓存字节统计 | M1-05、G0-07 |
| M1-07 | DONE | 平边界相交与 reduced step | 已覆盖精确落点、Fortran 最小步越界、重复点/小步保护、空间盒终止 | M1-04 |
| M1-08 | DONE | 几何与动态反射 | 真空海面/刚性海底镜面反射及动态 p/q 跳跃已通过纯组件和多反射对照 | M1-07 |
| M1-09 | DONE | `ReflectionEvent` 记录 | 已冻结 SeaSurface/Seabed、反射前点索引、切法向和入/反射慢度 | M1-08 |
| M1-10 | DONE | M1 组件回归 | 直达、真空/刚性、多层 Munk 的中间状态比较报告均通过 | M1-02～M1-09、O1 |

M1 出口条件：

- `constant_speed_direct`：位置、慢度、实走时和动态 p/q 通过解析解与
  Fortran 对比；
- `constant_speed_vacuum_rigid`：多次反射的交点、方向、事件类别、计数和
  动态跳跃通过；
- `munk_cerveny_cc`：分层 C-linear SSP、层界 reduced step 和轨迹状态通过；
- `RayPathCache` 离开追踪器后仍独立保有全部状态；
- 冻结缓存的结构与事件逐字段校验已通过；投影器完成后在 M2 再执行投影
  前后哈希检查；
- 组件误差使用仓库根 [数值规范](../../../doc/reference/REFERENCE_NUMERICAL_CONVENTIONS.md)
  第 9 节暂定容差，任何放宽必须记录最大误差位置。

`constant_speed_direct` 的 alpha=150 已完成 C++/Fortran 全轨迹逐字段对照：
512 个点、511 个积分步，覆盖 position/slowness、dynamic p/q、声速、实走时、
实际步长/权重和 predictor 中点；最坏绝对误差为 `8.53e-14 m`
（range，point 14），未放宽 D-07 暂定容差。

C-linear 内部层界已按 Fortran reduced-step 语义续追：先精确落在 node，
保留来向 segment，再以一次 `1e-3*deltas` 最小步进入相邻层并更新 hint；
上下穿、节点源、Snell 不变量和有限状态测试均已通过。

平边界镜面反射与 `CurvatureCorrection2` 已实现为纯组件并接入
`GeometryTracer`，显式覆盖 Standard/Double/Zero 三种动态曲率模式。
`constant_speed_vacuum_rigid` 的 alpha=1 高反射轨迹已完成 C++/Fortran
全轨迹逐字段对照：36,519 个点、33,597 个积分步、1,461 次海面反射和
1,460 次海底反射，最坏绝对误差为 `1.42e-14 m`（predictor 中点深度，
point 40）。alpha=500/1000 的额外上下行射线也已通过，用于冻结海面与海底
精确边界 tie 的非对称原版语义。

`munk_cerveny_cc` 的 alpha=250 多层折射轨迹已完成 311 点/310 步全字段
对照，最坏项为 reduced step 的 `1.68e-10 m`，仅占 D-07 容差的 0.67%。
alpha=500 的节点源近水平轨迹同样通过（299 点/298 步，108 次 reduced
step）；alpha=1/1000 的极端上下行轨迹分别以 547/543 点通过，覆盖 4/3 次
海面和 3/3 次海底反射。由此发现并修正了长程 modified-box 合法慢度漂移
被反射器误拒的问题，反射/缓存入口以 `1e-4` 作为损坏状态 guard，而逐字段
oracle 容差未放宽。M1-07～M1-10 全部关闭。

### M2：逐频投影、Cartesian Cerveny、CLI 与 SHD

| ID | 状态 | 任务 | 交付物与验收 | 依赖 |
|---|---|---|---|---|
| M2-01 | DONE | 衰减转换 | N/M/m/F/W/Q/L → Np/m → imaginary sound speed；无损、Thorp 和 raw-input immutability 已验证 | M1、O1 |
| M2-02 | DONE | 边界声学 | 真空、刚性、流体声学半空间的逐频复反射系数；弹性半空间明确拒绝 | M2-01、M1-09 |
| M2-03 | DONE | `FrequencyProjector` | 从只读 `RayPath` 重建复走时、幅度、未包裹相位和粘滞 active mask | M2-01、M2-02 |
| M2-04 | DONE | `PickEpsilon` | 已实现目标 `CM` minimum-width 分支；direct/Munk O1-04 epsilon 逐位一致 | M1、O1-04 |
| M2-05 | DONE | Cartesian Cerveny Influence | 已实现 p/q/gamma、KMAH、BranchCut、复根、窗口、Hermite、三图像源和双精度网格累加 | M2-03、M2-04 |
| M2-06 | DONE | 压力缩放 | 已实现 coherent Cartesian point-source 逐范围双精度缩放；O1 direct/Munk 与 legacy factor 位模式通过 | M2-05 |
| M2-07 | DONE | 单射线与焦散回归 | direct、Munk 焦散和 KMAH=-1 三个 O1-04 样本逐项及 legacy float 位模式通过 | M2-05、O1-04 |
| M2-08 | DONE | 裁剪版 `.env` parser | 已解析六例所需子集；km/degree/g·cm⁻³ 只在 I/O 边界转换，其他模式显式拒绝 | M1 |
| M2-09 | DONE | CLI 与错误/PRT 日志 | `bellhop_f2cpp <root>` 已执行完整单频链；支持标记、计时、缓存量和异常均写 PRT | M2-08 |
| M2-10 | DONE | SHD writer | 已实现小端 direct-access header/record 布局，writer 边界转 `complex<float>`，Python reader 通过 | M2-06、M2-09 |
| M2-11 | DONE | 启用标准算例 adapter | `f2cpp` 已可走 generate/run/validate/test；六例定义未复制或修改 | M2-09、M2-10 |
| M2-12 | DONE | 六例递增回归 | 六例完整复压力与 TL 均通过 `abs+rel` 和 `1e-3 dB` 阈值 | M2-01～M2-11 |
| M2-13 | DONE | sanitizer/异常路径回归 | 六类物理路径缩小版完整求解与异常终止拒绝已在 Debug ASan/UBSan 下通过 | M2-12 |
| M2-14 | DONE | Release 性能与内存基线 | R-15 保留完整缓存；六例 16 频 trace-once 模型相对重复 F2CPP 节省 `1.58%～55.84%`，分阶段时间、缓存和峰值 RSS 已固定 | M2-12、O1-05 |
| M2-15 | DONE | M2 快照与派生清单 | 已固定构建命令、测试证据、接口分组、性能门和源码树校验值，批准派生 RayReuse | M2-13、M2-14 |

六个单频算例按故障隔离顺序推进：

1. `constant_speed_direct`：先闭环无反射、无显式体吸收；
2. `constant_speed_vacuum_rigid`：加入多次几何反射和图像源；
3. `constant_speed_acoustic_bottom`：加入逐频复反射系数和 active mask；
4. `constant_speed_no_attenuation_5khz`：高频无损参照；
5. `constant_speed_thorp`：只改变水体吸收，隔离损耗路径；
6. `munk_cerveny_cc`：最后验证分层 SSP、焦散和远程传播。

M2 出口条件：

- 六例都能执行：

  ```bash
  make -C test/standard_cases test \
    VERSION=f2cpp CASE=<case> PROFILE=single
  ```

- 内部双精度复压力按暂定 `abs=1e-7`、`rel=1e-5` 比较；
- 最大 TL 差暂定不超过 `1e-3 dB`；
- SHD 只要求 header、坐标轴和复压力语义一致，不要求文件逐字节相同；
- 每个失败报告最大误差的接收点、复数绝对/相对误差、幅值误差和包裹相位差；
- Debug sanitizer 全部通过；
- Release 分别记录 Trace/Project/Influence/Scale/SHD；按 R-15，
  16 频 trace-once 模型的六例节省率均不得低于 1%，单频 Fortran 比值只作
  诊断；
- 完整 `RayPathCache` 可在追踪器销毁后独立驱动投影和声场计算；
- 投影、Influence 和写出前后，缓存保持只读且逐频量没有污染轨迹。

## 5. 推荐实施批次

不按“把所有头文件先写完”推进，而按可运行的纵向切片提交：

### 批次 A：骨架与数据契约

完成 G0。得到能构建、能跑 CTest、能拒绝非法多频输入的空程序。

### 批次 B：等声速无边界轨迹

完成 M1-01～M1-06，同时完成 O1-02 的最小逐步导出。得到第一条可逐点对比
的完整 `RayPath`。

### 批次 C：反射轨迹

完成 M1-07～M1-10 和 O1-03。以真空海面/刚性平底多次反射关闭 M1。

### 批次 D：单频声学投影

完成 M2-01～M2-04。先验证每条射线的复走时、反射幅相和 active mask，不
立即叠加完整接收场。

### 批次 E：Influence 与声场

完成 M2-05～M2-07。先固定一条射线和少量接收点，再扩大到一个完整接收
网格。

### 批次 F：兼容输入输出

完成 M2-08～M2-11。打通 `.env → SimulationCase → SHD`，启用统一 F2CPP
adapter。

### 批次 G：全矩阵与性能门

完成 M2-12～M2-15，形成 RayReuse 可派生的稳定点。

关键路径为：

```text
决策/CMake
  → 骨架与数据类型
  → SSP
  → Step/GeometryTracer
  → 边界/ReflectionEvent
  → M1
  → FrequencyProjector
  → Cartesian Cerveny Influence
  → parser/CLI/SHD
  → 六例端到端
  → 性能门与 M2 快照
```

oracle 导出与 C++ 骨架在逻辑上可独立，但在开始对应组件的数值复刻前必须
完成相应导出。

## 6. 测试与构建命令约定

计划中的标准本地命令为：

```bash
# Debug + sanitizer
cmake -S Bellhop_F2CPP -B Bellhop_F2CPP/build/debug \
  -DCMAKE_BUILD_TYPE=Debug \
  -DF2CPP_ENABLE_SANITIZERS=ON
cmake --build Bellhop_F2CPP/build/debug --parallel
ctest --test-dir Bellhop_F2CPP/build/debug --output-on-failure

# Release
cmake -S Bellhop_F2CPP -B Bellhop_F2CPP/build/release \
  -DCMAKE_BUILD_TYPE=Release
cmake --build Bellhop_F2CPP/build/release --parallel
ctest --test-dir Bellhop_F2CPP/build/release --output-on-failure

# 共享单频回归
make -C test/standard_cases test \
  VERSION=f2cpp CASE=constant_speed_direct PROFILE=single \
  EXECUTABLE="$PWD/Bellhop_F2CPP/build/release/bellhop_f2cpp"
```

CTest 标签建议固定为：

```text
unit
component
regression
sanitizer
performance
```

`performance` 不进入普通 Debug CTest；它只在固定 Release 编译选项、单线程
和记录硬件信息的条件下运行。

## 7. 风险与控制

| 风险 | 最早暴露点 | 控制 |
|---|---|---|
| CMake 缺失 | G0 | 先补工具链，不以临时手写编译命令替代正式构建 |
| 基础决策未冻结 | G0 | 公共类型落地前确认 D-01～D-06 |
| oracle 只有最终 SHD | O1/M1 | 先增加可选逐步、反射、单射线导出 |
| 原版幅度阈值截短几何 | M1/M2 | 几何追踪与逐频 active mask 分离；比较 active 前缀 |
| Step 求积信息保存不足 | M1 | 单测冻结 `points[i] ↔ steps[i] ↔ points[i+1]` |
| 反射双点索引含糊 | M1 | 按 D-10 保留双点；事件先指向反射前边界点 |
| 复根/KMAH 分支漂移 | M2 | 固定单射线样本和焦散专项回归 |
| Fortran 单精度压力掩盖差异 | M2 | 先比较内部诊断，再比较 SHD 量化结果 |
| parser 扩张成全模式移植 | M2 | 只接受六例所需子集，其他选项显式报错 |
| 过早优化改变累加顺序 | M2 | M1/M2 固定发射角顺序和单线程；性能数据只用于定位 |
| 缓存内存不可控 | M1/M2 | 每次运行记录射线数、点数、事件数和缓存估算字节 |

## 8. 每个工作包的完成定义

一个任务只有同时满足下列条件才可标为 `DONE`：

1. 实现代码和公共接口注释完成；
2. 对应单元/组件测试已加入 CTest；
3. 至少有解析解或 Fortran oracle 之一作为独立对照；
4. Debug 警告干净，相关 sanitizer 测试通过；
5. 错误报告包含输入、容差、最大误差和位置；
6. 新增变量、单位、索引或容差已同步仓库根 `doc/04`；
7. 用户可从 README 中找到构建、运行和复现实验命令；
8. 没有把不支持模式静默近似为已支持模式。

## 9. 当前状态与下一组实际任务

截至 2026-07-27，G0、M1、M2-01～M2-12、O1-02～O1-04/O1-06 已完成。
Release CLI 已打通：

```text
.env parser
  → SimulationCase / LaunchFanPlan
  → GeometryTracer / frozen RayPathCache
  → FrequencyProjector
  → Cartesian Cerveny Influence
  → pressure scaling
  → PRT / SHD
```

本轮端到端修正了两个只有高反射相干场才会放大的末位问题：

1. parser 保留原始 degree 端点，`LaunchFanPlanner` 按 Fortran `SubTab`
   先在度数域细分，再逐项转 radian；1570 点角度和 `Dalpha` 已冻结位模式；
2. `GeometryTracer` 用独立 `sin`/`cos` 调用，禁止 Clang 合并为
   `sincos`。刚性底第 396 条近边界射线恢复为 798 点/747 步/50 次反射，
   与 Fortran oracle 逐字段零误差。

六例最终结果均通过。最大压力绝对误差出现在
`constant_speed_vacuum_rigid`，为 `7.27386151e-8`；最大 TL 差同样在该例，
为 `3.12805176e-4 dB`。当前 Debug ASan/UBSan 和 Release 各 21 个 CTest、
标准算例 Python 69 个测试全部通过。`single_frequency_solver` 组件还在
sanitizer 下逐一执行直达、刚性反射、损耗声学底、5 kHz 无损、Thorp 和
Munk 缩小版完整数值链，并覆盖异常终止拒绝。

Influence 的生产路径已与单点详细诊断路径分开；再加入无损/空间均匀投影
快路径、零 taper 复指数短路和步长限制器复用后，Munk 的
Project+Influence 约为 `2.24 s`，三轮分项总计中位数为 `2.300 s`，
数值输出不变，并快于原版 `5.26 s` 总 CPU 时间。
角度感知的轨迹/事件 reserve 将 5 kHz、10,129,596 点缓存估算从约
`3.64 GB` 降至约 `1.23 GB`；实测峰值 RSS 为 `1,321,304,064 B`。
Munk 缓存估算为 `62,718,712 B`，实测峰值 RSS 为 `66,125,824 B`。

R-15 已确认保留完整冻结缓存。16 频 trace-once 摊销门六例全部通过，
M2-14/M2-15 已关闭。独立 `Bellhop_RayReuse/` 已按派生清单建立；F2CPP
后续仍保持单频定位，并按
[二维功能进一步复刻计划](./PLAN_FEATURE_REPLICATION.md)继续扩展二维
Bellhop 功能，不实施 3D、N×2D 或多频调度。
