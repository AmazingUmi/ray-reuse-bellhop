# 展示输入分类

```text
cases/
└── reliability/
    ├── munk_cerveny_cc.env
    └── munk_rayreuse_multifrequency.env
```

- `reliability/munk_cerveny_cc.env`：三套求解器共用的标准单频输入，用于一致性验证；
- `reliability/munk_rayreuse_multifrequency.env`：RayReuse 多频扩展输入
  （`FREQS` 记录 50–800 Hz 共 16 频，共享 launch-angle 数按 fmax=800 Hz
  取 16000），用于执行模式路线的 speed 展示。

输入文件是可版本控制的基础环境；运行时的 `.prt/.shd` 副本统一写入
`demo/results/`，不会回写本目录。
