#pragma once

#include <mutex>
#include <vector>
#include <memory>
#include <atomic>
#include <condition_variable>
#include <thread>
#include <chrono>
#include <tuple>
#include <string> 
#include <map> 
#include <fstream> // 必须包含 fstream 进行文件读写
    
#include <grpcpp/grpcpp.h> 
#include "raft.pb.h"
#include "common/logger.h"
#include "network/rpc_client.h" 

namespace raftkv {

class RaftRpcServiceImpl;

// 日志条目结构体
struct LogEntry {
    uint64_t term;   
    uint64_t index;  
    std::string data; 
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

    // Client Interface
    std::tuple<uint64_t, uint64_t, bool> Propose(std::string data);
    
    bool IsLeader();

private:
    void MainLoop();
    void BecomeCandidate();
    void BecomeLeader();
    void BecomeFollower(uint64_t term); 
    
    void SendRequestVote(int target_id);
    void SendAppendEntries(int target_id);
    
    // 核心函数：将已提交的日志应用到 KV 状态机
    void ApplyLogs();

    // 【新增】持久化相关函数
    void Persist();    // 将 current_term, voted_for, logs 写入磁盘
    void LoadState();  // 从磁盘恢复状态

    std::chrono::milliseconds GetRandomizedElectionTimeout();
    uint64_t GetLastLogIndex();
    uint64_t GetLastLogTerm();

private:
    // --- 1. Node Metadata ---
    int id_; 
    int port_;
    std::vector<std::string> peers_; 
    
    // 【新增】持久化文件名 (例如 node_0.storage)
    std::string persistence_file_;

    // --- 2. Network Components ---
    std::unique_ptr<grpc::Server> rpc_server_;
    std::unique_ptr<RaftRpcServiceImpl> rpc_service_;
    std::vector<std::unique_ptr<RaftRpcClient>> rpc_clients_;

    // --- 3. Threading ---
    std::recursive_mutex mutex_;
    std::atomic<bool> running_;
    std::unique_ptr<std::thread> background_thread_;

    // --- 4. Raft State (Persistent) ---
    // 这三个变量需要被持久化
    uint64_t current_term_; 
    int voted_for_;         
    std::vector<LogEntry> logs_; 

    // --- 5. Raft State (Volatile) ---
    RaftState state_;
    uint64_t commit_index_; 
    uint64_t last_applied_; 

    // 内存数据库 (State Machine)
    std::map<std::string, std::string> kv_store_;

    // --- 6. Leader Volatile State ---
    std::vector<uint64_t> next_index_;  
    std::vector<uint64_t> match_index_; 

    int vote_count_;
    
    // --- 7. Time ---
    std::chrono::steady_clock::time_point last_election_time_;
    std::chrono::milliseconds election_timeout_;
    std::chrono::steady_clock::time_point last_heartbeat_time_;
    std::chrono::milliseconds heartbeat_timeout_;
};

} // namespace raftkv