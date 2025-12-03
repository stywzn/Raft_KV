#pragma once

#include <mutex>
#include <vector>
#include <memory>
#include <atomic>
#include <condition_variable>
#include <thread>
#include <chrono>
#include <tuple>

#include <grpcpp/grpcpp.h> 
#include "raft.pb.h"
#include "common/logger.h"
#include "network/rpc_client.h" 

namespace raftkv {

class RaftRpcServiceImpl;

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
    std::tuple<int, int, bool> Propose(std::string data);
    bool IsLeader();

private:
    void MainLoop();
    void BecomeCandidate();
    void BecomeLeader();
    
    void SendRequestVote(int target_id);
    void SendAppendEntries(int target_id);
    
    std::chrono::milliseconds GetRandomizedElectionTimeout();

private:
    // --- 1. Node Metadata ---
    int id_; 
    int port_;
    std::vector<std::string> peers_; 
    
    // --- 2. Network Components ---
    std::unique_ptr<grpc::Server> rpc_server_;
    std::unique_ptr<RaftRpcServiceImpl> rpc_service_;
    std::vector<std::unique_ptr<RaftRpcClient>> rpc_clients_;

    // --- 3. Threading ---
    std::mutex mutex_;
    std::atomic<bool> running_;
    std::unique_ptr<std::thread> background_thread_;

    // --- 4. Raft State ---
    int current_term_; 
    int voted_for_;
    RaftState state_;
    int commit_index_;
    int last_applied_;

    // 【新增】得票计数器
    int vote_count_;
    
    // --- 5. Time ---
    std::chrono::steady_clock::time_point last_election_time_;
    std::chrono::milliseconds election_timeout_;
    std::chrono::milliseconds heartbeat_timeout_;
};

} // namespace raftkv