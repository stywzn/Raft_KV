cmake_minimum_required(VERSION 3.15)
project(raft-kv CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

message(STATUS "配置 gRPC 依赖 (使用 FetchContent)...")

# 禁用测试，加快编译
set(gRPC_BUILD_TESTS OFF CACHE BOOL "Opt out of building gRPC tests")
set(protobuf_BUILD_TESTS OFF CACHE BOOL "Opt out of building protobuf tests")

# 定义下载源
FetchContent_Declare(
    gRPC
    GIT_REPOSITORY https://github.com/grpc/grpc
    GIT_TAG v1.54.2
    GIT_SHALLOW TRUE
)

# 下载并加载
FetchContent_MakeAvailable(gRPC)

# ==========================================
# 2. 处理 Proto 文件
# ==========================================
set(PROTO_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/proto)
set(PROTO_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${PROTO_GEN_DIR})

set(PROTO_FILES ${PROTO_SRC_DIR}/raft.proto)

# [关键修复 1] 使用原始 Target 名称 (不带 gRPC:: 前缀)
# 当使用 FetchContent 源码编译时，gRPC 定义的 Target 叫 grpc_cpp_plugin
set(_PROTOBUF_PROTOC $<TARGET_FILE:protoc>)
set(_GRPC_CPP_PLUGIN_EXECUTABLE $<TARGET_FILE:grpc_cpp_plugin>)

add_custom_command(
    OUTPUT "${PROTO_GEN_DIR}/raft.pb.cc" "${PROTO_GEN_DIR}/raft.pb.h" "${PROTO_GEN_DIR}/raft.grpc.pb.cc" "${PROTO_GEN_DIR}/raft.grpc.pb.h"
    COMMAND ${_PROTOBUF_PROTOC}
    ARGS --proto_path=${PROTO_SRC_DIR}
    --cpp_out=${PROTO_GEN_DIR}
    --grpc_out=${PROTO_GEN_DIR}
    --plugin=protoc-gen-grpc=${_GRPC_CPP_PLUGIN_EXECUTABLE}
    ${PROTO_FILES}
    # [关键修复 2] 依赖原始 Target 名称
    DEPENDS ${PROTO_FILES} grpc_cpp_plugin protoc
    COMMENT "Running C++ protocol buffer compiler on raft.proto"
)

# ==========================================
# 3. 生成库
# ==========================================
add_library(raft_proto_lib
    ${PROTO_GEN_DIR}/raft.pb.cc
    ${PROTO_GEN_DIR}/raft.pb.h
    ${PROTO_GEN_DIR}/raft.grpc.pb.cc
    ${PROTO_GEN_DIR}/raft.grpc.pb.h
)

target_include_directories(raft_proto_lib PUBLIC ${PROTO_GEN_DIR})

# [关键修复 3] 链接原始 Target 名称
# 源码编译时，它们通常叫 libprotobuf 和 grpc++
target_link_libraries(raft_proto_lib PUBLIC libprotobuf grpc++)

# 针对我们自己生成的库，也不要开太严格的警告，因为 Protobuf 生成的代码里全是 Warning
target_compile_options(raft_proto_lib PRIVATE -w) # -w 意思是忽略所有警告



# ==========================================
# 4. 业务逻辑与可执行文件
# ==========================================

# 定义一个 library 包含我们的通用代码 (Logger)
# 暂时只有一个 logger.h，它是 header-only 的，所以其实不需要编译 .cpp
# 但为了后续扩展，我们先定义个 interface 库或者普通库
add_library(common_lib INTERFACE)
target_include_directories(common_lib INTERFACE src)

add_library(raft_core_lib
    src/raft/raft_node.cpp
    src/network/rpc_client.cpp
)
# Raft Core needs to know about Proto definitions and Logger
target_link_libraries(raft_core_lib
    PUBLIC
    raft_proto_lib
    common_lib
)
# Allow other targets to include raft headers easily
target_include_directories(raft_core_lib PUBLIC src)


# [3] Define Executable
add_executable(raft-kv src/cmd/main.cpp)

# [4] Link Everything Together
target_link_libraries(raft-kv
    PRIVATE
    raft_core_lib # <--- Link the logic we just defined
    raft_proto_lib
    common_lib
)
