# Bellhop RayReuse F2 末端有限性校验记录：f1511b9

## 结论

提交 `f1511b9` 将 Release solver 热路径的逐图像、逐最终贡献和逐压力更新
有限性检查，收敛到每个频率缩放入口已有的完整未缩放压力场扫描。它不是关闭
有限性校验：

- 公共 `CartesianCervenyInfluence::accumulate()` 仍在输入前和计算返回前
  扫描完整工作区；
- Debug 仍保留逐图像、逐贡献和逐压力更新的即时检查；
- 详细诊断路径仍检查 window、taper、指数和图像贡献；
- solver 私有预验证路径在每频 Influence 完成后立即进入
  `scaleCoherentCartesianPointPressure()`，其首项操作是扫描完整未缩放场。

新增回归以有限的 `double::max()` 幅度触发计算溢出，确认 Debug 与 Release
公共 API 都拒绝非有限输出。

Munk/Cerveny 结果：

| 配置 | `fe6b33f` wall 中位数 | `f1511b9` wall 中位数 | 本提交下降 | 相对 F1 前累计下降 |
|---|---:|---:|---:|---:|
| 2频 reuse | 10.3705 s | 9.0682 s | 12.56% | 54.95% |
| 16频 reuse | 135.572 s | 113.890 s | 15.99% | 59.37% |
| 16频 parallel-8 | 36.184 s | 29.697 s | 17.93% | 54.19% |
| 16频 parallel-10 | 30.823 s | 28.001 s | 9.15% | 54.67% |

2频 SHD SHA-256 保持
`cf1f9711aefcab087bd766c395a03b935c1c9cf13980335a368035515fd126bc`；
16频三配置保持
`f01ee48119549a82e79798322bf5227d8fc95054be82de955de5ccadef057c2c`。

## 运行身份

- 日期：2026-07-31
- Git commit：`f1511b941fa390f69633c8a46127044a08c11175`
- Git tree：`9977cf50d91fa550728e5b652ce1e1d28ef643d4`
- worktree：`dirty = false`
- Release executable SHA-256：
  `9ad4b4f355f1fbe1e1f379a08e69dc5157bf1e848666b4c97354c89bda49c5e3`
- 平台：Apple M4，macOS 26.5.2 arm64，10 logical CPUs，16 GiB
- 工具链：Apple clang 21.0.0，CMake 4.0.2
- Python：Conda `py`，CPython 3.12.9，NumPy 2.2.6

## 2频 smoke

协议为 serial reuse、1 次预热、3 次计量。

| 指标 | 中位数 | 三轮范围 |
|---|---:|---:|
| 外部 wall | 9.0682 s | 9.0487–9.0732 s |
| Influence | 8.7916 s | 8.7632–8.8071 s |
| max RSS | 307,808 KiB | 307,744–307,808 KiB |

Influence 相对 `fe6b33f` 的 `10.1325 s` 下降 `13.23%`。

## 16频确认

协议为 reuse、parallel-8、parallel-10，每配置 1 次预热、3 次计量；并行
配置固定 queue 2、memory budget 2048 MiB，计量轮次旋转配置顺序。

| 配置 | wall 中位数 | 三轮范围 | 范围/中位数 | RSS 中位数 | 相对 reuse |
|---|---:|---:|---:|---:|---:|
| reuse | 113.890 s | 113.877–114.844 s | 0.85% | 611,488 KiB | 1.000× |
| parallel-8 | 29.697 s | 29.561–31.583 s | 6.81% | 629,408 KiB | 3.835× |
| parallel-10 | 28.001 s | 26.651–28.633 s | 7.08% | 634,496 KiB | 4.067× |

reuse Influence 中位数为 `112.820 s`，相对 `fe6b33f` 的 `134.534 s`
下降 `16.14%`。p10 波动从上一提交的 `27.62%` 收敛到 `7.08%`，但中位数
只比 p8 低 `5.71%`；继续保留显式 workers 配置，不改变默认值。

## 验收

提交前完整运行：

```bash
RAYREUSE_BUILD_JOBS=4 Bellhop_RayReuse/scripts/quality_gate.sh
```

Debug 24/24、Release 24/24、Python 49/49、独立性扫描和无 F2CPP 隔离
Release 构建 24/24 全部通过。新增测试同时覆盖公共 API 的输出完整扫描和
统计计数；solver 的缩放入口完整扫描由现有 pressure scaling 与端到端测试
覆盖。

## 原始报告

- `Bellhop_RayReuse/build/benchmarks/munk_smoke_f2_terminal_validation_f1511b9.json`
  （SHA-256
  `1c7188331aed04c6f4aa1eb701e79b1217a6fbd343e3577d87ee395fa0954b7a`）
- `Bellhop_RayReuse/build/benchmarks/munk_regression_f2_terminal_validation_f1511b9.json`
  （SHA-256
  `d842d8ac6b95d054972ec7fbf9021c01821737670908c85b7af2f7a930c8ab17`）

JSON 是本机构建产物，不进入 Git。

## 下一步

1. 以 `f1511b9` 作为新的 2/16频性能基线。
2. 独立 screen 环境边界深度、右端幅度/相位等 range/depth 循环不变量的
   显式提升；若编译器已做等价提升则立即回滚。
3. 不再扩大 `accumulateImpl` 模板组合；继续关注指令体积。
4. 剩余安全候选收敛后运行 Munk 64频 reuse/p8/p10 精选矩阵。
