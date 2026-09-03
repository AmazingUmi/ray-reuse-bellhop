# Bellhop RayReuse 使用说明

## 构建与测试

从仓库根目录运行：

```bash
uv run cmake --preset release -S Bellhop_RayReuse
uv run cmake --build Bellhop_RayReuse/build/release --parallel
uv run ctest --test-dir Bellhop_RayReuse/build/release --output-on-failure
uv run make -C test/standard_cases test-unit
```

支持边界以 [`REFERENCE_FEATURE_SUPPORT_MATRIX.md`](../reference/REFERENCE_FEATURE_SUPPORT_MATRIX.md) 为准。

## 命令格式

```bash
Bellhop_RayReuse/build/release/bellhop_rayreuse <file-root> [options]
```

`<file-root>` 不含 `.env` 后缀。程序始终写 `<file-root>.prt`，产品由 ENV 的
run type 决定：

| run type | 单频输出 | 多频输出 |
|---|---|---|
| `CC/IC/SC` | `<root>.shd` | 一个多频 `<root>.shd` |
| `CR/IR/SR` | `<root>.shd` | 一个多频 `<root>.shd` |
| `CG/IG/SG`、`Cg/Ig/Sg` | `<root>.shd` | 一个多频 `<root>.shd` |
| `CB/IB/SB`、`CS` | `<root>.shd` | 一个多频 `<root>.shd` |
| `R/RG/RGO` | `<root>.ray` | 明确拒绝 |
| `AG/Ag/AB` | `<root>.arr` | `<root>_fNNN_<freq>Hz.arr` |
| `aG/ag/aB` | `<root>.arr` | `<root>_fNNN_<freq>Hz.arr` |
| `EG/Eg/EB` | `<root>.ray` | `<root>_fNNN_<freq>Hz.ray` |

## 多频与执行模式

ENV 可直接写严格升序频率列表，也可由 CLI 覆盖：

```bash
Bellhop_RayReuse/build/release/bellhop_rayreuse example \
  --frequencies-hz 500,1000 \
  --execution-mode fused \
  --verify-cache
```

执行模式：

- `nonreuse`：每频完整追踪；
- `fused`：支持域内的 production RayReuse 主路径；ray 内完成跨频率 fused
  Influence，pressure hot layout 为 `[range][depth][frequency]`，默认 serial；
- `reuse`：legacy trace-once、串行逐频 compatibility path；
- `parallel`：legacy 外层 frequency-parallel compatibility path。

静态 receiver-range parallel 示例：

```bash
Bellhop_RayReuse/build/release/bellhop_rayreuse example \
  --frequencies-hz 500,1000,2000 \
  --execution-mode fused \
  --range-parallel \
  --workers 8
```

range parallel 仅由 `--range-parallel` 显式开启；未指定 `--workers` 时默认
请求 4 workers，effective workers clamp 到 receiver range 数。每个 worker
独占连续 range block，输出继续 byte-identical。单独的 `--workers` 不会开启
range parallel。

legacy `parallel` 模式中，`--workers` 未指定时使用硬件并发数。SHD 的
`--output-queue-capacity` 仅限制完成队列，不是线程上限。A/a/E 的 parallel
worker 不直接写文件；主 consumer 按 frequency index 稳定发布。

当 coherent Cartesian Cerveny、多频、single-source、rectilinear TL 可以由
fused 执行时，显式选择 `reuse` 或 `parallel` 会收到一次 deprecation warning；
兼容路径仍按原行为运行。`nonreuse` 保留为 reference，CLI 全局默认也保持
`nonreuse`，避免把 fused 支持域外的产品静默改道。

上述内容是当前 IGR-2 production CLI。IGR-3 已批准的 future direction 是把
Cross-Frequency Fused + Static Range Parallel Influence execution 适配到其余
TL beam-family kernels，并在后续 IGR-3B 适配 Arrival contribution sink；它尚未
construction/acceptance，因此不改变本指南中的当前命令、默认值或支持域。见
[`IGR-3_SCOPE_AND_ARCHITECTURE_DECISION.md`](../worklists/IGR-3_SCOPE_AND_ARCHITECTURE_DECISION.md)。

## 产品生命周期与错误

- 成功模式切换会清除同 root 的异类产品、旧逐频产品和 `.tmp`；
- writer 通过临时文件原子发布单个产品；
- 多频运行中任一频失败会清理本次已发布的逐频产品；
- 环境解析或组合校验在新生命周期开始前失败时，旧有效产品保持不变；
- 未支持组合返回非零状态，并在可用时写入 PRT `FATAL ERROR`；
- ray-centered Cerveny 与 ray-centered GeoHat TL/A/a/E 都要求
  规则网格且 receiver ranges 至少两个并等间距；其中
  TL GeoHat 使用 `Cg/Ig/Sg`，产品使用 `Ag/ag/Eg`。未支持组合
  （如 Simple Gaussian 搭配 line source、ray-centered 搭配 irregular、
  3D/N×2D 等）返回非零状态，不会静默退化。

## 共享标准案例

单频产品入口：

```bash
uv run python test/standard_cases/codes/standard_cases.py test \
  --version rayreuse \
  --case arrival_geometric_hat_ascii \
  --profile single \
  --executable Bellhop_RayReuse/build/release/bellhop_rayreuse
```

两频产品入口：

```bash
uv run python test/standard_cases/codes/standard_cases.py test \
  --version rayreuse \
  --case eigenray_geometric_hat_ray_centered \
  --profile broadband_smoke \
  --rayreuse-execution-mode parallel \
  --executable Bellhop_RayReuse/build/release/bellhop_rayreuse
```

RayReuse 继续复用 `test/standard_cases/`；没有第二套算例库。
