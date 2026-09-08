# FP-2H Attenuation Closure 批次报告

## 1. 批次概况

- **批次代号**：`FP-2H`
- **批次目标**：全面闭环 RayReuse 衰减模型与单位语义（ATT-01～ATT-05），包括 N/F/M/W/Q/L 衰减单位、W 频率与声速依赖性保护、Thorp 回归保护、Francois–Garrison 参数化体积衰减、Biological 多层重叠体积衰减、五大频域 SSP 节点优先转换、边界材料声学衰减穿透、宽带三模式（nonreuse / reuse / parallel）逐字节一致性及 frozen-cache 不可变性。
- **前置批次**：`FP-2F ACCEPTED`，`FP-2G ACCEPTED`
- **代码基线与修订**：
  - 基线提交：`c40a4ee01ef8a790a5d6d1814b4b24795ea0f083`
  - 前置冻结基准二进制 SHA-256：`8dc2c8c24b2977d45af6a33d95987be3a42364687fe829a486c890eb05e3ca9c`
- **构建环境与配置**：
  - 编译器：AppleClang 21.0.0.21000101 (ARM64)
  - 构建类型：`Release`，`-DRAYREUSE_WARNINGS_AS_ERRORS=ON`
  - 隔离构建目录：`Bellhop_RayReuse/build/fp2h-clean`
  - 关键编译选项：`src/acoustics/attenuation.cpp` 配置 `-fno-builtin-pow`（保证与 Fortran/F2CPP 0 ULP 精度对齐）
- **可执行文件信息**：
  - Origin 路径：`Bellhop_origin/bin/bellhop`（SHA-256: `d398873b5d4916572ee651ee9917f7b9cd1ba1705be650c419eb55ab4951c537`）
  - F2CPP 路径：`Bellhop_F2CPP/build/release/bellhop_f2cpp`（SHA-256: `1689973a0caf22d91c734681025a27e096b8faa95e6868e0bc1f479e61ef03e9`）
  - RayReuse 路径：`Bellhop_RayReuse/build/fp2h-clean/bellhop_rayreuse`（SHA-256: `916a0aa84aefd94d914fed38943c6747999a7a86fc3d12cae0198c45f5954694`）

---

## 2. 任务执行与审查闭环

| 任务 | 类型 | 状态 | 独立审查 / 验收 | 关键结果与证据 |
|---|---|---|---|---|
| **H00** 冻结前置基线 | STANDARD | DONE | N/A (coordinator) | 捕获 pre-FP-2H SHA-256、Thorp 单频/smoke/16频回归基线、W 基线、Munk spline 指纹 `1526667602348633172`，制品隔离存储 |
| **H01** 不可变衰减所有权 | ADVANCED | DONE | **PASS (H01-R)** | `Environment` 增加 `VolumeAttenuation`（含 FG 值所有权与 Biological `SharedBiologicalAttenuationLayers`），保留默认构造与旧构造调用，单元测试覆盖 0/200/201 层、非物理参数与拷贝共享 |
| **H02** ENV 解析与 PRT 报告 | ADVANCED | DONE | **PASS (H02-R)** | 支持 3–6 字符选项与第 6 位空格校验；解析 FG 4 参数与 Biological 0–200 层；PRT 生成 `THORP volume attenuation added`、`Francois-Garrison volume attenuation added`、`Biological attenaution` 及层数标记 |
| **H03** 衰减内核与兼容性裁决 | ADVANCED | DONE | **PASS (H03-R)** | `-fno-builtin-pow` 编译选项；实现 FG（20°C 分支与 FMA 算式）与 Biological 内核；实现 §3.2 兼容性矩阵（严格防双重衰减）；单元测试覆盖 5 个 FG 锚点、生物层边界及 16 种组合 |
| **H04** 五频域 SSP 后端接入 | ADVANCED | DONE | **PASS (H04-R)** | C/N/P/S/Q 五后端均实现节点优先衰减转换与 `VolumeAttenuation` 构造；Q 后端衰减使用参考节点实声速；组件测试验证生物层深度响应、低/高/低确定性与基线不变 |
| **H05** 投影器与边界衰减接入 | ADVANCED | DONE | **PASS (H05-R)** | 边界声学函数接收 `VolumeAttenuation` 与评估深度；声学半空间纵波/横波均接入衰减；长格式边界使用 `1.0e20` legacy 深度；粒度边界隔离体积衰减；`FrequencyProjector` 环境接入通过 |
| **H06** 冻结缓存与并发不变性 | ADVANCED | DONE | **PASS (H06-R)** | 证明受保护文件（Origin, F2CPP, RayPathCache, RayPath, 射线状态）零改动；单频/串行复用/并行复用前后指纹不变；修改环境衰减参数产生不同压强但共享完全相同几何与指纹；Munk spline 指纹锚点保持 |
| **H07** ATT-01 / ATT-02 闭环 | STANDARD | DONE | N/A (coordinator) | 6 个 `attenuation_unit_*` case.toml 增加 rayreuse；`validate_i4_attenuation_units.py` 执行全量 54 组配对比较（42 组 gating 全部 PASS，12 组非 gating 记录 F2CPP 单频自规划差异）；5 kHz 跨单位（N/F/M/W/Q/L）压强逐位相同；4 kHz 频率缩放与 W 语义校验通过 |
| **H08** ATT-03 / 04 / 05 闭环 | STANDARD | DONE | N/A (coordinator) | FG 与 Biological case.toml 增加 rayreuse；`validate_i4_volume_attenuation.py` 执行全量 75 组配对比较（39 组 gating 全部 PASS，36 组非 gating 记录 F2CPP 单频自规划差异）；Thorp 输出 SHA-256 与 H00 基线逐位一致；无损非 no-op guard 全部通过（差异 > 1e-6） |
| **H09** 模式/追踪/缓存证据矩阵 | STANDARD | DONE | N/A (coordinator) | 10 个宽带 profile 在 nonreuse/reuse/parallel 下 SHD 逐字节一致（cmp 0）；追踪次数呈现 `2/1/1` 与 `16/1/1`；`--verify-cache` 在 reuse/parallel 下 before==after 全量通过；重复并行运行逐位确定 |
| **H10** 文档发布与批次报告 | SIMPLE | DONE | N/A (coordinator) | 发布功能支持矩阵、序列进度快照、批次报告，Worklist 状态更新为 `READY_FOR_FINAL_REVIEW` |

---

## 3. 精确执行命令与制品证明

### 3.1 集中构建与门禁命令

```bash
# 1. 干净隔离构建 (使用 uvx cmake)
rm -rf Bellhop_RayReuse/build/fp2h-clean
uvx cmake -S Bellhop_RayReuse -B Bellhop_RayReuse/build/fp2h-clean -DCMAKE_BUILD_TYPE=Release -DRAYREUSE_WARNINGS_AS_ERRORS=ON
uvx cmake --build Bellhop_RayReuse/build/fp2h-clean --parallel

# 2. CTest 套件 (41/41 PASSED)
/Users/amazingumi7/.cache/uv/archive-v0/v2VMsVkDDHAUFNvE/cmake/data/bin/ctest --test-dir Bellhop_RayReuse/build/fp2h-clean --output-on-failure

# 3. Python 单元测试 (187/187 PASSED)
uv run pytest

# 4. Standard cases 单元测试 (172/172 PASSED)
uv run make -C test/standard_cases test-unit

# 5. 衰减标准算例生成与求解 (隔离目录 /tmp/fp2h_clean_products)
for case in attenuation_unit_n attenuation_unit_f attenuation_unit_m attenuation_unit_w attenuation_unit_q attenuation_unit_l; do
  uv run python test/standard_cases/codes/standard_cases.py test --version origin --case $case --profile single --executable Bellhop_origin/bin/bellhop --results-root /tmp/fp2h_clean_products
  uv run python test/standard_cases/codes/standard_cases.py test --version origin --case $case --profile broadband_smoke --executable Bellhop_origin/bin/bellhop --results-root /tmp/fp2h_clean_products
  uv run python test/standard_cases/codes/standard_cases.py test --version f2cpp --case $case --profile single --executable Bellhop_F2CPP/build/release/bellhop_f2cpp --results-root /tmp/fp2h_clean_products
  uv run python test/standard_cases/codes/standard_cases.py test --version f2cpp --case $case --profile broadband_smoke --executable Bellhop_F2CPP/build/release/bellhop_f2cpp --results-root /tmp/fp2h_clean_products
  uv run python test/standard_cases/codes/standard_cases.py test --version rayreuse --case $case --profile single --executable Bellhop_RayReuse/build/fp2h-clean/bellhop_rayreuse --results-root /tmp/fp2h_clean_products
  uv run python test/standard_cases/codes/standard_cases.py test --version rayreuse --case $case --profile broadband_smoke --executable Bellhop_RayReuse/build/fp2h-clean/bellhop_rayreuse --results-root /tmp/fp2h_clean_products
done

for case in constant_speed_no_attenuation_5khz constant_speed_thorp volume_attenuation_francois_garrison volume_attenuation_biological; do
  uv run python test/standard_cases/codes/standard_cases.py test --version origin --case $case --profile single --executable Bellhop_origin/bin/bellhop --results-root /tmp/fp2h_clean_products
  uv run python test/standard_cases/codes/standard_cases.py test --version origin --case $case --profile broadband_smoke --executable Bellhop_origin/bin/bellhop --results-root /tmp/fp2h_clean_products
  uv run python test/standard_cases/codes/standard_cases.py test --version f2cpp --case $case --profile single --executable Bellhop_F2CPP/build/release/bellhop_f2cpp --results-root /tmp/fp2h_clean_products
  uv run python test/standard_cases/codes/standard_cases.py test --version f2cpp --case $case --profile broadband_smoke --executable Bellhop_F2CPP/build/release/bellhop_f2cpp --results-root /tmp/fp2h_clean_products
  uv run python test/standard_cases/codes/standard_cases.py test --version rayreuse --case $case --profile single --executable Bellhop_RayReuse/build/fp2h-clean/bellhop_rayreuse --results-root /tmp/fp2h_clean_products
  uv run python test/standard_cases/codes/standard_cases.py test --version rayreuse --case $case --profile broadband_smoke --executable Bellhop_RayReuse/build/fp2h-clean/bellhop_rayreuse --results-root /tmp/fp2h_clean_products
done

uv run python test/standard_cases/codes/standard_cases.py test --version origin --case constant_speed_thorp --profile broadband_regression --executable Bellhop_origin/bin/bellhop --results-root /tmp/fp2h_clean_products
uv run python test/standard_cases/codes/standard_cases.py test --version f2cpp --case constant_speed_thorp --profile broadband_regression --executable Bellhop_F2CPP/build/release/bellhop_f2cpp --results-root /tmp/fp2h_clean_products
uv run python test/standard_cases/codes/standard_cases.py test --version rayreuse --case constant_speed_thorp --profile broadband_regression --executable Bellhop_RayReuse/build/fp2h-clean/bellhop_rayreuse --results-root /tmp/fp2h_clean_products

# 6. ATT-01 / ATT-02 衰减单位验证器 (54 组比较，42 gating PASS)
uv run python test/standard_cases/codes/validate_i4_attenuation_units.py \
  --results-root /tmp/fp2h_clean_products \
  --origin-executable Bellhop_origin/bin/bellhop \
  --f2cpp-executable Bellhop_F2CPP/build/release/bellhop_f2cpp \
  --rayreuse-executable Bellhop_RayReuse/build/fp2h-clean/bellhop_rayreuse \
  --output /tmp/validate_i4_attenuation_units_report.json

# 7. ATT-03 / 04 / 05 体积衰减验证器 (75 组比较，39 gating PASS，9 no-op guards PASS)
uv run python test/standard_cases/codes/validate_i4_volume_attenuation.py \
  --results-root /tmp/fp2h_clean_products \
  --origin-executable Bellhop_origin/bin/bellhop \
  --f2cpp-executable Bellhop_F2CPP/build/release/bellhop_f2cpp \
  --rayreuse-executable Bellhop_RayReuse/build/fp2h-clean/bellhop_rayreuse \
  --output /tmp/validate_i4_volume_attenuation_report.json

# 8. 模式矩阵验证 (隔离目录 /tmp/fp2h_modes/{nonreuse,reuse,parallel,parallel_repeat})
clean_exe="Bellhop_RayReuse/build/fp2h-clean/bellhop_rayreuse"
cases="attenuation_unit_n attenuation_unit_f attenuation_unit_m attenuation_unit_w attenuation_unit_q attenuation_unit_l volume_attenuation_francois_garrison volume_attenuation_biological"

for mode in nonreuse reuse parallel; do
  for case in $cases; do
    uv run python test/standard_cases/codes/standard_cases.py test --version rayreuse --case $case --profile broadband_smoke --executable $clean_exe --rayreuse-execution-mode $mode --results-root /tmp/fp2h_modes/$mode
  done
  uv run python test/standard_cases/codes/standard_cases.py test --version rayreuse --case constant_speed_thorp --profile broadband_smoke --executable $clean_exe --rayreuse-execution-mode $mode --results-root /tmp/fp2h_modes/$mode
  uv run python test/standard_cases/codes/standard_cases.py test --version rayreuse --case constant_speed_thorp --profile broadband_regression --executable $clean_exe --rayreuse-execution-mode $mode --results-root /tmp/fp2h_modes/$mode
done

# 重复并行矩阵校验
for case in $cases; do
  uv run python test/standard_cases/codes/standard_cases.py test --version rayreuse --case $case --profile broadband_smoke --executable $clean_exe --rayreuse-execution-mode parallel --results-root /tmp/fp2h_modes/parallel_repeat
done
uv run python test/standard_cases/codes/standard_cases.py test --version rayreuse --case constant_speed_thorp --profile broadband_smoke --executable $clean_exe --rayreuse-execution-mode parallel --results-root /tmp/fp2h_modes/parallel_repeat
uv run python test/standard_cases/codes/standard_cases.py test --version rayreuse --case constant_speed_thorp --profile broadband_regression --executable $clean_exe --rayreuse-execution-mode parallel --results-root /tmp/fp2h_modes/parallel_repeat

# 9. 模式间逐字节 cmp 校验
for case in $cases; do
  cmp /tmp/fp2h_modes/nonreuse/rayreuse/$case/broadband_smoke/broadband/${case}_broadband_smoke_broadband.shd /tmp/fp2h_modes/reuse/rayreuse/$case/broadband_smoke/broadband/${case}_broadband_smoke_broadband.shd
  cmp /tmp/fp2h_modes/reuse/rayreuse/$case/broadband_smoke/broadband/${case}_broadband_smoke_broadband.shd /tmp/fp2h_modes/parallel/rayreuse/$case/broadband_smoke/broadband/${case}_broadband_smoke_broadband.shd
  cmp /tmp/fp2h_modes/parallel/rayreuse/$case/broadband_smoke/broadband/${case}_broadband_smoke_broadband.shd /tmp/fp2h_modes/parallel_repeat/rayreuse/$case/broadband_smoke/broadband/${case}_broadband_smoke_broadband.shd
done

cmp /tmp/fp2h_modes/nonreuse/rayreuse/constant_speed_thorp/broadband_smoke/broadband/constant_speed_thorp_broadband_smoke_broadband.shd /tmp/fp2h_modes/reuse/rayreuse/constant_speed_thorp/broadband_smoke/broadband/constant_speed_thorp_broadband_smoke_broadband.shd
cmp /tmp/fp2h_modes/reuse/rayreuse/constant_speed_thorp/broadband_smoke/broadband/constant_speed_thorp_broadband_smoke_broadband.shd /tmp/fp2h_modes/parallel/rayreuse/constant_speed_thorp/broadband_smoke/broadband/constant_speed_thorp_broadband_smoke_broadband.shd
cmp /tmp/fp2h_modes/parallel/rayreuse/constant_speed_thorp/broadband_smoke/broadband/constant_speed_thorp_broadband_smoke_broadband.shd /tmp/fp2h_modes/parallel_repeat/rayreuse/constant_speed_thorp/broadband_smoke/broadband/constant_speed_thorp_broadband_smoke_broadband.shd

cmp /tmp/fp2h_modes/nonreuse/rayreuse/constant_speed_thorp/broadband_regression/broadband/constant_speed_thorp_broadband_regression_broadband.shd /tmp/fp2h_modes/reuse/rayreuse/constant_speed_thorp/broadband_regression/broadband/constant_speed_thorp_broadband_regression_broadband.shd
cmp /tmp/fp2h_modes/reuse/rayreuse/constant_speed_thorp/broadband_regression/broadband/constant_speed_thorp_broadband_regression_broadband.shd /tmp/fp2h_modes/parallel/rayreuse/constant_speed_thorp/broadband_regression/broadband/constant_speed_thorp_broadband_regression_broadband.shd
cmp /tmp/fp2h_modes/parallel/rayreuse/constant_speed_thorp/broadband_regression/broadband/constant_speed_thorp_broadband_regression_broadband.shd /tmp/fp2h_modes/parallel_repeat/rayreuse/constant_speed_thorp/broadband_regression/broadband/constant_speed_thorp_broadband_regression_broadband.shd

# 10. --verify-cache 独立运行校验
clean_bin="$(pwd)/Bellhop_RayReuse/build/fp2h-clean/bellhop_rayreuse"
for target in w thorp fg bio; do
  mkdir -p /tmp/fp2h_modes/verify_cache/${target}_reuse /tmp/fp2h_modes/verify_cache/${target}_parallel
done
cp /tmp/fp2h_modes/reuse/rayreuse/attenuation_unit_w/broadband_smoke/broadband/attenuation_unit_w_broadband_smoke_broadband.env /tmp/fp2h_modes/verify_cache/w_reuse/case.env
(cd /tmp/fp2h_modes/verify_cache/w_reuse && "$clean_bin" case --frequencies-hz 4000,5000 --execution-mode reuse --verify-cache)
cp /tmp/fp2h_modes/parallel/rayreuse/attenuation_unit_w/broadband_smoke/broadband/attenuation_unit_w_broadband_smoke_broadband.env /tmp/fp2h_modes/verify_cache/w_parallel/case.env
(cd /tmp/fp2h_modes/verify_cache/w_parallel && "$clean_bin" case --frequencies-hz 4000,5000 --execution-mode parallel --verify-cache)

cp /tmp/fp2h_modes/reuse/rayreuse/constant_speed_thorp/broadband_smoke/broadband/constant_speed_thorp_broadband_smoke_broadband.env /tmp/fp2h_modes/verify_cache/thorp_reuse/case.env
(cd /tmp/fp2h_modes/verify_cache/thorp_reuse && "$clean_bin" case --frequencies-hz 1000,5000 --execution-mode reuse --verify-cache)
cp /tmp/fp2h_modes/parallel/rayreuse/constant_speed_thorp/broadband_smoke/broadband/constant_speed_thorp_broadband_smoke_broadband.env /tmp/fp2h_modes/verify_cache/thorp_parallel/case.env
(cd /tmp/fp2h_modes/verify_cache/thorp_parallel && "$clean_bin" case --frequencies-hz 1000,5000 --execution-mode parallel --verify-cache)

cp /tmp/fp2h_modes/reuse/rayreuse/volume_attenuation_francois_garrison/broadband_smoke/broadband/volume_attenuation_francois_garrison_broadband_smoke_broadband.env /tmp/fp2h_modes/verify_cache/fg_reuse/case.env
(cd /tmp/fp2h_modes/verify_cache/fg_reuse && "$clean_bin" case --frequencies-hz 5000,10000 --execution-mode reuse --verify-cache)
cp /tmp/fp2h_modes/parallel/rayreuse/volume_attenuation_francois_garrison/broadband_smoke/broadband/volume_attenuation_francois_garrison_broadband_smoke_broadband.env /tmp/fp2h_modes/verify_cache/fg_parallel/case.env
(cd /tmp/fp2h_modes/verify_cache/fg_parallel && "$clean_bin" case --frequencies-hz 5000,10000 --execution-mode parallel --verify-cache)

cp /tmp/fp2h_modes/reuse/rayreuse/volume_attenuation_biological/broadband_smoke/broadband/volume_attenuation_biological_broadband_smoke_broadband.env /tmp/fp2h_modes/verify_cache/bio_reuse/case.env
(cd /tmp/fp2h_modes/verify_cache/bio_reuse && "$clean_bin" case --frequencies-hz 2500,5000 --execution-mode reuse --verify-cache)
cp /tmp/fp2h_modes/parallel/rayreuse/volume_attenuation_biological/broadband_smoke/broadband/volume_attenuation_biological_broadband_smoke_broadband.env /tmp/fp2h_modes/verify_cache/bio_parallel/case.env
(cd /tmp/fp2h_modes/verify_cache/bio_parallel && "$clean_bin" case --frequencies-hz 2500,5000 --execution-mode parallel --verify-cache)

# 11. Git 与受保护文件检查 (0 diff)
git diff --check
git diff --exit-code -- Bellhop_origin Bellhop_F2CPP
git diff --exit-code -- \
  Bellhop_RayReuse/src/cache/ray_path_cache.cpp \
  Bellhop_RayReuse/include/rayreuse/cache/ray_path_cache.hpp \
  Bellhop_RayReuse/include/rayreuse/ray/ray_path.hpp
```

### 3.2 跨模式逐字节一致性与 SHA-256 汇总

所有 10 个宽带 profile 在 `nonreuse`、`reuse`、`parallel` 三种执行模式下生成的 `.shd` 文件均逐字节一致（`cmp` 返回 0）：

| 算例 / Profile | 频率 (Hz) | SHD SHA-256（三模式相同） | 追踪次数 (nonreuse / reuse / parallel) |
|---|---|---|---|
| `attenuation_unit_n` (smoke) | 4000, 5000 | `6c02651c34a1a50d651df18d4cea72bc1f6b2fc637c7543b6ac5badb543ce165` | `2 / 1 / 1` |
| `attenuation_unit_f` (smoke) | 4000, 5000 | `4cf98e60c99976b91874bfa8f9759dbada71ffa6b3cd3e451cfd7523add06b6d` | `2 / 1 / 1` |
| `attenuation_unit_m` (smoke) | 4000, 5000 | `9baac3160633ce61efbd576678e3e4322d0c5723c548f8a4d1b5a20db6e31a47` | `2 / 1 / 1` |
| `attenuation_unit_w` (smoke) | 4000, 5000 | `891306fec4e1c936bcdd059c3c522a45effba5770a685aa1504332fe7a2369a4` | `2 / 1 / 1` |
| `attenuation_unit_q` (smoke) | 4000, 5000 | `5ef677035fd8d5797af12f25306b4dcfa18588299f96437da51a62463933b3b8` | `2 / 1 / 1` |
| `attenuation_unit_l` (smoke) | 4000, 5000 | `3b05a8b91cb9c058f8a17d099c01ed08c64ab62914618a1a433f59bac03c9d82` | `2 / 1 / 1` |
| `volume_attenuation_francois_garrison` (smoke) | 5000, 10000 | `2837277287c8ccd3784d02f4ce55bc49b9e09ec68d8c937564a33dbeb473642f` | `2 / 1 / 1` |
| `volume_attenuation_biological` (smoke) | 2500, 5000 | `70459926cb04cd2b59867984b0d74823ce85fe0e4289589f4f226f4837e2c05f` | `2 / 1 / 1` |
| `constant_speed_thorp` (smoke) | 1000, 5000 | `1ddd8171315750ddf754136191bc9a7aa3e5b0747cc0b43286acabc74305a7f3` | `2 / 1 / 1` |
| `constant_speed_thorp` (regression) | 16频 (1–10k) | `c8ef3fad90e32753991b021eb804f4f26046c04783ab50a602a2983f26dcbcd2` | `16 / 1 / 1` |

### 3.3 缓存指纹与不可变性

在 `reuse` 和 `parallel` 模式下带 `--verify-cache` 运行时，前后语义指纹严格一致：
- `attenuation_unit_w`: `10638750469126791633` (before == after)
- `constant_speed_thorp`: `12163770556679950120` (before == after)
- `volume_attenuation_francois_garrison`: `4134998748544866669` (before == after)
- `volume_attenuation_biological`: `514508787683948826` (before == after)
- `munk_spline` 基线锚点：`1526667602348633172` (before == after)

### 3.4 验证器全量比较与摘要

1. **ATT-01 / ATT-02 验证器** (`validate_i4_attenuation_units.py`):
   - 验证器架构：`bellhop.rayreuse.i4_attenuation_unit_validation` (schema_version 2)
   - 报告文件路径：`/tmp/validate_i4_attenuation_units_report.json`（SHA-256: `ed9fc72305c35645ec3fe32e6a053ce38d28f0a71447ae610d6cb6c2f475b776`）
   - 总配对比较数：**54 / 54**（6 算例 × (单频 1 频 × 3 对 + 烟测 2 频 × 3 对)）
   - Gating 判定：**42 组 gating 全部通过**（Origin↔RayReuse 所有频点，以及所有 5000 Hz / 单频频点）；12 组非 gating 频点（4000 Hz 下 F2CPP 因单频自规划 800 条角）正确记录指标与原因。
   - 跨单位逐位相同：5000 Hz 下 N/F/M/W/Q/L 压强逐位相同；4000 Hz 下线性单位（F/W/Q/L）互相同构，常数单位（N/M）互相同构，W 频率缩放验证通过。
   - 聚合摘要 SHA-256：
     - `origin_field_aggregate`: `adf33e6e36c3b6bd142abbde6a10c6a96de4c72845424eca11d20971c8d8f398`
     - `f2cpp_field_aggregate`: `d4d6cccb89c78efdec9253f45c6aae3e23dc0e7f696222cdd0bdc60dde7d19a6`
     - `rayreuse_field_aggregate`: `01c525e7791049c3ada7d2077a3a18e427e57c59e798e5b5fc9217211da79f94`
     - `rendered_environment_aggregate`: `ccd6667586821a9f26549b54f0c808c2eb5ad88ef2c146a200b2b60bdb69f175`

2. **ATT-03 / 04 / 05 验证器** (`validate_i4_volume_attenuation.py`):
   - 验证器架构：`bellhop.rayreuse.i4_volume_attenuation_validation` (schema_version 2)
   - 报告文件路径：`/tmp/validate_i4_volume_attenuation_report.json`（SHA-256: `7ab94e49c8ea356f3889f10370237e7dec1a6649bc1ac16114966650e71bec44`）
   - 总配对比较数：**75 / 75**（Thorp 57 组 + FG 9 组 + Bio 9 组）
   - Gating 判定：**39 组 gating 全部通过**（Origin↔RayReuse 所有频点、所有 fmax 频点）；36 组非 gating 频点（F2CPP 在 f < fmax 处因单频自规划而产生发射角采样差异）正确记录。
   - Thorp H00 冻结哈希校验：单频（`27450009cbc6861ffc8f89e127432c09c852ca34af47e8a057e7d218db3f48ea`）、烟测（`1ddd8171315750ddf754136191bc9a7aa3e5b0747cc0b43286acabc74305a7f3`）、16频回归（`c8ef3fad90e32753991b021eb804f4f26046c04783ab50a602a2983f26dcbcd2`）SHA-256 均与前置基线 100% 吻合。
   - 无损非 no-op guard：Thorp, FG, Bio 9 组检查全部通过（差异 > 1e-6）。
   - 聚合摘要 SHA-256：
     - `origin_field_aggregate`: `eae69e5c118388e384b9b474056c58d3859e9e51e0fc75cf5f36f62041d71f11`
     - `f2cpp_field_aggregate`: `c30b9be42f329c48de1fba91d7f7129ee7d5877dfd46fced019d5c6371ef11b4`
     - `rayreuse_field_aggregate`: `1becd2dc1c025f72500d4bfccc81e8987fcec677b59fe609ab0e4bde08ff4c07`
     - `rendered_environment_aggregate`: `57c9e77caa88e9cac97cc0be85a1386f4e392105a9d8f1bc6d6d830b882bba9c`

### 3.5 集中回归与测试通过情况

1. **Clean CTest Suite** (`Bellhop_RayReuse/build/fp2h-clean`): **41 / 41 PASSED** (100%)
2. **Pytest Suite**: **187 / 187 PASSED** (100%，含新增的验证器单元与合成测试)
3. **Standard Cases Unit Suite** (`make -C test/standard_cases test-unit`): **172 / 172 PASSED** (100%)
4. **代表性冻结回归**: C-linear, PCHIP, N2-linear, Cubic Spline, Quadrilateral, Multi-source, Irregular receiver, Flat elastic halfspace 全绿。
5. **Git 边界检查**:
   - `git diff --check`: 零空白/格式问题
   - `git diff --exit-code -- Bellhop_origin Bellhop_F2CPP`: 零改动
   - `git diff --exit-code -- Bellhop_RayReuse/src/cache/ray_path_cache.cpp Bellhop_RayReuse/include/rayreuse/cache/ray_path_cache.hpp`: 零改动

---

## 4. 范围限定与残余风险说明

- **范围限定**：产品级标准算例 oracle 证据覆盖 C-linear 直达成场的 N/F/M/W/Q/L 衰减单位与 Thorp / Francois–Garrison / Biological 体积衰减模型；非 C-linear SSP 后端（P/N/S/Q）及弹性半空间纵横波衰减的泛化由完整的组件级测试、确定性复算与受保护的架构契约闭环证明，不将产品级声明过度外推至未建立专用标准算例的组合。
- **残余风险**：
  - Raw 标签与环境级模型的兼容缝隙：冲突与单次应用逻辑已在内核层由 16 组单元测试穷举闭环。
  - 指纹排他性：指纹仅度量冻结几何射线路径，排除环境级 FG/Biological 参数载荷；跨环境复用在应用层由不同 `Environment` 参数控制。
  - 历史 PRT 拼写：`Biological attenaution` 保持历史拼写以满足 oracle 标记对拍。
  - 无未关闭的 HIGH / BLOCKER 风险。

---

## 5. 批次结论

本批次所有功能开发、审查与验证全部通过，无未关闭 HIGH/BLOCKER 缺陷，无架构或缓存破坏，已就绪进入 Final Review。
