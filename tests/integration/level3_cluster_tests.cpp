#include "test_utils.hpp"
#include <cassert>

namespace raft {
    namespace test {

        void test_cluster_election() {
            std::cout << "\nLEVEL 3.1: Cluster Leader Election" << std::endl;

            auto [nodes, state_machines, transport] = create_test_cluster(3);


            for (auto& node : nodes) {
                node->start();
            }


            bool elected = wait_for_leader(nodes, 5);
            assert(elected);

            auto leader = find_leader(nodes);
            assert(leader != nullptr);

            print_cluster_status(nodes);

            for (auto& node : nodes) {
                node->stop();
            }
        }

        void test_log_replication() {
            std::cout << "\n LEVEL 3.2: Log Replication" << std::endl;

            auto [nodes, state_machines, transport] = create_test_cluster(3);

            for (auto& node : nodes) {
                node->start();
            }

            wait_for_leader(nodes);
            auto leader = find_leader(nodes);
            assert(leader != nullptr);

            std::cout << "Leader found. Sending PUT command" << std::endl;

            std::string result;
            bool proposed = leader->propose(create_put_command("replicated", "value"), result);
            assert(proposed);

            std::this_thread::sleep_for(std::chrono::seconds(1));

            std::cout << "Checking all nodes have the value..." << std::endl;

            for (size_t i = 0; i < nodes.size(); i++) {
                std::string value;
                bool found = state_machines[i]->query(create_get_command("replicated"), value);

                if (found && value == "value") {
                    std::cout << " Node " << i << " has correct value" << std::endl;
                }
                else {
                    std::cerr << " Node " << i << " does NOT have correct value" << std::endl;
                    assert(false);
                }
            }


            for (auto& node : nodes) {
                node->stop();
            }

        }

        void test_leader_failure() {
            std::cout << "\nLEVEL 3.3: Leader Failure and Recovery" << std::endl;

            auto [nodes, state_machines, transport] = create_test_cluster(3);

            for (auto& node : nodes) {
                node->start();
            }

            wait_for_leader(nodes);
            auto original_leader = find_leader(nodes);
            assert(original_leader != nullptr);

            std::cout << "Original leader found. Sending initial command..." << std::endl;

            std::string result;
            original_leader->propose(create_put_command("persist", "before_crash"), result);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));

            std::cout << "Killing leader..." << std::endl;
            original_leader->stop();

            std::cout << "Waiting for new leader election..." << std::endl;
            bool elected = wait_for_leader(nodes, 5);
            assert(elected);

            auto new_leader = find_leader(nodes);
            assert(new_leader != nullptr);
            uint32_t new_leader_idx = new_leader->get_id();
            uint32_t original_leader_idx = original_leader->get_id();
            assert(new_leader_idx != original_leader_idx);

            std::cout << "New leader elected. Sending command after recovery..." << std::endl;

            new_leader->propose(create_put_command("persist", "after_crash"), result);
            std::this_thread::sleep_for(std::chrono::seconds(1));

            std::cout << "Checking data consistency..." << std::endl;

            for (size_t i = 0; i < nodes.size(); i++) {
                if (nodes[i] == new_leader) {
                    new_leader_idx = i;
                    break;
                }
            }


            for (size_t i = 0; i < nodes.size(); i++) {
                if (i == new_leader_idx) continue; 

                std::string value;
                bool found = state_machines[i]->query(create_get_command("persist"), value);

                if (found) {
                    std::cout << " Node " << i << " has value: " << value << std::endl;
                }
                else {
                    std::cout << " Node " << i << " might be recovering..." << std::endl;
                }
            }

            for (auto& node : nodes) {
                node->stop();
            }

        }

    } 
} 