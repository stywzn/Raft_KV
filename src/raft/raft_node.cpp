#include "raft/raft_node.h"
#include "network/raft_server.h" 
#include <random>
#include <chrono>
#include <iostream> // 引入 iostream 以使用 std::cout

namespace raftkv {

const int kMinElectionTimeoutMs = 150;
const int kMaxElectionTimeoutMs = 300;
const int kHeartbeatIntervalMs = 50;

// =========================================================
// Lifecycle Methods
// =========================================================

RaftNode::RaftNode(int id, const std::vector<std::string>& peers, int port)
    : id_(id), peers_(peers), port_(port), 
      running_(false), 
      current_term_(0), voted_for_(-1), 
      state_(Follower), 
      commit_index_(0), last_applied_(0),
      vote_count_(0) {
    
    heartbeat_timeout_ = std::chrono::milliseconds(kHeartbeatIntervalMs);
    election_timeout_ = GetRandomizedElectionTimeout();
    last_election_time_ = std::chrono::steady_clock::now();

    for (const auto& peer_addr : peers) {
        rpc_clients_.push_back(std::make_unique<RaftRpcClient>(peer_addr));
    }
    
    LOG_INFO("RaftNode " << id_ << " initialized on port " << port_);
}

RaftNode::~RaftNode() {
    Stop();
}

void RaftNode::Start() {
    if (running_) return;
    running_ = true;

    // 1. Start Server
    std::string server_address = "0.0.0.0:" + std::to_string(port_);
    rpc_service_ = std::make_unique<RaftRpcServiceImpl>(*this);
    
    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(rpc_service_.get());
    rpc_server_ = builder.BuildAndStart();
    LOG_INFO("gRPC Server listening on " << server_address);

    // 2. Start Thread
    LOG_INFO("RaftNode " << id_ << " starting main loop...");
    background_thread_ = std::make_unique<std::thread>([this]() {
        this->MainLoop();
    });
}

void RaftNode::Stop() {
    if (!running_) return;
    running_ = false;

    if (rpc_server_) {
        LOG_INFO("Shutting down gRPC server...");
        rpc_server_->Shutdown();
    }
    if (background_thread_ && background_thread_->joinable()) {
        background_thread_->join();
    }
    LOG_INFO("RaftNode " << id_ << " stopped.");
}

// =========================================================
// Core Logic
// =========================================================

void RaftNode::MainLoop() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = std::chrono::steady_clock::now();

        switch (state_) {
            case Follower:
            case Candidate: {
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_election_time_);
                if (elapsed >= election_timeout_) {
                    LOG_INFO("Election timeout reached. Starting election...");
                    BecomeCandidate();
                }
                break;
            }
            case Leader: {
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_election_time_);
                if (elapsed >= heartbeat_timeout_) {
                    SendAppendEntries(-1); // Broadcast
                    last_election_time_ = now;
                }
                break;
            }
        }
    }
}

void RaftNode::BecomeCandidate() {
    current_term_++;
    voted_for_ = id_;
    vote_count_ = 1; 
    last_election_time_ = std::chrono::steady_clock::now();
    election_timeout_ = GetRandomizedElectionTimeout();
    state_ = Candidate;

    LOG_INFO("Became Candidate at term " << current_term_);

    for (size_t i = 0; i < peers_.size(); ++i) {
        if ((int)i == id_) continue;
        SendRequestVote(i);
    }
}

void RaftNode::BecomeLeader() {
    state_ = Leader;
    LOG_INFO("Became LEADER at term " << current_term_);
    SendAppendEntries(-1);
}

// =========================================================
// Network Senders (The Mouth)
// =========================================================

void RaftNode::SendRequestVote(int target_id) {
    // 【调试信息】确认函数被调用了
    std::cout << "!!! TRYING TO SEND VOTE TO " << target_id << " !!!" << std::endl; 

    // 1. 准备请求数据
    raftpb::RequestVoteRequest req;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        req.set_term(current_term_);
        req.set_candidate_id(id_);
        req.set_last_log_index(0);
        req.set_last_log_term(0);
    } 

    LOG_DEBUG("Sending RequestVote to Node " << target_id);

    // 2. 异步发送
    std::thread([this, target_id, req]() {
        // 【调试信息】确认线程启动了
        std::cout << "!!! THREAD STARTED FOR " << target_id << " !!!" << std::endl;
        
        raftpb::RequestVoteResponse resp;
        // 这一步会阻塞，直到超时或返回
        bool success = rpc_clients_[target_id]->RequestVote(req, &resp);

        if (!success) {
            std::cout << "!!! RPC FAILED FOR " << target_id << " !!!" << std::endl;
            return;
        }

        // 成功后的逻辑
        std::lock_guard<std::mutex> lock(this->mutex_);
        
        // 状态检查
        if (this->state_ != Candidate || this->current_term_ != req.term()) {
            return;
        }

        if (resp.term() > (uint64_t)this->current_term_) {
            this->current_term_ = resp.term();
            this->state_ = Follower;
            this->voted_for_ = -1;
            return;
        }

        if (resp.vote_granted()) {
            this->vote_count_++;
            LOG_INFO("Got Vote from Node " << target_id << ". Total: " << this->vote_count_);
            if (this->vote_count_ > (int)(this->peers_.size() / 2)) {
                this->BecomeLeader();
            }
        }
    }).detach();
}

void RaftNode::SendAppendEntries(int target_id) {
    if (target_id == -1) {
        for (size_t i = 0; i < peers_.size(); ++i) {
            if ((int)i == id_) continue;
            
            std::thread([this, i]() {
                raftpb::AppendEntriesRequest req;
                raftpb::AppendEntriesResponse resp;
                
                {
                    std::lock_guard<std::mutex> lock(this->mutex_);
                    req.set_term(this->current_term_);
                    req.set_leader_id(this->id_);
                }

                bool success = this->rpc_clients_[i]->AppendEntries(req, &resp);
                
                if (success) {
                    std::lock_guard<std::mutex> lock(this->mutex_);
                    if (resp.term() > (uint64_t)this->current_term_) {
                        this->current_term_ = resp.term();
                        this->state_ = Follower;
                    }
                }
            }).detach();
        }
    }
}

// =========================================================
// RPC Handlers (The Ears)
// =========================================================

void RaftNode::HandleRequestVote(const raftpb::RequestVoteRequest& req, 
                                 raftpb::RequestVoteResponse* resp) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (req.term() > (uint64_t)current_term_) {
        current_term_ = req.term();
        state_ = Follower;
        voted_for_ = -1; 
    }

    if (req.term() >= (uint64_t)current_term_ && (voted_for_ == -1 || voted_for_ == (int)req.candidate_id())) {
        voted_for_ = req.candidate_id();
        resp->set_vote_granted(true);
        LOG_INFO("Voted FOR Node " << req.candidate_id() << " at Term " << req.term());
        last_election_time_ = std::chrono::steady_clock::now();
    } else {
        resp->set_vote_granted(false);
    }
    resp->set_term(current_term_);
}

void RaftNode::HandleAppendEntries(const raftpb::AppendEntriesRequest& req, 
                                   raftpb::AppendEntriesResponse* resp) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (req.term() >= (uint64_t)current_term_) {
        current_term_ = req.term();
        state_ = Follower;
        last_election_time_ = std::chrono::steady_clock::now();
        // LOG_DEBUG("Received Heartbeat from Leader " << req.leader_id());
    }
    
    resp->set_success(true);
    resp->set_term(current_term_);
}

// =========================================================
// Utils
// =========================================================

std::chrono::milliseconds RaftNode::GetRandomizedElectionTimeout() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(kMinElectionTimeoutMs, kMaxElectionTimeoutMs);
    return std::chrono::milliseconds(dist(gen));
}

std::tuple<int, int, bool> RaftNode::Propose(std::string data) {
    std::lock_guard<std::mutex> lock(mutex_);
    (void)data;
    return {0, current_term_, state_ == Leader};
}

bool RaftNode::IsLeader() {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_ == Leader;
}

} // namespace raftkv