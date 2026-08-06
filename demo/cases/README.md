# 展示输入分类

```text
cases/
├── reliability/
│   └── munk_cerveny_cc.env
└── rayreuse_multifrequency/
    └── munk_rayreuse_multifrequency.env
```

- `reliability/`：三套求解器共用的标准单频输入，用于一致性验证；
- `rayreuse_multifrequency/`：RayReuse 多频扩展输入，用于展示一次射线追踪投影多个频率。

输入文件是可版本控制的基础环境；运行时的 `.prt/.shd` 副本统一写入
`demo/results/`，不会回写本目录。
