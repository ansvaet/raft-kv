#pragma once
#include "../../include/raft/types.hpp"
#include "log_manager.hpp"
#include <memory>
#include <vector>
#include <atomic>
#include <chrono>
#include <limits>
#include <mutex>
#include <random>

namespace raft {

    // Узлы нумеруются с 0, поэтому «нет значения» не может быть 0
    constexpr uint32_t kNoNode = std::numeric_limits<uint32_t>::max();

    class IStateMachine;
    class INetworkTransport;
    class Serializer;

    class ConsensusEngine {
    public:
        struct Config {
            uint32_t node_id;
            uint32_t total_nodes;
            uint32_t election_timeout_min = 150;
            uint32_t election_timeout_max = 300;
            uint32_t heartbeat_interval = 50;
        };

        ConsensusEngine(const Config& config,
            std::shared_ptr<LogManager> log,
            std::shared_ptr<IStateMachine> state_machine,
            std::shared_ptr<INetworkTransport> transport,
            std::shared_ptr<Serializer> serializer);

        ~ConsensusEngine();

        void tick();

     
        void handle_message(const std::string& type, const std::string& data, uint32_t from);


        bool propose_command(const std::string& command_data, std::string& result);

        NodeState get_state() const { return state_; }
        uint64_t get_current_term() const { return current_term_; }
        uint64_t get_commit_index() const { return commit_index_; }
        uint64_t get_last_applied() const { return last_applied_; }
        // Остановленный узел не считается лидером, даже если state_ остался LEADER
        bool is_leader() const { return running_ && state_ == NodeState::LEADER; }
        void stop() { running_ = false; }
        bool is_running() const { return running_; }


        uint32_t get_leader_id() const {
            return (state_ == NodeState::LEADER) ? config_.node_id : leader_id_.load();
        }

    private:
        void reset_election_timer();
        bool election_timeout_elapsed() const;

        void become_follower(uint64_t term, uint32_t leader_id = kNoNode);
        void become_candidate();
        void become_leader();

        void start_election();
        void send_vote_requests();
        void send_heartbeats(bool force = false);
        void send_append_entries_to(uint32_t follower_id);

        void apply_committed_entries();
        void update_commit_index();

        void handle_vote_request(const VoteRequest& req, uint32_t from);
        void handle_vote_response(const VoteResponse& resp, uint32_t from);
        void handle_append_entries(const AppendEntriesRequest& req, uint32_t from);
        void handle_append_entries_response(const AppendEntriesResponse& resp, uint32_t from);

        Config config_;
        std::shared_ptr<LogManager> log_;
        std::shared_ptr<IStateMachine> state_machine_;
        std::shared_ptr<INetworkTransport> transport_;
        std::shared_ptr<Serializer> serializer_;


        std::atomic<uint64_t> current_term_{ 0 };
        std::atomic<uint32_t> voted_for_{ kNoNode };


        std::atomic<NodeState> state_{ NodeState::FOLLOWER };
        std::atomic<uint64_t> commit_index_{ 0 };
        std::atomic<uint64_t> last_applied_{ 0 };
        std::atomic<bool> running_{ true };

        // Leader state
        std::vector<uint64_t> next_index_;
        std::vector<uint64_t> match_index_;
        std::atomic<uint32_t> leader_id_{ kNoNode };

        // Election state
        std::atomic<uint32_t> votes_received_{ 0 };
        std::atomic<bool> election_in_progress_{ false };

        // Timers
        std::chrono::steady_clock::time_point last_heartbeat_;
        std::chrono::steady_clock::time_point election_timeout_;
        std::chrono::steady_clock::time_point last_heartbeat_sent_;
        std::mt19937 rng_{ std::random_device{}() };

        mutable std::mutex leader_mutex_;
        mutable std::mutex state_mutex_;
    };

} // namespace raft