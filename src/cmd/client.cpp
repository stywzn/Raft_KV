#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <random>
#include <algorithm>
#include <thread>  // <--- 【新增】必须加上这个才能用 sleep_for
#include <grpcpp/grpcpp.h>

#include "raft.pb.h"
#include "raft.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using raftpb::RaftService;
using raftpb::ClientRequest;
using raftpb::ClientResponse;
using raftpb::SearchRequest;
using raftpb::SearchResponse;

class VectorDBClient {
public:
    VectorDBClient(std::shared_ptr<Channel> channel)
        : stub_(RaftService::NewStub(channel)) {}

    // 插入向量
    void InsertVector(int64_t id, const std::vector<float>& vec) {
        ClientRequest request;
        
        auto* vec_data = request.mutable_vector_insert();
        vec_data->set_id(id);
        for (float f : vec) {
            vec_data->add_vector(f);
        }

        ClientResponse response;
        ClientContext context;
        context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(2000));

        Status status = stub_->Propose(&context, request, &response);

        if (status.ok()) {
            if (response.success()) {
                std::cout << "[INSERT] Success: ID=" << id << std::endl;
            } else {
                std::cout << "[INSERT] Failed: Not Leader" << std::endl;
            }
        } else {
            std::cout << "[RPC Error] " << status.error_message() << std::endl;
        }
    }

    // 搜索向量
    void SearchVector(const std::vector<float>& query, int top_k) {
        SearchRequest request;
        for (float f : query) {
            request.add_vector(f);
        }
        request.set_top_k(top_k);

        SearchResponse response;
        ClientContext context;
        context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(2000));

        Status status = stub_->Search(&context, request, &response);

        if (status.ok()) {
            std::cout << "\n--- Top " << top_k << " Results ---" << std::endl;
            for (const auto& res : response.results()) {
                // 打印格式化：ID 和 距离
                std::cout << "ID: " << res.id() << " \tDistance: " << res.score() << std::endl;
            }
            std::cout << "-------------------------\n" << std::endl;
        } else {
            std::cout << "[Search Error] " << status.error_message() << std::endl;
        }
    }

private:
    std::unique_ptr<RaftService::Stub> stub_;
};

// 辅助函数：生成随机向量
std::vector<float> generate_random_vector(int dim) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_real_distribution<float> dis(0.0, 1.0);
    
    std::vector<float> vec;
    vec.reserve(dim);
    for(int i=0; i<dim; ++i) vec.push_back(dis(gen));
    return vec;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cout << "Usage: ./raft-client <port>" << std::endl;
        return 1;
    }

    std::string target_str = "127.0.0.1:" + std::string(argv[1]);
    VectorDBClient client(grpc::CreateChannel(target_str, grpc::InsecureChannelCredentials()));

    int dim = 128; // 必须和 Server 端一致
    
    std::cout << "Connected to " << target_str << ". Inserting 100 vectors..." << std::endl;

    // 1. 插入 100 条随机向量
    for (int i = 0; i < 100; ++i) {
        std::vector<float> vec = generate_random_vector(dim);
        client.InsertVector(i, vec); // ID 从 0 到 99
        // 稍微停顿一下，防止把日志刷太快看不清
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    std::cout << "Insertion done. Waiting for replication..." << std::endl;
    // 等待 Raft 复制和 Apply
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // 2. 执行搜索
    std::cout << "Executing Search..." << std::endl;
    std::vector<float> query = generate_random_vector(dim);
    client.SearchVector(query, 5); // 搜 Top 5

    return 0;
}