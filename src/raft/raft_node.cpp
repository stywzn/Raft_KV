#include "raft/raft_node.h"
#include "network/raft_server.h" 
#include <random>
#include <chrono>
#include <algorithm> 
#include <fstream>   

namespace raftkv {

const int kMinElectionTimeoutMs = 3000;
const int kMaxElectionTimeoutMs = 6000;
const int kHeartbeatIntervalMs = 500;

RaftNode::RaftNode(int id, const std::vector<std::string>& peers, int port)
    : id_(id), peers_(peers), port_(port), 
      running_(false), 
      current_term_(0), voted_for_(-1), 
      state_(Follower), 
      commit_index_(0), last_applied_(0),
      vote_count_(0) {
    
    persistence_file_ = "node_" + std::to_string(id_) + ".storage";

    // 【初始化 HNSW】
    // M=16 (连接数), ef_construction=200 (构建精度)
    hnsw_space_ = std::make_unique<hnswlib::L2Space>(dim_);
    hnsw_index_ = std::make_unique<hnswlib::HierarchicalNSW<float>>(hnsw_space_.get(), max_elements_, 16, 200);

    LoadState(); // 恢复 Raft 状态 (Term/Logs)

    if (logs_.empty()) {
        logs_.push_back({0, 0, ""}); 
    }

    heartbeat_timeout_ = std::chrono::milliseconds(kHeartbeatIntervalMs);
    election_timeout_ = GetRandomizedElectionTimeout();
    last_election_time_ = std::chrono::steady_clock::now();

    for (const auto& peer_addr : peers) {
        rpc_clients_.push_back(std::make_unique<RaftRpcClient>(peer_addr));
    }
    
    LOG_INFO("VectorDB Node " << id_ << " initialized. Dim=" << dim_);
}

RaftNode::~RaftNode() {
    Stop();
}

void RaftNode::Start() {
    if (running_) return;
    running_ = true;
    std::string server_address = "0.0.0.0:" + std::to_string(port_);
    rpc_service_ = std::make_unique<RaftRpcServiceImpl>(*this);
    
    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(rpc_service_.get());
    rpc_server_ = builder.BuildAndStart();
    LOG_INFO("gRPC Server listening on " << server_address);
    LOG_INFO("RaftNode " << id_ << " starting main loop...");
    background_thread_ = std::make_unique<std::thread>([this]() { this->MainLoop(); });
}

void RaftNode::Stop() {
    if (!running_) return;
    running_ = false;
    if (rpc_server_) rpc_server_->Shutdown();
    if (background_thread_ && background_thread_->joinable()) background_thread_->join();
    LOG_INFO("RaftNode " << id_ << " stopped.");
}

uint64_t RaftNode::GetLastLogIndex() { return logs_.back().index; }
uint64_t RaftNode::GetLastLogTerm() { return logs_.back().term; }

void RaftNode::BecomeFollower(uint64_t term) {
    if (term > current_term_) {
        LOG_INFO("Term updated: " << current_term_ << " -> " << term);
        current_term_ = term;
        voted_for_ = -1;
        Persist();
    }
    state_ = Follower;
    last_election_time_ = std::chrono::steady_clock::now();
}

// =========================================================
// State Machine (Vector Insertion)
// =========================================================

void RaftNode::ApplyLogs() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    while (last_applied_ < commit_index_) {
        last_applied_++;
        const auto& entry = logs_[last_applied_];
        
        if (entry.data.empty()) continue; 

        // 1. 反序列化
        raftpb::ClientRequest req;
        if (!req.ParseFromString(entry.data)) {
            LOG_ERROR("Failed to parse log entry at index " << last_applied_);
            continue;
        }

        // 2. 写入 HNSW
        if (req.has_vector_insert()) {
            const auto& vec_data = req.vector_insert();
            int64_t id = vec_data.id();
            
            // 将 Protobuf 数组转为 std::vector
            std::vector<float> point;
            point.reserve(vec_data.vector_size());
            for (float f : vec_data.vector()) point.push_back(f);

            if (point.size() != (size_t)dim_) {
                LOG_ERROR("Dim mismatch! Expected " << dim_ << ", got " << point.size());
                continue;
            }

            try {
                hnsw_index_->addPoint(point.data(), id);
                LOG_INFO("[HNSW] Inserted Vector ID=" << id << " Index=" << last_applied_);
            } catch (const std::exception& e) {
                LOG_ERROR("HNSW AddPoint Error: " << e.what());
            }
        }
    }
}

// =========================================================
// Search Interface
// =========================================================

std::vector<std::pair<int64_t, float>> RaftNode::Search(const std::vector<float>& query_vec, int top_k) {
    std::lock_guard<std::recursive_mutex> lock(mutex_); // 简单加锁

    if (query_vec.size() != (size_t)dim_) {
        LOG_ERROR("Search dim mismatch");
        return {};
    }

    std::vector<std::pair<int64_t, float>> results;
    try {
        // HNSW Search
        auto result_queue = hnsw_index_->searchKnn(query_vec.data(), top_k);
        
        // 结果是最大堆(最远的在上面)，需要取出后反转
        while (!result_queue.empty()) {
            auto item = result_queue.top();
            result_queue.pop();
            results.push_back({item.first, item.second}); // ID, Distance
        }
        std::reverse(results.begin(), results.end());
    } catch (const std::exception& e) {
        LOG_ERROR("Search Error: " << e.what());
    }
    return results;
}

// =========================================================
// Main Loop & Consensus (Unchanged logic)
// =========================================================

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
                    LOG_INFO("Timeout. Become Candidate...");
                    BecomeCandidate();
                }
                break;
            }
            case Leader: {
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_heartbeat_time_);
                if (elapsed >= heartbeat_timeout_) {
                    SendAppendEntries(-1); 
                    last_heartbeat_time_ = now;
                }
                break;
            }
        }
    }
}

void RaftNode::BecomeCandidate() {
    current_term_++; voted_for_ = id_; Persist();
    vote_count_ = 1; last_election_time_ = std::chrono::steady_clock::now();
    election_timeout_ = GetRandomizedElectionTimeout(); state_ = Candidate;
    LOG_INFO("Candidate Term " << current_term_);
    for (size_t i = 0; i < peers_.size(); ++i) if ((int)i != id_) SendRequestVote(i);
}

void RaftNode::BecomeLeader() {
    state_ = Leader; LOG_INFO("LEADER Term " << current_term_);
    next_index_.assign(peers_.size(), GetLastLogIndex() + 1);
    match_index_.assign(peers_.size(), 0);
    SendAppendEntries(-1); last_heartbeat_time_ = std::chrono::steady_clock::now();
}

std::tuple<uint64_t, uint64_t, bool> RaftNode::Propose(std::string data) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (state_ != Leader) return {0, current_term_, false};
    logs_.push_back({current_term_, GetLastLogIndex() + 1, data});
    Persist();
    LOG_INFO("Leader propose Index " << logs_.back().index);
    SendAppendEntries(-1);
    return {logs_.back().index, current_term_, true};
}

bool RaftNode::IsLeader() { std::lock_guard<std::recursive_mutex> lock(mutex_); return state_ == Leader; }

void RaftNode::SendRequestVote(int target_id) {
    raftpb::RequestVoteRequest req;
    req.set_term(current_term_); req.set_candidate_id(id_);
    req.set_last_log_index(GetLastLogIndex()); req.set_last_log_term(GetLastLogTerm());
    std::thread([this, target_id, req]() {
        raftpb::RequestVoteResponse resp;
        if(rpc_clients_[target_id]->RequestVote(req, &resp)) {
            std::lock_guard<std::recursive_mutex> lock(this->mutex_);
            if(state_ != Candidate || current_term_ != req.term()) return;
            if(resp.term() > current_term_) { BecomeFollower(resp.term()); return; }
            if(resp.vote_granted()) {
                if(++vote_count_ > (int)peers_.size()/2) BecomeLeader();
            }
        }
    }).detach();
}

void RaftNode::SendAppendEntries(int target_id) {
    if (target_id == -1) { for(size_t i=0; i<peers_.size(); ++i) if((int)i!=id_) SendAppendEntries(i); return; }
    raftpb::AppendEntriesRequest req;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (state_ != Leader) return;
        uint64_t next = next_index_[target_id];
        if (next - 1 >= logs_.size()) return;
        req.set_term(current_term_); req.set_leader_id(id_);
        req.set_prev_log_index(next - 1); req.set_prev_log_term(logs_[next - 1].term);
        req.set_leader_commit(commit_index_);
        for(size_t i=next; i<logs_.size(); ++i) {
            auto* e = req.add_entries(); e->set_term(logs_[i].term); e->set_index(logs_[i].index); e->set_data(logs_[i].data);
        }
    }
    std::thread([this, target_id, req]() {
        raftpb::AppendEntriesResponse resp;
        if(rpc_clients_[target_id]->AppendEntries(req, &resp)) {
            std::lock_guard<std::recursive_mutex> lock(this->mutex_);
            if(state_ != Leader || current_term_ != req.term()) return;
            if(resp.term() > current_term_) { BecomeFollower(resp.term()); return; }
            if(resp.success()) {
                match_index_[target_id] = req.prev_log_index() + req.entries_size();
                next_index_[target_id] = match_index_[target_id] + 1;
                for(uint64_t N = GetLastLogIndex(); N > commit_index_; --N) {
                    if(logs_[N].term != current_term_) continue;
                    int count = 1;
                    for(size_t i=0; i<peers_.size(); ++i) if((int)i!=id_ && match_index_[i] >= N) count++;
                    if(count > (int)peers_.size()/2) { commit_index_ = N; break; }
                }
            } else { if(next_index_[target_id] > 1) next_index_[target_id]--; }
        }
    }).detach();
}

void RaftNode::HandleRequestVote(const raftpb::RequestVoteRequest& req, raftpb::RequestVoteResponse* resp) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if(req.term() > current_term_) BecomeFollower(req.term());
    bool log_ok = (req.last_log_term() > GetLastLogTerm()) || (req.last_log_term() == GetLastLogTerm() && req.last_log_index() >= GetLastLogIndex());
    if(req.term() == current_term_ && log_ok && (voted_for_ == -1 || voted_for_ == req.candidate_id())) {
        voted_for_ = req.candidate_id(); Persist(); resp->set_vote_granted(true); last_election_time_ = std::chrono::steady_clock::now();
    } else resp->set_vote_granted(false);
    resp->set_term(current_term_);
}

void RaftNode::HandleAppendEntries(const raftpb::AppendEntriesRequest& req, raftpb::AppendEntriesResponse* resp) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if(req.term() < current_term_) { resp->set_success(false); resp->set_term(current_term_); return; }
    if(req.term() > current_term_) BecomeFollower(req.term());
    state_ = Follower; last_election_time_ = std::chrono::steady_clock::now();
    if(req.prev_log_index() >= logs_.size() || logs_[req.prev_log_index()].term != req.prev_log_term()) {
        resp->set_success(false); resp->set_term(current_term_); return;
    }
    resp->set_success(true); resp->set_term(current_term_);
    uint64_t index = req.prev_log_index();
    bool changed = false;
    for(const auto& e : req.entries()) {
        index++;
        if(index >= logs_.size()) { logs_.push_back({e.term(), e.index(), e.data()}); changed = true; }
        else if(logs_[index].term != e.term()) { logs_.resize(index); logs_.push_back({e.term(), e.index(), e.data()}); changed = true; }
    }
    if(changed) Persist();
    if(req.leader_commit() > commit_index_) commit_index_ = std::min(req.leader_commit(), GetLastLogIndex());
}

void RaftNode::Persist() {
    std::ofstream outfile(persistence_file_, std::ios::trunc);
    outfile << current_term_ << " " << voted_for_ << "\n" << logs_.size() << "\n";
    // 暂时只存 Raft 状态，数据部分因为是二进制，需要更复杂的序列化
    // 这里为了演示，我们先不存 data 的具体内容，或者需要 Base64 编码
    // *注意*：为了不破坏 HNSW，重启后数据暂时会丢失，除非实现 HNSW 的 saveIndex
    for(const auto& e : logs_) outfile << e.term << " " << e.index << " " << "BLOB" << "\n";
    outfile.close();
}

void RaftNode::LoadState() {
    std::ifstream infile(persistence_file_);
    if(!infile.is_open()) return;
    size_t size; std::string dummy;
    infile >> current_term_ >> voted_for_ >> size;
    logs_.clear();
    uint64_t t, i;
    for(size_t k=0; k<size; ++k) {
        infile >> t >> i >> dummy;
        logs_.push_back({t, i, ""}); 
    }
    infile.close();
}

std::chrono::milliseconds RaftNode::GetRandomizedElectionTimeout() {
    static std::random_device rd; static std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(kMinElectionTimeoutMs, kMaxElectionTimeoutMs);
    return std::chrono::milliseconds(dist(gen));
}

} // namespace raftkv