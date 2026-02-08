# Raft KV Store - Distributed Key-Value Store

A distributed key-value in-memory store implementation using the Raft consensus algorithm. 

## Features

- **Raft consensus algorithm** implementation
- **Leader election** with randomized timeouts
- **Log replication** and commitment
- **Fault tolerance** - survives leader failures
- **Consistency** - linearizable operations
- **Interactive CLI** for testing and management
- **JSON-based** command serialization

## Architecture

```
┌─────────────────────────────────────────────────┐
│                    Client                       │
└────────────────┬─────────────────┬──────────────┘
                 │                 │
          ┌──────▼──────┐   ┌──────▼──────┐
          │   Leader    │   │   Follower  │
          │  (Node 0)   │   │  (Node 1)   │
          └──────┬──────┘   └──────┬──────┘
                 │                 │
          ┌──────▼─────────────────▼──────┐
          │         Raft Consensus        │
          │  • Leader Election            │
          │  • Log Replication            │
          │  • Safety Guarantees          │
          └───────────────────────────────┘
                 │                 │
          ┌──────▼──────┐   ┌──────▼──────┐
          │   Log       │   │ State Machine│
          │  Manager    │   │   (KV Store) │
          └─────────────┘   └──────────────┘
```

## Quick Start

### Build
```bash
# Using Make
make

# Or using CMake
mkdir build && cd build
cmake ..
make
```


### Interactive Commands
```
status      - Show cluster status
kv          - Show KV store contents
put k v     - Put key-value pair (e.g., put name Alice)
get k       - Get value by key
del k       - Delete key
kill        - Kill current leader (failure test)
test        - Run demo-test sequence
help        - Show available commands
exit        - Exit program
```

## Dependencies
- C++17 or higher
- nlohmann/json library
- pthreads
