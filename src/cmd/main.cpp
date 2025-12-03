#include <iostream>
#include <thread>
#include <vector>
#include <string>
#include "common/logger.h"
#include "raft/raft_node.h"

int main(int argc, char** argv) {
    // 1. 读取命令行参数，确定身份
    int node_id = 0;
    if (argc > 1) {
        node_id = std::atoi(argv[1]);
    }

    // 2. 定义集群配置 (3个节点的地址)
    // Node 0 -> 127.0.0.1:5001
    // Node 1 -> 127.0.0.1:5002
    // Node 2 -> 127.0.0.1:5003
    std::vector<std::string> peers = {
        "127.0.0.1:5001",
        "127.0.0.1:5002",
        "127.0.0.1:5003"
    };

    // 3. 计算自己的端口
    // 比如 ID=0, Port=5001
    int port = 5001 + node_id;

    LOG_INFO("--- Raft KV Server ---");
    LOG_INFO("Node ID: " << node_id);
    LOG_INFO("Port: " << port);

    // 4. 启动节点
    raftkv::RaftNode node(node_id, peers, port);
    node.Start();

    // 5. 防止主线程退出
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
}