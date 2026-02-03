#include "raft/node.hpp"
#include "network/virtual_network.hpp" 
#include <iostream>
#include <thread>
#include <vector>
#include <csignal>

std::atomic<bool> running{ true };

void signal_handler(int signal) {
    std::cout << "\nПолучен сигнал завершения" << std::endl;
    running = false;
}

int main() {
    std::signal(SIGINT, signal_handler);

    constexpr int NODE_COUNT = 3;

    auto network = std::make_shared<network::VirtualNetwork>();
    std::vector<std::unique_ptr<raft::RaftNode>> nodes;
    std::vector<std::thread> threads;

    std::cout << "Запуск Raft кластера из " << NODE_COUNT << " узлов" << std::endl;

    // Создаем узлы
    for (uint32_t i = 0; i < NODE_COUNT; ++i) {
        network->register_node(i);
    }

    // Запускаем каждый узел в отдельном потоке
    for (uint32_t i = 0; i < NODE_COUNT; ++i) {
        nodes.push_back(std::make_unique<raft::RaftNode>(i, NODE_COUNT, network));
    }
    for (auto& node : nodes) {
        threads.emplace_back([&node]() {
            node->run();
            });
    }
    
    int iteration = 0;
    while (running) {
        std::this_thread::sleep_for(std::chrono::seconds(2));

        
        for (size_t i = 0; i < nodes.size(); ++i) {
            std::string state_str;
            switch (nodes[i]->get_state()) {
            case raft::NodeState::FOLLOWER: state_str = "FOLLOWER"; break;
            case raft::NodeState::CANDIDATE: state_str = "CANDIDATE"; break;
            case raft::NodeState::LEADER: state_str = "LEADER"; break;
            }
            std::cout << "  Узел " << i << ": " << state_str
                << " (срок: " << nodes[i]->get_current_term() << ")" << std::endl;
        }

        if (iteration > 10) {
      
            break;
        }
    }

    running = false;


    for (auto& t : threads) {
        if (t.joinable()) t.join();
    }

    return 0;
}