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
set(PROTO_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/protos) # 注意：通常建议文件夹叫 protos，如果你的叫 proto 请改回 src/proto
set(PROTO_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${PROTO_GEN_DIR})

# 确保路径正确，你的文件结构里是 src/protos/raft.proto 还是 src/proto/raft.proto?
# 根据之前的报错，建议检查这里。假设是 src/protos/raft.proto
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
# 3. 生成库
# ==========================================
add_library(raft_proto_lib
    ${PROTO_GEN_DIR}/raft.pb.cc
    ${PROTO_GEN_DIR}/raft.pb.h
    ${PROTO_GEN_DIR}/raft.grpc.pb.cc
    ${PROTO_GEN_DIR}/raft.grpc.pb.h
)

target_include_directories(raft_proto_lib PUBLIC ${PROTO_GEN_DIR})

# 链接原始 Target 名称
target_link_libraries(raft_proto_lib PUBLIC libprotobuf grpc++)
target_compile_options(raft_proto_lib PRIVATE -w) 

# ==========================================
# 4. 业务逻辑与可执行文件
# ==========================================

add_library(common_lib INTERFACE)
target_include_directories(common_lib INTERFACE src)

add_library(raft_core_lib
    src/raft/raft_node.cpp
    src/network/rpc_client.cpp
    # 如果 raft_server.cpp 也在 src/network 下，记得加进来
    # src/network/raft_server.cpp 
)

target_link_libraries(raft_core_lib
    PUBLIC
    raft_proto_lib
    common_lib
)
target_include_directories(raft_core_lib PUBLIC src)

# --- Server Executable ---
add_executable(raft-kv src/cmd/main.cpp)
target_link_libraries(raft-kv
    PRIVATE
    raft_core_lib 
    raft_proto_lib
    common_lib
)

# --- Client Executable (关键修复部分) ---
add_executable(raft-client src/cmd/client.cpp)

# 必须链接 raft_proto_lib 才能找到 raft.pb.h
# 必须链接 grpc++ 才能找到 grpcpp/grpcpp.h
target_link_libraries(raft-client
    PRIVATE
    raft_proto_lib
    grpc++
    libprotobuf
    common_lib
)