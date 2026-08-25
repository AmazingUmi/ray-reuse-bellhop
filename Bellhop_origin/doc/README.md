# Bellhop Origin 文档索引

`Bellhop_origin` 是项目的二维 Fortran 单频行为 oracle。生产语义以源代码和
标准算例结果为准；本目录只说明构建、诊断和性能插桩的使用方法。

## Guides

- [`guides/GUIDE_BUILD_TOOLCHAIN.md`](./guides/GUIDE_BUILD_TOOLCHAIN.md)：gfortran、GNU Make、
  VS Code 和日常构建流程；
- [`guides/GUIDE_ORACLE_DIAGNOSTICS.md`](./guides/GUIDE_ORACLE_DIAGNOSTICS.md)：SSP、单射线、
  反射与 Influence oracle 导出；
- [`guides/GUIDE_STAGE_PROFILING.md`](./guides/GUIDE_STAGE_PROFILING.md)：默认关闭的 Trace、
  Influence、Scale 和 Output 分阶段计时。

当前项目工作见 [`../../doc/plans/PLAN_CURRENT_WORK.md`](../../doc/plans/PLAN_CURRENT_WORK.md)，
标准算例见 [`../../test/standard_cases/README.md`](../../test/standard_cases/README.md)。
