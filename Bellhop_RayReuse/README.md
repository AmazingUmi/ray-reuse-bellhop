# Bellhop RayReuse

本目录是独立的 C++20 多频 Bellhop 实现。它已完成
Bellhop_F2CPP 二维 production surface 的 Feature Parity，并在此基础上提供
`nonreuse` reference、production `fused` RayReuse，以及兼容保留的 legacy
`reuse` / frequency-`parallel` broadband execution。

```text
Bellhop_F2CPP → Bellhop_RayReuse
Production Feature Parity: COMPLETE
Remaining F2CPP parity GAP: 0

Feature Parity accepted production HEAD: 0721fb3
Feature Parity final acceptance documentation commit: 88ba8b7
IGR-2 productionization commit: e7f2705
IGR-3A fused TL adaptation commit: dda1c2c
IGR-3B fused Arrival closure commit: 0050f59
```

长期维护入口：

- [Feature Parity Final Report](./doc/reports/REPORT_FEATURE_PARITY_FINAL.md)
- [Feature Support Matrix](./doc/reference/REFERENCE_FEATURE_SUPPORT_MATRIX.md)
- [Usage Guide](./doc/guides/GUIDE_USAGE.md)
- [Current Status](./doc/status/STATUS_PROGRESS.md)

## 架构与工程边界

- F2CPP 与 RayReuse 拥有独立 CMake 工程、源码和可执行程序，彼此不链接；
- `RayPath`/`RayPathCache` 保存 frequency-independent frozen geometry；
- 幅相、复走时、反射结果、Influence/Arrival/Eigenray workspace 和 writer state
  均为 frequency-local；
- 所有 RayReuse 路径都不把逐频声学状态写回 frozen cache；
- production fused TL 的 coherent pressure 与 I/S intensity payload，以及
  fused Arrival 的 ordered variable-length lanes，均使用逻辑
  `[range][depth][frequency]` hot layout；可选 range workers 各自独占连续
  receiver-range block；
- F2CPP 和 Origin 只作为 reference/oracle，不参与 RayReuse production ownership；
- support matrix 中的 evidence-bounded 组合边界仍适用，尤其不得把 Q 的已验收
  slice 外推为所有 SSP × beam × product 组合。

总体设计见
[ARCHITECTURE_BELLHOP_RAY_REUSE.md](../doc/architecture/ARCHITECTURE_BELLHOP_RAY_REUSE.md)。

production fused TL 支持域（IGR-3A）为多频
（≥2 频率）单 source、规则 receiver grid 的 TL 运行，run-mode 覆盖等于各
beam family 的合法产品 run mode——Cerveny Gaussian（`CC/IC/SC`、`CR/IR/SR`）、
geometric hat（`CG/IG/SG`、`Cg/Ig/Sg`）与 geometric Gaussian（`CB/IB/SB`）
支持 coherent/incoherent/semi-coherent，simple Gaussian 仅其唯一合法模式
coherent（`CS`）。IGR-3B 已增加多频、规则 receiver grid 的
Geometric Hat Cartesian/ray-centered 与 Geometric Gaussian `A/a`
（`G/g/B`）fused execution，允许 multisource 并按 source 流式生成每频 ARR；
fused eligibility 始终是合法 beam×run-mode support matrix 的子集。IGR-3A
与 IGR-3B 均已 `ACCEPTED / CLOSED`。权威 closure 见
[IGR-3 Scope & Architecture Decision](./doc/worklists/IGR-3_SCOPE_AND_ARCHITECTURE_DECISION.md)。

## 构建与质量门

命令默认从仓库根目录运行：

```bash
uv run cmake --preset debug -S Bellhop_RayReuse
uv run cmake --build Bellhop_RayReuse/build/debug --parallel
uv run ctest --test-dir Bellhop_RayReuse/build/debug --output-on-failure

uv run cmake --preset release -S Bellhop_RayReuse
uv run cmake --build Bellhop_RayReuse/build/release --parallel
uv run ctest --test-dir Bellhop_RayReuse/build/release --output-on-failure
```

提交前质量门：

```bash
RAYREUSE_BUILD_JOBS=4 uv run bash Bellhop_RayReuse/scripts/quality_gate.sh
```

该脚本执行 Debug sanitizer 与 Release build/CTest、standard-cases Python unit
suite、PlotRead unit suite、F2CPP 独立性扫描，以及移除 F2CPP 目录后的隔离
Release build/CTest。测试数量会随仓库演进，因此不在本 README 固定记录。

工程门：

```bash
RAYREUSE_BUILD_JOBS=4 uv run bash Bellhop_RayReuse/scripts/engineering_gate.sh
```

该脚本检查 C++ 格式、运行 Clang static analyzer，并验证安装、`--version` 与
CPack TGZ/SHA-256。当前 release artifact 仅用于内部验证；许可证和目标平台
契约冻结前不作为公开发行包。边界见
[GUIDE_RELEASE.md](./doc/guides/GUIDE_RELEASE.md)。

其他按需门：

```bash
Bellhop_RayReuse/scripts/model_matrix_gate.sh
Bellhop_RayReuse/scripts/intermediate_state_gate.sh
Bellhop_RayReuse/scripts/single_thread_microbenchmark.sh
```

它们分别用于三实现数值矩阵、中间几何状态和同工作量阶段微基准，不属于每次
文档修改必须重复执行的默认门。

## 运行与 execution modes

单频调用使用 ENV 中的频率：

```bash
Bellhop_RayReuse/build/release/bellhop_rayreuse <file-root>
```

产品由 ENV run type 决定：支持矩阵范围内的 TL family 写 SHD，`R/E` 写 RAY，
`A/a` 写 ARR；多频 `A/a/E` 按频率发布独立文件，多频 R 明确拒绝。完整产品、
source/receiver、boundary、attenuation 和 writer 语义见
[GUIDE_USAGE.md](./doc/guides/GUIDE_USAGE.md)。

宽带频率可由 CLI 严格升序列表覆盖：

```bash
Bellhop_RayReuse/build/release/bellhop_rayreuse <file-root> \
  --frequencies-hz 50,100,250 \
  --execution-mode nonreuse
```

- `nonreuse`：每个频率独立 trace 与 projection，作为 execution baseline；
- `fused`：支持域内的 production RayReuse 主路径；TL 与 `G/g/B × A/a`
  均在 ray 内跨频率 fused Influence，默认 serial；
- `reuse`：legacy 逐频串行 cache-reuse compatibility path；
- `parallel`：legacy frequency-parallel compatibility path。

production receiver-range parallel 示例：

```bash
Bellhop_RayReuse/build/release/bellhop_rayreuse <file-root> \
  --frequencies-hz 50,100,250 \
  --execution-mode fused \
  --range-parallel \
  --workers 8
```

`--range-parallel` 未指定 `--workers` 时默认请求 4 workers；effective workers
会 clamp 到 receiver range 数。单独指定 `--workers` 不会隐式开启 range
parallel。legacy frequency-parallel 示例：

```bash
Bellhop_RayReuse/build/release/bellhop_rayreuse <file-root> \
  --frequencies-hz 50,100,250 \
  --execution-mode parallel \
  --workers 8 \
  --output-queue-capacity 2 \
  --memory-budget-mib 4096
```

legacy `parallel` 的 `--workers` 默认采用硬件并发数；完成队列容量只能为 1 或
2，默认 2。显式 memory budget 限制 cache 与活动 frequency workspaces，不等同于整个进程 RSS。
RayReuse 也接受 ENV 频率记录中的严格递增列表；CLI 覆盖优先于 ENV。

## Profiling 与 benchmark

Influence 热点诊断默认关闭：

```bash
Bellhop_RayReuse/build/release/bellhop_rayreuse <file-root> \
  --frequencies-hz 50,250 \
  --execution-mode reuse \
  --profile-influence
```

parallel 逐频任务计时可用 `--profile-frequency-tasks`。这两个选项只用于诊断，
不能与未启用诊断的正式 wall-clock 样本混用。`--profile-influence` 在 TL 上只对
Cartesian Cerveny 定义（所有执行模式，其余 beam family 直接报错）；fused 运行
中只有 Cartesian Cerveny 填充 Influence 计数，各 family 的统计 envelope 与
fused PRT 模式行见
[Feature Support Matrix](./doc/reference/REFERENCE_FEATURE_SUPPORT_MATRIX.md)
的 fused 支持域小节。

可重复 benchmark 使用共享标准算例、轮换采样顺序、外部 wall、隔离进程
max RSS 和产品哈希门，并记录提交、机器、工具链、workers、频率与原始样本。
协议见 [GUIDE_BENCHMARKING.md](./doc/guides/GUIDE_BENCHMARKING.md)。

当前可安全保留的性能结论以
[Feature Parity Final Report](./doc/reports/REPORT_FEATURE_PARITY_FINAL.md) 的
Performance Snapshot 为准：代表性单频 case 中 RayReuse 与 F2CPP 近似持平或
更快；broadband reuse 收益取决于 trace 占比；geometry reuse 后 Influence 是
主要热点；parallel speedup 是当前机器的 snapshot，不是跨硬件保证。

## 文档

- [RayReuse 文档索引](./doc/README.md)
- [Feature Parity Final Report](./doc/reports/REPORT_FEATURE_PARITY_FINAL.md)
- [Feature Support Matrix](./doc/reference/REFERENCE_FEATURE_SUPPORT_MATRIX.md)
- [Usage Guide](./doc/guides/GUIDE_USAGE.md)
- [Benchmark Guide](./doc/guides/GUIDE_BENCHMARKING.md)
- [当前项目工作与候选方向](../doc/plans/PLAN_CURRENT_WORK.md)
- [共享标准算例](../test/standard_cases/README.md)

dated reports、frozen Worklists/Batch Reports 与 `archive/` 是当时证据，不作为
当前状态或自动恢复的待办。
