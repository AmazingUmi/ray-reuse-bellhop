# Bellhop RayReuse F1 基准记录：96f23f8

## 结论

提交 `96f23f8` 将冻结射线缓存和已校验逐频投影接入 Influence 私有预验证
入口，同时保留公共 API 的完整防御性校验。Munk/Cerveny 2频 serial reuse
在相同协议下：

| 指标 | 优化前 `44ddeab` | 优化后 `96f23f8` | 变化 |
|---|---:|---:|---:|
| 外部 wall 中位数 | 20.1310 s | 18.7690 s | -6.77% |
| Influence 中位数 | 19.8713 s | 18.5109 s | -6.85% |
| max RSS 中位数 | 307,760 KiB | 307,856 KiB | +96 KiB |
| SHD SHA-256 | `cf1f9711…` | `cf1f9711…` | 逐字节一致 |

这是 F1 的第一项安全热路径优化；没有改变射线、segment、range、depth、
image 或复数贡献的累加顺序。

## 运行身份与协议

- 日期：2026-07-30
- 平台：Apple M4，macOS 26.5.2 arm64，10 logical CPUs，16 GiB
- 工具链：Apple clang 21.0.0，CMake 4.0.2
- Python：Conda `py`，CPython 3.12.9，NumPy 2.2.6
- 算例：`munk_cerveny_cc / broadband_smoke`
- 频率：50、250 Hz
- 模式：serial `reuse`
- 协议：1 次预热、3 次计量，外部 wall 与隔离子进程 max RSS

优化前身份：

- commit：`44ddeab7dc43c053e85cf1346a2e026521042b9d`
- worktree：`dirty = false`
- executable SHA-256：
  `63c3f6e3d23c88a40f4460e862bde87bf8351f5358a091654d4fd375539bb169`

优化后身份：

- commit：`96f23f8f32c273de7dc2d07de0ad8419b0594485`
- tree：`11adb6ea271ec042bc695279d62473402e7d4b51`
- worktree：`dirty = false`
- executable SHA-256：
  `0ea968ea5328edc8ec8816a0f3e1fb6a0149295d2a35673b645a59b26ad637f3`

两组报告的 ENV 和全部 SHD 样本均通过哈希门；共同 SHD SHA-256 为
`cf1f9711aefcab087bd766c395a03b935c1c9cf13980335a368035515fd126bc`。

## 原始样本

| 版本 | wall 范围 | Influence 范围 | RSS 范围 |
|---|---:|---:|---:|
| `44ddeab` | 20.0706–20.1841 s | 19.8114–19.9330 s | 307,744–307,808 KiB |
| `96f23f8` | 18.7510–18.7918 s | 18.4893–18.5227 s | 307,696–307,872 KiB |

原始报告：

- `Bellhop_RayReuse/build/benchmarks/munk_smoke_pre_f1_44ddeab.json`
  （SHA-256
  `7d8aaf09494e725fc3e1a088b4529a0f8609f196346a280f9f1f0560b6c8148b`）
- `Bellhop_RayReuse/build/benchmarks/munk_smoke_f1_96f23f8.json`
  （SHA-256
  `0ae1a03e66b35976a317261f529c8a56939734738ff4cf2ba2ca1a6840ca5db0`）

JSON 是本机构建产物，不进入 Git；本文件冻结可审阅结果和报告哈希。

## 诊断计数

`--profile-influence` 默认关闭；一次 Munk 2频诊断得到：

| 项目 | 数值 |
|---|---:|
| 射线累积 | 10,000 |
| 已验证射线点（逐射线完整扫描） | 0 |
| 已验证工作区值（逐射线完整扫描） | 0 |
| 活动射线点 | 3,367,946 |
| segment candidates / eligible | 3,304,654 / 2,247,392 |
| receiver range evaluations | 4,972,960 |
| receiver depth evaluations | 999,564,960 |
| image evaluations | 2,998,694,880 |
| window / taper rejections | 1,697,322,678 / 894,589,970 |
| 非零 image contributions | 406,782,232 |
| validation / precompute / hot loop | 0.0002 / 0.1427 / 18.4380 s |

该运行启用了计数和细分计时，不能与默认关闭统计的正式 wall 直接比较。它
证明重复全量扫描已经消失，也确认剩余时间由 depth/image 热循环主导。

## 验收

提交前完整运行：

```bash
RAYREUSE_BUILD_JOBS=4 Bellhop_RayReuse/scripts/quality_gate.sh
```

结果包括 Debug 24/24、Release 24/24、Python 49/49、独立性扫描和无 F2CPP
隔离 Release 构建 24/24，全部通过。

## 下一步

1. 在已验证索引范围内一次取得压力 span/reference，以线性索引完成 read、
   add、write，消除同一贡献的两次 `workspace.at()` 边界检查；
2. 保持每个接收点的贡献顺序，先用 Munk 2频复测并要求 SHD 逐字节一致；
3. 若有稳定收益，运行 Munk 16频 reuse、parallel-8、parallel-10，每配置
   1 次预热、至少 3 次计量；
4. F1 关闭后再进入 range-major 临时工作区、depth tile 和 SoA 的独立 F2
   实验；任何改变累加顺序的方案另行制定误差预算；
5. 暂不运行 Munk 64频全矩阵，也不改变默认 worker 数。
