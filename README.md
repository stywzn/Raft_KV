# Raft-KV (C++20)

A high-performance distributed Key-Value store implementation based on the Raft consensus algorithm.
Built with Modern C++ (C++20), gRPC, and Protobuf.

## 🚧 Current Status (Work In Progress)

* [x] **Infrastructure:** Modern CMake build system with `FetchContent` (No local dependencies required).
* [x] **Network:** gRPC server/client implementation for Raft communication.
* [x] **Consensus (Basic):** Leader election logic (RequestVote), randomized timeouts, and term management.
* [ ] **Consensus (Advanced):** Log replication, persistence, and snapshotting (Coming soon).
* [ ] **Storage:** KV Engine implementation.

## 🛠️ Build & Run

### Requirements
* Linux / macOS / WSL
* C++20 Compiler (GCC 13+ or Clang 16+)
* CMake 3.15+

### Build
```bash
mkdir build && cd build
cmake ..
make -j$(nproc)