## [日期]12-2 问题：Ubuntu 系统 gRPC 库缺失导致 CMake 配置失败

### 1. 现象 (Situation)
**错误信息：**
```text
CMake Error at CMakeLists.txt:13 (find_package):
  Could not find a package configuration file provided by "gRPC" ...

  背景： 在 Ubuntu 环境下，尝试使用 find_package(gRPC REQUIRED) 直接查找系统安装的库。

2. 原因分析 (Root Cause)
直接原因： Ubuntu 的 apt install 安装的 gRPC 二进制包通常不包含 CMake Config 文件（gRPCConfig.cmake），导致 CMake 无法定位。

深层原因 (工程思考)： 依赖本地环境（System-wide installation）会导致“环境漂移”问题。即代码在我电脑上能跑，换台电脑（版本不同）就挂了，不具备可移植性 (Portability)。

3. 解决方案 (Action)
放弃本地依赖，改用 CMake 的 FetchContent 模块，在构建时从源码拉取并编译 gRPC。这样实现了 Hermetic Build（封闭构建）。
代码变更 (Diff)：

CMake

// ❌ Old: 依赖本地环境，很不稳定
// find_package(gRPC REQUIRED)

// ✅ New: 源码编译，版本可控
include(FetchContent)
FetchContent_Declare(
  gRPC
  GIT_REPOSITORY [https://github.com/grpc/grpc](https://github.com/grpc/grpc)
  GIT_TAG        v1.54.2
)
FetchContent_MakeAvailable(gRPC)
4. 收获 (Key Takeaway)
理解了 C++ 包管理的痛点。

掌握了 Modern CMake 的 FetchContent 用法。

面试话术： “在搭建基础架构时，为了保证团队协作的一致性，我特意避开了系统级依赖，设计了一套基于源码分发的构建系统。”