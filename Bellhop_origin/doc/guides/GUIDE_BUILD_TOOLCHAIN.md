# VS Code 与 Bellhop/gfortran 编译学习笔记

> 环境核对：2026-08-25。当前主环境为 Apple Silicon Mac、macOS 26.5、
> Homebrew GCC/gfortran 15.2、GNU Make、LLDB、VS Code。
>
> 目标：保持日常操作简单，用一条 `make` 完成 Bellhop 编译，并能在 VS Code 中一键构建和运行原始算例。

---

## 1. 当前环境

本机已确认的工具：

| 项目 | 当前值 |
|---|---|
| CPU 架构 | Apple Silicon `arm64` |
| Fortran 编译器 | `/opt/homebrew/bin/gfortran` |
| gfortran 版本 | Homebrew GCC 15.2.0 |
| Make | `/usr/bin/make` 3.81 |
| 调试器 | LLDB / CodeLLDB |
| Bellhop 源码目录 | `Bellhop_origin/` |
| 可执行文件 | `Bellhop_origin/bin/bellhop` |

检查环境：

```bash
uname -m
which gfortran
gfortran --version
which make
make --version
which lldb
lldb --version
```

Apple Silicon 上 Homebrew 的默认安装前缀是：

```text
/opt/homebrew
```

Intel Mac 通常使用 `/usr/local`。因此，换到另一台 Mac 时应先运行 `which gfortran`，再检查 VS Code 中的编译器路径。

---

## 2. 最简单的日常工作流

在项目根目录执行：

```bash
# 编译 Bellhop
make -C Bellhop_origin

# 查看当前编译器、参数和输出位置
make -C Bellhop_origin info

# 查看程序依赖的动态库
make -C Bellhop_origin runtime-deps

# 清理中间文件和可执行文件
make -C Bellhop_origin clean

# 完整重新编译
make -C Bellhop_origin clean
make -C Bellhop_origin
```

也可以进入源码目录后执行：

```bash
cd Bellhop_origin
make
```

VS Code 中：

- `Command + Shift + B`：编译 Bellhop；
- `F5`：先编译，再用默认算例运行；
- 默认算例由 `test/standard_cases/cases/munk_cerveny_cc/` 定义，F5 前生成到 `test/standard_cases/results/origin/munk_cerveny_cc/single/f000_50Hz/`。

> 日常增量编译只需 `make`。只有修改编译参数、链接参数，或者怀疑旧 `.o/.mod` 文件干扰结果时才需要 `make clean`。

---

## 3. 使用 GNU Make

本机使用系统自带的 GNU Make：

```text
/usr/bin/make
```

原版 Acoustic Toolbox 文档通常写：

```bash
make
make clean
make -j4
```

这些命令在当前 Mac 上可以直接使用。

`-j4` 表示最多并行执行 4 个编译任务：

```bash
make -C Bellhop_origin -j4
```

当前 Makefile 已显式描述 Fortran 模块依赖，可以安全并行。不过项目规模不大，默认 `make` 已足够简单。

---

## 4. VS Code 插件

项目推荐插件记录在 `.vscode/extensions.json`。

### 4.1 Modern Fortran

扩展 ID：

```text
fortran-lang.linter-gfortran
```

用途：

- Fortran 语法高亮；
- 调用 gfortran 检查语法；
- 配合 `fortls` 提供补全、跳转和符号查询；
- 识别 `.f90` 和 Fortran 模块。

### 4.2 CodeLLDB

扩展 ID：

```text
vadimcn.vscode-lldb
```

用途：

- 在 Apple Silicon Mac 上运行和调试原生程序；
- 断点、单步运行、调用栈和变量查看；
- 可调试带 DWARF 符号的 gfortran 程序。

CodeLLDB 与本机 Apple Silicon 和 LLDB 工具链匹配，适合运行和调试 gfortran 生成的 Mach-O 程序。

### 4.3 Markdown All in One

扩展 ID：

```text
yzhang.markdown-all-in-one
```

用于维护项目文档和学习笔记。

### 4.4 暂时不需要

- Code Runner：容易绕开 Makefile、模块依赖和正确运行目录；
- 多个 Fortran 语言插件：可能重复诊断；
- CMake Tools：当前项目使用 Makefile；
- Makefile Tools：单一构建任务用 VS Code 内置 Task 已足够。

---

## 5. VS Code 配置说明

### 5.1 `settings.json`

核心配置：

```json
{
  "fortran.linter.compiler": "gfortran",
  "fortran.linter.compilerPath": "/opt/homebrew/bin/gfortran",
  "fortran.linter.includePaths": [
    "${workspaceFolder}/Bellhop_origin/misc",
    "${workspaceFolder}/Bellhop_origin/Bellhop",
    "${workspaceFolder}/Bellhop_origin/build/mod"
  ],
  "fortran.linter.extraArgs": [
    "-std=gnu",
    "-Wno-tabs"
  ]
}
```

说明：

- 显式使用 Homebrew `gfortran`，避免误用 Apple `/usr/bin/gcc`；
- Apple 的 `/usr/bin/gcc` 实际上是 Clang，不包含 gfortran；
- `build/mod` 用于存放和查找 `.mod` 文件；
- 不启用保存时自动格式化，避免旧 Fortran 文件产生巨大 diff。

### 5.2 `tasks.json`

VS Code 默认构建任务等价于：

```bash
cd Bellhop_origin
/usr/bin/make bellhop
```

任务中额外设置：

```json
"PATH": "/opt/homebrew/bin:${env:PATH}"
```

原因是从 Finder 启动的 VS Code 不一定继承终端的完整 PATH。显式加入 `/opt/homebrew/bin` 可以确保 Makefile 找到正确的 gfortran。

### 5.3 `launch.json`

关键配置：

```json
{
  "type": "lldb",
  "program": "${workspaceFolder}/Bellhop_origin/bin/bellhop",
  "args": ["munk_cerveny_cc_f000_50Hz"],
  "cwd": "${workspaceFolder}/test/standard_cases/results/origin/munk_cerveny_cc/single/f000_50Hz",
  "preLaunchTask": "Prepare Munk Standard Case"
}
```

含义：

- `program`：Bellhop 可执行文件；
- `args`：输入文件根名，不带 `.env`；
- `cwd`：算例目录，Bellhop 会从这里读取 `.env` 并写出 `.prt/.shd`；
- `preLaunchTask`：F5 前自动执行编译。

> Bellhop 对当前工作目录敏感。程序路径正确但 `cwd` 错误时，仍会找不到环境文件。

---

## 6. 当前 Makefile 结构

Makefile 位于：

```text
Bellhop_origin/Makefile
```

它只构建原始 2-D Bellhop 所需源码，不构建完整 Acoustic Toolbox 的 Kraken、Scooter、tslib 等组件。

主要目录：

```makefile
FC = gfortran

BUILD_DIR = build
OBJ_DIR   = $(BUILD_DIR)/obj
MOD_DIR   = $(BUILD_DIR)/mod
BIN_DIR   = bin
```

输出结构：

```text
Bellhop_origin/build/obj    # .o 目标文件
Bellhop_origin/build/mod    # .mod 模块文件
Bellhop_origin/bin/bellhop  # 最终程序
```

默认目标：

```makefile
all: bellhop

bellhop: $(TARGET)
```

因此下面两条命令等价：

```bash
make -C Bellhop_origin
make -C Bellhop_origin bellhop
```

Makefile 显式列出了 Fortran 模块依赖顺序，避免下游源码在所需 `.mod` 文件生成前被编译。

---

## 7. 编译参数

当前参数：

```makefile
FFLAGS  = -O2 -g -std=gnu -ffree-line-length-none
MODFLAGS = -J$(MOD_DIR) -I$(MOD_DIR)
```

### 7.1 Fortran 参数

| 参数 | 含义 | 选择原因 |
|---|---|---|
| `-O2` | 稳健的性能优化 | 兼顾速度、编译时间和数值可控性 |
| `-g` | 写入 DWARF 调试符号 | 便于 LLDB 定位源码和调用栈 |
| `-std=gnu` | 接受 GNU Fortran 方言 | 兼容 Bellhop 中的旧代码和 GNU 扩展 |
| `-ffree-line-length-none` | 取消自由格式行长限制 | 避免长公式和调用语句被截断 |
| `-Jbuild/mod` | 指定 `.mod` 输出目录 | 避免模块文件散落在源码目录 |
| `-Ibuild/mod` | 指定模块搜索目录 | 让后续源文件找到已编译模块 |

`-g` 与 `-O2` 可以同时使用。程序仍进行优化，但调试时部分变量可能被优化掉、执行顺序也可能与源码略有差异。日常运行没有影响。

### 7.2 为什么不默认使用 `-O3`

`-O3` 会启用更积极的循环和向量化优化，但不保证 Bellhop 一定更快。应在建立数值基线后再通过计时决定。

### 7.3 为什么不使用 `-ffast-math`

`-ffast-math` 允许编译器放宽 IEEE 浮点规则，可能改变：

- NaN 和无穷处理；
- 运算重排；
- 舍入行为；
- 极小数处理。

科研基线构建优先保持数值语义，因此不默认启用。

### 7.4 为什么不使用 `-mcpu=native`

`-mcpu=native` 会针对当前 Apple Silicon 型号生成指令，可能降低程序在其他 Mac 上的可移植性。只有确认程序始终在同一台机器运行并完成数值对比后才考虑使用。

### 7.5 常见编译命令参数

```text
-c source.f90    只编译，不链接
-o output.o      指定输出文件
```

构建过程：

```text
.f90 源码
   ↓ gfortran -c
.o 目标文件 + .mod 模块文件
   ↓ gfortran 链接
bin/bellhop
```

---

## 8. macOS 动态链接

### 8.1 当前链接方式

macOS 使用动态链接，不构建完全静态程序。当前链接参数为空：

```makefile
LDFLAGS =
```

Homebrew gfortran 会动态链接其运行库，例如：

```text
libgfortran.dylib
libquadmath.dylib
libgcc_s.dylib
```

### 8.2 检查依赖

```bash
make -C Bellhop_origin runtime-deps
```

该目标在 macOS 上执行：

```bash
otool -L Bellhop_origin/bin/bellhop
```

### 8.3 在本机运行

只要当前 Homebrew GCC/gfortran 安装保持有效，`bin/bellhop` 可以直接运行。

### 8.4 分发到其他 Mac

不能假设另一台 Mac 已安装相同路径和版本的 Homebrew GCC。分发前可选择：

1. 要求目标机器安装 Homebrew GCC；
2. 在目标机器重新执行 `make`；
3. 将依赖的 `.dylib` 一起打包，并使用 `install_name_tool` 修改加载路径；
4. 进一步制作签名和公证后的 macOS 应用包。

当前科研阶段推荐前两种：简单、透明、容易复现。若以后需要向非开发用户发布，再单独设计 dylib 打包、代码签名和公证流程。

> macOS 上“程序可以在本机运行”与“可复制到任意 Mac 单文件运行”是两个不同目标。

---

## 9. Apple Silicon 注意事项

### 9.1 确认程序架构

```bash
file Bellhop_origin/bin/bellhop
```

正常应包含：

```text
arm64
```

### 9.2 避免混用 Intel 与 ARM Homebrew

Apple Silicon 原生 Homebrew：

```text
/opt/homebrew
```

Rosetta/Intel Homebrew 常见路径：

```text
/usr/local
```

如果编译器和动态库分别来自两套架构，可能出现：

```text
bad CPU type in executable
mach-o file, but is an incompatible architecture
```

检查：

```bash
file /opt/homebrew/bin/gfortran
file Bellhop_origin/bin/bellhop
```

二者都应为 `arm64` 或通用二进制中包含 `arm64`。

### 9.3 Finder 启动 VS Code 的 PATH

从终端启动的 VS Code 通常继承 Shell PATH；从 Finder/Dock 启动时不一定如此。因此 `.vscode/tasks.json` 显式添加：

```text
/opt/homebrew/bin
```

---

## 10. 常见问题

### `gfortran: command not found`

检查：

```bash
which gfortran
brew list gcc
```

若未安装：

```bash
brew install gcc
```

### VS Code 可以编辑，但提示找不到模块

检查：

- `fortran.linter.compilerPath` 是否为 `/opt/homebrew/bin/gfortran`；
- `Bellhop_origin/build/mod` 是否在 include paths 中；
- 是否至少成功执行过一次 `make`。

### F5 找不到 `.env`

检查：

```json
"cwd": "${workspaceFolder}/test/standard_cases/results/origin/munk_cerveny_cc/single/f000_50Hz"
```

以及：

```json
"args": ["munk_cerveny_cc_f000_50Hz"]
```

对应文件应为：

```text
test/standard_cases/results/origin/munk_cerveny_cc/single/f000_50Hz/munk_cerveny_cc_f000_50Hz.env
```

### 修改 Makefile 参数后程序似乎没有变化

执行完整重建：

```bash
make -C Bellhop_origin clean
make -C Bellhop_origin
```

### 其他 Mac 提示找不到 `libgfortran.dylib`

运行：

```bash
make -C Bellhop_origin runtime-deps
```

确认依赖路径。科研协作阶段最简单的解决办法是在目标 Mac 安装 Homebrew GCC 后重新编译。

### 编译出现 Warning

Warning 不等于构建失败。判断是否成功应看：

- Make 返回码是否为 0；
- 是否出现 `Error` 或 `make: ***`；
- `Bellhop_origin/bin/bellhop` 是否生成；
- 基准算例是否产生合理结果。

### 旧 GCC 14 环境出现 `overriding deployment version from '16.0' to '26.0'`

以下是升级到 GCC 15.2 前的历史排错记录，不代表当前工具链仍有该问题。使用
旧 Homebrew GCC 14 复现历史基线时，它可能按较早的 macOS SDK 构建，而
Command Line Tools 提供 macOS 26 SDK；汇编阶段可能输出：

```text
clang: warning: overriding deployment version from '16.0' to '26.0'
```

这次实测中该警告没有阻止编译，生成文件仍是有效的 arm64 Mach-O 程序。它属于编译器工具链与新版 SDK 的版本提示，不是 Bellhop 源码错误。

建议：

1. 不要为了隐藏提示随意添加放宽数值语义的编译参数；
2. 保持 Xcode Command Line Tools 与 Homebrew GCC 为兼容版本；
3. 后续通过 `brew update`、`brew upgrade gcc` 更新工具链后重新完整编译；
4. 更新前先记录基准结果，更新后做数值回归比较。

---

## 11. 科研计算建议

1. 保留原始 Bellhop 算例和输出作为数值基线。
2. 修改算法后先运行小算例，再运行多频或大规模算例。
3. 浮点结果采用绝对/相对容差比较，不要求 `.shd` 逐字节一致。
4. 记录 gfortran 版本、Makefile 参数、Git 提交和可执行文件哈希。
5. 不在缺少基线时启用 `-ffast-math` 或 CPU 专用优化。
6. 跨机器对比时记录 CPU 架构和动态库版本。

记录版本：

```bash
uname -m
gfortran --version
make --version
otool -L Bellhop_origin/bin/bellhop
```

记录哈希：

```bash
shasum -a 256 Bellhop_origin/bin/bellhop
```

---

## 12. 最终结论

- 当前主编译器是 `/opt/homebrew/bin/gfortran`。
- 日常构建命令是 `make -C Bellhop_origin`。
- VS Code 使用系统 `make`，并显式把 `/opt/homebrew/bin` 加入 PATH。
- 调试器使用 CodeLLDB。
- 输出程序为 `Bellhop_origin/bin/bellhop`。
- 默认参数为 `-O2 -g -std=gnu -ffree-line-length-none`。
- macOS 不使用完整 `-static`；通过 `otool -L` 检查 Homebrew 动态库依赖。
- 科研阶段优先保证数值基线、可复现性和构建透明性，而不是追求复杂的多配置工程体系。
