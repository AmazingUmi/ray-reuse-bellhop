# RayReuse Feature Sync 状态

> 状态：2026-08-20 完成并封板。本文记录 RR-B1～RR-B4 的依赖和关闭结果，
> 不启动后续算法或性能阶段。

## 路线与依赖

```text
RR-B1 Environment / Boundary Sync
  ├─ RR-B2 General Ray Product Sync
  └─ RR-B3 Arrival / Eigenray Integration
       └─ RR-B4 Integration / Regression Closure
```

| Batch | 关闭结果 |
|---|---|
| RR-B1 | 环境、边界、sidecar、elastic LL 和 ReflectionEvent raw material 已同步并冻结 |
| RR-B2 | R 产品 core 与 frequency-explicit RAY writer 完成 |
| RR-B3 | A/a/E core、G/B traversal 和 frequency-local product 完成 |
| RR-B4 | 正式 parser/executable/CLI/standard-case 接入与全回归完成 |

## 关闭判据

- RR-B1～RR-B3 声明能力均可从正式 executable 使用；
- R/A/a/E 入口闭环，无 TL/SHD silent fallback；
- 单频产品与 Origin/F2CPP 一致；
- 多频 A/a/E 的三种模式逐频一致且不跨频 merge；
- frozen cache fingerprint 前后不变；
- 既有 TL single/broadband/reuse/parallel 无回归；
- standard cases 继续是唯一共享测试资产；
- 未发现 correctness blocker。

实际数值和测试计数见 [`../PROGRESS.md`](../../status/PROGRESS.md)，支持与延期边界见
[`../FEATURE_SUPPORT_MATRIX.md`](../../reference/FEATURE_SUPPORT_MATRIX.md)。
