# VS Code 与 Bellhop/gfortran 编译学习笔记

> 适用环境：Windows、VS Code、MinGW-w64、GNU Fortran、GNU Make、Bellhop 2-D。
>
> 核心目标：用尽量简单的方式完成代码编辑、编译、运行和基础调试，并生成不依赖 MinGW 运行库 DLL 的 `bellhop.exe`。

---

## 1. 最终工作流

日常使用只需要记住两件事：

- 在 VS Code 中按 `Ctrl + Shift + B`：编译 Bellhop。
- 按 `F5`：先编译，再用默认算例运行 Bellhop。

也可以在项目根目录使用命令行：

```powershell
# 增量编译：只重新编译发生变化的源码
mingw32-make

# 清理所有目标文件、模块文件和可执行文件
mingw32-make clean

# 完整重新编译
mingw32-make clean
mingw32-make
```

最终输出：

```text
bin/bellhop.exe
```

构建中间文件统一放在：

```text
build/obj    # .o 目标文件
build/mod    # .mod Fortran 模块文件
```

> 一般不需要每次都执行 `clean`。只有修改了编译参数、链接参数，或者怀疑旧的 `.o/.mod` 文件造成异常时，才进行完整重建。

---

## 2. 为什么 Windows 使用 `mingw32-make`

`make` 和 `mingw32-make` 本质上都是 GNU Make。

| 环境 | 常见命令名 |
|---|---|
| Linux/macOS | `make` |
| MSYS2/Cygwin | `make` |
| 独立 MinGW-w64 工具链 | `mingw32-make` |

本机 MinGW 工具链提供的是：

```text
C:\Users\beatr\Documents\mingw64\bin\mingw32-make.exe
```

因此原版 Acoustic Toolbox 文档中的：

```bash
make
make clean
make -j4
```

在当前 Windows 环境中分别写成：

```powershell
mingw32-make
mingw32-make clean
mingw32-make -j4
```

`-j4` 表示最多同时执行 4 个编译任务，只影响速度，不改变计算结果。Fortran 模块之间存在先后依赖；当前 Makefile 已显式描述依赖关系，因此可以并行，但项目规模不大，日常直接使用 `mingw32-make` 更简单。

---

## 3. 推荐的 VS Code 插件

### 必要插件

1. **Modern Fortran**（`fortran-lang.linter-gfortran`）
   - Fortran 语法高亮；
   - 代码补全和符号跳转；
   - 调用 `gfortran` 进行静态检查；
   - 可配合 `fortls` 提供语言服务。

2. **C/C++**（`ms-vscode.cpptools`）
   - 这里主要用于调用 GDB；
   - 可以为 Fortran 程序提供断点、单步执行、变量查看和调用栈。

3. **Markdown All in One**（`yzhang.markdown-all-in-one`）
   - 便于维护项目学习笔记和技术文档。

### 暂时不需要的插件

- Code Runner：容易绕过 Makefile、模块依赖和正确的运行目录。
- 多个 Fortran 插件：可能产生重复诊断或语言服务冲突。
- CMake Tools：当前项目使用 Makefile，不必增加额外构建系统。
- Makefile Tools：当前只需要一个固定构建任务，VS Code 内置 Task 已足够。

> 原则：插件不是越多越好。科研代码环境最重要的是构建可重复、工作目录正确、输入文件明确。

---

## 4. VS Code 项目配置

项目配置保存在 `.vscode` 目录中，可以随 Git 仓库一起保存。

### 4.1 `settings.json`：Fortran 编辑设置

关键配置：

```json
{
  "fortran.linter.compiler": "gfortran",
  "fortran.linter.compilerPath": "C:\\Users\\beatr\\Documents\\mingw64\\bin\\gfortran.exe",
  "fortran.linter.includePaths": [
    "${workspaceFolder}/misc",
    "${workspaceFolder}/Bellhop"
  ],
  "fortran.linter.extraArgs": [
    "-std=gnu",
    "-Wno-tabs"
  ],
  "[fortran]": {
    "editor.formatOnSave": false,
    "editor.tabSize": 3,
    "editor.insertSpaces": true
  }
}
```

设计考虑：

- 显式指定 `gfortran.exe`，避免 VS Code 找错编译器。
- 将 `misc` 和 `Bellhop` 加入模块搜索路径。
- 不启用保存时自动格式化，避免旧 Fortran 文件产生大面积无意义改动。
- 在资源管理器和搜索中隐藏 `.o`、`.mod`、`.exe`、`.shd` 等生成文件。

### 4.2 `tasks.json`：一键编译

默认构建任务的本质是：

```powershell
mingw32-make bellhop
```

关键配置：

```json
{
  "label": "Build Bellhop",
  "type": "process",
  "command": "C:\\Users\\beatr\\Documents\\mingw64\\bin\\mingw32-make.exe",
  "args": ["bellhop"],
  "options": {
    "cwd": "${workspaceFolder}",
    "env": {
      "PATH": "C:\\Users\\beatr\\Documents\\mingw64\\bin;${env:PATH}"
    }
  },
  "problemMatcher": "$gcc"
}
```

这里使用 `process` 而不是拼接一长串 PowerShell 命令，使路径和参数更稳定。

### 4.3 `launch.json`：运行和调试

默认运行配置：

```json
{
  "name": "Run Bellhop example",
  "type": "cppdbg",
  "request": "launch",
  "program": "${workspaceFolder}\\bin\\bellhop.exe",
  "args": ["MunkB_Coh_CervenyC"],
  "cwd": "${workspaceFolder}\\test_origin_bellhop",
  "MIMode": "gdb",
  "miDebuggerPath": "C:\\Users\\beatr\\Documents\\mingw64\\bin\\gdb.exe",
  "preLaunchTask": "Build Bellhop"
}
```

需要特别区分：

- `program`：要运行的可执行文件。
- `args`：传给 Bellhop 的输入文件根名，不带 `.env` 后缀。
- `cwd`：程序运行目录；Bellhop 会在这里寻找 `.env`，并在这里产生 `.prt/.shd` 等结果。
- `preLaunchTask`：按 `F5` 前自动完成编译。

> Bellhop 的运行目录非常重要。即使 EXE 路径正确，如果 `cwd` 不包含对应的 `.env` 文件，程序仍然无法正确运行。

---

## 5. 当前 Makefile 的结构

当前 Makefile 只构建原始 2-D Bellhop 所需的源码，不再尝试构建缺失的 Kraken、Scooter、tslib 等完整 Acoustic Toolbox 组件。

主要变量：

```makefile
FC = gfortran

BUILD_DIR = build
OBJ_DIR   = $(BUILD_DIR)/obj
MOD_DIR   = $(BUILD_DIR)/mod
BIN_DIR   = bin
TARGET    = $(BIN_DIR)/bellhop.exe
```

默认目标：

```makefile
all: bellhop

bellhop: $(TARGET)
```

因此下面两条命令在当前项目中效果基本相同：

```powershell
mingw32-make
mingw32-make bellhop
```

Makefile 显式列出 Fortran 模块依赖顺序，避免在模块对应的 `.mod` 文件尚未生成时提前编译下游源码。

---

## 6. 编译参数说明

当前编译参数：

```makefile
FFLAGS = -O2 -std=gnu -ffree-line-length-none -Wall -Wextra -Wno-tabs
MODFLAGS = -J$(MOD_DIR) -I$(MOD_DIR)
LDFLAGS = -static
```

### 6.1 `FFLAGS`：Fortran 源码编译参数

| 参数 | 含义 | 当前选择理由 |
|---|---|---|
| `-O2` | 启用较稳健的性能优化 | 兼顾运行速度、编译时间和数值可控性 |
| `-std=gnu` | 接受 GNU Fortran 方言 | 兼容 Bellhop 中的旧式代码和 GNU 扩展 |
| `-ffree-line-length-none` | 取消自由格式源码的默认行长限制 | Bellhop 中存在较长公式和调用语句 |
| `-Wall` | 启用常见警告 | 帮助发现潜在问题，但不把警告当错误 |
| `-Wextra` | 启用额外警告 | 提高代码检查覆盖面 |
| `-Wno-tabs` | 不报告 Tab 字符警告 | 原始源码中存在 Tab，不影响计算 |

### 6.2 `MODFLAGS`：Fortran 模块参数

| 参数 | 含义 |
|---|---|
| `-Jbuild/mod` | 将编译生成的 `.mod` 文件放入 `build/mod` |
| `-Ibuild/mod` | 编译其他源码时从 `build/mod` 查找模块 |

### 6.3 编译命令中的常见参数

```text
-c source.f90     只编译，不链接
-o output.o       指定输出文件
```

典型编译过程：

```text
Fortran 源码 (.f90)
        ↓  gfortran -c
目标文件 (.o) + 模块文件 (.mod)
        ↓  gfortran 链接
可执行文件 (bellhop.exe)
```

### 6.4 为什么没有使用 `-O3` 和 `-ffast-math`

- `-O3` 会启用更激进的循环和向量化优化，未必总能带来明显收益。
- `-ffast-math` 允许编译器放宽 IEEE 浮点规则，可能改变 NaN、无穷、舍入和运算重排行为。
- 对数值基准尚未完全建立的科研程序，`-O2` 是更稳妥的默认选择。

后续若需要性能优化，应先固定回归算例和误差容限，再比较不同参数下的运行时间和输出误差。

---

## 7. 静态链接与程序分发

默认的 MinGW/gfortran 程序通常依赖：

```text
libgfortran-5.dll
libgcc_s_seh-1.dll
libquadmath-0.dll
libwinpthread-1.dll
```

如果目标电脑没有对应 MinGW 环境，程序会因为找不到 DLL 而无法启动。

当前 Makefile 使用：

```makefile
LDFLAGS = -static
```

最终链接命令类似：

```powershell
gfortran -static -o bin/bellhop.exe <所有目标文件>
```

这不是简单地把 DLL 复制进 EXE，而是将 GNU/MinGW 运行库的静态版本链接进程序。

验证后的 `bellhop.exe`：

- 文件大小约 28 MB；
- 不再依赖 `libgfortran`、`libgcc`、`libquadmath` 或 `libwinpthread` DLL；
- 仍依赖 `KERNEL32.dll` 和 `api-ms-win-crt-*`。

后两类属于现代 Windows 的系统组件，正常情况下无需随程序分发。

检查 EXE 依赖的方法：

```powershell
& "C:\Users\beatr\Documents\mingw64\bin\objdump.exe" `
  -p .\bin\bellhop.exe |
  Select-String "DLL Name"
```

> “完全静态”在 Windows 上通常是指第三方编译器运行库被静态链接；Windows 自身的系统 DLL 仍然动态加载，这是正常设计。

---

## 8. Windows 下载标记与应用控制问题

### 8.1 最初的现象

虽然以下命令能够显示版本：

```powershell
gfortran --version
```

但真正编译 Fortran 时失败：

```text
cannot execute .../f951.exe
An Application Control policy has blocked this file
```

原因是：

- `gfortran.exe` 是编译驱动程序；
- 真正编译 Fortran 源码的是后端 `f951.exe`；
- 只验证 `gfortran --version` 不足以证明完整编译链可用。

### 8.2 `Zone.Identifier`

从互联网下载的压缩包可能带有 `Zone.Identifier=3`。解压工具可能把这一标记传播给压缩包内的每一个文件。

检查方法：

```powershell
Get-Item "完整文件路径" -Stream *
```

确认下载来源和哈希可信后，可以解除：

```powershell
$root = "C:\Users\beatr\Documents\mingw64"

Get-ChildItem -LiteralPath $root -Recurse -File |
    Unblock-File
```

更稳妥的做法是在解压前：

1. 右键原始压缩包；
2. 打开“属性”；
3. 勾选“解除锁定”；
4. 再解压到新目录。

### 8.3 Smart App Control / WDAC

即使下载标记已经清除，未签名的 `f951.exe` 仍可能被 Smart App Control 或 WDAC 阻止。

相关事件位置：

```text
事件查看器
→ 应用程序和服务日志
→ Microsoft
→ Windows
→ CodeIntegrity
→ Operational
```

事件 ID `3077` 表示 App Control 在强制模式下阻止了文件。

需要注意：

- 把 MinGW 移到 `Program Files` 不会自动解决签名/信誉问题；
- Smart App Control 通常不能只为某一个 EXE 添加例外；
- 企业管理电脑应联系管理员配置允许策略；
- 不应在未确认下载来源和哈希的情况下直接解除整棵目录的锁定。

最终应使用一次真实编译验证工具链：

```powershell
mingw32-make clean
mingw32-make
```

而不是只依赖：

```powershell
gfortran --version
```

---

## 9. 如何理解当前编译警告

当前源码可以成功编译，但 `-Wall -Wextra` 会报告一些警告，主要包括：

1. 浮点数直接使用 `==` 或 `/=` 比较；
2. 编译器认为某些数组下标可能为 0；
3. 某些变量可能未初始化；
4. 未使用的变量或参数；
5. 较大局部数组被移到静态存储区；
6. 字符串实参与形参长度不同。

这些警告不等于程序一定有错误，也不会阻止生成 EXE，但不能全部简单忽略。

建议按以下顺序处理：

1. 先建立已知算例的输出基线；
2. 优先检查“可能未初始化”和“可能越界”；
3. 修改后比较 `.prt/.shd` 输出；
4. 对浮点结果使用容差比较，而不是要求逐字节完全相同；
5. 最后再清理未使用变量、Tab 和代码风格类警告。

> 科研代码的优先级应当是：结果可复现与数值正确性 > 警告数量为零 > 代码形式整齐。

---

## 10. 常见问题速查

### VS Code 能打开代码，但提示找不到模块

检查：

- `fortran.linter.compilerPath` 是否正确；
- `misc` 和 `Bellhop` 是否在 `fortran.linter.includePaths` 中；
- 是否至少成功编译过一次，从而生成 `build/mod/*.mod`。

### 按 `F5` 后找不到 `.env`

检查 `launch.json` 的：

```json
"cwd": "${workspaceFolder}\\test_origin_bellhop"
```

以及：

```json
"args": ["MunkB_Coh_CervenyC"]
```

文件应为：

```text
test_origin_bellhop/MunkB_Coh_CervenyC.env
```

### 修改源码后需要先清理吗

通常不需要：

```powershell
mingw32-make
```

Make 会根据修改时间只重新编译必要文件。

### 修改了编译参数，但程序好像没有变化

Make 默认不会自动识别变量字符串发生变化。执行：

```powershell
mingw32-make clean
mingw32-make
```

### EXE 在其他电脑提示缺少 `libgfortran-5.dll`

确认链接命令中包含：

```text
-static
```

然后完整重新编译，并用 `objdump -p` 检查依赖。

### 编译输出很多 Warning，是否代表失败

不是。判断构建是否成功应看：

- 命令返回码是否为 0；
- 是否出现 `Error` 或 `make: ***`；
- `bin/bellhop.exe` 是否生成；
- 基准算例是否能够运行并产生合理结果。

---

## 11. 推荐的科研使用习惯

1. 保留一个未经修改的原版 Bellhop 算例和输出作为基线。
2. 每次修改算法后，先运行小算例，再运行大规模多频算例。
3. 对声压、传播损失等浮点结果采用明确的绝对/相对误差容限。
4. 将源码、Makefile、`.vscode` 配置和小型输入文件纳入 Git。
5. 不把 `.o`、`.mod`、大型 `.shd` 和临时输出当作源码管理。
6. 不在没有数值基线的情况下启用 `-ffast-math` 等激进优化。
7. 发布 EXE 时记录编译器版本、Makefile 参数、Git 提交和 SHA256。

记录当前编译器版本：

```powershell
gfortran --version
mingw32-make --version
gdb --version
```

记录可执行文件哈希：

```powershell
Get-FileHash .\bin\bellhop.exe -Algorithm SHA256
```

---

## 12. 最终结论

- VS Code 只负责编辑、任务启动和调试；真正的构建规则由 Makefile 决定。
- Windows 下的 `mingw32-make` 与原版文档中的 `make` 是同一类工具。
- 当前项目只需一套默认构建，不必为日常科研工作区分 Debug/Release。
- `-O2` 是当前兼顾性能和数值稳健性的默认优化级别。
- `-static` 已将 GNU/MinGW 运行库静态链接进 `bellhop.exe`，程序不再依赖 MinGW DLL。
- Windows 系统 DLL 仍然保留，这是正常且必要的。
- 工具链是否可用应通过完整编译验证，不能只看 `gfortran --version`。
- 编译警告应结合数值回归逐步处理，而不是为了“零警告”盲目修改科研代码。
