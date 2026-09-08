# 展示结果分类

```text
results/
├── reliability/
│   ├── origin/                 # 三版本单频一致性（可再生成）
│   ├── f2cpp/
│   ├── broadband/
│   └── run_summary.json
└── speed/                      # 四条执行模式路线的唯一多频结果
    ├── nonreuse/
    ├── reuse_serial/
    ├── reuse_frequency_w4/
    ├── reuse_range_w4/
    └── run_summary.json        # CLI 参数、耗时与 PRT 分相计时
```

- `reliability/origin|f2cpp|broadband/`：同一个单频环境的三版本直接运行结果；
- `speed/`：`speed.py` 运行四条执行模式路线产生的多频 SHD 与计时；
  `reliability.py routes` 的 2×2 一致性图也从这里读取。

除本文档外，所有内容均为可再生成文件，不进入 Git。
