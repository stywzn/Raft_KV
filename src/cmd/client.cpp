#include <iostream>
#include <memory>
#include <string>
#include <grpcpp/grpcpp.h>

// 【关键修改】必须包含这两个头文件
#include "raft.pb.h"       // 定义 ClientRequest, ClientResponse
#include "raft.grpc.pb.h"  // 定义 RaftService, RaftService::Stub

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

// RaftService 在 raftpb 命名空间下
using raftpb::RaftService;
using raftpb::ClientRequest;
using raftpb::ClientResponse;

class RaftClient {
public:
    RaftClient(std::shared_ptr<Channel> channel)
        : stub_(RaftService::NewStub(channel)) {}

    void Propose(const std::string& data) {
        ClientRequest request;
        request.set_data(data);

        ClientResponse response;
        ClientContext context;

        // 设置一个合理的超时时间，防止客户端无限等待
        auto deadline = std::chrono::system_clock::now() + std::chrono::milliseconds(3000);
        context.set_deadline(deadline);

        Status status = stub_->Propose(&context, request, &response);

        if (status.ok()) {
            if (response.success()) {
                std::cout << "[SUCCESS] Leader accepted: " << data << std::endl;
            } else {
                std::cout << "[FAILED] Connected node is NOT Leader." << std::endl;
                // 这里以后可以打印 response.leader_hint()
            }
        } else {
            std::cout << "[RPC ERROR] " << status.error_code() << ": " << status.error_message() << std::endl;
        }
    }

private:
    std::unique_ptr<RaftService::Stub> stub_;
};

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cout << "Usage: ./raft-client <port> <data>" << std::endl;
        std::cout << "Example: ./raft-client 5001 \"User=Tom\"" << std::endl;
        return 1;
    }

    std::string port = argv[1];
    std::string data = argv[2];
    std::string target_str = "127.0.0.1:" + port;

    std::cout << "Connecting to " << target_str << "..." << std::endl;

    RaftClient client(grpc::CreateChannel(target_str, grpc::InsecureChannelCredentials()));
    client.Propose(data);

    return 0;
}