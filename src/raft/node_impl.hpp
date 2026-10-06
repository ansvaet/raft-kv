#pragma once
#include "../../include/raft/node.hpp"
#include "consensus.hpp"
#include "log_manager.hpp"
#include "serializer.hpp"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

namespace raft {

    class RaftNodeImpl : public IRaftNode {
    public:
        RaftNodeImpl(uint32_t node_id,
            uint32_t total_nodes,
            std::shared_ptr<INetworkTransport> transport,
            std::shared_ptr<IStateMachine> state_machine);

        ~RaftNodeImpl();

        void start() override;
        void stop() override;

        NodeState get_state() const override;
        uint64_t get_current_term() const override;
        uint32_t get_id() const override { return config_.node_id; }
        bool is_leader() const override;

        void propose_async(std::string command_data, ProposeCallback callback) override;
        bool propose(const std::string& command_data, std::string& result) override;
        bool query(const std::string& query_data, std::string& result) override;

        void print_status() const override;
        uint64_t get_commit_index() const override;
        uint64_t get_last_applied() const override;

    private:
        void run_loop();

        ConsensusEngine::Config config_;
        std::shared_ptr<INetworkTransport> transport_;
        std::shared_ptr<IStateMachine> state_machine_;

        std::shared_ptr<LogManager> log_manager_;
        std::shared_ptr<Serializer> serializer_;
        std::unique_ptr<ConsensusEngine> consensus_;

        std::atomic<bool> running_{ false };
        std::thread worker_thread_;

        // Входящие предложения от других потоков; консенсус трогает только worker_thread_
        struct Proposal {
            std::string command;
            ProposeCallback callback;
        };
        std::mutex inbox_mutex_;
        std::condition_variable inbox_cv_;
        std::deque<Proposal> inbox_;
        bool accepting_ = false;  // под inbox_mutex_: цикл ещё примет предложение
    };

} 