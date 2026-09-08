# BB-N1 — Namespace & Product Identity Cleanup

## Scope

Identity-only refactor：清理 Bellhop Broadband 产品身份残留。不改算法、不改执行语义、不顺带修 CI。

Baseline: `605c261`（BB-M1 complete）。

## Frozen decisions

- `Bellhop Broadband` = 产品身份；`RayReuse` = 声线轨迹复用技术/历史名称；`Serial / Freq Para / Range Para` = 执行手段；`fused kernel/workspace/adapter/sink` = Range Para 内部实现术语。RayReuse 不做全局清零。
- 目录：`Bellhop_RayReuse/` → `Bellhop_Broadband/`（git mv）；`include/rayreuse/` → `include/broadband/`。
- C++ namespace：`rayreuse` → `broadband`（include/src/app/tests/tools），科学符号/类名/算法概念名不动。
- CMake：`bellhop_rayreuse_core` → `bellhop_broadband_core`；`bellhop_rayreuse_project_options` → `bellhop_broadband_project_options`；测试 target 前缀 `bellhop_rayreuse_*` → `bellhop_broadband_*`；`AddRayReuseTest.cmake`/`add_rayreuse_test` → `AddBroadbandTest.cmake`/`add_broadband_test`；CTest 命名空间 `rayreuse.{unit,component,reuse,nonreuse,cli}.*` → `broadband.*`。
- 宏/环境变量：`RAYREUSE_VERSION` → `BELLHOP_BROADBAND_VERSION`；`RAYREUSE_ENABLE_SANITIZERS`/`RAYREUSE_WARNINGS_AS_ERRORS`/`RAYREUSE_WORKSPACE_ROOT` → `BROADBAND_*`；active script env vars（`RAYREUSE_BUILD_JOBS`、`RAYREUSE_MICROBENCH_*`、`RAYREUSE_INTERMEDIATE_OUTPUT`、`RAYREUSE_MATRIX_*`）→ `BROADBAND_*`。不留兼容 alias。
- Python/runner：第三实现 identity `"rayreuse"` → `"broadband"`；helper 改名（`BROADBAND_EXECUTION_MODES`、`BROADBAND_REUSE_MODES`、`broadband_execution_arguments`、`broadband_prt_markers`、`require_broadband_execution_mode`、`process_broadband`）；`--rayreuse-executable`/`--rayreuse-probe` → `--broadband-*`；`benchmark_rayreuse.py` → `benchmark_broadband.py`（默认输出 `broadband_benchmark.json`）；case.toml `versions`/`version_prt_markers` 同步。
- Demo：产品路径与第三实现 identity 同步（reliability.py / Makefile / README）；`rayreuse_multifrequency` 展示名、schema 字符串、图题为 RayReuse 技术展示，保留。
- 保留：repo 名 `ray-reuse-bellhop`、pyproject project name、`RayPath`/`RayPathCache`、`Reuse*Solver`、`RayReuseFrequencyConsumer`、fused 术语、support matrix 的 `RAYREUSE_EXTENSION` 分类、历史 docs/reports/worklists/reviews/workreports/archive 正文。
- 附带产品级小字符串（身份级、无语义影响）：C++ 测试通过语 `All Bellhop RayReuse ...` → `All Bellhop Broadband ...`；`.rayreuse-backup` → `.broadband-backup`（src+test 同步）；parser 测试 fixture 前缀 `rayreuse_*` → `broadband_*`。
- 保护：`Bellhop_origin/`、`Bellhop_F2CPP/` 零改动。

## Tasks

### A01 [STANDARD] 目录/文件 git mv + 引用同步（CMake、scripts、workflow、.gitignore、demo、README、docs、tests、pyproject）
Status: DONE
Reviewer: N/A

### A02 [STANDARD] C++ namespace 迁移（rayreuse → broadband）
Status: DONE
Reviewer: N/A

### A03 [STANDARD] CMake target/macro/CTest 命名空间迁移
Status: DONE
Reviewer: N/A

### A04 [STANDARD] Python runner/benchmark/validators/model_matrix/case manifests 身份迁移
Status: DONE
Reviewer: N/A

### A05 [STANDARD] Living docs 产品身份更新
Status: DONE
Reviewer: N/A

## Acceptance

- clean configure + Release build 通过；
- full CTest 只跑一次通过；
- targeted Python：standard_cases / benchmark / model_matrix 通过；
- 1 个最小 broadband smoke 通过；
- `Bellhop_Broadband/build/release/bellhop_broadband` 可正常运行；
- grep 活跃范围无产品级残留（`Bellhop_RayReuse/`、`include/rayreuse/`、`namespace rayreuse`、`rayreuse::`、`bellhop_rayreuse_core`、`bellhop_rayreuse_project_options`、`RAYREUSE_VERSION`、`RAYREUSE_BUILD_JOBS`、`benchmark_rayreuse.py`、`results/rayreuse`）；
- `git diff --check` clean；`Bellhop_origin/`、`Bellhop_F2CPP/` zero diff。

## Evidence

- `git mv`：`Bellhop_RayReuse/ → Bellhop_Broadband/`、`include/rayreuse/ → include/broadband/`、`AddRayReuseTest.cmake → AddBroadbandTest.cmake`、`benchmark_rayreuse.py → benchmark_broadband.py`、`tests/test_benchmark_rayreuse.py → tests/test_benchmark_broadband.py`（297 files changed，rename 保留历史）。
- clean configure + Release build：`cmake --preset release` + `cmake --build --preset release --parallel 8` → 100%（含 `bellhop_broadband`、`bellhop_broadband_geometry_oracle_probe`）。
- full CTest（release preset，仅一次）：50/50 passed；label 摘要显示 `broadband.reuse.serial/frequency/range` 新命名空间。
- targeted Python：
  - codes unit tests（standard_cases/benchmark/model_matrix/validators）：`Ran 177 tests ... OK`，runner 产物写入 `results/broadband/`；
  - `model_matrix.py --profiles single,broadband_smoke --execution-modes nonreuse,reuse --reuse-modes serial,frequency --case constant_speed_direct --case munk_cerveny_cc` → exit 0，`MODEL MATRIX PASSED: 4 case/profile result(s)`；
  - `benchmark_broadband.py --case constant_speed_direct --frequencies-csv 50,100 --repeats 1 --warmups 0` → 报告写出，schema_version=4 不变；
  - demo showcase tests 3/3 OK；`rayreuse_multifrequency.py check` → environment/rayreuse READY（新 `Bellhop_Broadband/build/release/bellhop_broadband` 路径）。
- smoke：`bellhop_broadband munk_cerveny_cc --frequencies-hz 50,100 --execution-mode reuse --reuse-mode serial` → PRT 头 `BELLHOP BROADBAND / program version = 0.1.0`、`frequency count = 2`，SHD/PRT 产出；`--version` → `Bellhop Broadband 0.1.0`。
- acceptance grep：全部 10 个产品级 pattern 在活跃范围 0 命中；仅历史材料（`doc/archive/PLAN_PROJECT_IMPLEMENTATION_2026-08-14.md`、BB-M1 前术语映射表）与保留技术名（demo `rayreuse-multifrequency` 展示）命中。
- `git diff --check` → clean；`git status/diff -- Bellhop_origin Bellhop_F2CPP` → zero diff。
- pre-existing（非本批回归，未扩 scope 修复）：`results/reference/origin/single/attenuation_unit_f.json` compact reference 未生成/未入库，基线 605c261 worktree 复现同样 exit 2；用有 committed reference 的 case 验证 matrix 通过。

## Intentionally retained

- repo 名 `ray-reuse-bellhop` 与 root pyproject project name；
- 技术术语：`RayPath`/`RayPathCache`、`ReuseSerialSolver`/`ReuseFreqParaSolver`/`ReuseRangeParaSolver`、`RayReuseFrequencyConsumer`、fused kernel/workspace/adapter/sink 注释术语、"Production fused RayReuse kernel" 等 CamelCase 技术注释；
- support matrix `RAYREUSE_EXTENSION` 分类（support matrix 文件未改动）；
- demo `rayreuse_multifrequency` 展示族（文件名、case 名、结果/图目录、`bellhop.rayreuse.multifrequency_*` schema、图题、`rayreuse-multifrequency` Make target）；
- 历史 docs/reports/worklists/reviews/workreports/archive 正文与 BB-M1 历史术语映射表；
- 历史 generated results（磁盘上的 `results/rayreuse/`、历史 model_matrix/benchmark JSON）不改写。
- 附带身份级重命名（无语义影响）：model_matrix 报告字段 `rayreuse_execution_modes`/`rayreuse_reuse_modes` → `broadband_*`（随 runner identity，schema_version 不变）；validators 报告键 `*_rayreuse_*` → `*_broadband_*`；`.rayreuse-backup` → `.broadband-backup`；geometry_oracle_probe JSON `producer: "rayreuse"` → `"broadband"`。

## Blockers / Findings

- none
