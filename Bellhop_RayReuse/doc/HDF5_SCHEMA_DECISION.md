# HDF5 输出格式决策

## 当前决定

HDF5 在本阶段只冻结候选 schema，不进入实现和发布依赖。默认结果继续使用
Acoustic Toolbox 兼容 SHD，原因如下：

- 当前标准算例和全部数值门均以 SHD 为兼容接口；
- Conda `py` 未安装 `h5py`，系统构建环境也未发现 HDF5；
- 直接加入 HDF5 会同时引入 CMake 探测、动态库分发、压缩策略和跨语言复数
  表示等尚未关闭的决策；
- 当前没有结果尺寸、互操作需求或 SHD 限制证明该依赖已经必要。

因此，未出现明确使用方需求前，不增加 HDF5 writer，也不改变 SHD 默认输出。

## 候选 schema v1

若未来启用，文件根组采用以下数据集：

| 名称 | 类型 | 形状 |
|---|---|---|
| `frequency_hz` | float64 | `[F]` |
| `receiver_depth_m` | float64 | `[D]` |
| `receiver_range_m` | float64 | `[R]` |
| `pressure_real` | float64 | `[F,D,R]` |
| `pressure_imag` | float64 | `[F,D,R]` |

压力实部和虚部分开保存，避免 HDF5 compound complex 在 C++、Python 和
MATLAB 间的类型差异。chunk 以单频切片为边界，首版默认不压缩；parallel
solver 仍由单 writer 按频率 hyperslab 写入。

根属性至少包含：

- `schema_version = 1`
- `producer_version`
- `title`
- `complete`：创建时为 false，成功 finalize 后改为 true

HDF5 数值门比较 schema、坐标轴和压力值，不使用整个文件的字节哈希。

## 未来启用前必须关闭的决策

1. HDF5 是必需依赖还是可选构建；
2. 压力是否保持内部 float64 精度；
3. 是否压缩及默认压缩级别；
4. 动态库由系统提供、静态链接还是随包分发；
5. CLI 使用显式 `shd|hdf5|both` 选择，默认是否仍为 SHD；
6. 异常终止时临时文件与原子提交策略。
