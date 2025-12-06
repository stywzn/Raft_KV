#include "raft/raft_node.h"
#include "network/raft_server.h" 
#include <random>
#include <chrono>
#include <algorithm> // for std::min, std::max
#include <fstream>   // 文件流

namespace raftkv {

// 调试模式超时设置
const int kMinElectionTimeoutMs = 3000;
const int kMaxElectionTimeoutMs = 6000;
const int kHeartbeatIntervalMs = 500;

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
    
    // 【新增】设置持久化文件名
    persistence_file_ = "node_" + std::to_string(id_) + ".storage";

    // 【新增】尝试从磁盘恢复状态
    LoadState();

    // 如果是第一次启动（没存档），或者存档损坏，logs_ 可能为空
    // 必须保证 logs_ 至少有一条 dummy entry
    if (logs_.empty()) {
        logs_.push_back({0, 0, ""}); 
    }

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
// Persistence (Disk I/O)
// =========================================================

void RaftNode::Persist() {
    // 简单的文本存储格式：
    // Term VotedFor
    // LogSize
    // Term Index Data
    
    std::ofstream outfile(persistence_file_, std::ios::trunc);
    if (!outfile.is_open()) {
        LOG_ERROR("Failed to open persistence file: " << persistence_file_);
        return;
    }

    outfile << current_term_ << " " << voted_for_ << "\n";
    outfile << logs_.size() << "\n";

    for (const auto& entry : logs_) {
        // 注意：这里假设 entry.data 不包含换行符或空格
        // 如果 data 是空的，写入一个占位符或者处理空格逻辑
        std::string data_to_write = entry.data.empty() ? "-" : entry.data;
        outfile << entry.term << " " << entry.index << " " << data_to_write << "\n";
    }
    
    outfile.close();
}

void RaftNode::LoadState() {
    std::ifstream infile(persistence_file_);
    if (!infile.is_open()) {
        LOG_INFO("No persistence file found, starting fresh.");
        return;
    }

    if (!(infile >> current_term_ >> voted_for_)) {
        return;
    }

    size_t size;
    if (!(infile >> size)) {
        return;
    }

    logs_.clear();
    
    uint64_t term, index;
    std::string data;
    
    for (size_t i = 0; i < size; ++i) {
        infile >> term >> index >> data;
        if (data == "-") data = ""; // 恢复空字符串
        logs_.push_back({term, index, data});
    }

    infile.close();
    LOG_INFO("Restored state from disk: Term=" << current_term_ << " LogSize=" << logs_.size());
}

// =========================================================
// Helper Methods
// =========================================================

uint64_t RaftNode::GetLastLogIndex() {
    return logs_.back().index;
}

uint64_t RaftNode::GetLastLogTerm() {
    return logs_.back().term;
}

void RaftNode::BecomeFollower(uint64_t term) {
    if (term > current_term_) {
        LOG_INFO("Term updated: " << current_term_ << " -> " << term);
        current_term_ = term;
        voted_for_ = -1;
        Persist(); // 【新增】Term 变更，必须落盘
    }
    state_ = Follower;
    last_election_time_ = std::chrono::steady_clock::now();
}

// =========================================================
// Core Logic & State Machine
// =========================================================

void RaftNode::ApplyLogs() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    while (last_applied_ < commit_index_) {
        last_applied_++;
        const auto& entry = logs_[last_applied_];
        
        std::string command = entry.data;
        if (command.empty()) continue; 

        size_t delimiter_pos = command.find('=');
        if (delimiter_pos != std::string::npos) {
            std::string key = command.substr(0, delimiter_pos);
            std::string value = command.substr(delimiter_pos + 1);
            
            kv_store_[key] = value;
            
            LOG_INFO("[State Machine] Applied Log Index " << last_applied_ 
                     << ": Put " << key << " = " << value 
                     << ". Map Size: " << kv_store_.size());
        } else {
             LOG_DEBUG("Skipping invalid command: " << command);
        }
    }
}

void RaftNode::MainLoop() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        auto now = std::chrono::steady_clock::now();

        ApplyLogs();

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
                auto elapsed_heartbeat = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_heartbeat_time_);
                if (elapsed_heartbeat >= heartbeat_timeout_) {
                    SendAppendEntries(-1); 
                    last_heartbeat_time_ = now;
                }
                break;
            }
        }
    }
}

void RaftNode::BecomeCandidate() {
    current_term_++;
    voted_for_ = id_;
    Persist(); // 【新增】Term 和 VotedFor 变更，必须落盘
    
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
    
    next_index_.assign(peers_.size(), GetLastLogIndex() + 1);
    match_index_.assign(peers_.size(), 0);

    SendAppendEntries(-1);
    last_heartbeat_time_ = std::chrono::steady_clock::now();
}

// =========================================================
// Client Interface
// =========================================================

std::tuple<uint64_t, uint64_t, bool> RaftNode::Propose(std::string data) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    
    if (state_ != Leader) {
        return {0, current_term_, false};
    }

    LogEntry entry;
    entry.term = current_term_;
    entry.index = GetLastLogIndex() + 1;
    entry.data = data;
    
    logs_.push_back(entry);
    
    // 【新增】新日志进入，必须落盘
    // 必须在发给 Follower 之前完成持久化
    Persist(); 
    
    LOG_INFO("Leader proposed new log index " << entry.index << " term " << entry.term);
    
    SendAppendEntries(-1);

    return {entry.index, entry.term, true};
}

bool RaftNode::IsLeader() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return state_ == Leader;
}

// =========================================================
// Network Senders
// =========================================================

void RaftNode::SendRequestVote(int target_id) {
    raftpb::RequestVoteRequest req;
    req.set_term(current_term_);
    req.set_candidate_id(id_);
    req.set_last_log_index(GetLastLogIndex());
    req.set_last_log_term(GetLastLogTerm());

    LOG_DEBUG("Sending RequestVote to Node " << target_id);

    std::thread([this, target_id, req]() {
        raftpb::RequestVoteResponse resp;
        bool success = rpc_clients_[target_id]->RequestVote(req, &resp);

        if (!success) return;

        std::lock_guard<std::recursive_mutex> lock(this->mutex_);
        
        if (this->state_ != Candidate || this->current_term_ != req.term()) return;

        if (resp.term() > this->current_term_) {
            this->BecomeFollower(resp.term());
            return;
        }

        if (resp.vote_granted()) {
            this->vote_count_++;
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
            SendAppendEntries(i); 
        }
        return;
    }

    raftpb::AppendEntriesRequest req;
    
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (state_ != Leader) return; 

        uint64_t next_idx = next_index_[target_id];
        uint64_t prev_log_idx = next_idx - 1;
        
        if (prev_log_idx >= logs_.size()) {
             LOG_ERROR("Log gap error for node " << target_id);
             return;
        }

        req.set_term(current_term_);
        req.set_leader_id(id_);
        req.set_prev_log_index(prev_log_idx);
        req.set_prev_log_term(logs_[prev_log_idx].term); 
        req.set_leader_commit(commit_index_);

        for (size_t i = next_idx; i < logs_.size(); ++i) {
            raftpb::LogEntry* entry = req.add_entries();
            entry->set_term(logs_[i].term);
            entry->set_index(logs_[i].index);
            entry->set_data(logs_[i].data);
        }
    }

    std::thread([this, target_id, req]() {
        raftpb::AppendEntriesResponse resp;
        bool success = this->rpc_clients_[target_id]->AppendEntries(req, &resp);
        
        if (!success) return;

        std::lock_guard<std::recursive_mutex> lock(this->mutex_);
        if (this->state_ != Leader || this->current_term_ != req.term()) return;

        if (resp.term() > this->current_term_) {
            this->BecomeFollower(resp.term());
            return;
        }

        if (resp.success()) {
            uint64_t log_count = req.entries_size();
            if (log_count > 0) {
                this->match_index_[target_id] = req.prev_log_index() + log_count;
                this->next_index_[target_id] = this->match_index_[target_id] + 1;
                
                for (uint64_t N = this->GetLastLogIndex(); N > this->commit_index_; --N) {
                    if (this->logs_[N].term != this->current_term_) continue; 
                    
                    int count = 1; 
                    for (size_t i = 0; i < this->peers_.size(); ++i) {
                        if ((int)i == this->id_) continue;
                        if (this->match_index_[i] >= N) {
                            count++;
                        }
                    }
                    
                    if (count > (int)(this->peers_.size() / 2)) {
                        this->commit_index_ = N;
                        LOG_INFO("Leader Commit Index updated to " << N);
                        break;
                    }
                }
            }
        } else {
            if (this->next_index_[target_id] > 1) {
                this->next_index_[target_id]--;
            }
        }
    }).detach();
}

// =========================================================
// RPC Handlers
// =========================================================

void RaftNode::HandleRequestVote(const raftpb::RequestVoteRequest& req, 
                                 raftpb::RequestVoteResponse* resp) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    
    if (req.term() > current_term_) {
        BecomeFollower(req.term());
    }

    bool log_is_ok = (req.last_log_term() > GetLastLogTerm()) ||
                     (req.last_log_term() == GetLastLogTerm() && req.last_log_index() >= GetLastLogIndex());

    if (req.term() == current_term_ && log_is_ok && 
        (voted_for_ == -1 || voted_for_ == (int)req.candidate_id())) {
        
        voted_for_ = req.candidate_id();
        // 【新增】改变了 voted_for 必须存盘
        Persist(); 

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
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    
    if (req.term() < current_term_) {
        resp->set_success(false);
        resp->set_term(current_term_);
        return;
    }

    if (req.term() > current_term_) {
        BecomeFollower(req.term());
    }
    
    state_ = Follower; 
    last_election_time_ = std::chrono::steady_clock::now();

    if (req.prev_log_index() >= logs_.size()) {
        resp->set_success(false);
        resp->set_term(current_term_);
        return;
    }

    if (logs_[req.prev_log_index()].term != req.prev_log_term()) {
        resp->set_success(false);
        resp->set_term(current_term_);
        return;
    }

    resp->set_success(true);
    resp->set_term(current_term_);

    // 1. 追加日志
    uint64_t index = req.prev_log_index(); 
    bool log_changed = false; // 标记日志是否变动

    for (const auto& entry_proto : req.entries()) {
        index++;
        if (index >= logs_.size()) {
            logs_.push_back({entry_proto.term(), entry_proto.index(), entry_proto.data()});
            log_changed = true;
            LOG_INFO(">>> [Replication Success] Follower appended Log: Index=" << index << " Data=" << entry_proto.data());
        } else {
             if (logs_[index].term != entry_proto.term()) {
                logs_.resize(index);
                logs_.push_back({entry_proto.term(), entry_proto.index(), entry_proto.data()});
                log_changed = true;
                LOG_INFO(">>> [Conflict Fixed] Follower overwrote Log: Index=" << index);
            }
        }
    }

    // 【新增】如果有日志写入，必须持久化
    if (log_changed) {
        Persist();
    }

    // 2. 更新 Commit Index
    if (req.leader_commit() > commit_index_) {
        commit_index_ = std::min(req.leader_commit(), GetLastLogIndex());
    }
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

} // namespace raftkv