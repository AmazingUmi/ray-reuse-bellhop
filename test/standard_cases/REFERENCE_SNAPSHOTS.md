# 标准算例紧凑数值参考

## 职责与范围

本阶段将原版 Bellhop Fortran 定义为场结果的主要数值 oracle，
`Bellhop_F2CPP` 用作单频 C++ 派生一致性参考，`Bellhop_RayReuse` 是覆盖
single、nonreuse、reuse 和 parallel 的被验收对象。快照只冻结共享标准算例
的代表性数值，不代替完整 SHD 逐场比较。

首批参考限定为六个算例的 `single` profile。每个频率切片保存：

- bearing、source depth、receiver depth、receiver range 的规则网格采样；
- 全场复压力幅值最大点；
- 每点坐标、复压力实部/虚部、幅值和 TL；
- ENV、SHD、oracle 可执行文件 SHA-256、来源提交和 SHD 布局。

规则网格在 bearing/source depth/receiver depth 轴最多各取 3 点，在 range 轴
最多取 5 点，均包含端点并尽量均匀。若最大幅值点与网格点重合，只保存一次
并同时记录两个选择标签。

## 更新规则

参考文件位于 `results/reference/origin/single/`。完整 SHD 仍是可再生构建
产物，不进入 Git。参考更新必须显式运行生成命令并单独审查；任何测试或质量
门都不得自动覆盖参考文件。

生成前先运行原版单频算例：

```bash
conda run -n py python test/standard_cases/codes/standard_cases.py test \
  --version origin --case all --profile single
```

再对每个 `run_manifest.json` 调用：

```bash
conda run -n py python test/standard_cases/codes/reference_snapshots.py \
  generate /path/to/run_manifest.json /path/to/reference.json \
  --source-revision <verified-commit>
```

提交参考更新时必须同时记录：修改原因、oracle 源码/可执行文件身份、最大数值
变化以及完整三模型回归结果。不得仅因候选实现发生变化而重建 oracle。

检查一个或多个参考文件的 schema 与派生量内部一致性：

```bash
conda run -n py python test/standard_cases/codes/reference_snapshots.py \
  check test/standard_cases/results/reference/origin/single/*.json
```

将任意候选 SHD 的频率切片与参考比较并可选保存 JSON 报告：

```bash
conda run -n py python test/standard_cases/codes/reference_snapshots.py \
  validate /path/to/reference.json /path/to/candidate.shd \
  --candidate-frequency-index 0 --report /path/to/report.json
```

## 验收矩阵

快速门使用已提交的紧凑快照和单元测试，不启动求解器。集成门运行六例
single 和两频 broadband smoke：原版 Fortran 是主参考，F2CPP 与 RayReuse
逐频比较；RayReuse 的三个多频模式再互相比较。16频与 Munk 性能矩阵只在
里程碑时手动运行。

复压力绝对/相对误差是主判据，TL 是幅值高于既定 pressure floor 后的辅助
判据。相位只在参考与候选幅值均高于 `1.0e-4` 时比较，避免近零复压力的
无意义相位跳变；`1.0e-3 rad` 相位限值与 `1.0e-7` 压力绝对预算在该 floor
处一致。具体数值统一来自 `codes/tolerances.toml`，G3 必须用三模型实测误差
复核该门，后续不得凭经验收紧。
