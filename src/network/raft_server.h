#pragma once

#include <grpcpp/grpcpp.h>
#include "raft.grpc.pb.h"
#include "raft/raft_node.h"
#include "common/logger.h"

namespace raftkv {

class RaftRpcServiceImpl final : public raftpb::RaftService::Service {
public:
    explicit RaftRpcServiceImpl(RaftNode& node) : node_(node) {}

    // 1. Handle RequestVote RPC
    grpc::Status RequestVote(grpc::ServerContext* context, 
                             const raftpb::RequestVoteRequest* request, 
                             raftpb::RequestVoteResponse* response) override {
        node_.HandleRequestVote(*request, response);
        return grpc::Status::OK;
    }

    // 2. Handle AppendEntries RPC (Heartbeat)
    grpc::Status AppendEntries(grpc::ServerContext* context, 
                               const raftpb::AppendEntriesRequest* request, 
                               raftpb::AppendEntriesResponse* response) override {
        node_.HandleAppendEntries(*request, response);
        return grpc::Status::OK;
    }

    // ==================================================
    // 【新增】3. Handle Client Propose RPC
    // ==================================================
    grpc::Status Propose(grpc::ServerContext* context,
                         const raftpb::ClientRequest* request,
                         raftpb::ClientResponse* response) override {
        
        LOG_INFO("RPC Received: Client Propose Data: " << request->data());

        // 调用 RaftNode 的核心逻辑
        auto result = node_.Propose(request->data());
        
        // result 是 tuple<index, term, is_leader>
        bool is_leader = std::get<2>(result);

        if (is_leader) {
            response->set_success(true);
        } else {
            response->set_success(false);
            // 这里以后可以扩展，返回 Leader 的地址给客户端重定向
            response->set_leader_hint("Unknown"); 
        }
        return grpc::Status::OK;
    }

private:
    RaftNode& node_;
};

} // namespace raftkv