# Tiny-Milvus: Distributed Vector Database based on Raft

![Build Status](https://img.shields.io/badge/build-passing-brightgreen) ![C++](https://img.shields.io/badge/C%2B%2B-20-blue) ![AI-Infra](https://img.shields.io/badge/AI--Infra-VectorDB-orange)

**Tiny-Milvus** is a high-performance distributed vector database designed for **AI Infra** and **RAG (Retrieval-Augmented Generation)** scenarios. 

It combines the strong consistency of **Raft** with the efficient ANN (Approximate Nearest Neighbor) capabilities of **HNSW**, providing a scalable storage backend for massive vector embeddings.

---

## 🚀 Core Features

### 1. Vector Search Engine (AI Native)
* **HNSW Index**: Integrated `hnswlib` for millisecond-level ANN search.
* **High Performance**: Replaced traditional KV engines with high-dimensional vector indexing.
* **Distance Metrics**: Supports L2 (Euclidean) distance for similarity calculation.

### 2. Distributed Consensus (Raft)
* **Leader Election**: Handles split votes and node failures automatically.
* **Log Replication**: Ensures all vector insertions are replicated across the cluster before being indexed.
* **Crash Safety**: WAL-based persistence ensures zero data loss even after power failures.

### 3. Engineering & Architecture
* **RPC Framework**: Built on **gRPC** for robust node communication.
* **Concurrency**: Optimized with `recursive_mutex` to prevent deadlocks in high-concurrency RPC callbacks.
* **Client SDK**: Provides a C++ CLI client for vector insertion and Top-K search.

---

## 🛠️ Tech Stack

* **Consensus**: Raft Algorithm (Custom Implementation)
* **Vector Index**: HNSW (Hierarchical Navigable Small World)
* **Communication**: gRPC / Protobuf
* **Language**: C++20

---

## ⚡ Quick Start

### 1. Build
```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
