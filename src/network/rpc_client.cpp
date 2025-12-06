#include "network/rpc_client.h"

namespace raftkv {

RaftRpcClient::RaftRpcClient(const std::string& address) {
    // 创建一个不加密的通道
    auto channel = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
    // 创建 Stub (存根)，以后就通过 Stub 调用远程函数
    stub_ = raftpb::RaftService::NewStub(channel);
}

// src/network/rpc_client.cpp

bool RaftRpcClient::RequestVote(const raftpb::RequestVoteRequest& args, 
                                raftpb::RequestVoteResponse* reply) {
    grpc::ClientContext context;
    auto deadline = std::chrono::system_clock::now() + std::chrono::milliseconds(3000);
    context.set_deadline(deadline);

    grpc::Status status = stub_->RequestVote(&context, args, reply);

    if (!status.ok()) {
        // 【新增】把这行日志放出来！我们要看看到底为什么失败！
        LOG_WARN("RPC RequestVote failed: " << status.error_message() << " (" << status.error_code() << ")");
        return false;
    }
    return true;
}

bool RaftRpcClient::AppendEntries(const raftpb::AppendEntriesRequest& args, 
                                  raftpb::AppendEntriesResponse* reply) {
    grpc::ClientContext context;
    auto deadline = std::chrono::system_clock::now() + std::chrono::milliseconds(3000);
    context.set_deadline(deadline);

    grpc::Status status = stub_->AppendEntries(&context, args, reply);

    if (!status.ok()) {
        return false;
    }
    return true;
}

} // namespace raftkv