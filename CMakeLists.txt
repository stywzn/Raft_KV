cmake_minimum_required(VERSION 3.15)
project(raft-kv CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

message(STATUS "配置依赖库 (使用 FetchContent)...")

# 禁用测试，加快编译
set(gRPC_BUILD_TESTS OFF CACHE BOOL "Opt out of building gRPC tests")
set(protobuf_BUILD_TESTS OFF CACHE BOOL "Opt out of building protobuf tests")

# ----------------------------------------------------------
# 1. 下载依赖库
# ----------------------------------------------------------

# (1) gRPC
FetchContent_Declare(
    gRPC
    GIT_REPOSITORY https://github.com/grpc/grpc
    GIT_TAG v1.54.2
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(gRPC)

# (2) hnswlib (向量检索核心库) 【新增】
FetchContent_Declare(
    hnswlib
    GIT_REPOSITORY https://github.com/nmslib/hnswlib.git
    GIT_TAG v0.8.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(hnswlib)

# ==========================================
# 2. 处理 Proto 文件
# ==========================================
set(PROTO_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/protos) 
set(PROTO_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${PROTO_GEN_DIR})

# 自动检测 proto 文件路径
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/protos/raft.proto")
    set(PROTO_FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/protos/raft.proto)
    set(PROTO_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/protos)
else()
    set(PROTO_FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/proto/raft.proto)
    set(PROTO_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/proto)
endif()

# 获取 protoc 和 plugin 的路径
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
    DEPENDS ${PROTO_FILES} grpc_cpp_plugin protoc
    COMMENT "Running C++ protocol buffer compiler on raft.proto"
)

# ==========================================
# 3. 生成 Proto 库
# ==========================================
add_library(raft_proto_lib
    ${PROTO_GEN_DIR}/raft.pb.cc
    ${PROTO_GEN_DIR}/raft.pb.h
    ${PROTO_GEN_DIR}/raft.grpc.pb.cc
    ${PROTO_GEN_DIR}/raft.grpc.pb.h
)

target_include_directories(raft_proto_lib PUBLIC ${PROTO_GEN_DIR})

# 链接 gRPC 和 Protobuf
target_link_libraries(raft_proto_lib PUBLIC libprotobuf grpc++)
# 忽略生成代码的警告
target_compile_options(raft_proto_lib PRIVATE -w) 

# ==========================================
# 4. 业务逻辑库 (Raft Core)
# ==========================================

add_library(common_lib INTERFACE)
target_include_directories(common_lib INTERFACE src)

add_library(raft_core_lib
    src/raft/raft_node.cpp
    src/network/rpc_client.cpp
)

target_link_libraries(raft_core_lib
    PUBLIC
    raft_proto_lib
    common_lib
    hnswlib  # 【新增】将 hnswlib 链接到核心库，以便 RaftNode 使用
)
target_include_directories(raft_core_lib PUBLIC src)

# ==========================================
# 5. 可执行文件
# ==========================================

# --- Server (Raft 节点) ---
add_executable(raft-kv src/cmd/main.cpp)
target_link_libraries(raft-kv
    PRIVATE
    raft_core_lib 
    raft_proto_lib
    common_lib
)

# --- Client (客户端) ---
add_executable(raft-client src/cmd/client.cpp)
target_link_libraries(raft-client
    PRIVATE
    raft_proto_lib
    grpc++
    libprotobuf
    common_lib
)