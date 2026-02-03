#include "node.hpp"
#include <iostream>
#include <random>
#include <thread>
#include <sstream>
#include <algorithm>

namespace raft {

    RaftNode::RaftNode(uint32_t id, uint32_t total_nodes,
        std::shared_ptr<network::VirtualNetwork> network)
        : id_(id), total_nodes_(total_nodes), network_(network) {

        // Случайный таймаут выборов 150-300мс 
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dist(150, 300);
        election_timeout_ = std::chrono::milliseconds(dist(gen));

        reset_election_timer();
        std::cout << "Узел " << id_ << " создан. Таймаут выборов: "
            << election_timeout_.count() << "мс" << std::endl;
    }

    void RaftNode::reset_election_timer() {
        last_heartbeat_ = std::chrono::steady_clock::now();
    }

    bool RaftNode::should_become_candidate() const {

        if (state_ == NodeState::LEADER) {
            return false;
        }

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_heartbeat_);
        return elapsed > election_timeout_;
    }

    void RaftNode::send_vote_request() {
        VoteRequest req{
            .term = current_term_,
            .candidate_id = id_,
            .last_log_index = static_cast<uint64_t>(log_.size()),
            .last_log_term = log_.empty() ? 0 : log_.back().term
        };

        
        std::stringstream ss;
        ss << req.term << " " << req.candidate_id << " "
            << req.last_log_index << " " << req.last_log_term;

        
        for (uint32_t i = 0; i < total_nodes_; ++i) {
            if (i != id_) {
                network_->send(id_, i, "RequestVote", ss.str());
            }
        }
    }

    void RaftNode::start_election() {
        std::cout << "Узел " << id_ << " начинает выборы в сроке "
            << current_term_ + 1 << std::endl;

        
        state_ = NodeState::CANDIDATE;
        current_term_++;
        voted_for_ = id_; 
        votes_received_ = 1;
        election_in_progress_ = true;
        reset_election_timer();

        send_vote_request();
    }

    void RaftNode::handle_vote_request(const VoteRequest& req, uint32_t from_id) {
        std::cout << "Узел " << id_ << " получил запрос голоса от узла "
            << from_id << " в сроке " << req.term << std::endl;

        bool vote_granted = false;

        if ((state_ == NodeState::LEADER || state_ == NodeState::CANDIDATE) &&
            req.term > current_term_) {
            std::cout << "Лидер/кандидат " << id_ << " видит больший срок "
                << req.term << ", становится фолловером" << std::endl;
            current_term_ = req.term;
            state_ = NodeState::FOLLOWER;
            voted_for_ = -1;
            election_in_progress_ = false;
            reset_election_timer();
        }
        if (req.term < current_term_) {
            std::cout << "  Отказ: устаревший срок" << std::endl;
        }
        else if (req.term > current_term_) {
        
            current_term_ = req.term;
            state_ = NodeState::FOLLOWER;
            voted_for_ = -1;
            reset_election_timer();
        }

        if (req.term >= current_term_) {
            bool log_is_ok = true;
            if (!log_.empty()) {
                // лог кандидата не менее полный
                uint64_t my_last_term = log_.back().term;
                if (req.last_log_term < my_last_term) {
                    log_is_ok = false;
                }
                else if (req.last_log_term == my_last_term &&
                    req.last_log_index < log_.size()) {
                    log_is_ok = false;
                }
            }

            if (log_is_ok && (voted_for_ == -1 || voted_for_ == from_id)) {
                vote_granted = true;
                voted_for_ = from_id;
                reset_election_timer();
                std::cout << "Голос отдан узлу " << from_id << std::endl;
            }
        }

        VoteResponse resp{
            .term = current_term_,
            .vote_granted = vote_granted
        };

        std::stringstream ss;
        ss << resp.term << " " << (resp.vote_granted ? "1" : "0");
        network_->send(id_, from_id, "VoteResponse", ss.str());
    }

    void RaftNode::handle_vote_response(const VoteResponse& resp, uint32_t from_id) {
        if (!election_in_progress_) {
            return;
        }

        std::cout << "Узел " << id_ << " получил ответ от узла "
            << from_id << ": "
            << (resp.vote_granted ? "ЗА" : "ПРОТИВ")
            << " (срок: " << resp.term << ")" << std::endl;

        
        if (resp.term > current_term_) {
            current_term_ = resp.term;
            state_ = NodeState::FOLLOWER;
            voted_for_ = -1;
            election_in_progress_ = false;
            reset_election_timer();
            return;
        }

    
        if (resp.term == current_term_ && resp.vote_granted) {
            votes_received_++;
            std::cout << "  Голосов получено: " << votes_received_
                << "/" << total_nodes_ << std::endl;

           
            if (votes_received_ > total_nodes_ / 2) {
                become_leader();
            }
        }
    }

    void RaftNode::become_leader() {
        std::cout << "\n=== Узел " << id_ << " стал ЛИДЕРОМ в сроке "
            << current_term_ << " ===" << std::endl;
        state_ = NodeState::LEADER;
        election_in_progress_ = false;
    }

    void RaftNode::process_messages() {
        network::Message msg;
        while (network_->receive(id_, msg)) {
            std::stringstream ss(msg.data);

            if (msg.type == "RequestVote") {
                VoteRequest req;
                ss >> req.term >> req.candidate_id
                    >> req.last_log_index >> req.last_log_term;
                handle_vote_request(req, msg.from_id);

            }
            else if (msg.type == "VoteResponse") {
                VoteResponse resp;
                uint32_t vote_flag;
                ss >> resp.term >> vote_flag;
                resp.vote_granted = (vote_flag == 1);
                handle_vote_response(resp, msg.from_id);
            }
            else if (msg.type == "AppendEntries") {
                AppendEntriesRequest req;
                ss >> req.term >> req.leader_id
                    >> req.prev_log_index >> req.prev_log_term;
                handle_append_entries(req, msg.from_id);
            }
        }
    }
    void RaftNode::handle_append_entries(const AppendEntriesRequest& req, uint32_t from_id) {

        if (req.term > current_term_) {
            current_term_ = req.term;
            state_ = NodeState::FOLLOWER;
            voted_for_ = -1;
            election_in_progress_ = false;
        }

        reset_election_timer();

        std::stringstream ss;
        ss << current_term_ << " " << 1; 
        network_->send(id_, from_id, "AppendEntriesResponse", ss.str());
    }

    void RaftNode::run() {
        std::cout << "Узел " << id_ << " запущен" << std::endl;

        while (true) {
            switch (state_.load()) {
            case NodeState::FOLLOWER:
                follower_loop();
                break;
            case NodeState::CANDIDATE:
                candidate_loop();
                break;
            case NodeState::LEADER:
                leader_loop();
                break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }


    void RaftNode::follower_loop() {
        process_messages();
        if (should_become_candidate()) {
            start_election();
        }
    }

    void RaftNode::candidate_loop() {
        process_messages();
        if (should_become_candidate()) {
            
            start_election();
        }
    }

    void RaftNode::leader_loop() {
        process_messages();

        static auto last_heartbeat_time = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();

        if (std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_heartbeat_time).count() > 50) {

            send_heartbeat();
            last_heartbeat_time = now;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    void RaftNode::send_heartbeat() {
       
        for (uint32_t i = 0; i < total_nodes_; ++i) {
            if (i != id_) {
                std::stringstream ss;
                ss << current_term_ << " " << id_ << " "
                    << log_.size() << " " << (log_.empty() ? 0 : log_.back().term);

                network_->send(id_, i, "AppendEntries", ss.str());
            }
        }
    }

} 