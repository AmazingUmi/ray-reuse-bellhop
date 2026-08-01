# 标准算例结果

本目录只保存由 `codes/standard_cases.py` 生成的输入、求解器输出和运行清单。
除本说明外，内容均为可再生文件，不进入 Git。

结果按以下层级组织：

```text
results/<version>/<case>/<profile>/
├── run_manifest.json
└── fNNN_<frequency>Hz/
    ├── <root>.env
    ├── <root>.prt
    └── <root>.shd
```

冻结的小型参考采样放在 `results/reference/`，并通过 `.gitignore` 显式放行；
完整 SHD 始终由测试过程重新生成。参考文件的生成、来源和更新规则见
[`../REFERENCE_SNAPSHOTS.md`](../REFERENCE_SNAPSHOTS.md)。
