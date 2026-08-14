# Bellhop F2CPP 使用说明

## 1. 程序用途

`bellhop_f2cpp` 是裁剪后的 C++20 单频二维 Bellhop 实现。它读取一个
Bellhop `.env` 文件；coherent、incoherent 与 semi-coherent TL 模式写出
PRT/SHD，ray-trace 与 eigenray 写出 PRT/RAY，arrivals 写出 PRT/ARR。

程序内部始终先生成完整的频率无关射线轨迹，冻结为只读
`RayPathCache`，再执行单频投影和声场累加：

```text
.env
  → SimulationCase
  → GeometryTracer
  → frozen RayPathCache
  ├─ C/I/S TL  → FrequencyProjector → CartesianCervenyInfluence
  │            → pressure/intensity scaling → .prt + .shd
  ├─ A/a arrivals → G/g/B contribution → ArrivalWorkspace → .arr
  ├─ E eigenray   → G/g/B receiver hits → ray prefixes → .ray
  └─ R ray trace  → RayWriter → .prt + .ray
```

F2CPP 一次运行只接受一个频率。多频调度、实际轨迹复用和多频 SHD 属于
后续独立的 `Bellhop_RayReuse` 工程。

## 2. 环境要求

编译程序需要：

- CMake 3.24 或更高版本；
- 支持 C++20 的 Clang、Apple Clang 或 GCC；
- `make`，因为当前 preset 使用 `Unix Makefiles`；
- 约 2 GB 可用内存，以运行最大的 5 kHz 标准案例。

标准算例、SHD 校验和场比较还需要：

- Python 3.11 或更高版本；
- NumPy；
- 仓库自带的
  [`test/PlotRead/bellhop_io_py`](../../test/PlotRead/README.md)，标准算例会直接
  引用源码；人工使用 `bellhop-shd` 时按 PlotRead 文档完成一次 editable install。

检查常用工具：

```bash
cmake --version
c++ --version
make --version
python3 --version
python3 -c "import numpy; print(numpy.__version__)"
```

当前最终快照在 macOS arm64、Apple Clang 21、CMake 4.0.2 上通过验证。

## 3. 编译

以下命令均从 `Bellhop_F2CPP/` 目录运行：

```bash
cd Bellhop_F2CPP
```

### 3.1 Release

Release 用于正式计算和性能测试：

```bash
cmake --preset release
cmake --build --preset release --parallel
```

生成：

```text
build/release/bellhop_f2cpp
```

### 3.2 Debug + sanitizer

Debug preset 开启高警告、`-Werror`、AddressSanitizer 和
UndefinedBehaviorSanitizer：

```bash
cmake --preset debug
cmake --build --preset debug --parallel
```

生成：

```text
build/debug/bellhop_f2cpp
```

Debug 版本用于诊断，不应用于性能基线。

### 3.3 重新配置或重新编译

修改 CMake 配置后重新运行对应 configure preset：

```bash
cmake --preset release
cmake --build --preset release --parallel
```

要求清理目标后再编译时：

```bash
cmake --build --preset release --clean-first --parallel
```

本工程没有安装步骤；直接使用 `build/<配置>/bellhop_f2cpp`。

## 4. 测试

### 4.1 C++ 单元与组件测试

先完成相应配置和构建，再执行：

```bash
ctest --preset debug
ctest --preset release
```

显示失败测试的完整输出：

```bash
ctest --preset release --output-on-failure
```

只运行某一类测试：

```bash
ctest --test-dir build/release \
  -R 'frequency_projector|single_frequency_solver' \
  --output-on-failure
```

复刻封板基线的 AppleClang Debug ASan/UBSan、AppleClang Release 与 GCC 14
Release/Werror 均为 37/37 CTest。

### 4.2 Python 标准算例基础测试

以下命令从仓库根目录运行：

```bash
python3 -m unittest discover \
  -s test/standard_cases/codes/tests -p 'test_*.py'
```

当前项目基线为 145/145。

### 4.3 六十五个单频端到端案例

从仓库根目录运行：

```bash
python3 test/standard_cases/codes/standard_cases.py test \
  --version f2cpp \
  --case all \
  --profile single \
  --executable Bellhop_F2CPP/build/release/bellhop_f2cpp
```

该命令运行当前 65 个单频案例，覆盖 SHD、RAY、ASCII/binary ARR 与 eigenray
产品，以及 I0～I8 和 B1～B3 的代表性输入。案例定义与 profile 的唯一清单
位于 `test/standard_cases/cases/` 和 `coverage.toml`；逐 iteration 的数值误差、
Origin oracle 与冻结哈希见 [`PROGRESS.md`](./PROGRESS.md) 和
[`validation/`](./validation/)，不在本使用文档重复维护案例枚举。
结果写入：

```text
test/standard_cases/results/f2cpp/<case>/single/
```

只校验已有结果而不重新计算：

```bash
python3 test/standard_cases/codes/standard_cases.py validate \
  --version f2cpp --case all --profile single
```

### 4.4 与 Fortran SHD 比较

```bash
python3 test/standard_cases/codes/compare_fields.py \
  /path/to/reference.shd \
  /path/to/f2cpp.shd
```

程序检查坐标轴、频率、复压力组合误差和 TL 误差。默认容差位于
`test/standard_cases/codes/tolerances.toml`。

### 4.5 摊销性能门

先运行六个单频标准案例，然后从 `Bellhop_F2CPP/` 执行：

```bash
python3 tests/tools/test_evaluate_amortized_performance.py

python3 tests/tools/evaluate_amortized_performance.py \
  --frequency-count 16 \
  --minimum-savings-percent 1 \
  ../test/standard_cases/results/f2cpp/*/single/f000_*/*.prt
```

该工具使用 PRT 中的分阶段时间计算：

```text
T_repeat(N) = N × (T_trace + T_freq)
T_reuse(N)  = T_trace + N × T_freq
```

这是 F2CPP 的缓存就绪性能门，不表示 F2CPP 已实现多频运行。实际宽带收益
必须在 RayReuse 中再次实测。

## 5. 可执行文件用法

### 5.1 命令格式

```bash
bellhop_f2cpp <file-root>
```

查看帮助：

```bash
./build/release/bellhop_f2cpp --help
```

`<file-root>` 可以是相对路径或绝对路径，但必须省略 `.env` 扩展名。
程序读取：

```text
<file-root>.env
```

当 top option 第 5 字符或 bottom option 第 2 字符为 `~`/`*` 时，还会读取
同目录、同 file-root 的 `<file-root>.ati` 或 `<file-root>.bty`。

始终写出：

```text
<file-root>.prt
```

`CC`、`IC`、`SC` TL 模式另写 `<file-root>.shd`；`R` ray-trace 与 `E`
eigenray 写 `<file-root>.ray`；`A/a` arrivals 写 `<file-root>.arr`。每次成功
只保留当前模式的正式产品。输出与输入位于同一目录；需要保留旧结果时应先
复制或更换 file-root。

### 5.2 直接运行

假设当前目录包含 `example.env`：

```bash
/absolute/path/to/Bellhop_F2CPP/build/release/bellhop_f2cpp example
```

也可以从任意目录传入绝对 file-root：

```bash
/absolute/path/to/bellhop_f2cpp \
  /absolute/path/to/cases/example
```

### 5.3 用标准案例生成一个可运行输入

从仓库根目录执行：

```bash
python3 test/standard_cases/codes/standard_cases.py generate \
  --version f2cpp \
  --case constant_speed_direct \
  --profile single

f2cpp_case_root="test/standard_cases/results/f2cpp/constant_speed_direct/single/f000_50Hz/constant_speed_direct_f000_50Hz"

Bellhop_F2CPP/build/release/bellhop_f2cpp "$f2cpp_case_root"

python3 test/standard_cases/codes/standard_cases.py validate \
  --version f2cpp \
  --case constant_speed_direct \
  --profile single
```

### 5.4 退出状态

| 状态码 | 含义 |
|---:|---|
| `0` | 计算成功，或成功显示 `--help` |
| `1` | 输入、数值计算或输出发生错误 |
| `2` | 命令行参数数量错误 |

运行错误会写到标准错误。如果 PRT 已成功打开，文件末尾还会包含
`FATAL ERROR:` 和具体原因。

## 6. 支持的输入范围

复刻封板的 supported / intentional divergence / deferred 分类以
[`FEATURE_SUPPORT_MATRIX.md`](./FEATURE_SUPPORT_MATRIX.md) 为准。下表给出
实际可输入范围：

| 项目 | 支持范围 |
|---|---|
| 维度与场 | 二维；coherent complex pressure，incoherent/semi-coherent intensity-derived pressure，普通射线，arrivals 或 eigenray prefixes |
| 运行类型 | Cartesian Cerveny `CC`/`IC`/`SC`，或 ray-centered Cerveny `CR`/`IR`/`SR`；非 beam-shift `R`/`RG`/`RGO` ray trace；`A/a` arrivals；`E` eigenray。A/a/E 的 beam family 支持 Cartesian geometric hat `G`、ray-centered geometric hat `g` 和 Cartesian geometric Gaussian `B` |
| 频率与声源 | 每次运行恰好一个频率；支持一个或多个 source depth |
| 水体 | 一个水层、C-linear、N²-linear、PCHIP、Cubic Spline 或 Q 型范围相关二维 SSP |
| 二维边界 | top/bottom 均支持 `V/R/A/G/F`；top `F + .trc`、bottom `F + .brc`；平坦边界及 `.ati/.bty` 的 piecewise-linear `LS`、canonical curvilinear `C` short format；piecewise-linear `LL` fluid/elastic P/S long format |
| 衰减 | `N/F/M/W/Q/L` 输入单位；可选 Thorp、Francois–Garrison 或 biological 体积衰减 |
| 接收网格 | Cartesian TL：规则 depth×range 笛卡尔积，或等数量 depth/range 轴的 irregular SHD；ray-centered TL：仅规则网格。A/a/E 的 Cartesian G/B 支持规则及 Origin 配对 irregular 接收器，ray-centered g 仅规则、等间距 range；R 允许单个 receiver range |
| 波束 | Cartesian/ray-centered Cerveny TL：`F/M/W` beam width × `D/S/Z` reflection-curvature condition，1～3 个图像源；A/a/E 仅 G/g/B，不读取 TL 的 beam/image 两行；R 同样不读取，并支持 `.sbp` 与显式 `Nalpha=1` |
| 发射角 | TL 与 A/a/E 使用既有自动规划；R 的显式 `Nalpha` 原样使用，`0` 自动取 50 |

不支持的选项会明确报错，而不会静默降级。当前不支持：

- 多频 `.env` 或一次运行多频；
- `A/a/E` 的 simple Gaussian `S`、Cerveny `C`、ray-centered Gaussian 及其他
  未验收 family；Cartesian P/V/H 仍按 Origin legacy no-op 兼容，物理 V/H
  仅由 ray-centered Cerveny 计算；
- ray-centered irregular receiver grid；首个纵切会明确拒绝，不会退化为
  Cartesian 或静默套用 CC 的 irregular legacy 深度语义；
- `G+LL`、`F+LL`、`CS/CL` legacy 混合写法、curvilinear long format、边界粗糙度；
- `P/W` reflection-coefficient 路径；当前 Origin 2D 中它们没有完整的实际反射消费/写出链；
- 小写 `m` 幂律单位；
- analytic SSP、多个水体介质或水体 shear 参数；
- beam shift；
- 3D 或 N×2D。

输入沿用 Bellhop I/O 单位：接收距离和范围盒使用 km，角度使用 degree，
密度输入使用 g/cm³；parser 在边界转换为内部 SI 单位。

### 6.1 多 source depth

source depth 记录可给一个或多个值。输入值先按 Origin 语义转换为 REAL4、
升序排列并保留重复值；当数量至少为 3 时，也支持只给首末深度并自动等间距
展开。所有 source 共用一个按 1500 m/s 参考声速规划的 D-02 launch fan，
但每个 source 都独立计算起点声速、epsilon、束窗和压力缩放。

求解器逐 source 建立、冻结、消费并释放 `RayPathCache`；不同 source 的压力
不会相干相加。PRT 中的 ray/point 数是各 source 总和，cache bytes 是逐源
峰值。SHD 写真实 source 数和排序后的深度向量，压力记录按 source-major、
receiver-depth 次序排列。当前安全子集仍要求每个 source 严格位于水体内部。

### 6.2 Irregular receiver grid

在七字符 run type 的第 5 个位置写 `I` 可选择 irregular SHD 布局，例如
`'CC RI2'`。receiver depth 数必须等于 receiver range 数；SHD 头仍保存两条
完整坐标轴并标记 `irregular`，但每个 source 只写一条、长度为 range 数的
压力记录。第 5 个字符留空或写 `R` 时仍是标准 depth×range 笛卡尔积。

需要注意当前 Origin 2D 的 `InfluenceCervenyCart` 遗留行为：虽然输入和 SHD
声明为 `(Rr(i), Rz(i))` 配对，它在 `CC` 分支实际对所有 range 使用
`Rz(1)`。F2CPP 为逐场兼容明确复刻这一可观察语义，同时保留完整 depth 轴；
不能把此模式理解为已经提供修正后的任意配对 CC 场。真正逐 range 使用
`Rz(i)` 的现代化模式需与后续 beam-family 工作一并设计，不能静默改变同一
ENV 的 Origin 结果。

### 6.3 Source beam-pattern

在 run type 的第 3 个字符写 `*`（例如 `CC*`）时，CLI 会读取与 `.env`
同根的 `.sbp`。留空或写 `O` 使用全向源。`.sbp` 格式为：

```text
N
angle_1_deg  power_1_dB
...
angle_N_deg  power_N_dB
```

当前安全子集要求 `N >= 2`、角度严格递增，所有角度和 dB 值有限。每个 dB
值先按 `10^(dB/20)` 转换成线性压力幅度，再在相邻角节点间线性插值；表域外
沿第一段或最后一段线性外推，不做端值钳制。为保持逐频投影的有限、非负
契约，配置后的整个 launch fan 上外推/插值结果必须非负。

方向图由所有 source depth 共用，且不依赖频率。它只在频率投影入口乘到
每条射线的初始幅度，不改射线位置、步长、反射事件或冻结缓存。因此同一
source 的 1/2 kHz 仍复用相同几何，多个 source 也不会把方向图写回全局
source amplitude。

### 6.4 Cerveny field component

C/I/S TL 的 image/window 行第三项接受大写 `P`、`V` 或 `H`：

```text
3  5  'V'
```

CLI 会保留该选择并在 PRT 的 `Component =` 行回显；未知值、小写值和空值
会明确拒绝。需要特别注意，本项目当前所复刻的 Origin 2D
`InfluenceCervenyCart` 虽解析 P/V/H，却完全不读取该字段，SHD 也没有
component 元数据，因此 Cartesian 下三种选择会产生同一个压力场。这是经
Origin/F2CPP 三控制例逐字节冻结的 legacy 行为，不代表已经计算了物理垂直/
水平分量。ray-centered Cerveny 则真实消费该字段：`P` 保留标量压力贡献，
`V`、`H` 使用局部射线切向/法向与 Origin 相同的梯度投影公式。两组验证报告
分别见 `doc/validation/i7_cartesian_components_report.json` 与
`doc/validation/i7_ray_centered_components_report.json`。

### 6.5 Cartesian Cerveny beam width 与反射曲率

CC 的 beam 设置记录为：

```text
'<width><curvature>'  epsilon_multiplier  loop_range_km
```

`width` 可为 `F`（space filling）、`M`（minimum width）或 `W`（WKB）；
`curvature` 可为 `D`（完整反射动态跳变量加倍）、`S`（standard）或 `Z`
（完整跳变量清零），九种大写组合均支持。`M` 使用 `loop_range_km` 选取
最小束宽；`F` 由发射角间隔选取束宽；`W` 使用源点声速梯度和每条射线的
发射角计算实 epsilon，并按 real(q) 过零规则更新 KMAH。WKB 保留 Origin
可观察的 `cos(alpha**2)` 表达式；不得按 `cos(alpha)**2` 改写。

F/M/W 只影响逐频 influence，不改中心射线；D/S/Z 会改变冻结 RayPath 的
动态 p/q，但不改变中心位置、走时或镜面反射慢度。因此不同 curvature mode
的缓存不可混用，同一 mode 下仍可跨频复用。当前安全输入仍要求有限、正的
epsilon multiplier；loop range 统一要求为正。beam shift 继续明确延期。

### 6.6 R 模式二维射线输出

run type 使用 `R`、`RG` 或 `RGO` 时进入 ray-trace；这三种写法当前等价，
安全范围限于全向源、真空海面 `V`、刚性海底 `R`、无 beam shift 和无损耗
前缀。`R*` 方向图会被明确拒绝，避免方向图截断语义被静默忽略。显式
`Nalpha` 会原样使用；写 `0` 时固定自动生成 50 个发射角。

R 模式在 integrator 的 `step zbox rbox` 记录后结束输入，不读取 TL
专用的 `MS` beam 和 image/window 两行；若仍提供这些行，会作为尾随输入
报错。每个 source 使用同一个发射角扇、独立建立并冻结缓存，再按 source-major、
launch-angle-major 顺序写 RAY。RAY 头包含 title、frequency、`1 1 NSz`、
`Nalpha 1`、上下边界深度和 `'rz'`；每条射线记录发射角、点数、top/bottom
bounce 数及 `range depth` 坐标。每次反射保留入射与反射两侧的同坐标重复点，
以兼容 Origin reader。该模式只输出 PRT/RAY，不执行逐频投影或声场累加，
也不创建 SHD。

### 6.7 A/a arrivals 与 E eigenray

run type 首字符 `A` 生成 ASCII `.arr`，小写 `a` 生成 GNU Fortran
sequential-unformatted binary `.arr`。第二字符选择 `G`（Cartesian geometric
hat）、`g`（ray-centered geometric hat）或 `B`（Cartesian geometric
Gaussian）；后续 `*`、`X/R`、irregular 等字符沿用已支持的 source pattern、
point/line 和接收布局语义。A/a 在每个 source 的独立 workspace 中按 Origin
`AddArr` 的 last-only delay/phase duplicate 规则合并；满 cell 时只以更强候选
替换第一个最弱到达，PRT 会记录 candidate、merge、cusp、replacement、discard
和 saturated-cell 统计。binary `a` 的 record marker/字段宽度与 GNU Fortran
Origin 文件一致，不承诺其他编译器的私有 unformatted ABI。

run type `E` 使用同样的 G/g/B receiver contribution traversal，但每个命中都
独立写出对应冻结射线的 prefix；不执行 arrival duplicate merge 或容量截断。
`.ray` 头中的 `Nalpha` 是发射扇数量，不是输出 block 数；正文必须读到 EOF，
允许零 block、同一 launch angle 多个 block，以及 block 数大于或小于
`Nalpha`。普通 R 仍保持 header-derived 固定 block 数，两种 reader 语义不得
混用。A/a/E 都只支持单频二维安全子集，不读取 TL 专用 beam/image 行。

### 6.8 C/I/S 相干模式

TL run type 的第一个字符选择累加方式：`C` coherent、`I` incoherent、
`S` semi-coherent；第二个字符选择 Cartesian Cerveny `C` 或 ray-centered
Cerveny `R`，例如 `CC/IC/SC` 或 `CR/IR/SR`。三种相干模式只改变逐频投影、
Influence 累加和最终场缩放，中心射线、动态射线、反射事件及其冻结缓存完全
共享。

`C` 逐 beam 累加复压力。`I` 对每条 beam 的图像源合成贡献取模平方，再累加
实强度；`S` 在逐频 source 投影时先乘
`sqrt(2) * abs(sin(omega/c * source_depth * sin(alpha)))` Lloyd mirror
幅度，随后采用与 I 相同的逐 beam 强度累加。I/S 在全部 beam 完成后取
`sqrt(real(intensity))`，再按 run type 第 4 字符执行 point 或 line 的最终
扩散缩放。

C/I/S 写出相同 frequency/source/depth/range 布局的 SHD。C 保存复压力；
I/S 仍占用兼容的复数槽，但虚部严格为零，当前 Origin 兼容负缩放使其非零
实部为负。三例最终场与效果门见
`doc/validation/i7_coherence_modes_report.json`。

### 6.9 Ray-centered Cerveny

TL run type 的第二个字符写 `R` 可选择 ray-centered Cerveny，例如 coherent
pressure 使用 `CR`，incoherent/semi-coherent 分别使用 `IR`/`SR`。该 family
复用与 Cartesian 相同的中心/动态射线缓存，但用射线局部坐标中的法向距离
决定 beam window 和接收器命中，沿射线路径插值动态 `p/q`、复走时、KMAH
及反射状态，再按第 6.4 节的 `P/V/H` 规则累加最终场。`F/M/W` beam width、
`D/S/Z` curvature、point/line source geometry 和 C/I/S 累加契约继续适用。

首个纵切只支持规则 depth×range 接收网格，并要求至少两个严格递增、等间距
的 receiver range。run type 第 5 字符若选择 irregular grid 会在 parser 和
模型边界明确拒绝；这项限制避免把 ray-centered 的逐 range 求交静默替换为
Cartesian irregular 的 Origin legacy `Rz(1)` 语义。CC/P、CR/P、CR/V、
CR/H 四例的冻结验证见
`doc/validation/i7_ray_centered_components_report.json`。

### 6.10 Q 型范围相关 SSP

当 top/SSP option 的第一个字符为 `Q` 时，`.env` 中仍需提供参考 SSP 深度
节点；这些节点定义二维网格的深度轴，并继续提供密度与原始衰减。实声速
矩阵放在同根 `.ssp` 文件中：

```text
3
0.0  0.35  0.80
1500.0  1540.0  1580.0
1500.0  1520.0  1540.0
```

第一行是 range 节点数，第二行是严格递增的 range 节点（km）；其后必须按
`.env` 参考 SSP 的深度节点顺序，每个深度恰写一行、每行恰有同样数量的
实声速（m/s）。当前安全契约要求至少两个 range 和 depth 节点、所有声速
有限且为正、总网格样本数不超过 2,000,000，并拒绝尾随记录。流式解析没有
同根 sidecar 位置，因此 Q 输入只能通过文件路径加载。

几何侧按 Origin 顺序先做深度线性、再做 range 线性，返回 `c/cr/cz/crz`
（`crr=czz=0`）；密度和逐频虚声速仍来自 `.env` 参考 SSP 的深度插值。
追迹器同时保存 depth/range 单元，步长不会跨过当前 Q range cell。`.ssp`
的 range 域应比射线空间盒多留至少一个 minimum-step 余量，否则越出 SSP
域会作为数值失败报告，而不是静默按空间盒正常退出。

体积衰减由 top/SSP option 的第 4 个字符选择。Francois–Garrison 使用 `F`，
并在 option 后紧跟一行 `temperature_C salinity_psu pH mean_depth_m`；例如：

```text
'CVWF'
20.0 35.0 8.0 0.0
```

Biological 使用 `B`，随后是层数以及每层的
`top_depth_m bottom_depth_m resonance_frequency_Hz Q coefficient_dB_per_km`：

```text
'CVWB'
2
400.0 600.0 5000.0 10.0 0.01
500.0 700.0 1000.0  6.0 0.01
```

普通 `A` 型海床材料记录支持流体或弹性半空间：

```text
'A' 0.0
100.0  2000.0  1000.0  2.0  0.5  1.0 /
```

六列依次为 `depth_m alphaR_mps betaR_mps rho_g_cm3 alphaI betaI`；五列写法
省略 `betaI` 并按零处理。`betaR>0` 时启用弹性 P/S 反射，P、S 的原始衰减
都使用 top/SSP option 指定的同一单位和体积衰减模型，并只在逐频投影时
换算。`betaR=0` 时要求 `betaI=0`。当前安全子集要求材料深度与水层底深
一致；原版允许二者不同的记录尚未开放。

层深度端点闭合，重叠层按输入顺序累加；允许 0 层，安全上限为 200 层。
F2CPP 对非有限值和非物理参数采用比原版更严格的显式拒绝策略。

bottom `G` 在 bottom option 后读取 `depth_m Mz_phi`：

```text
'G' 0.0
100.0  3.0 /
```

程序按 UW-APL grain-size 分段公式保存 `vr/rhor/alpha2_f`，在每个冻结反射
事件处用当地水声速生成有效流体底质。沉积物损耗固定按 Origin 的 `L` loss
parameter 换算，不叠加 top option 的 Thorp/Francois–Garrison/Biological
体积衰减；水体自身仍照常应用这些衰减。当前安全子集要求记录深度等于水层
底深、派生声速比/密度比为正，并拒绝 `G+LL`。上游 2D Fortran 的 `G`
初始化遗漏已在 oracle 中按其 3D 正确路径修复并单独记录，不复刻未初始化
NaN 行为。

bottom `F` 不读取材料记录，而是从与 `.env` 同根的 `.brc` 文件读取反射表：

```text
4
 0.0  0.20    0.0
30.0  0.40   30.0
60.0  0.70  100.0
90.0  0.90  160.0
```

首行是至少为 2 的节点数，随后每行依次为 `grazing_angle_deg magnitude
phase_deg`。角度必须严格递增，幅值必须非负；相位可超过 `[-180,180]`，
程序不会自动解缠。反射时以 `abs(atan2(Th,Tg))` 转为度并折叠到
`[0,90]`，幅值与已展开相位分别线性插值；表域外取零幅值、零相位。即使
节点幅值为零，其表内相位仍会进入累计相位；`F` 不使用 `A/G` 的 `1e-5`
小系数抑制，但后续通用 active-amplitude 门仍生效。`.brc` 只影响逐频投影，
不写回冻结轨迹；可与平坦或 short-format `LS/C` bathymetry 组合，当前拒绝
`F+LL`。CLI 必须使用文件根路径运行，stream-only 解析无法解析 sibling
`.brc`。为复刻 Origin，域判断和二分区间先使用单精度舍入后的查询角，
插值权重仍使用原双精度角；这只影响表端点附近约半个单精度 ULP 的窄区间。

折线边界文件示例：

```text
LS
3
0.0  100.0
1.0  120.0
2.0   90.0
```

第一列为 km，第二列为 m；范围必须严格递增，至少两个节点。程序与原版一样
在首末节点外作常深水平延拓。顶边界 option 的有效写法如 `'CVW ~'`（内部
空格不可省略），底边界如 `'R~'` 或 `'A~'`。

`LL` long format 每个节点携带一套流体或弹性 P/S 地声参数：

```text
LL
3
-1.0  1000.0  1600.0  0.0  1.5  0.10  0.0
 2.0  1000.0  1800.0  0.0  2.0  0.20  0.0
 4.0  1000.0  2000.0  0.0  2.5  0.30  0.0
```

七列依次为 `range_km depth_m alphaR betaR rho_g_cm3 alphaI betaI`；
`betaR=betaI=0` 表示流体材料，非零值表示 elastic shear 参数。材料归属于节点右侧的 segment，精确落在节点时保留
到达侧 segment。声学半空间反射时，活动节点的原始材料随事件写入冻结轨迹，
各频率再按 ENV 的衰减单位换算；与原版一致，`LL` 的体积衰减求值深度为
`1e20 m`。真空/刚性边界仍由边界条件优先，忽略 long-format 材料。

可从共享标准案例模板开始修改：

```text
test/standard_cases/cases/<case-id>/origin.env.in
```

## 7. 输出说明

### PRT

PRT 是文本日志，包含：

- 解析后的主要环境和网格配置；
- 发射角、射线数、轨迹点数和缓存字节数；
- C/I/S TL 模式的 Trace、Project、Influence、Scale、SHD 分阶段时间，
  A/a 的 workspace/accumulator/ARR 统计，E 的 hit/prefix/RAY 统计，或
  ray-trace 模式的 Trace 与 RAY 写出时间；
- 成功标记或致命错误。

C/I/S TL 成功文件以以下标记结束：

```text
Bellhop F2CPP completed successfully
```

ray-trace 成功文件以 `Bellhop F2CPP ray trace completed successfully` 结束。
A/a 和 E 分别以 `Bellhop F2CPP arrivals completed successfully`、
`Bellhop F2CPP eigenray completed successfully` 结束。

### SHD

SHD 保存单频复压力场：

- 内部累加使用 `complex<double>`；
- 仅在 writer 边界量化为 `complex<float>`；
- 布局可由仓库的
  [`test/PlotRead/bellhop_io_py`](../../test/PlotRead/README.md) 读取；
- 维度顺序为 frequency/source depth/receiver depth/range。

SHD 由 C/I/S TL 模式生成，三者使用相同布局；I/S 的复数压力槽虚部为零。

### ARR

ASCII `A` 与 binary `a` 保存同一 source-major、receiver-cell-major、arrival-
major 语义：频率、source/receiver 轴、每 source 的最大到达数、每 cell 的到达
计数，以及 amplitude、phase、复 delay、source/receiver declination 和
top/bottom bounce。ASCII 适合检查与跨工具交换；binary 复刻当前 GNU Fortran
Origin 的 sequential-unformatted 小端 record 布局。零到达 cell/source 是合法
产品，不代表运行失败。

### RAY

RAY 是二维 `rz` 文本轨迹文件，按 source-major、launch-angle-major 顺序保存
每条射线的发射角、点数、上下边界反射次数与坐标。反射前后的同坐标点会
同时保留；可用它们重建入射/反射折点。R 模式正文有固定射线数；E 模式按
命中写变长 prefix stream 到 EOF，并允许 header-only 零命中产品。两者都不
同时生成 SHD/ARR。

### 输出安全与资源上限

C/I/S TL 在分配场工作区和打开 SHD 前，先检查接收器场值总数、
source × workspace 总量、SHD record words/bytes、最终 record 号及总文件
字节是否可由 Origin 兼容整数和本机流偏移安全表达。当前项目上限为每次
TL 求解保留不超过 2,000,000 个复数场值、总射线数不超过
2,000,000；R/A/a/E 不分配 TL 压力工作区，因此不受未使用的场 workspace
上限误伤，但仍执行总射线数及各自 ARR/RAY layout/capacity 门。

SHD、RAY 和 ARR 都先写同目录临时文件，完整关闭成功后才发布为最终产品。发布
失败时旧的有效文件保持不变，临时文件会清理。同一 file-root 从 CC 切换到
R/A/a/E 或切回 CC 时，成功后会删除其他模式的陈旧产品；启动时也会清除
陈旧 `.shd.tmp/.ray.tmp/.arr.tmp`。PRT 仍会在失败时写出 `FATAL ERROR` 诊断。
标准案例的 `run/test` 阶段同样先删除旧 manifest、PRT、SHD、RAY、ARR 和临时
文件，避免失败执行被上一轮结果伪装为通过。

## 8. 常见问题

### `No module named tomllib`

当前 `python3` 低于 3.11。切换到 Python 3.11 或更高版本。

### `No module named numpy`

当前 Python 环境没有 NumPy。安装 NumPy 或切换到项目已有的 Python/conda
测试环境。

### `Could not read presets`

确认当前目录是 `Bellhop_F2CPP/`，并检查 CMake 版本不低于 3.24。

### 程序提示 `unable to open environment file`

传入的是 file-root，不是文件名。对于 `/data/example.env`，参数应为
`/data/example`。

### PRT 中出现 `FATAL ERROR`

读取该行后的具体原因。常见情况是 `.env` 使用了未支持的 Bellhop 模式、
非等距规则接收距离或缺失 `.ati/.bty/.trc/.brc/.sbp` 附属文件。

### 5 kHz 标准案例占用较多内存

这是完整冻结轨迹缓存的预期成本。当前 5 kHz 基线约含 1013 万个轨迹点，
缓存约 1.23 GB，实测峰值 RSS 约 1.32 GB。

## 9. 相关文档

- [F2CPP 文档索引](./README.md)
- [构建与实施计划](./BUILD_PLAN.md)
- [最终派生清单](./DERIVATION_MANIFEST.md)
- [共享标准算例说明](../../test/standard_cases/README.md)
- [全项目基础变量、单位与数值规范](../../doc/04-基础变量单位与数值规范.md)
