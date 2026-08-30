# FP-2I Line Source Closure 批次报告

## 1. 批次概况

- **批次代号**：`FP-2I`
- **批次目标**：全面闭环 RayReuse 线声源模型与产品缩放（SRC-02、PRD-08），包括 ENV 解析 RunType 第 4 位 `'X'`、`SimulationCase` 中的不可变 `SourceGeometry` 模型表示、Cartesian Cerveny / Ray-centered Cerveny / Geometric Hat / Geometric Gaussian 中的线声源波束 ratio 计算、Simple Gaussian 显式拒绝线声源、PressureScaling 线声源柱面扩散与常数因子缩放、ArrivalWriter 接收点线声源幅值缩放、全求解器（单频/宽带串行复用/宽带并行复用/Arrival/Eigenray）接入、三方（Origin/F2CPP/RayReuse）oracle 闭环、宽带三模式逐字节一致性以及 frozen-cache 不可变性。
- **前置批次**：`FP-2F ACCEPTED`，`FP-2G ACCEPTED`，`FP-2H ACCEPTED`
- **代码基线与修订**：
  - 基线提交：`099a2b1 feat(rayreuse): complete FP-2H attenuation closure`
- **构建环境与配置**：
  - 编译器：AppleClang 21.0.0.21000101 (ARM64)
  - 构建类型：`Release`，`-DRAYREUSE_WARNINGS_AS_ERRORS=ON`
  - 隔离构建目录：`Bellhop_RayReuse/build/fp2i-clean`
- **可执行文件信息**：
  - Origin 路径：`Bellhop_origin/bin/bellhop`（SHA-256: `d398873b5d4916572ee651ee9917f7b9cd1ba1705be650c419eb55ab4951c537`）
  - F2CPP 路径：`Bellhop_F2CPP/build/release/bellhop_f2cpp`（SHA-256: `1689973a0caf22d91c734681025a27e096b8faa95e6868e0bc1f479e61ef03e9`）
  - RayReuse 路径：`Bellhop_RayReuse/build/fp2i-clean/bellhop_rayreuse`（SHA-256: `5e60ac103a46dded9bef5b16978fb26c628f92dfb2accf41e7d827d73e85136c`）

---

## 2. 任务执行与审查闭环

| 任务 | 类型 | 状态 | 独立审查 / 验收 | 关键结果与证据 |
|---|---|---|---|---|
| **I00** 冻结前置基线 | STANDARD | DONE | N/A (coordinator) | 捕获 pre-FP-2I 基线、CTest 41/41、pytest 187/187、make unit 172/172 全部通过 |
| **I01** 模型与核心类型 | ADVANCED | DONE | **PASS (I01-R)** | `simulation_case.hpp` 增加 `enum class SourceGeometry { Point, Line }`，`SimulationCase` 拥有 `sourceGeometry_` 与 getter，保留构造函数默认参数与旧调用兼容，单元测试覆盖 Point/Line 保存、Simple Gaussian 拒绝、非法 enum 校验 |
| **I02** ENV 解析与 PRT 报告 | STANDARD | DONE | **PASS (I02-R)** | 解析 RunType 第 4 位 `'X'` 为 `SourceGeometry::Line`，空格与 `'R'` 为 `Point`；Simple Gaussian 拒绝 `'CS X'`；PRT 生成 `"Line source (Cartesian coordinates)"` 与 `"Point source (cylindrical coordinates)"` |
| **I03** TL 波束 Influence ratio 内核 | ADVANCED | DONE | **PASS (I03-R)** | Cartesian Cerveny、Ray-centered Cerveny、Geometric Hat、Geometric Gaussian 均接入 `SourceGeometry`；线声源使用 `ratio = 1.0`（Geometric Gaussian 使用 `1.0 / sqrt(2*pi)`）；Simple Gaussian 严格要求 Point source；组件测试通过 |
| **I04** 场压强扩散缩放 (Pressure Scaling) | ADVANCED | DONE | **PASS (I04-R)** | 实现 `constexpr float kLegacyPi = 3.14159265F; const float linePrefix = -4.0F * std::sqrt(kLegacyPi);`，线声源因子 `static_cast<double>(linePrefix) * beamScale` 作用于包含 0 距离在内的所有接收距离；支持相干压强与非相干强度转换；单元测试覆盖锚点与精度对齐 |
| **I05** 到达结构幅值缩放 (Arrival Writer) | STANDARD | DONE | **PASS (I05-R)** | `ArrivalWriter` 实现 `sourceScale(SourceGeometry, range)`，线声源使用 `4.0F * sqrt(pi)` 缩放每条到达记录幅值；点声源保持 `1/sqrt(range)`（0 距离 `1e5F`）；ASCII 与 Binary 格式均通过 |
| **I06** 求解器与宽带管线接入 | ADVANCED | DONE | **PASS (I06-R)** | `SingleFrequencySolver`、`BroadbandNonreuseSolver`、`SerialRayReuseSolver`、`ParallelRayReuseSolver`、`ArrivalSolver`、`EigenraySolver` 贯通 `sourceGeometry()`；证明几何追踪与 `RayPathCache` 零改动，线声源为频域局部幅值计算；`--verify-cache` 验证通过 |
| **I07** 标准算例与三方 Oracle 闭环 | STANDARD | DONE | N/A (coordinator) | `source_geometry_line` 与 `arrival_line_directional_multisource` case.toml 增加 rayreuse；`source_geometry_point_explicit` 增加 rayreuse；F2CPP↔RayReuse 0 差异/0 ULP，Origin↔RayReuse 容差通过；`validate_i8_arrivals.py` 全量 9 案例全部 passed |
| **I08** 宽带模式与缓存不可变性矩阵 | STANDARD | DONE | N/A (coordinator) | `source_geometry_line` 与 `arrival_line_directional_multisource` 宽带 profile 在 `nonreuse`、`reuse`、`parallel` 下产品逐字节一致（`cmp` 0）；追踪次数呈现 `2/1/1`（单源）与 `4/2/2`（双源）；`--verify-cache` before==after 严格守恒 |
| **I09** 文档发布与批次报告 | SIMPLE | DONE | N/A (coordinator) | 编写批次报告，更新 Worklist 为 `READY_FOR_FINAL_REVIEW` |

---

## 3. 精确执行命令与制品证明

### 3.1 集中构建与门禁命令

```bash
# 1. 干净隔离构建
rm -rf Bellhop_RayReuse/build/fp2i-clean
uvx cmake -S Bellhop_RayReuse -B Bellhop_RayReuse/build/fp2i-clean -DCMAKE_BUILD_TYPE=Release -DRAYREUSE_WARNINGS_AS_ERRORS=ON
uvx cmake --build Bellhop_RayReuse/build/fp2i-clean --parallel

# 2. CTest 套件 (41/41 PASSED)
uvx --from cmake ctest --test-dir Bellhop_RayReuse/build/fp2i-clean --output-on-failure

# 3. Python 单元测试 (187/187 PASSED)
uv run pytest

# 4. Standard cases 单元测试 (172/172 PASSED)
uv run make -C test/standard_cases test-unit

# 5. 到达结构全量三方验证器 (9 案例全部 PASS)
uv run python test/standard_cases/codes/validate_i8_arrivals.py \
  --origin-executable Bellhop_origin/bin/bellhop \
  --f2cpp-executable Bellhop_F2CPP/build/release/bellhop_f2cpp \
  --rayreuse-executable Bellhop_RayReuse/build/fp2i-clean/bellhop_rayreuse \
  --results-root /tmp/fp2i_test \
  --output /tmp/fp2i_arrivals_report.json

# 6. 跨执行模式逐字节 cmp 校验
cmp /tmp/fp2i_modes/nonreuse/rayreuse/source_geometry_line/broadband_smoke/broadband/source_geometry_line_broadband_smoke_broadband.shd /tmp/fp2i_modes/reuse/rayreuse/source_geometry_line/broadband_smoke/broadband/source_geometry_line_broadband_smoke_broadband.shd
cmp /tmp/fp2i_modes/reuse/rayreuse/source_geometry_line/broadband_smoke/broadband/source_geometry_line_broadband_smoke_broadband.shd /tmp/fp2i_modes/parallel/rayreuse/source_geometry_line/broadband_smoke/broadband/source_geometry_line_broadband_smoke_broadband.shd

cmp /tmp/fp2i_modes/nonreuse/rayreuse/arrival_line_directional_multisource/broadband_smoke/broadband/arrival_line_directional_multisource_broadband_smoke_broadband_f000_500Hz.arr /tmp/fp2i_modes/reuse/rayreuse/arrival_line_directional_multisource/broadband_smoke/broadband/arrival_line_directional_multisource_broadband_smoke_broadband_f000_500Hz.arr
cmp /tmp/fp2i_modes/reuse/rayreuse/arrival_line_directional_multisource/broadband_smoke/broadband/arrival_line_directional_multisource_broadband_smoke_broadband_f000_500Hz.arr /tmp/fp2i_modes/parallel/rayreuse/arrival_line_directional_multisource/broadband_smoke/broadband/arrival_line_directional_multisource_broadband_smoke_broadband_f000_500Hz.arr

cmp /tmp/fp2i_modes/nonreuse/rayreuse/arrival_line_directional_multisource/broadband_smoke/broadband/arrival_line_directional_multisource_broadband_smoke_broadband_f001_1000Hz.arr /tmp/fp2i_modes/reuse/rayreuse/arrival_line_directional_multisource/broadband_smoke/broadband/arrival_line_directional_multisource_broadband_smoke_broadband_f001_1000Hz.arr
cmp /tmp/fp2i_modes/reuse/rayreuse/arrival_line_directional_multisource/broadband_smoke/broadband/arrival_line_directional_multisource_broadband_smoke_broadband_f001_1000Hz.arr /tmp/fp2i_modes/parallel/rayreuse/arrival_line_directional_multisource/broadband_smoke/broadband/arrival_line_directional_multisource_broadband_smoke_broadband_f001_1000Hz.arr

# 7. --verify-cache 校验
clean_exe="$(pwd)/Bellhop_RayReuse/build/fp2i-clean/bellhop_rayreuse"
(cd /tmp/fp2i_verify_cache/line_tl_reuse && "$clean_exe" case --frequencies-hz 50,100 --execution-mode reuse --verify-cache)
(cd /tmp/fp2i_verify_cache/line_tl_parallel && "$clean_exe" case --frequencies-hz 50,100 --execution-mode parallel --verify-cache)
(cd /tmp/fp2i_verify_cache/line_arr_reuse && "$clean_exe" case --frequencies-hz 500,1000 --execution-mode reuse --verify-cache)
(cd /tmp/fp2i_verify_cache/line_arr_parallel && "$clean_exe" case --frequencies-hz 500,1000 --execution-mode parallel --verify-cache)

# 8. Git 受保护路径检查 (0 diff)
git diff --check
git diff --exit-code -- Bellhop_origin Bellhop_F2CPP
git diff --exit-code -- \
  Bellhop_RayReuse/src/cache/ray_path_cache.cpp \
  Bellhop_RayReuse/include/rayreuse/cache/ray_path_cache.hpp \
  Bellhop_RayReuse/include/rayreuse/ray/ray_path.hpp
```

### 3.2 跨模式逐字节一致性与 SHA-256 汇总

| 算例 / Profile | 频率 (Hz) | 产品类型 | 产品 SHA-256（三模式完全相同） | 追踪次数 (nonreuse / reuse / parallel) |
|---|---|---|---|---|
| `source_geometry_line` (smoke) | 50, 100 | SHD (TL) | `544d2b7dbc8e87fcd618a5d7ef3ffe88eee1c6c40bf797e17dc721eff4d6741d` | `2 / 1 / 1` |
| `arrival_line_directional_multisource` (smoke) | 500 | ARR (f000) | `f923d3be699db7f4465f16be3f256afed94f1b3e5995d1e0dddc334b314704d7` | `4 / 2 / 2` |
| `arrival_line_directional_multisource` (smoke) | 1000 | ARR (f001) | `0899cf03da4107ca4c6ff415111cc795e81e9aceafa23db0ec3c6b497b49d2f3` | `4 / 2 / 2` |

### 3.3 缓存指纹与不可变性

在 `reuse` 和 `parallel` 模式下带 `--verify-cache` 运行时，前后语义指纹严格守恒：
- `source_geometry_line`: `11632125087325642441` (before == after)
- `arrival_line_directional_multisource` (source 0): `4994599520005947392` (before == after)
- `arrival_line_directional_multisource` (source 1): `5953004235750977769` (before == after)

---

## 4. 验收结论

FP-2I 全面闭环了 `SRC-02`（Line Source）与 `PRD-08`（Line Source Product Scaling）。
全量 CTest (41/41)、pytest (187/187)、make test-unit (172/172)、Arrivals 三方 oracle (9/9) 全部通过。
三模式（nonreuse / reuse / parallel）逐字节一致，frozen cache 不可变性守恒。
至此，`Bellhop_F2CPP → Bellhop_RayReuse` 的最后一个已知 Feature Parity GAP 已全部关闭。

**结论：`FP-2I ACCEPTED / CLOSED`**
