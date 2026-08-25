# F2CPP 报告索引

本目录保存完成阶段的性能与数值验证证据，不承担当前待办职责。

- [`PERFORMANCE.md`](./PERFORMANCE.md)：P1～P4-02 的 profile、优化、并行决策
  与停止条件。
- [`validation/`](./validation/)：验证器生成的冻结 JSON 报告。

## Validation 分组

| 范围 | 报告前缀 | 主题 |
|---|---|---|
| I3 | `i3_*` | 分段/曲线边界、long-format 材料与 Fortran oracle |
| I4 | `i4_*` | 衰减、弹性/粒度海床、表格反射 |
| I5 | `i5_*` | Q 型二维 SSP 几何与最终场 |
| I6 | `i6_*` | source/receiver、方向图、ray trace 与输出安全 |
| I7 | `i7_*` | component、beam option、coherence 与 beam family |
| I8 | `i8_*` | arrival、eigenray、accumulator 与输出安全 |

冻结 JSON 中的 `command`/`validator_command` 字段记录报告生成时的原始目录，
可能仍出现旧的 `Bellhop_F2CPP/doc/validation/`。这些字段作为历史证据不改写；
新生成报告应写入 `Bellhop_F2CPP/doc/reports/validation/`。
