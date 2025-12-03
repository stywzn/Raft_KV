#pragma once

#include <grpcpp/grpcpp.h>
#include "raft.grpc.pb.h"
#include "common/logger.h"

namespace raftkv {

class RaftRpcClient {
public:
    // 连接到指定的 address (例如 "127.0.0.1:5002")
    RaftRpcClient(const std::string& address);

    // 发送 RequestVote
    // 返回值: bool (是否调用成功), 响应内容通过 output 参数返回
    bool RequestVote(const raftpb::RequestVoteRequest& args, 
                     raftpb::RequestVoteResponse* reply);

    // 发送 AppendEntries
    bool AppendEntries(const raftpb::AppendEntriesRequest& args, 
                       raftpb::AppendEntriesResponse* reply);

private:
    std::unique_ptr<raftpb::RaftService::Stub> stub_;
};

} // namespace raftkv