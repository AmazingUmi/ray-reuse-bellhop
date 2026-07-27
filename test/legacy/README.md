# 非活动历史测试材料

本目录仅保存迁移前的来源材料，不参与标准算例、CI、PlotRead 或数值 oracle。

- `origin_bellhop_reference/`：旧 Munk 单频输入和生成结果；物理输入已迁入 `test/standard_cases/cases/munk_cerveny_cc/`。
- `ray_reuse_experiment/`：早期多频实验输入和大体积 SHD；来源及数值正确性未冻结，禁止作为 RayReuse 验收基线。

新的职责划分：

- 物理算例、频率 profile、执行矩阵和比较规则：`test/standard_cases/`；
- SHD 格式读取/绘图的自包含测试：`test/PlotRead/`；
- 临时完整输出：`test/standard_cases/results/`，不进入 Git。

历史二进制暂时原样保留以保证迁移可恢复。确认不再需要追溯后，可单独评审是否从当前版本删除；删除不会自动清理既有 Git 历史。
