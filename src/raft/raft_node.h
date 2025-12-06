#pragma once

#include <mutex>
#include <vector>
#include <memory>
#include <atomic>
#include <thread>
#include <chrono>
#include <tuple>
#include <string> 
#include <fstream>
    
#include <grpcpp/grpcpp.h> 
#include "raft.pb.h"
#include "common/logger.h"
#include "network/rpc_client.h" 

// 【核心】引入 hnswlib
#include "hnswlib/hnswlib.h"

namespace raftkv {

class RaftRpcServiceImpl;

struct LogEntry {
    uint64_t term;   
    uint64_t index;  
    std::string data; // 存放序列化后的 ClientRequest
};

enum RaftState {
    Follower,
    Candidate,
    Leader
};

class RaftNode {
public:
    RaftNode(int id, const std::vector<std::string>& peers, int port);
    ~RaftNode();

    void Start();
    void Stop();

    // RPC Handlers
    void HandleRequestVote(const raftpb::RequestVoteRequest& req, 
                           raftpb::RequestVoteResponse* resp);
    void HandleAppendEntries(const raftpb::AppendEntriesRequest& req, 
                             raftpb::AppendEntriesResponse* resp);

    // 写接口: 插入向量 (Propose)
    std::tuple<uint64_t, uint64_t, bool> Propose(std::string data);
    
    // 【新增】读接口: 搜索向量 (Search)
    // 返回 pair<ID, Distance>
    std::vector<std::pair<int64_t, float>> Search(const std::vector<float>& query_vec, int top_k);

    bool IsLeader();

private:
    void MainLoop();
    void BecomeCandidate();
    void BecomeLeader();
    void BecomeFollower(uint64_t term); 
    
    void SendRequestVote(int target_id);
    void SendAppendEntries(int target_id);
    
    // 状态机应用: 解析 Protobuf 并写入 HNSW
    void ApplyLogs();

    void Persist();    // 暂时只持久化 Raft 元数据
    void LoadState(); 

    std::chrono::milliseconds GetRandomizedElectionTimeout();
    uint64_t GetLastLogIndex();
    uint64_t GetLastLogTerm();

private:
    int id_; 
    int port_;
    std::vector<std::string> peers_; 
    std::string persistence_file_;

    std::unique_ptr<grpc::Server> rpc_server_;
    std::unique_ptr<RaftRpcServiceImpl> rpc_service_;
    std::vector<std::unique_ptr<RaftRpcClient>> rpc_clients_;

    std::recursive_mutex mutex_;
    std::atomic<bool> running_;
    std::unique_ptr<std::thread> background_thread_;

    // Raft State
    uint64_t current_term_; 
    int voted_for_;         
    std::vector<LogEntry> logs_; 

    RaftState state_;
    uint64_t commit_index_; 
    uint64_t last_applied_; 

    // 【替换】HNSW 向量检索引擎
    // 1. 空间 (L2 欧氏距离)
    std::unique_ptr<hnswlib::L2Space> hnsw_space_;
    // 2. 索引
    std::unique_ptr<hnswlib::HierarchicalNSW<float>> hnsw_index_;
    // 3. 配置参数
    int dim_ = 128;            // 向量维度 (必须固定)
    int max_elements_ = 10000; // 最大容量

    // Leader Volatile
    std::vector<uint64_t> next_index_;  
    std::vector<uint64_t> match_index_; 

    int vote_count_;
    
    std::chrono::steady_clock::time_point last_election_time_;
    std::chrono::milliseconds election_timeout_;
    std::chrono::steady_clock::time_point last_heartbeat_time_;
    std::chrono::milliseconds heartbeat_timeout_;
};

} // namespace raftkv