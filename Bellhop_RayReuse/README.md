# Bellhop RayReuse

本目录是独立的 C++20 多频 Bellhop 实现。它已完成
Bellhop_F2CPP 二维 production surface 的 Feature Parity，并在此基础上提供
`nonreuse`、`reuse` 和 `parallel` broadband execution。

```text
Bellhop_F2CPP → Bellhop_RayReuse
Production Feature Parity: COMPLETE
Remaining F2CPP parity GAP: 0

Accepted production HEAD: 0721fb3
Final acceptance documentation commit: 88ba8b7
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
- `reuse` 与 `parallel` 不把逐频声学状态写回 frozen cache；
- F2CPP 和 Origin 只作为 reference/oracle，不参与 RayReuse production ownership；
- support matrix 中的 evidence-bounded 组合边界仍适用，尤其不得把 Q 的已验收
  slice 外推为所有 SSP × beam × product 组合。

总体设计见
[ARCHITECTURE_BELLHOP_RAY_REUSE.md](../doc/architecture/ARCHITECTURE_BELLHOP_RAY_REUSE.md)。

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
- `reuse`：只 trace 一次 frozen geometry，再逐频执行 acoustic projection；
- `parallel`：共享 frozen geometry，以有界 worker/queue 并行逐频 projection，
  并由单 writer 保持确定性发布顺序。

并行示例：

```bash
Bellhop_RayReuse/build/release/bellhop_rayreuse <file-root> \
  --frequencies-hz 50,100,250 \
  --execution-mode parallel \
  --workers 8 \
  --output-queue-capacity 2 \
  --memory-budget-mib 4096
```

`--workers` 默认采用硬件并发数；完成队列容量只能为 1 或 2，默认 2。显式
memory budget 限制 cache 与活动 frequency workspaces，不等同于整个进程 RSS。
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
不能与未启用诊断的正式 wall-clock 样本混用。

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
