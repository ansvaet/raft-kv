#pragma once
#include "state.hpp"
#include "../network/virtual_network.hpp"
#include <atomic>
#include <vector>
#include <chrono>

namespace raft {

    class RaftNode {
    public:
        RaftNode(uint32_t id, uint32_t total_nodes, std::shared_ptr<network::VirtualNetwork> network);

        void run();
        void stop();

        NodeState get_state() const { return state_; }
        uint64_t get_current_term() const { return current_term_; }

        void process_messages();
        void send_vote_request();
        void handle_vote_request(const VoteRequest& req, uint32_t from_id);
        void handle_vote_response(const VoteResponse& resp, uint32_t from_id);
        void handle_append_entries(const AppendEntriesRequest& req, uint32_t from_id);
    private:
        
        const uint32_t id_;
        const uint32_t total_nodes_;
        std::shared_ptr<network::VirtualNetwork> network_;

        // Постоянное состояние (in-memory)
        uint64_t current_term_ = 0;
        uint32_t voted_for_ = -1;  // -1 = ни за кого
        std::vector<LogEntry> log_;

        // Временное состояние
        std::atomic<NodeState> state_ = NodeState::FOLLOWER;

        std::chrono::steady_clock::time_point last_heartbeat_;
        std::chrono::milliseconds election_timeout_;


        uint32_t votes_received_ = 0;
        bool election_in_progress_ = false;

        void follower_loop();
        void candidate_loop();
        void leader_loop();

        void reset_election_timer();
        bool should_become_candidate() const;
        void start_election();
        void become_leader();
        void send_heartbeat();
    };

} 