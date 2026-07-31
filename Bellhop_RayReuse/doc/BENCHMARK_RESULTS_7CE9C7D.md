# Bellhop RayReuse F2 循环不变量记录：7ce9c7d

## 结论

提交 `7ce9c7d` 将以下只读标量显式提升出 Cartesian Cerveny depth 热循环：

- 海面和海底深度在整条射线累积期间不变；
- 右端频率点的幅度和反射相位在当前 segment 内不变。

原实现从 `environment_` 和 `frequencyState.points[rightIndex]` 在每个接收
深度重新取值。AppleClang 21 在当前对象、span 和压力写入组合下没有完成
等价提升；显式局部标量消除了受别名分析约束的重复加载。图像、depth、range、
segment 和射线的遍历及复数累加顺序均未改变。

Munk/Cerveny 结果：

| 配置 | `f1511b9` wall 中位数 | `7ce9c7d` wall 中位数 | 本提交下降 | 相对 F1 前累计下降 |
|---|---:|---:|---:|---:|
| 2频 reuse | 9.0682 s | 7.3463 s | 18.99% | 63.51% |
| 16频 reuse | 113.890 s | 87.578 s | 23.10% | 68.75% |
| 16频 parallel-8 | 29.697 s | 25.525 s | 14.05% | 60.63% |
| 16频 parallel-10 | 28.001 s | 23.715 s | 15.31% | 61.61% |

2频 SHD SHA-256 保持
`cf1f9711aefcab087bd766c395a03b935c1c9cf13980335a368035515fd126bc`；
16频三配置保持
`f01ee48119549a82e79798322bf5227d8fc95054be82de955de5ccadef057c2c`。

## 运行身份

- 日期：2026-07-31
- Git commit：`7ce9c7d51fbc152c26d3b3130a81fc022b074b49`
- Git tree：`6464e9b497931ee0a4ef470e8f2eb7a1f9a5fd40`
- worktree：`dirty = false`
- Release executable SHA-256：
  `d45bbba2f176089e149c39c96839086e95a388e886a42dda85e52c505f21e5c8`
- 平台：Apple M4，macOS 26.5.2 arm64，10 logical CPUs，16 GiB
- 工具链：Apple clang 21.0.0，CMake 4.0.2
- Python：Conda `py`，CPython 3.12.9，NumPy 2.2.6

## 2频 smoke

协议为 serial reuse、1 次预热、3 次计量。

| 指标 | 中位数 | 三轮范围 |
|---|---:|---:|
| 外部 wall | 7.3463 s | 7.3409–7.3680 s |
| Influence | 7.0961 s | 7.0936–7.1192 s |
| max RSS | 307,776 KiB | 307,760–307,824 KiB |

Influence 相对 `f1511b9` 的 `8.7916 s` 下降 `19.29%`。

## 16频确认

协议为 reuse、parallel-8、parallel-10，每配置 1 次预热、3 次计量；并行
配置固定 queue 2、memory budget 2048 MiB，计量轮次旋转配置顺序。

| 配置 | wall 中位数 | 三轮范围 | 范围/中位数 | RSS 中位数 | 相对 reuse |
|---|---:|---:|---:|---:|---:|
| reuse | 87.578 s | 87.577–87.695 s | 0.13% | 611,536 KiB | 1.000× |
| parallel-8 | 25.525 s | 25.303–26.501 s | 4.70% | 630,896 KiB | 3.431× |
| parallel-10 | 23.715 s | 22.797–24.677 s | 7.93% | 634,352 KiB | 3.693× |

reuse Influence 中位数为 `86.563 s`，相对 `f1511b9` 的 `112.820 s`
下降 `23.27%`。并行绝对 wall 继续下降，但串行收益更大，因此相对当前
reuse 的加速比下降；这不是并行回退。p10 中位数比 p8 低 `7.09%`，波动
仍更大，默认 worker 不变。

## 验收

提交前完整运行：

```bash
RAYREUSE_BUILD_JOBS=4 Bellhop_RayReuse/scripts/quality_gate.sh
```

Debug 24/24、Release 24/24、Python 49/49、独立性扫描和无 F2CPP 隔离
Release 构建 24/24 全部通过。目标组件、single、nonreuse、reuse、parallel
测试也在 2频 screen 前单独通过。

## 原始报告

- `Bellhop_RayReuse/build/benchmarks/munk_smoke_f2_loop_invariants_7ce9c7d.json`
  （SHA-256
  `25b486b0b35371e102bf633a2f68f3ceb9d5faddbccf5d2d5aead62b40cc1359`）
- `Bellhop_RayReuse/build/benchmarks/munk_regression_f2_loop_invariants_7ce9c7d.json`
  （SHA-256
  `838034eeb4dabc5e3389852d8760a73c16566d3f409cde42fca92abe4f89c965`）

JSON 是本机构建产物，不进入 Git。

## 下一步

1. 以 `7ce9c7d` 作为新的 2/16频性能基线。
2. 已 screen segment 左右端状态及 position、slowness、sound speed、q、
   tau、gamma 差值缓存：2频 wall 为 `7.6172 s`，相对本提交慢 `3.69%`，
   Influence 慢 `3.71%`；SHD 不变，已完整回滚。其本地 JSON SHA-256 为
   `6b32e3973b4b2283571ef02405e7a7aa8d5a59bf1941c89762ab8929c3023bb5`。
3. 下一候选只审计 receiver-depth 循环边界和连续数据指针，不再增加
   segment 复数差值局部量。
4. 候选继续经过 2频 screen，不能稳定获益则立即回滚。
5. 安全局部性候选收敛后运行 Munk 64频 reuse/p8/p10 精选矩阵。
