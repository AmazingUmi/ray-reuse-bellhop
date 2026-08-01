# H4 本地跨编译器验证操作计划

## 目标与当前条件

H4 只做本地可移植性验证，不配置或推送远端。当前可直接使用：

| 角色 | 编译器 |
|---|---|
| C++ 基线 | `/usr/bin/clang++`，Apple clang 21.0.0 |
| C++ 对照 | `/opt/homebrew/bin/g++-14`，GCC 14.2.0 |
| Fortran 基线 | `/opt/homebrew/bin/gfortran-14`，GNU Fortran 14.2.0 |

本机没有第二套独立 Fortran 编译器。H4 可先关闭 C++ AppleClang↔GCC
矩阵；Fortran 跨编译器项必须等 arm64 macOS 可用的 LLVM Flang 或另一目标
平台编译器到位后执行，不能把同一 gfortran 的别名当作两个工具链。

## H4-0：冻结输入与目录

1. 从干净提交开始，记录 `git rev-parse HEAD` 和 `git status --porcelain`；
2. 记录编译器完整版本、CMake 版本、SDK 和 flags；
3. 所有构建进入独立目录，不覆盖日常 preset：

```text
Bellhop_F2CPP/build/toolchain/appleclang-release
Bellhop_F2CPP/build/toolchain/gcc14-release
Bellhop_RayReuse/build/toolchain/appleclang-release
Bellhop_RayReuse/build/toolchain/gcc14-release
```

4. 关闭 fast-math，固定 `CMAKE_BUILD_TYPE=Release`、`BUILD_TESTING=ON` 和
单线程 benchmark 环境。

## H4-1：双 C++ 工具链构建

AppleClang：

```bash
cmake -S Bellhop_F2CPP \
  -B Bellhop_F2CPP/build/toolchain/appleclang-release \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
  -DF2CPP_WARNINGS_AS_ERRORS=ON \
  -DBUILD_TESTING=ON
cmake --build Bellhop_F2CPP/build/toolchain/appleclang-release --parallel 4
ctest --test-dir Bellhop_F2CPP/build/toolchain/appleclang-release \
  --output-on-failure

cmake -S Bellhop_RayReuse \
  -B Bellhop_RayReuse/build/toolchain/appleclang-release \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
  -DRAYREUSE_WARNINGS_AS_ERRORS=ON \
  -DBUILD_TESTING=ON
cmake --build Bellhop_RayReuse/build/toolchain/appleclang-release --parallel 4
ctest --test-dir Bellhop_RayReuse/build/toolchain/appleclang-release \
  --output-on-failure
```

GCC 14 使用相同命令，将构建目录改为 `gcc14-release`、编译器改为
`/opt/homebrew/bin/g++-14`。若 GCC 在 macOS 上不支持现有 warning 或 sanitizer
选项，应在 CMake 以 compiler-id 条件修正，不得全局降低警告等级。

验收：F2CPP 两套各 21/21，RayReuse 两套各 25/25；无新 warning。

## H4-2：每工具链数值矩阵

分别将 `model_matrix.py` 的 F2CPP/RayReuse 可执行文件指向 AppleClang 和
GCC 构建，输出：

```text
test/standard_cases/results/toolchain/model_matrix_appleclang.json
test/standard_cases/results/toolchain/model_matrix_gcc14.json
```

以 GCC 轮为例：

```bash
conda run -n py python test/standard_cases/codes/model_matrix.py \
  --origin-executable Bellhop_origin/bin/bellhop \
  --f2cpp-executable \
    Bellhop_F2CPP/build/toolchain/gcc14-release/bellhop_f2cpp \
  --rayreuse-executable \
    Bellhop_RayReuse/build/toolchain/gcc14-release/bellhop_rayreuse \
  --profiles single,broadband_smoke \
  --report \
    test/standard_cases/results/toolchain/model_matrix_gcc14.json
```

每套必须满足：

- 六例 single 和六例两频 smoke 12/12；
- 原版→RayReuse 门控失败 0；
- RayReuse nonreuse/reuse/parallel SHD 在同一工具链内逐字节一致；
- F2CPP broadband 低频仍按 D-02 规则只作非门控诊断。

跨工具链不要求 SHD 字节一致。使用现有复压力组合门、TL `1e-3 dB` 和相位
floor 比较 AppleClang↔GCC 的 F2CPP、RayReuse 输出，并报告每例最坏字段。

## H4-3：中间状态矩阵

分别使用两套 `*_geometry_oracle_probe` 运行
`intermediate_state_matrix.py`：

- 同一工具链内 F2CPP↔RayReuse CSV 必须逐字节一致；
- 两套工具链各自相对 Fortran schema v2 必须通过 D-07 字段容差；
- AppleClang↔GCC 跨工具链采用 D-07 数值门，不采用文件哈希门；
- direct、vacuum/rigid、Munk 的点/步/反射计数必须严格相等。

若 geometry 首先分歧，应先定位最坏 point/field，再运行最终场；不得通过放宽
最终压力容差掩盖中间状态错误。

## H4-4：性能与资源记录

正确性全部通过后，再按每模型预热 2 次、计量 7 次运行微基准。AppleClang
和 GCC 轮换顺序，记录：

- formula core 中位数、min/max 和 MAD；
- direct/Munk 的原始 Trace/Project/Influence；
- `/usr/bin/time -l` 最大 RSS；
- 可执行文件 SHA-256、完整 flags 和编译器版本。

性能不是跨编译器阻断门。若 7 次样本变异系数超过 `5%`，只记录数据，不作
快慢结论；不得据此改变默认 worker。

## H4-5：第二 Fortran 编译器

只有获得真正独立的 Fortran 编译器后执行：

1. 构建原版二维 Bellhop，保留 `BELLHOP_PROFILE_STAGES` 和 oracle 诊断；
2. 运行六例 single 紧凑参考门及三例中间状态 validator；
3. 与 gfortran 结果进行数值比较，不要求 SHD 字节一致；
4. 记录 compiler/runtime 依赖和 stage/RSS；
5. 若编译器改变默认实数或复数语义，必须显式 flags 固定 binary32/binary64，
   不得修改 oracle 迎合结果。

## 出口与失败处理

H4 C++ 出口：双工具链构建/CTest、两套三模型矩阵、两套中间状态矩阵全部
通过，并产生汇总 JSON/Markdown。Fortran 出口独立记录为“已通过”或“等待
第二编译器”，不阻塞本地 C++ 矩阵，但阻止宣称完整跨编译器发布支持。

任一失败按 `compile → contract test → intermediate state → final field →
performance` 顺序定位。只允许针对明确 compiler-id 的构建修正；数值公式、
累加顺序或容差变更必须单独评审和提交。
