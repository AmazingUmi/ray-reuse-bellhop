# BELLHOP 2-D 分阶段计时

原版二维 `bellhop` 默认只保留既有总 CPU Time，不执行额外的阶段计时调用。
需要诊断时显式设置：

```bash
BELLHOP_PROFILE_STAGES=1 Bellhop_origin/bin/bellhop <file-root>
```

PRT 末尾会增加：

- `Stage Trace seconds`
- `Stage Influence seconds`
- `Stage Scale seconds`
- `Stage Output seconds`

计时使用 `CPU_TIME`。Trace 和 Influence 在逐射线边界累加；Scale 在每个源
深度缩放调用边界累加；Output 包含 SHD 打开、压力记录写入及 flush。初始化、
环境解析和诊断导出仍只计入总 CPU Time，因此阶段和不要求严格等于总时间。

本地回归入口为：

```bash
Bellhop_RayReuse/scripts/fortran_profile_gate.sh
```

该门分别以关闭/开启状态运行 direct single，要求默认 PRT 不出现阶段字段、
开启 PRT 四个字段齐全，并要求两次 SHD 逐字节一致。

2026-08-01 Apple M4 / gfortran `-O2 -g -std=gnu` 单次诊断记录：

| 算例 | 总 CPU / s | Trace / s | Influence / s | Scale / s | Output / s |
|---|---:|---:|---:|---:|---:|
| direct single 50 Hz | 0.0169 | 0.00498 | 0.01167 | 0.000001 | 0.000198 |
| Munk single 50 Hz | 2.65 | 0.01580 | 2.63189 | 0.000030 | 0.000721 |

Munk 的 Influence 占已分类 CPU 时间约 99%，与 RayReuse 的 Influence 主瓶颈
诊断方向一致。两例 profiled SHD 均与关闭计时/冻结 oracle 哈希相同。
