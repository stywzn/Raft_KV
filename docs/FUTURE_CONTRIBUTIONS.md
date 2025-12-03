# 🚀 Open Source Contribution Plan (开源突击计划)

> "Talk is cheap. Show me the code."

## Target: gRPC (Google Remote Procedure Call)

### 1. Fix C++20 Attribute Warnings in GCC 13+
* **Status:** Pending
* **Discovery Date:** 2025-XX-XX
* **Context:** compiling gRPC v1.54.2 with GCC 13.3.0 in C++20 mode.

#### The Issue
GCC 13 reports `warning: attribute ignored` for `[[no_unique_address]]` (macro `GPR_NO_UNIQUE_ADDRESS`) when used in a `union`.
Standard C++ requires attributes to be placed *after* the `union` keyword, but legacy code often places them before.

#### Reproduction (Evidence)
File: `src/core/lib/promise/loop.h` (in gRPC source)
Error log:
```text
warning: attribute ignored in declaration of union grpc_core::promise_detail::Loop<F>::<unnamed> [-Wattributes]
note: attribute for ... must follow the ‘union’ keyword



太棒了！看到这几行带时间戳的日志，标志着你的 C++ Raft KV 项目正式跑通了“最小原型”！

你现在拥有一套工业级的构建系统（CMake + FetchContent）、一个线程安全的日志库、以及谷歌亲生的 RPC 序列化协议。很多学生折腾一周都卡在环境上，而你已经拿到了入场券。

关于记录 PR 想法，为了让你每次打开项目都能看到，从而产生动力，我们不用普通的文本文件。我们用工程师的方式来管理这种“技术野心”。

1. 创建“开源贡献计划书”
请在项目根目录（或者 docs/ 下）创建一个专门的文件，名字叫 FUTURE_CONTRIBUTIONS.md（用大写，醒目）。

在这个文件里，我们不仅记录“要修什么”，还要记录“复现路径”和“技术分析”，这样你以后回来修的时候，不用重新回忆。

Action: 创建 FUTURE_CONTRIBUTIONS.md 并写入以下内容：

Markdown

# 🚀 Open Source Contribution Plan (开源突击计划)

> "Talk is cheap. Show me the code."

## Target: gRPC (Google Remote Procedure Call)

### 1. Fix C++20 Attribute Warnings in GCC 13+
* **Status:** Pending
* **Discovery Date:** 2025-XX-XX
* **Context:** compiling gRPC v1.54.2 with GCC 13.3.0 in C++20 mode.

#### The Issue
GCC 13 reports `warning: attribute ignored` for `[[no_unique_address]]` (macro `GPR_NO_UNIQUE_ADDRESS`) when used in a `union`.
Standard C++ requires attributes to be placed *after* the `union` keyword, but legacy code often places them before.

#### Reproduction (Evidence)
File: `src/core/lib/promise/loop.h` (in gRPC source)
Error log:
```text
warning: attribute ignored in declaration of union grpc_core::promise_detail::Loop<F>::<unnamed> [-Wattributes]
note: attribute for ... must follow the ‘union’ keyword
Plan of Action (Patch Strategy)
Fork grpc/grpc repo.

Locate src/core/lib/promise/loop.h.

Move GPR_NO_UNIQUE_ADDRESS from before union to after union.

Before: GPR_NO_UNIQUE_ADDRESS union { ... }

After: union GPR_NO_UNIQUE_ADDRESS { ... } (Need to verify macro expansion)

Verify compilation with -Werror on GCC 13.

Submit Pull Request titled: "Fix GCC 13 attribute warnings in promise/loop.h".