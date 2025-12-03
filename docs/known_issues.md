# 已知问题与技术债 (Known Issues & Technical Debt)

## 1. 第三方库编译警告 (Upstream Warnings)

* **状态:** Open (Won't Fix)
* **发现日期:** 2025-XX-XX
* **编译器版本:** GCC 13.3.0
* **涉及组件:** gRPC v1.54.2, Protobuf
* **现象:** 编译过程中出现大量关于 `GPR_NO_UNIQUE_ADDRESS`、`warn_unused_result` 以及 formatting `%d` 的警告。
* **原因分析:** GCC 13 对 C++20 标准属性（Attributes）和类型检查非常严格。gRPC v1.54 版本代码在某些属性位置上不符合新编译器的最佳实践。
* **处理决策:** **忽略 (Ignore)**。这些警告属于第三方库内部实现细节，不影响生成的二进制文件的正确性。为了保持依赖管理的简洁性（FetchContent），我们选择不 patch 第三方源码，而是通过在 CMake 中对第三方目标关闭 `-Werror` 来规避构建失败。

---