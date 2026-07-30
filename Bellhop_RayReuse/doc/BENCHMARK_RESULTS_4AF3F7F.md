# Bellhop RayReuse F1 关闭记录：4af3f7f

## 结论

提交 `4af3f7f` 在已验证工作区形状和循环索引范围内缓存压力 span，以一次
depth-major 线性索引取得单元引用，替代每次复数贡献的两次
`FrequencyWorkspace::at()` 调用。它不改变内存布局、遍历顺序或复数累加
顺序。

Munk/Cerveny 结果：

| 配置 | 对照 wall 中位数 | `4af3f7f` wall 中位数 | 下降 |
|---|---:|---:|---:|
| 2频 reuse，对照 `96f23f8` | 18.7690 s | 16.8596 s | 10.17% |
| 2频 reuse，对照 F1 前 `44ddeab` | 20.1310 s | 16.8596 s | 16.25% |
| 16频 reuse，对照 `c77ff60` | 280.284 s | 229.728 s | 18.04% |
| 16频 parallel-8，对照 `c77ff60` | 64.829 s | 55.783 s | 13.95% |
| 16频 parallel-10，对照 `c77ff60` | 61.776 s | 55.180 s | 10.68% |

2频 SHD SHA-256 保持
`cf1f9711aefcab087bd766c395a03b935c1c9cf13980335a368035515fd126bc`；
16频三配置 SHD SHA-256 保持
`f01ee48119549a82e79798322bf5227d8fc95054be82de955de5ccadef057c2c`。
两者均与优化前基线逐字节一致。

## 运行身份

- 日期：2026-07-30
- Git commit：`4af3f7ff52975402011e196eeeab0d5c1dddd672`
- Git tree：`4300a17376638967a004aa2b6ae23db4c72f96f5`
- worktree：`dirty = false`
- Release executable SHA-256：
  `43a1982d12fed6f0016276a2a5d65f70ffe5a19f02ee660b958ca63542715353`
- 平台：Apple M4，macOS 26.5.2 arm64，10 logical CPUs，16 GiB
- 工具链：Apple clang 21.0.0，CMake 4.0.2
- Python：Conda `py`，CPython 3.12.9，NumPy 2.2.6

## 2频 smoke

协议为 serial reuse、1 次预热、3 次计量。

| 指标 | 中位数 | 三轮范围 |
|---|---:|---:|
| 外部 wall | 16.8596 s | 16.8375–16.8611 s |
| Influence | 16.5937 s | 16.5765–16.6034 s |
| max RSS | 307,776 KiB | 307,728–307,872 KiB |

Influence 相对 `96f23f8` 下降 `10.36%`，相对 F1 前下降 `16.49%`。

## 16频确认

协议为 reuse、parallel-8、parallel-10，每配置 1 次预热、3 次计量；并行
配置固定 queue 2、memory budget 2048 MiB，计量轮次旋转配置顺序。

| 配置 | wall 中位数 | 三轮范围 | 范围/中位数 | RSS 中位数 |
|---|---:|---:|---:|---:|
| reuse | 229.728 s | 229.680–229.793 s | 0.05% | 611,488 KiB |
| parallel-8 | 55.783 s | 55.731–58.133 s | 4.30% | 630,896 KiB |
| parallel-10 | 55.180 s | 51.329–55.283 s | 7.16% | 634,720 KiB |

reuse Influence 中位数为 `228.580 s`，相对 `c77ff60` 的 `279.211 s`
下降 `18.13%`。parallel-8/10 相对当前 reuse 分别为 `4.118×/4.163×`。
10 workers 中位数只快约 `1.1%`，范围仍大于 8 workers，因此继续作为显式
调优配置，不改变默认 worker。

并行 PRT 的 Influence 是所有 worker 逐频时间之和，不与 wall 直接比较。

## 验收

提交前完整运行：

```bash
RAYREUSE_BUILD_JOBS=4 Bellhop_RayReuse/scripts/quality_gate.sh
```

Debug 24/24、Release 24/24、Python 49/49、独立性扫描和无 F2CPP 隔离
Release 构建 24/24 全部通过。depth-major span 与检查访问共享同一存储的
契约也已加入单元测试。

## 原始报告

- `Bellhop_RayReuse/build/benchmarks/munk_smoke_f1_linear_index_4af3f7f.json`
  （SHA-256
  `8dd99a0a80c59466313c1b4cd2e6056d39b87b376416bd10fd8858abeaf19367`）
- `Bellhop_RayReuse/build/benchmarks/munk_regression_f1_4af3f7f.json`
  （SHA-256
  `90cdeb57ccd178bf6ee956753819c80f2ad31fa85f30e751b603eab65c8a8d64`）

JSON 是本机构建产物，不进入 Git。

## F1 关闭与下一步

F1 的默认关闭计数/细分计时、solver 预验证入口、重复完整工作区校验移出、
线性压力访问、2频性能门、16频三配置确认和 SHD 字节一致性均已完成。

下一步进入 F2，继续遵守“一次只改一个变量”：

1. 首先建立 range-major 临时压力布局的独立候选，明确 SHD 写出转换边界；
2. 仅当2频 Munk 有稳定收益且 SHD 一致时，才扩展到16频；
3. 随后依次评估 receiver depth tile、射线预计算 SoA 和编译器向量化报告；
4. 暂不改变默认 worker，不运行 Munk 64频全矩阵。
