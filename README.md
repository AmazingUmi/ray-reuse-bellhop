# Bellhop Ray-Reuse

本仓库维护三套相互独立、由共享标准算例连接的 Bellhop 实现：

| 组件 | 当前角色 |
|---|---|
| `Bellhop_origin/` | 可重现的 Fortran 科学与文件行为 oracle |
| `Bellhop_F2CPP/` | 已封板的 C++20 二维单频 production reference |
| `Bellhop_RayReuse/` | Bellhop Broadband 产品实现（目录名保留历史名称）；已完成 F2CPP production Feature Parity 的独立多频实现，RayReuse 是其中的轨迹复用算法族 |

当前正式状态：

```text
Bellhop_F2CPP → Bellhop_RayReuse
Production Feature Parity: COMPLETE
Remaining F2CPP parity GAP: 0

Feature Parity accepted production HEAD: 0721fb3
Feature Parity final acceptance documentation commit: 88ba8b7
IGR-2 productionization commit: e7f2705
```

RayReuse 侧产品名为 **Bellhop Broadband**（可执行程序 `bellhop_broadband`），
在已对齐的二维 production surface 上以两层 CLI 组织宽带执行：
`--execution-mode <nonreuse|reuse>`（默认 `nonreuse`）加
`--reuse-mode <serial|frequency|range>`（reuse 下默认 `serial`），并行资源由
`--trace-workers` 与 `--reuse-workers` 表达（默认均为 1）。reuse 的 range 路线
在 IGR-3 后的支持域覆盖全部合法 TL beam families 与 `G/g/B × A/a` 多频
Arrivals（含多 source streaming）。详细结论、支持边界和运行方式见：

- [Feature Parity Final Report](Bellhop_RayReuse/doc/reports/REPORT_FEATURE_PARITY_FINAL.md)
- [RayReuse Feature Support Matrix](Bellhop_RayReuse/doc/reference/REFERENCE_FEATURE_SUPPORT_MATRIX.md)
- [RayReuse Usage Guide](Bellhop_RayReuse/doc/guides/GUIDE_USAGE.md)
- [当前项目工作与候选方向](doc/plans/PLAN_CURRENT_WORK.md)

IGR-3 已按两个串行 Batch 完成并 `ACCEPTED / CLOSED`（IGR-3A fused TL
adaptation commit `dda1c2c`；IGR-3B fused Arrival closure commit `0050f59`）：
统一 fused Influence executor 已覆盖当前支持域内的 TL beam families 与
`G/g/B × A/a` Arrivals。在此之上，BB-1（Bellhop Broadband 命名与两层执行
模型，2026-09-06 `ACCEPTED`）将产品/CLI 重组为
`nonreuse|reuse × serial|frequency|range`。权威边界见
[IGR-3 Scope & Architecture Decision](Bellhop_RayReuse/doc/worklists/IGR-3_SCOPE_AND_ARCHITECTURE_DECISION.md)
与 [Feature Support Matrix](Bellhop_RayReuse/doc/reference/REFERENCE_FEATURE_SUPPORT_MATRIX.md)。

## 项目结构

```text
Bellhop_origin/       Fortran 单频 oracle
Bellhop_F2CPP/        C++20 二维单频 production reference
Bellhop_RayReuse/     C++20 多频轨迹复用 production implementation
test/PlotRead/        SHD 读取、绘图和自包含 fixtures
test/standard_cases/  Origin、F2CPP、RayReuse 共用算例与 validators
test/legacy/          不参与当前测试的迁移前历史材料
demo/                 可靠性与多频结果展示
```

F2CPP 与 RayReuse 拥有独立 CMake 工程、源码和可执行程序，彼此不链接。
RayReuse 保留 frequency-independent `RayPath`/`RayPathCache` 几何，并把幅相、
复走时、反射结果、Influence/Arrival/Eigenray workspace 与 writer state 保持为
frequency-local。完整契约见
[总体架构](doc/architecture/ARCHITECTURE_BELLHOP_RAY_REUSE.md)。

## Python 与结果读取

仓库使用根目录 uv 环境和 `.python-version`：

```bash
uv sync
uv run pytest
```

PlotRead 已集中到 `test/PlotRead/bellhop_io_py/`，运行不依赖 MATLAB。生成标准
结果并读取 SHD 的示例：

```bash
uv run make -C test/standard_cases test \
  VERSION=origin CASE=munk_cerveny_cc PROFILE=single

uv run bellhop-shd info \
  test/standard_cases/results/origin/munk_cerveny_cc/single/f000_50Hz/munk_cerveny_cc_f000_50Hz.shd

uv run bellhop-shd plot \
  test/standard_cases/results/origin/munk_cerveny_cc/single/f000_50Hz/munk_cerveny_cc_f000_50Hz.shd \
  -o /tmp/munk.png
```

Python 的 `--frequency-index` 从 0 开始；`--frequency` 按最接近的 Hz 值选择。
完整说明见 [PlotRead README](test/PlotRead/README.md)。

## 测试与文档入口

- [共享标准算例](test/standard_cases/README.md)
- [RayReuse 构建、运行与质量门](Bellhop_RayReuse/README.md)
- [项目文档索引](doc/README.md)
- [RayReuse 文档索引](Bellhop_RayReuse/doc/README.md)
- [Demo](demo/README.md)

`archive/`、dated reports 和 frozen Batch artifacts 是历史证据快照，不代表当前
状态或自动恢复的待办；当前结论以上述 final report、support matrix 和 status
文档为准。
