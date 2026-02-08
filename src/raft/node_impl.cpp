#include "node_impl.hpp"
#include <iostream>
#include <chrono>

namespace raft {

    RaftNodeImpl::RaftNodeImpl(uint32_t node_id,
        uint32_t total_nodes,
        std::shared_ptr<INetworkTransport> transport,
        std::shared_ptr<IStateMachine> state_machine)
        : transport_(std::move(transport))
        , state_machine_(std::move(state_machine))
        , running_(false)
    {
        config_.node_id = node_id;
        config_.total_nodes = total_nodes;

        log_manager_ = std::make_shared<LogManager>();
        serializer_ = std::make_shared<Serializer>();

        consensus_ = std::make_unique<ConsensusEngine>(
            config_, log_manager_, state_machine_, transport_, serializer_
        );
    }

    RaftNodeImpl::~RaftNodeImpl() {
        stop();
    }

    void RaftNodeImpl::start() {
        if (running_) {
            return;
        }
        consensus_ = std::make_unique<ConsensusEngine>(
            config_, log_manager_, state_machine_, transport_, serializer_
        );
        running_ = true;
        worker_thread_ = std::thread(&RaftNodeImpl::run_loop, this);
    }

    void RaftNodeImpl::stop() {
        if (!running_) {
            return;
        }
        running_ = false;

        if (consensus_) {
            consensus_->stop();
        }

        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }

    }

    void RaftNodeImpl::run_loop() {
        using namespace std::chrono;

        while (running_) {
            consensus_->tick();
            std::this_thread::sleep_for(milliseconds(10));
        }
    }

    NodeState RaftNodeImpl::get_state() const {
        return consensus_->get_state();
    }

    uint64_t RaftNodeImpl::get_current_term() const {
        return consensus_->get_current_term();
    }

    bool RaftNodeImpl::is_leader() const {
        return consensus_->is_leader();
    }

    bool RaftNodeImpl::propose(const std::string& command_data, std::string& result) {
        return consensus_->propose_command(command_data, result);
    }

    bool RaftNodeImpl::query(const std::string& query_data, std::string& result) {
        return state_machine_->query(query_data, result);
    }

    void RaftNodeImpl::print_status() const {
        std::string state_str;
        switch (get_state()) {
        case NodeState::FOLLOWER: state_str = "FOLLOWER"; break;
        case NodeState::CANDIDATE: state_str = "CANDIDATE"; break;
        case NodeState::LEADER: state_str = "LEADER"; break;
        }

        std::cout << "  State: " << state_str << std::endl;
        std::cout << "  Term: " << get_current_term() << std::endl;
        std::cout << "  Commit index: " << get_commit_index() << std::endl;
        std::cout << "  Last applied: " << get_last_applied() << std::endl;
        std::cout << "  Log size: " << log_manager_->size() << std::endl;
    }

    uint64_t RaftNodeImpl::get_commit_index() const {
        return consensus_->get_commit_index();
    }

    uint64_t RaftNodeImpl::get_last_applied() const {
        return consensus_->get_last_applied();
    }

    std::unique_ptr<IRaftNode> create_raft_node(
        uint32_t node_id,
        uint32_t total_nodes,
        std::shared_ptr<INetworkTransport> transport,
        std::shared_ptr<IStateMachine> state_machine) {

        return std::make_unique<RaftNodeImpl>(
            node_id, total_nodes, transport, state_machine
        );
    }

} 