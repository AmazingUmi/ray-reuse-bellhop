# Bellhop RayReuse F2 图像专化记录：eedc790

## 结论

提交 `eedc790` 将 Cartesian Cerveny 的 `imageCount=1/2/3` 在每条射线进入
Influence 时分派到独立模板实例，并将 true/surface/bottom 图像类型改为
编译期常量。默认三图像热路径不再为每个 receiver depth 执行图像循环、
索引到类型的映射和 `switch`。图像贡献仍严格按 true → surface → bottom
的原顺序相加，接收点之间及射线之间的累加顺序不变。

Munk/Cerveny 结果：

| 配置 | F1 `4af3f7f` wall 中位数 | `eedc790` wall 中位数 | 本提交下降 | 相对 F1 前累计下降 |
|---|---:|---:|---:|---:|
| 2频 reuse | 16.8596 s | 12.4479 s | 26.17% | 38.17% |
| 16频 reuse | 229.728 s | 161.678 s | 29.62% | 42.32% |
| 16频 parallel-8 | 55.783 s | 42.557 s | 23.71% | 34.35% |
| 16频 parallel-10 | 55.180 s | 40.255 s | 27.05% | 34.84% |

2频 SHD SHA-256 保持
`cf1f9711aefcab087bd766c395a03b935c1c9cf13980335a368035515fd126bc`；
16频三配置保持
`f01ee48119549a82e79798322bf5227d8fc95054be82de955de5ccadef057c2c`。
两者均与 F1 和更早基线逐字节一致。

## F2 前置布局实验

在保留图像专化前，先按“一次只改一个变量”完成两个 2频 dirty screen：

| 候选 | wall 中位数 | 相对 F1 | RSS 中位数 | 判断 |
|---|---:|---:|---:|---|
| range-major 临时压力累加器，末端转回 depth-major | 16.8910 s | 慢 0.19% | 309,424 KiB | 回滚 |
| segment 内先收集 range，再按 depth → range 累加 | 17.7544 s | 慢 5.31% | 307,760 KiB | 回滚 |

两个候选均通过目标组件测试且 SHD 哈希不变，但没有性能收益。第一项还增加
约 1.6 MiB RSS；第二项保持同一接收点的射线贡献顺序，却恶化了当前数据访问
和循环开销。因此两者未进入 16频矩阵，源码已完整回滚。

## 运行身份

- 日期：2026-07-31
- Git commit：`eedc790b9f7b9e1c75e9359bd0ac8698b2efe270`
- Git tree：`a1a2ca0953394b0730a726d47eb233db7319c28e`
- worktree：`dirty = false`
- Release executable SHA-256：
  `ab0b81f8985829afaae0666c2c582533e73cc49bd6876e659b594cfc34a59342`
- 平台：Apple M4，macOS 26.5.2 arm64，10 logical CPUs，16 GiB
- 工具链：Apple clang 21.0.0，CMake 4.0.2
- Python：Conda `py`，CPython 3.12.9，NumPy 2.2.6

## 2频 smoke

协议为 serial reuse、1 次预热、3 次计量。

| 指标 | 中位数 | 三轮范围 |
|---|---:|---:|
| 外部 wall | 12.4479 s | 12.4322–12.4651 s |
| Influence | 12.1889 s | 12.1791–12.2216 s |
| max RSS | 307,760 KiB | 307,696–307,760 KiB |

Influence 相对 F1 的 `16.5937 s` 下降 `26.55%`。

## 16频确认

协议为 reuse、parallel-8、parallel-10，每配置 1 次预热、3 次计量；并行
配置固定 queue 2、memory budget 2048 MiB，计量轮次旋转配置顺序。

| 配置 | wall 中位数 | 三轮范围 | 范围/中位数 | RSS 中位数 | 相对 reuse |
|---|---:|---:|---:|---:|---:|
| reuse | 161.678 s | 161.460–162.609 s | 0.71% | 611,520 KiB | 1.000× |
| parallel-8 | 42.557 s | 42.524–44.259 s | 4.08% | 629,296 KiB | 3.799× |
| parallel-10 | 40.255 s | 38.324–40.774 s | 6.09% | 634,432 KiB | 4.016× |

reuse Influence 中位数为 `160.587 s`，相对 F1 的 `228.580 s` 下降
`29.75%`。parallel-10 的绝对 wall 比 parallel-8 低 `5.41%`，但三轮范围
仍更大；本提交不改变默认 worker。并行 PRT 的 Influence 是所有 worker
逐频时间之和，不与 wall 直接比较。

## 测试与数值门

新增 `imageCount=1/2/3` 分派组件测试，逐个比较专化热路径与保留详细图像
诊断的通用路径，压力数组要求逐位相同；`imageCount=2` 因此不再只依赖构造
参数校验。

提交前完整运行：

```bash
RAYREUSE_BUILD_JOBS=4 Bellhop_RayReuse/scripts/quality_gate.sh
```

Debug 24/24、Release 24/24、Python 49/49、独立性扫描和无 F2CPP 隔离
Release 构建 24/24 全部通过。

## 原始报告

- `Bellhop_RayReuse/build/benchmarks/munk_smoke_f2_image_specialization_eedc790.json`
  （SHA-256
  `df4d8eba2adc054c75f0fa2d8bb39ed40dfba9739cc4584bf0c11ad43dcf54ef`）
- `Bellhop_RayReuse/build/benchmarks/munk_regression_f2_image_specialization_eedc790.json`
  （SHA-256
  `d54a550ddd2050780a4a403186eea1610e6c5358d6bf88eed1beaae10f196be0`）
- 两个回滚 screen 的本地 JSON SHA-256 分别为
  `9cec91c97a3a48c531d467b68f2e82a2b96378f9739e18c4bc07144422fb4cf7`
  和
  `0156306f0d17e1eaa304f3eec30a3b3cdecd58847a31f628db9b276035887c4e`。

JSON 是本机构建产物，不进入 Git。

## 下一步

1. 保留 `eedc790` 作为新的 2/16频性能基线。
2. F2 下一候选先取得 AppleClang 向量化报告，定位窗口拒绝和实数预计算中
   未向量化的具体依赖；不直接改变复数压力累加顺序。
3. `PrecomputedRayValues` 已是按字段分离的向量集合；先审计字段加载和
   `reserve/resize` 行为，再决定是否值得进一步 SoA/紧凑化。
4. 仅对能在 2频 screen 稳定获益的候选运行 16频矩阵。
5. F2 安全候选收敛后再运行 Munk 64频 reuse/p8/p10 精选矩阵，并据此决定
   默认 worker 和紧预算余量校准。
