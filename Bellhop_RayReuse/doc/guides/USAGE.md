# Bellhop RayReuse 使用说明

## 构建与测试

从仓库根目录运行：

```bash
uv run cmake --preset release -S Bellhop_RayReuse
uv run cmake --build Bellhop_RayReuse/build/release --parallel
uv run ctest --test-dir Bellhop_RayReuse/build/release --output-on-failure
uv run make -C test/standard_cases test-unit
```

支持边界以 [`FEATURE_SUPPORT_MATRIX.md`](../reference/FEATURE_SUPPORT_MATRIX.md) 为准。

## 命令格式

```bash
Bellhop_RayReuse/build/release/bellhop_rayreuse <file-root> [options]
```

`<file-root>` 不含 `.env` 后缀。程序始终写 `<file-root>.prt`，产品由 ENV 的
run type 决定：

| run type | 单频输出 | 多频输出 |
|---|---|---|
| `CC` | `<root>.shd` | 一个多频 `<root>.shd` |
| `R/RG/RGO` | `<root>.ray` | 明确拒绝 |
| `A` | `<root>.arr` | `<root>_fNNN_<freq>Hz.arr` |
| `a` | `<root>.arr` | `<root>_fNNN_<freq>Hz.arr` |
| `E` | `<root>.ray` | `<root>_fNNN_<freq>Hz.ray` |

## 多频与执行模式

ENV 可直接写严格升序频率列表，也可由 CLI 覆盖：

```bash
Bellhop_RayReuse/build/release/bellhop_rayreuse example \
  --frequencies-hz 500,1000 \
  --execution-mode reuse \
  --verify-cache
```

执行模式：

- `nonreuse`：每频完整追踪；
- `reuse`：trace once，串行逐频投影；
- `parallel`：trace once，按运行时 worker 数进行外层频率并行。

并行示例：

```bash
Bellhop_RayReuse/build/release/bellhop_rayreuse example \
  --frequencies-hz 500,1000,2000 \
  --execution-mode parallel \
  --workers 8
```

`--workers` 未指定时使用硬件并发数；没有 4T/8T 程序级上限。SHD 的
`--output-queue-capacity` 仅限制完成队列，不是线程上限。A/a/E 的 parallel
worker 不直接写文件；主 consumer 按 frequency index 稳定发布。

## 产品生命周期与错误

- 成功模式切换会清除同 root 的异类产品、旧逐频产品和 `.tmp`；
- writer 通过临时文件原子发布单个产品；
- 多频运行中任一频失败会清理本次已发布的逐频产品；
- 环境解析或组合校验在新生命周期开始前失败时，旧有效产品保持不变；
- 未支持组合返回非零状态，并在可用时写入 PRT `FATAL ERROR`；
- 多频 R、line source、irregular receiver、ray-centered family 等不会静默退化。

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
  --case eigenray_geometric_gaussian \
  --profile broadband_smoke \
  --rayreuse-execution-mode parallel \
  --executable Bellhop_RayReuse/build/release/bellhop_rayreuse
```

RayReuse 继续复用 `test/standard_cases/`；没有第二套算例库。
