#include "consensus.hpp"
#include "../include/kv/command.hpp"
#include "../network/virtual_transport.hpp" 
#include "serializer.hpp"
#include <random>
#include <chrono>
#include <algorithm>
#include <iostream>

namespace raft {

    using namespace std::chrono;

    ConsensusEngine::ConsensusEngine(const Config& config,
        std::shared_ptr<LogManager> log,
        std::shared_ptr<IStateMachine> state_machine,
        std::shared_ptr<INetworkTransport> transport,
        std::shared_ptr<Serializer> serializer)
        : config_(config)
        , log_(std::move(log))
        , state_machine_(std::move(state_machine))
        , transport_(std::move(transport))
        , serializer_(std::move(serializer))
        , leader_id_(0)
        , election_in_progress_(false)
    {
        next_index_.resize(config_.total_nodes, 1);
        match_index_.resize(config_.total_nodes, 0);

        //random election timeout
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dist(config_.election_timeout_min,
            config_.election_timeout_max);
        election_timeout_ms_ = dist(gen);

        reset_election_timer();
    }

    ConsensusEngine::~ConsensusEngine() {}

    void ConsensusEngine::tick() {
        if (!running_) return;

        network::VirtualMessage msg;
        while (transport_->receive(config_.node_id, msg)) {
            handle_message(msg.type, msg.data, msg.from_id);
        }

        switch (state_) {
        case NodeState::FOLLOWER:
            if (election_timeout_elapsed()) {
                become_candidate();
            }
            break;

        case NodeState::CANDIDATE:
            if (election_timeout_elapsed()) {
                start_election();
            }
            break;

        case NodeState::LEADER:
            send_heartbeats();
            apply_committed_entries();
            update_commit_index();
            break;
        }
        apply_committed_entries();
    }

    void ConsensusEngine::handle_message(const std::string& type, const std::string& data, uint32_t from) {
        if (!running_) return;
        try {
            if (type == "VoteRequest") {
                VoteRequest req;
                if (serializer_->deserialize_vote_request(data, req)) {
                    handle_vote_request(req, from);
                }
            }
            else if (type == "VoteResponse") {
                VoteResponse resp;
                if (serializer_->deserialize_vote_response(data, resp)) {
                    handle_vote_response(resp, from);
                }
            }
            else if (type == "AppendEntries") {
                AppendEntriesRequest req;
                if (serializer_->deserialize_append_entries(data, req)) {
                    handle_append_entries(req, from);
                }
            }
            else if (type == "AppendEntriesResponse") {
                AppendEntriesResponse resp;
                if (serializer_->deserialize_append_entries_response(data, resp)) {
                    handle_append_entries_response(resp, from);
                }
            }
            else {
                std::cerr << "[Node " << config_.node_id << "] Unknown message type: " << type << std::endl;
            }
        }
        catch (const std::exception& e) {
            std::cerr << "[Node " << config_.node_id << "] Failed to handle message from "
                << from << " type " << type << ": " << e.what() << std::endl;
        }
    }

    bool ConsensusEngine::propose_command(const std::string& command_data, std::string& result) {
        /*if (!running_) {
            std::cout << "[Node " << config_.node_id << "] Not running, cannot propose command" << std::endl;
            return false;
        }*/
        if (state_ != NodeState::LEADER) {
            std::cout << "[Node " << config_.node_id << "] Not leader, cannot propose command" << std::endl;
            return false;
        }

        kv::Command cmd;
        if (!kv::Command::deserialize(command_data, cmd)) {
            std::cerr << "[Leader " << config_.node_id << "] Failed to parse command" << std::endl;
            return false;
        }


        LogEntry entry(current_term_, command_data);
        log_->append(entry);

        uint64_t entry_index = log_->get_last_index();


        for (uint32_t i = 0; i < config_.total_nodes; ++i) {
            if (i != config_.node_id) {
                send_append_entries_to(i);
            }
        }

        auto start = steady_clock::now();
        while (commit_index_ < entry_index) {
            auto elapsed = duration_cast<milliseconds>(steady_clock::now() - start);
            if (elapsed.count() > 5000) {
                std::cerr << "[Leader " << config_.node_id << "] Command commit timeout after "
                    << elapsed.count() << "ms" << std::endl;
                return false;
            }

            network::VirtualMessage msg;
            if (transport_->receive(config_.node_id, msg)) {
                handle_message(msg.type, msg.data, msg.from_id);
            }

            if (commit_index_ < entry_index) {
                std::this_thread::sleep_for(milliseconds(50));
            }
        }


        if (cmd.type == kv::CommandType::GET) {
            return state_machine_->query(command_data, result);
        }


        return true;
    }

    void ConsensusEngine::reset_election_timer() {
        last_heartbeat_ = steady_clock::now();
        election_timeout_ = last_heartbeat_ + milliseconds(election_timeout_ms_);
    }

    bool ConsensusEngine::election_timeout_elapsed() const {
        return steady_clock::now() >= election_timeout_;
    }

    void ConsensusEngine::become_follower(uint64_t term, uint32_t leader_id) {
        if (term > current_term_) {
            current_term_ = term;
            voted_for_ = 0;
        }

        state_ = NodeState::FOLLOWER;
        leader_id_ = leader_id;
        election_in_progress_ = false;
        votes_received_ = 0;

        reset_election_timer();

        /*std::cout << "[Node " << config_.node_id << "] Became FOLLOWER in term "
            << current_term_ << std::endl;*/
    }

    void ConsensusEngine::become_candidate() {
        current_term_++;
        state_ = NodeState::CANDIDATE;
        voted_for_ = config_.node_id;
        votes_received_ = 1;
        election_in_progress_ = true;
        leader_id_ = 0;

        reset_election_timer();

        /*std::cout << "[Node " << config_.node_id << "] Became CANDIDATE in term "
            << current_term_ << std::endl;*/

        start_election();
    }

    void ConsensusEngine::become_leader() {
        state_ = NodeState::LEADER;
        leader_id_ = config_.node_id;
        election_in_progress_ = false;

        {
            std::lock_guard<std::mutex> lock(leader_mutex_);
            uint64_t next_log_index = log_->get_last_index() + 1;
            for (uint32_t i = 0; i < config_.total_nodes; ++i) {
                next_index_[i] = next_log_index;
                match_index_[i] = 0;
            }
        }

        //std::cout << "\n=== Node " << config_.node_id << " became LEADER in term "
        //    << current_term_ << " ===" << std::endl;

        send_heartbeats();
    }

    void ConsensusEngine::start_election() {
        //std::cout << "[Node " << config_.node_id << "] Starting election for term "
        //    << current_term_ << std::endl;

        send_vote_requests();
    }

    void ConsensusEngine::send_vote_requests() {
        VoteRequest req;
        req.term = current_term_;
        req.candidate_id = config_.node_id;
        req.last_log_index = log_->get_last_index();
        req.last_log_term = log_->get_last_term();

        std::string data = serializer_->serialize_vote_request(req);

        for (uint32_t i = 0; i < config_.total_nodes; ++i) {
            if (i != config_.node_id) {
                network::VirtualMessage msg(config_.node_id, i, "VoteRequest", data);
                transport_->send(msg);
            }
        }
    }

    void ConsensusEngine::send_heartbeats() {
        static auto last_heartbeat_time = steady_clock::now();
        auto now = steady_clock::now();

        if (duration_cast<milliseconds>(now - last_heartbeat_time).count() < config_.heartbeat_interval) {
            return;
        }

        last_heartbeat_time = now;

        for (uint32_t i = 0; i < config_.total_nodes; ++i) {
            if (i != config_.node_id) {
                send_append_entries_to(i);
            }
        }
    }

    void ConsensusEngine::send_append_entries_to(uint32_t follower_id) {
        std::lock_guard<std::mutex> lock(leader_mutex_);

        AppendEntriesRequest req;
        req.term = current_term_;
        req.leader_id = config_.node_id;
        req.leader_commit = commit_index_;

        uint64_t next_idx = next_index_[follower_id];
        uint64_t prev_log_index = next_idx - 1;

        LogEntry prev_entry;
        if (log_->get_entry(prev_log_index, prev_entry)) {
            req.prev_log_index = prev_log_index;
            req.prev_log_term = prev_entry.term;
        }
        else {
            req.prev_log_index = 0;
            req.prev_log_term = 0;
        }

        std::vector<LogEntry> entries;
        if (log_->get_entries_from(next_idx, entries)) {
            req.entries = std::move(entries);
        }

        std::string data = serializer_->serialize_append_entries(req);
        network::VirtualMessage msg(config_.node_id, follower_id, "AppendEntries", data);
        transport_->send(msg);
    }

    void ConsensusEngine::apply_committed_entries() {
        while (last_applied_ < commit_index_) {
            uint64_t index_to_apply = last_applied_ + 1;

            LogEntry entry;
            if (!log_->get_entry(index_to_apply, entry)) {
                std::cerr << "[Node " << config_.node_id << "] Failed to get log entry "
                    << index_to_apply << std::endl;
                break;
            }

            std::string result;
            state_machine_->apply(entry.data, result);

            last_applied_ = index_to_apply;
        }
    }

    void ConsensusEngine::update_commit_index() {
        if (state_ != NodeState::LEADER) {
            return;
        }

        std::lock_guard<std::mutex> lock(leader_mutex_);

        std::vector<uint64_t> match_copy = match_index_;
        match_copy.push_back(log_->get_last_index());

        std::sort(match_copy.begin(), match_copy.end());
        uint64_t new_commit_index = match_copy[match_copy.size() / 2];


        if (new_commit_index > commit_index_) {
            LogEntry entry;
            if (log_->get_entry(new_commit_index, entry) && entry.term == current_term_) {
                commit_index_ = new_commit_index;
                //std::cout << "[Leader " << config_.node_id << "] Updated commit_index to "
                //    << commit_index_ << std::endl;
            }
        }
    }

    void ConsensusEngine::handle_vote_request(const VoteRequest& req, uint32_t from) {
        VoteResponse resp;
        resp.term = current_term_;
        resp.vote_granted = false;

        if (req.term < current_term_) {
            std::cout << "[Node " << config_.node_id << "] Vote denied to " << from
                << ": stale term (" << req.term << " < " << current_term_ << ")" << std::endl;
        }
        else {

            if (req.term > current_term_) {
                become_follower(req.term, 0);
                resp.term = current_term_;
            }


            bool log_ok = true;
            uint64_t our_last_term = log_->get_last_term();
            uint64_t our_last_index = log_->get_last_index();

            if (req.last_log_term < our_last_term) {
                log_ok = false;
            }
            else if (req.last_log_term == our_last_term && req.last_log_index < our_last_index) {
                log_ok = false;
            }

            if (log_ok && (voted_for_ == 0 || voted_for_ == from)) {
                resp.vote_granted = true;
                voted_for_ = from;
                reset_election_timer();
                //std::cout << "[Node " << config_.node_id << "] Granted vote to " << from << std::endl;
            }
        }

        std::string data = serializer_->serialize_vote_response(resp);
        network::VirtualMessage msg(config_.node_id, from, "VoteResponse", data);
        transport_->send(msg);
    }

    void ConsensusEngine::handle_vote_response(const VoteResponse& resp, uint32_t from) {
        if (!election_in_progress_ || state_ != NodeState::CANDIDATE) {
            return;
        }

        if (resp.term > current_term_) {
            become_follower(resp.term, 0);
            return;
        }

        if (resp.term == current_term_ && resp.vote_granted) {
            votes_received_++;

    /*        std::cout << "[Node " << config_.node_id << "] Votes received: " << votes_received_
                << "/" << config_.total_nodes << std::endl;*/

            if (votes_received_ > config_.total_nodes / 2) {
                become_leader();
            }
        }
    }

    void ConsensusEngine::handle_append_entries(const AppendEntriesRequest& req, uint32_t from) {
        AppendEntriesResponse resp;
        resp.term = current_term_;
        resp.success = false;
        resp.match_index = 0;

        if (req.term < current_term_) {
            std::cout << "[Node " << config_.node_id << "] AppendEntries denied from "
                << from << ": stale term (" << req.term << " < " << current_term_ << ")" << std::endl;
        }
        else {
           
            if (req.term > current_term_) {
                become_follower(req.term, from);
                resp.term = current_term_;
            }

           
            reset_election_timer();

            if (!log_->check_consistency(req.prev_log_index, req.prev_log_term)) {
                std::cout << "[Node " << config_.node_id << "] AppendEntries denied from "
                    << from << ": log inconsistency" << std::endl;
            }
            else {
                resp.success = true;

               
                if (!req.entries.empty()) {
                    log_->truncate_from(req.prev_log_index + 1);

                    
                    for (const auto& entry : req.entries) {
                        log_->append(entry);
                    }
                }

                resp.match_index = req.prev_log_index + req.entries.size();

               
                if (req.leader_commit > commit_index_) {
                    commit_index_ = std::min(req.leader_commit, log_->get_last_index());
                    //std::cout << "[Node " << config_.node_id << "] Updated commit_index to "
                    //    << commit_index_ << std::endl;
                }
            }
        }

        std::string data = serializer_->serialize_append_entries_response(resp);
        network::VirtualMessage msg(config_.node_id, from, "AppendEntriesResponse", data);
        transport_->send(msg);
    }

    void ConsensusEngine::handle_append_entries_response(const AppendEntriesResponse& resp, uint32_t from) {
        if (state_ != NodeState::LEADER) {
            return;
        }

        if (resp.term > current_term_) {
            become_follower(resp.term, 0);
            return;
        }

        if (resp.success) {
            std::lock_guard<std::mutex> lock(leader_mutex_);

            next_index_[from] = resp.match_index + 1;
            match_index_[from] = resp.match_index;
        }
        else {
          
            std::lock_guard<std::mutex> lock(leader_mutex_);
            if (next_index_[from] > 1) {
                next_index_[from]--;
               /* std::cout << "[Leader " << config_.node_id << "] Decremented next_index["
                    << from << "] to " << next_index_[from] << std::endl;*/
            }
        }
    }

} 