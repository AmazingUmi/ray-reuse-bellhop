# Bellhop RayReuse F2 Hermite 快路径记录：fe6b33f

## 结论

提交 `fe6b33f` 保留公共 `cervenyHermiteTaper()` 的有限性和半径关系校验，
并为 Cartesian Cerveny Influence 内部已验证参数增加同公式的无重复校验
helper。接收深度、射线状态、频率和由其计算的 `radiusMax` 已在进入热路径
前验证；因此不再为每次图像评估重复检查 offset、full radius、zero radius
和固定半径关系。公式、分支边界和浮点运算顺序不变。

Munk/Cerveny 结果：

| 配置 | `eedc790` wall 中位数 | `fe6b33f` wall 中位数 | 本提交下降 | 相对 F1 前累计下降 |
|---|---:|---:|---:|---:|
| 2频 reuse | 12.4479 s | 10.3705 s | 16.69% | 48.49% |
| 16频 reuse | 161.678 s | 135.572 s | 16.15% | 51.63% |
| 16频 parallel-8 | 42.557 s | 36.184 s | 14.98% | 44.19% |
| 16频 parallel-10 | 40.255 s | 30.823 s | 23.43% | 50.11% |

2频 SHD SHA-256 保持
`cf1f9711aefcab087bd766c395a03b935c1c9cf13980335a368035515fd126bc`；
16频三配置保持
`f01ee48119549a82e79798322bf5227d8fc95054be82de955de5ccadef057c2c`。

## 向量化审计和回滚候选

使用 AppleClang 21 的
`-Rpass=loop-vectorize -Rpass-missed=loop-vectorize
-Rpass-analysis=loop-vectorize` 编译核心。结果显示：

- `PrecomputedRayValues` 已由 `p/q/gamma/kmah` 四个独立 vector 组成，并在
  每条射线预计算前分别 `reserve(pointCount)`；当前不是 AoS；
- depth 热循环仍受详细诊断控制流、复指数调用、有限性检查调用和压力读改写
  依赖阻止，未自动向量化；
- 射线预计算循环还受 SSP evaluate、KMAH 递推和异常早退约束，直接紧凑化
  不能自动解决这些依赖。

据此先试验“是否采集详细诊断”的额外模板专化。它将模板组合从 6 个扩大到
12 个，虽然 SHD 不变，2频 wall 却从 `12.4479 s` 回退到 `18.2163 s`
（慢 `46.34%`）。该候选已完整回滚；后续优化必须同时约束热函数代码体积，
不能只依据源级分支数量判断。

## 运行身份

- 日期：2026-07-31
- Git commit：`fe6b33f530cb73ca9edb90f68d42a3de6689f23b`
- Git tree：`bd6ded65ed121dd590c55e194747cb6dd4bafe94`
- worktree：`dirty = false`
- Release executable SHA-256：
  `49f40120bc80df33be9f72371fecd54f0475688bba2e8fda01bd25ccb7187023`
- 平台：Apple M4，macOS 26.5.2 arm64，10 logical CPUs，16 GiB
- 工具链：Apple clang 21.0.0，CMake 4.0.2
- Python：Conda `py`，CPython 3.12.9，NumPy 2.2.6

## 2频 smoke

协议为 serial reuse、1 次预热、3 次计量。

| 指标 | 中位数 | 三轮范围 |
|---|---:|---:|
| 外部 wall | 10.3705 s | 10.3557–10.4056 s |
| Influence | 10.1325 s | 10.1049–10.1571 s |
| max RSS | 307,712 KiB | 307,712–307,744 KiB |

Influence 相对 `eedc790` 的 `12.1889 s` 下降 `16.87%`。

## 16频确认

协议为 reuse、parallel-8、parallel-10，每配置 1 次预热、3 次计量；并行
配置固定 queue 2、memory budget 2048 MiB，计量轮次旋转配置顺序。

| 配置 | wall 中位数 | 三轮范围 | 范围/中位数 | RSS 中位数 | 相对 reuse |
|---|---:|---:|---:|---:|---:|
| reuse | 135.572 s | 135.101–141.235 s | 4.52% | 611,424 KiB | 1.000× |
| parallel-8 | 36.184 s | 33.938–37.798 s | 10.67% | 632,640 KiB | 3.747× |
| parallel-10 | 30.823 s | 28.889–37.402 s | 27.62% | 634,656 KiB | 4.398× |

reuse Influence 中位数为 `134.534 s`，相对 `eedc790` 的 `160.587 s`
下降 `16.22%`。p10 的中位 wall 最低，但三轮范围显著大于 p8 和 reuse；
继续保留 8/10 workers 为显式配置，不改变默认 worker。

## 验收

提交前完整运行：

```bash
RAYREUSE_BUILD_JOBS=4 Bellhop_RayReuse/scripts/quality_gate.sh
```

Debug 24/24、Release 24/24、Python 49/49、独立性扫描和无 F2CPP 隔离
Release 构建 24/24 全部通过。公共 Hermite API 的无效参数测试继续通过；
内部 helper 只由已经过环境、接收网格和射线状态校验的 Influence 调用。

## 原始报告

- `Bellhop_RayReuse/build/benchmarks/munk_smoke_f2_unchecked_taper_fe6b33f.json`
  （SHA-256
  `87675368fdaa9ed46666836de26e38d09bbc6083ceb8e381ebc25af2d888d3a3`）
- `Bellhop_RayReuse/build/benchmarks/munk_regression_f2_unchecked_taper_fe6b33f.json`
  （SHA-256
  `bbbc845c883a84f2f0214453fc00ae87defed9091566d80f34db9ad8774680cb`）
- 已回滚诊断模板专化的本地 screen JSON SHA-256：
  `c59f87346269150ba2a80da7fe5ab95ef5b84e021ff738695a3136c9da52f36c`。

JSON 和向量化审计构建目录是本机构建产物，不进入 Git。

## 下一步

1. 以 `fe6b33f` 作为新的 2/16频性能基线。
2. 先审计热路径剩余有限性检查的所有权：公共 API、详细诊断和 Debug 保留
   防御性检查；只有 solver 已验证路径存在统一频率末端有限性门时，才建立
   单一、不会倍增模板代码体积的候选。
3. 独立 screen range/depth 循环不变量显式提升；若编译器已完成同等优化，
   立即回滚。
4. 暂不实施笼统的“再做一次 SoA”；先用汇编/采样证明字段加载是瓶颈。
5. 安全候选收敛后运行 Munk 64频 reuse/p8/p10 精选矩阵，再决定默认 worker。
