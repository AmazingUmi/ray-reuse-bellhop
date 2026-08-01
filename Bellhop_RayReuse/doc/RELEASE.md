# Bellhop RayReuse 内部发布构建

## 当前定位

项目版本为 `0.1.0`。当前发布出口只安装命令行程序和 README，不导出静态
core、公共头文件或 CMake package config，也不承诺 C++ ABI。

仓库尚未提供 LICENSE、COPYING 或 NOTICE，Bellhop/Acoustic Toolbox 派生
内容的许可与归属也尚未冻结。因此生成的 TGZ 只能作为内部验证构建产物，
不能作为对外发行包。

本机 AppleClang 构建没有固定 `CMAKE_OSX_DEPLOYMENT_TARGET`，产物只保证在
记录的构建平台上验证；不能据此宣称支持旧版 macOS、Intel Mac 或 Linux。

## 构建与验证

从仓库根目录运行：

```bash
RAYREUSE_BUILD_JOBS=4 Bellhop_RayReuse/scripts/engineering_gate.sh
```

该入口依次执行：

1. 全量 C++ clang-format 检查；
2. 由 CMake compilation database 驱动的 Clang static analyzer；
3. Release、install 和 CPack TGZ 构建；
4. 安装目录内 `bellhop_rayreuse --version` 烟测；
5. TGZ SHA-256 输出。

本地默认通过 Conda 环境 `py` 运行 Python 驱动；CI 使用
`RAYREUSE_PYTHON_MODE=system`。

## 已验证的内部产物

2026-08-01 在 Apple M4 / Darwin arm64 上以源提交 `b04ccbc` 运行上述工程门
并通过，生成：

- `bellhop-rayreuse-0.1.0-Darwin-arm64.tar.gz`
- SHA-256：`18da3737b67e6919fbb9dc8eebd97e1764d6a01f3fa16a83a374f8e16f24a70b`

该哈希只标识上述本机内部验证产物，不代表签名、公证或跨平台发行认证。

## 对外发布前置条件

- 冻结 LICENSE、版权归属和 NOTICE；
- 决定最低 macOS 版本及是否支持 Intel/Linux；
- 在目标平台 CI 上构建并验证包；
- 建立 release notes/tag 规则；
- 配置仓库远端、首次运行 GitHub Actions，并将两套工程门设为主分支必需
  检查。
