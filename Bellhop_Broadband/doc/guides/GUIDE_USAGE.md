# Bellhop Broadband 使用说明

（组件目录仍为 `Bellhop_Broadband/`；产品与可执行程序自 BB-1 起更名为
Bellhop Broadband / `bellhop_broadband`，RayReuse 是其中的轨迹复用算法族。）

## 构建与测试

从仓库根目录运行：

```bash
uv run cmake --preset release -S Bellhop_Broadband
uv run cmake --build Bellhop_Broadband/build/release --parallel
uv run ctest --test-dir Bellhop_Broadband/build/release --output-on-failure
uv run make -C test/standard_cases test-unit
```

支持边界以 [`REFERENCE_FEATURE_SUPPORT_MATRIX.md`](../reference/REFERENCE_FEATURE_SUPPORT_MATRIX.md) 为准。

## 命令格式

```bash
Bellhop_Broadband/build/release/bellhop_broadband <file-root> [options]
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
Bellhop_Broadband/build/release/bellhop_broadband example \
  --frequencies-hz 500,1000 \
  --execution-mode reuse \
  --reuse-mode serial \
  --verify-cache
```

执行模型分两层：`--execution-mode <nonreuse|reuse>`（默认 `nonreuse`）选择
是否复用轨迹，`--reuse-mode <serial|frequency|range>`（仅在 reuse 下合法，
默认 `serial`）选择 RayReuse 的复用组织方式：

- `nonreuse`：每频完整追踪（`NonReuseSolver`）；
- `reuse × serial`：trace once 后逐频串行投影（`ReuseSerialSolver`）；
- `reuse × frequency`：trace once 后按 frequency tasks 在 reuse workers 间
  分配（`ReuseFreqParaSolver`）；
- `reuse × range`：支持域内的 production RayReuse 路线
  （`ReuseRangeParaSolver`）；ray 内完成跨频率 fused Influence，coherent
  pressure 与 I/S intensity payload 均为 `[range][depth][frequency]`
  hot layout。

Range Reuse 示例：

```bash
Bellhop_Broadband/build/release/bellhop_broadband example \
  --frequencies-hz 500,1000,2000 \
  --execution-mode reuse \
  --reuse-mode range \
  --reuse-workers 8
```

range 路线的 reuse worker 数由 `--reuse-workers` 表达，默认 1；effective
workers clamp 到 receiver range 数。每个 worker 独占连续 range block，
输出继续 byte-identical。旧 `--range-parallel` 与通用 `--workers` 选项已
删除，继续使用会以 unknown option 拒绝。

frequency 路线的 reuse worker 数同样由 `--reuse-workers` 表达，默认 1，
effective 值受频率数与 memory budget 向下 clamp。SHD 的
`--output-queue-capacity` 仅在 reuse+frequency 的多频 TL 路线合法，只限制
完成队列，不是线程上限。A/a/E 的 frequency worker 不直接写文件；主
consumer 按 frequency index 稳定发布。

`nonreuse` 保留为 reference，CLI 全局默认保持 `nonreuse`，避免把 reuse
支持域外的产品静默改道。BB-1 起旧 `reuse`/`parallel`/`fused` 模式值与
旧 deprecation warning 一并移除，三条 reuse 路线均为显式的一等选项。

range 路线支持域：TL 为多频（≥2 频率）、单 source、规则 receiver grid、
≥2 个等间距 receiver ranges 的运行，且 run mode 对所选 beam family 合法
（IGR-3A）；A/a 为多频、Geometric Hat（两坐标系）/Geometric Gaussian、
规则等距网格的 Arrival，允许 multisource 并按 source 流式生成每频 ARR
（IGR-3B）。range eligibility 始终是各 beam family 合法 beam×run-mode
support matrix 的子集：range 路线只支持规则 receiver grid（Cartesian
GeoHat 与 Cartesian GeoGaussian 的其他路线支持 paired irregular
receivers，range 路线不提供）；simple Gaussian 非 coherent 组合在产品层
本身拒绝（非 range 路线限制）；`R` 产品不接受 reuse，`E` 的 range 路线
被拒绝；单频 TL 同样拒绝显式 reuse。family × run mode 覆盖与支持边界以
[`REFERENCE_FEATURE_SUPPORT_MATRIX.md`](../reference/REFERENCE_FEATURE_SUPPORT_MATRIX.md)
的 range 路线支持域小节为准。见
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
  --version broadband \
  --case arrival_geometric_hat_ascii \
  --profile single \
  --executable Bellhop_Broadband/build/release/bellhop_broadband
```

两频产品入口：

```bash
uv run python test/standard_cases/codes/standard_cases.py test \
  --version broadband \
  --case eigenray_geometric_hat_ray_centered \
  --profile broadband_smoke \
  --execution-mode reuse --reuse-mode frequency \
  --executable Bellhop_Broadband/build/release/bellhop_broadband
```

RayReuse 继续复用 `test/standard_cases/`；没有第二套算例库。
