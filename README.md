# Raft-KV: Distributed Key-Value Storage with Crash Safety

![Build Status](https://img.shields.io/badge/build-passing-brightgreen) ![C++](https://img.shields.io/badge/C%2B%2B-20-blue) ![gRPC](https://img.shields.io/badge/gRPC-Framework-red) ![License](https://img.shields.io/badge/license-MIT-green)

**Raft-KV** is a high-performance, strongly consistent distributed Key-Value storage system built with **Modern C++20** and **gRPC**. 

It implements the core **Raft Consensus Algorithm**, featuring automatic leader election, log replication, and a **WAL-based persistence mechanism** that ensures data durability and crash recovery.

---

## 🚀 Key Features

### 1. Core Consensus (Raft)
* **Leader Election**: Implements randomized election timeouts to handle split votes and leader failures efficiently.
* **Log Replication**: Strong consistency replication with safety checks (`prev_log_term`, `prev_log_index`) and majority quorum commits.
* **Heartbeat Mechanism**: Maintains authority stability and prevents unnecessary re-elections.

### 2. Storage & Reliability (The Hard Parts)
* **Crash Safety & Persistence**: Implements **Write-Ahead Logging (WAL)**.
    * Persists `CurrentTerm`, `VotedFor`, and `Logs` to disk in real-time.
    * **Crash Recovery**: Nodes automatically restore state from disk upon restart, ensuring **Zero Data Loss**.
* **KV State Machine**: Linearly consistent application of committed logs to the in-memory KV engine (`std::map`).

### 3. Engineering & Concurrency
* **RPC Framework**: Built on **gRPC/Protobuf** for robust inter-node communication.
* **Deadlock Prevention**: Designed with `std::recursive_mutex` to handle complex re-entrant locking scenarios between RPC callbacks and the main event loop.
* **Client Interaction**: Provides a CLI client to interact with the cluster dynamically.

---

## 🛠️ Tech Stack

* **Language**: C++20 (Smart Pointers, Threads, Atomics, File Streams)
* **RPC**: gRPC, Protocol Buffers
* **Build System**: CMake (FetchContent integration for dependency management)
* **Development Environment**: Linux / WSL2

---

## 📂 Project Structure

```text
Raft-KV/
├── src/
│   ├── cmd/            # Entry points (Server main, Client CLI)
│   ├── raft/           # Core Raft logic (Consensus, FSM, Persistence)
│   ├── network/        # gRPC Service Implementation
│   └── protos/         # Protobuf definitions
├── CMakeLists.txt      # Build configuration
└── node_*.storage      # Persistence files (generated at runtime)
