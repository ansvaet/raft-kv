#pragma once
#include "types.hpp"
#include <memory>
#include <string>

namespace raft {

    class IRaftNode {
    public:
        virtual ~IRaftNode() = default;

        
        virtual void start() = 0;
        virtual void stop() = 0;

        
        virtual NodeState get_state() const = 0;
        virtual uint64_t get_current_term() const = 0;
        virtual uint32_t get_id() const = 0;
        virtual bool is_leader() const = 0;

        // Потокобезопасно. callback вызывается ровно один раз из потока узла;
        // он должен быть быстрым и не вызывать блокирующий propose()
        virtual void propose_async(std::string command_data, ProposeCallback callback) = 0;

        // Блокирующая обёртка над propose_async: true, если команда закоммичена
        // и применена. Нельзя вызывать из callback узла
        virtual bool propose(const std::string& command_data, std::string& result) = 0;

        virtual bool query(const std::string& query_data, std::string& result) = 0;

        virtual void print_status() const = 0;
        virtual uint64_t get_commit_index() const = 0;
        virtual uint64_t get_last_applied() const = 0;
    };

    std::unique_ptr<IRaftNode> create_raft_node(
        uint32_t node_id,
        uint32_t total_nodes,
        std::shared_ptr<INetworkTransport> transport,
        std::shared_ptr<IStateMachine> state_machine
    );

}