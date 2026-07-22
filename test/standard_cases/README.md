# Bellhop 单频标准算例

本目录把项目文档中 P1 阶段要求的单频 Fortran oracle 整理为可重复运行的标准算例。
输入文件进入版本控制，生成的 `.prt/.shd` 写入 `output/`，不会覆盖已有参考文件。

## 当前算例

| 算例 | 目的 | 主要特征 | 当前验收层级 |
|---|---|---|---|
| `constant_speed_direct` | 最小解析/冒烟基线 | 1500 m/s 等声速；窄角度；5 km 内不触碰边界 | 运行成功、PRT 配置、SHD 头 |
| `munk_cerveny_cc` | 现有工程基线标准化 | 50 Hz Munk SSP；Cartesian Cerveny；100 km 接收网格 | 运行成功、PRT 配置、SHD 头 |

`munk_cerveny_cc.env` 保留现有
`test/test_origin_bellhop/MunkB_Coh_CervenyC.env` 的数值设置，只统一了文件根名和注释。
`test/test_ray_reuse/` 中的多频文件是历史实验材料，不是原版 Bellhop 的可信 oracle，不能作为这两个算例的期望输出。

## 运行

先在项目根目录构建原版二维 Bellhop：

```bash
make -C Bellhop_origin
test/standard_cases/run_cases.sh
```

也可以显式指定可执行文件和算例：

```bash
test/standard_cases/run_cases.sh /path/to/bellhop constant_speed_direct
test/standard_cases/run_cases.sh /path/to/bellhop munk_cerveny_cc
```

脚本会：

1. 将输入复制到 `test/standard_cases/output/<case>/`；
2. 在隔离目录中运行 Bellhop；
3. 检查 `.prt`、`.shd` 是否生成；
4. 检查 PRT 中的模式、波束类型、接收网格和运行错误；
5. 读取 SHD 固定记录头，检查频率数、维度和中心频率。

SHD 复压力不做逐字节哈希比较。不同编译器或平台可能只在单精度末位产生差异，正式回归应按复压力和 TL 容差比较。

## 尚需补充（进入 C++ 移植前）

以下内容仍属于 P1 的必要缺口，按优先级排列：

1. **数值期望值与容差**：保存指定接收点的复压力实部/虚部和 TL；冻结最大绝对误差、相对误差及 TL 误差阈值。
2. **MATLAB 读取验收**：用 `test/test_origin_bellhop/plotshd_origin/read_shd.m` 实际读取两份输出，确认频率轴、源深、接收深度、距离轴和压力维度；当前环境未安装 MATLAB/Octave，尚未执行。
3. **轨迹 oracle**：增加可选导出并记录每步 `position/slowness/dynamicP/dynamicQ/soundSpeed/travelTime`。
4. **边界 oracle**：新增至少一个平海面/平海底多次反射算例，并导出反射前后状态和 `SeaSurface/Seabed` 事件属性。
5. **Influence oracle**：导出单条指定发射角对指定接收点的复压力贡献，覆盖普通传播和焦散附近。
6. **衰减与海底类型**：增加水体频率相关吸收、刚性海底、声学半空间海底三个独立算例，避免把多种机制混在一个回归中。
7. **可追溯元数据**：记录 git commit、编译器版本/选项、平台、CPU、线程数、运行时间和峰值内存；`.prt` 的 CPU Time 不宜作为稳定期望值。
8. **实验材料隔离**：为 `test/test_ray_reuse/` 增加来源说明和非基线标记，后续宽带非复用基线应使用独立目录和独立期望数据。

只有第 1～5 项完成后，单频 Fortran 结果才足以作为 C++ 轨迹、动态射线和 Influence 三层误差定位的 oracle。
