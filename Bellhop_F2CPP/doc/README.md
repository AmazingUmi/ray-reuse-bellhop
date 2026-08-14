# Bellhop F2CPP 文档

本目录保存只服务 `Bellhop_F2CPP` 子项目的使用、构建和验收资料。

| 文档 | 内容 | 主要读者 |
|---|---|---|
| [PROGRESS.md](./PROGRESS.md) | 当前施工阶段、已完成能力和最新验证基线 | 所有人 |
| [FEATURE_SUPPORT_MATRIX.md](./FEATURE_SUPPORT_MATRIX.md) | 二维单频 supported、intentional divergence 与 deferred/out-of-scope 封板矩阵 | 使用者、维护者 |
| [USAGE.md](./USAGE.md) | 环境要求、编译、测试、CLI、输入输出和故障排查 | 使用者、测试人员 |
| [BUILD_PLAN.md](./BUILD_PLAN.md) | G0/M1/M2 工作包、依赖顺序和完成证据 | 开发者、维护者 |
| [FURTHER_REPLICATION_PLAN.md](./FURTHER_REPLICATION_PLAN.md) | 排除 3D 后的二维功能扩展顺序、依赖和验收门 | 开发者、维护者 |
| [DERIVATION_MANIFEST.md](./DERIVATION_MANIFEST.md) | M2 历史快照身份、校验和、性能门和 RayReuse 派生清单 | 审计者、RayReuse 开发者 |
| [INTERMEDIATE_STATE_CONTRACT.md](./INTERMEDIATE_STATE_CONTRACT.md) | 数值接口、单位和 geometry probe schema v1 | 数值开发者、审计者 |
| [validation/](./validation/) | I3～I8 Fortran oracle、最终场指标和冻结哈希 | 数值开发者、审计者 |

全项目设计、理论和变量契约仍保存在仓库根目录的 `doc/`：

- [Bellhop 源码分析与宽带复用设计](../../doc/01-Bellhop源码分析与宽带复用设计.md)
- [项目实施待办](../../doc/02-项目实施待办.md)
- [基础变量、单位与数值规范](../../doc/04-基础变量单位与数值规范.md)

`Bellhop_F2CPP/README.md` 是子项目首页；本目录不保存生成的 PRT、SHD
或构建产物。
