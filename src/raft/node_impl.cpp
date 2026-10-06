#include "node_impl.hpp"
#include <iostream>
#include <chrono>
#include <future>
#include <stdexcept>

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
        {
            std::lock_guard<std::mutex> lock(inbox_mutex_);
            accepting_ = true;
        }
        running_ = true;
        worker_thread_ = std::thread(&RaftNodeImpl::run_loop, this);
    }

    void RaftNodeImpl::stop() {
        if (!running_) {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(inbox_mutex_);
            running_ = false;
        }
        inbox_cv_.notify_all();

        if (consensus_) {
            consensus_->stop();
        }

        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
    }

    void RaftNodeImpl::run_loop() {
        using namespace std::chrono;
        // Входящие сообщения транспорта пока опрашиваются, поэтому цикл просыпается
        // и по таймеру; новое предложение будит его сразу
        constexpr auto kPollInterval = milliseconds(1);

        std::deque<Proposal> batch;
        while (running_) {
            {
                std::lock_guard<std::mutex> lock(inbox_mutex_);
                batch.swap(inbox_);
            }
            for (auto& p : batch) {
                consensus_->propose(std::move(p.command), std::move(p.callback));
            }
            batch.clear();

            consensus_->tick();

            std::unique_lock<std::mutex> lock(inbox_mutex_);
            inbox_cv_.wait_for(lock, kPollInterval,
                [this] { return !inbox_.empty() || !running_; });
        }

        // После этого propose_async сразу отвечает STOPPED, так что ни один
        // callback не потеряется
        {
            std::lock_guard<std::mutex> lock(inbox_mutex_);
            accepting_ = false;
            batch.swap(inbox_);
        }
        consensus_->fail_pending(ProposeStatus::STOPPED);
        for (auto& p : batch) {
            ProposeResult r;
            r.status = ProposeStatus::STOPPED;
            p.callback(std::move(r));
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

    void RaftNodeImpl::propose_async(std::string command_data, ProposeCallback callback) {
        {
            std::lock_guard<std::mutex> lock(inbox_mutex_);
            if (accepting_) {
                inbox_.push_back(Proposal{ std::move(command_data), std::move(callback) });
                inbox_cv_.notify_one();
                return;
            }
        }
        ProposeResult r;
        r.status = ProposeStatus::STOPPED;
        callback(std::move(r));
    }

    bool RaftNodeImpl::propose(const std::string& command_data, std::string& result) {
        // Из потока узла ждать нельзя: этот же поток должен закоммитить запись
        if (std::this_thread::get_id() == worker_thread_.get_id()) {
            throw std::logic_error("RaftNodeImpl::propose called from the node thread");
        }

        auto promise = std::make_shared<std::promise<ProposeResult>>();
        auto future = promise->get_future();
        propose_async(command_data, [promise](ProposeResult r) {
            promise->set_value(std::move(r));
        });

        // Завершение гарантировано: commit, propose_timeout или остановка узла
        ProposeResult r = future.get();
        result = std::move(r.result);
        return r.status == ProposeStatus::OK;
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