#pragma once

#include <grpcpp/grpcpp.h>
#include "raft.grpc.pb.h"
#include "raft/raft_node.h"
#include "common/logger.h"

namespace raftkv {

class RaftRpcServiceImpl final : public raftpb::RaftService::Service {
public:
    explicit RaftRpcServiceImpl(RaftNode& node) : node_(node) {}

    grpc::Status RequestVote(grpc::ServerContext*, const raftpb::RequestVoteRequest* req, raftpb::RequestVoteResponse* resp) override {
        node_.HandleRequestVote(*req, resp);
        return grpc::Status::OK;
    }

    grpc::Status AppendEntries(grpc::ServerContext*, const raftpb::AppendEntriesRequest* req, raftpb::AppendEntriesResponse* resp) override {
        node_.HandleAppendEntries(*req, resp);
        return grpc::Status::OK;
    }

    grpc::Status Propose(grpc::ServerContext*, const raftpb::ClientRequest* req, raftpb::ClientResponse* resp) override {
        // 序列化整个 ClientRequest (含 VectorData)
        std::string serialized_req;
        if (!req->SerializeToString(&serialized_req)) {
            return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to serialize request");
        }
        
        auto result = node_.Propose(serialized_req);
        
        if (std::get<2>(result)) resp->set_success(true);
        else resp->set_success(false);
        return grpc::Status::OK;
    }

    // 【新增】Search 接口
    grpc::Status Search(grpc::ServerContext*, const raftpb::SearchRequest* req, raftpb::SearchResponse* resp) override {
        std::vector<float> query;
        for (float f : req->vector()) query.push_back(f);
        
        auto results = node_.Search(query, req->top_k());
        
        for (const auto& res : results) {
            auto* item = resp->add_results();
            item->set_id(res.first);
            item->set_score(res.second);
        }
        return grpc::Status::OK;
    }

private:
    RaftNode& node_;
};

} // namespace raftkv