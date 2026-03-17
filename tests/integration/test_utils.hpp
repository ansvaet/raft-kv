#pragma once

#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>
#include <random>
#include <iomanip>
#include <sstream>

#include "../../include/raft/types.hpp"
#include "../../src/network/virtual_transport.hpp"
#include "../../src/raft/node_impl.hpp"
#include "../../src/kv/kv_store.hpp"

namespace raft {
    namespace test {

        // Утилита для генерации уникальных ID запросов
        inline uint64_t generate_request_id() {
            static std::atomic<uint64_t> counter{ 1 };
            return counter++;
        }

        // Утилита для создания PUT команды
        inline std::string create_put_command(const std::string& key,
            const std::string& value,
            uint64_t client_id = 1000) {
            nlohmann::json j;
            j["type"] = "PUT";
            j["key"] = key;
            j["value"] = value;
            j["client_id"] = client_id;
            j["request_id"] = generate_request_id();
            return j.dump();
        }

        // Утилита для создания GET команды
        inline std::string create_get_command(const std::string& key,
            uint64_t client_id = 1000) {
            nlohmann::json j;
            j["type"] = "GET";
            j["key"] = key;
            j["client_id"] = client_id;
            j["request_id"] = generate_request_id();
            return j.dump();
        }

        // Утилита для создания DELETE команды
        inline std::string create_delete_command(const std::string& key,
            uint64_t client_id = 1000) {
            nlohmann::json j;
            j["type"] = "DELETE";
            j["key"] = key;
            j["client_id"] = client_id;
            j["request_id"] = generate_request_id();
            return j.dump();
        }

        // Утилита для создания тестового кластера
        inline std::tuple<std::vector<std::shared_ptr<IRaftNode>>,
            std::vector<std::shared_ptr<IStateMachine>>,
            std::shared_ptr<network::VirtualTransport>>
            create_test_cluster(int node_count = 3) {
            auto transport = std::make_shared<network::VirtualTransport>();
            std::vector<std::shared_ptr<IRaftNode>> nodes;
            std::vector<std::shared_ptr<IStateMachine>> state_machines;

            for (int i = 0; i < node_count; i++) {
                transport->register_node(i);
                auto state_machine = std::make_shared<kv::KvStateMachine>();
                state_machines.push_back(state_machine);

                auto node = create_raft_node(i, node_count, transport, state_machine);
                nodes.push_back(std::move(node));
            }

            return { nodes, state_machines, transport };
        }

        // Утилита для поиска лидера
        inline std::shared_ptr<IRaftNode> find_leader(const std::vector<std::shared_ptr<IRaftNode>>& nodes) {
            for (auto& node : nodes) {
                if (node->is_leader()) {
                    return node;
                }
            }
            return nullptr;
        }

        // Утилита для ожидания выборов лидера
        inline bool wait_for_leader(const std::vector<std::shared_ptr<IRaftNode>>& nodes,
            int timeout_seconds = 5) {
            auto start = std::chrono::steady_clock::now();
            while (std::chrono::steady_clock::now() - start < std::chrono::seconds(timeout_seconds)) {
                if (find_leader(nodes) != nullptr) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(500)); // Стабилизация
                    return true;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            return false;
        }

        // Утилита для печати статуса кластера
        inline void print_cluster_status(const std::vector<std::shared_ptr<IRaftNode>>& nodes) {
            std::cout << "\n=== CLUSTER STATUS ===" << std::endl;
            for (size_t i = 0; i < nodes.size(); i++) {
                std::string state;
                switch (nodes[i]->get_state()) {
                case NodeState::FOLLOWER: state = "FOLLOWER"; break;
                case NodeState::CANDIDATE: state = "CANDIDATE"; break;
                case NodeState::LEADER: state = "LEADER"; break;
                }

                std::cout << "Node " << i << ": "
                    << std::left << std::setw(10) << state
                    << " Term: " << std::setw(3) << nodes[i]->get_current_term()
                    << " Commit: " << nodes[i]->get_commit_index()
                    << (nodes[i]->is_leader() ? " ★ LEADER" : "")
                    << std::endl;
            }
        }

        // Утилита для остановки узла
        inline void stop_node(const std::shared_ptr<IRaftNode>& node) {
            if (node) {
                node->stop();
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }

        // Утилита для запуска узла
        inline void start_node(const std::shared_ptr<IRaftNode>& node) {
            if (node) {
                node->start();
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }

    } // namespace test
} // namespace raft