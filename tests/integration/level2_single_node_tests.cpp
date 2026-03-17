#include "test_utils.hpp"
#include <cassert>

namespace raft {
    namespace test {

        void test_single_node_raft_integration() {
            std::cout << "\n🔬 LEVEL 2: Testing Basic Node Functionality in Cluster" << std::endl;

            auto transport = std::make_shared<network::VirtualTransport>();
            transport->register_node(0);
            transport->register_node(1);
            transport->register_node(2);

            auto state_machine0 = std::make_shared<kv::KvStateMachine>();
            auto state_machine1 = std::make_shared<kv::KvStateMachine>();
            auto state_machine2 = std::make_shared<kv::KvStateMachine>();

            std::vector<std::shared_ptr<IRaftNode>> nodes;
            nodes.push_back(create_raft_node(0, 3, transport, state_machine0));
            nodes.push_back(create_raft_node(1, 3, transport, state_machine1));
            nodes.push_back(create_raft_node(2, 3, transport, state_machine2));

            std::vector<std::shared_ptr<IStateMachine>> state_machines = {
                state_machine0, state_machine1, state_machine2
            };
            std::cout << "Test 2.1: Cluster elects a leader" << std::endl;

            for (auto& node : nodes) {
                node->start();
            }

            std::cout << "  Waiting for leader election..." << std::endl;

            bool elected = wait_for_leader(nodes, 5);
            assert(elected);

            auto leader = find_leader(nodes);
            assert(leader != nullptr);

            int leader_idx = -1;
            for (size_t i = 0; i < nodes.size(); i++) {
                if (nodes[i] == leader) {
                    leader_idx = i;
                    break;
                }
            }
            std::cout << " Cluster elected leader (node " << leader_idx
                << ") in term " << leader->get_current_term() << std::endl;

            std::cout << "\nTest 2.2: Leader appends command to log" << std::endl;

            uint64_t initial_commit = leader->get_commit_index();
            std::string result;

            bool proposed = leader->propose(create_put_command("test_key", "test_value"), result);

            assert(proposed);
            std::this_thread::sleep_for(std::chrono::milliseconds(500)); 

            assert(leader->get_commit_index() > initial_commit);
            std::cout << " Command committed, commit index: " << leader->get_commit_index() << std::endl;

            std::cout << "\nTest 2.3: Command replicated to all nodes" << std::endl;

            std::this_thread::sleep_for(std::chrono::milliseconds(500)); 

            for (size_t i = 0; i < nodes.size(); i++) {
                std::string value;
                bool found = state_machines[i]->query(create_get_command("test_key"), value);

                if (found && value == "test_value") {
                    std::cout << " Node " << i << " has correct value" << std::endl;
                }
                else {
                    std::cerr << " Node " << i << " does not have correct value" << std::endl;
                    assert(false);
                }
            }

            std::cout << "\nTest 2.4: Query works correctly" << std::endl;

            std::string get_result;
            bool query_success = leader->query(create_get_command("test_key"), get_result);

            assert(query_success);
            assert(get_result == "test_value");
            std::cout << "  GET returns correct value: " << get_result << std::endl;

            for (auto& node : nodes) {
                node->stop();
            }

        }

    } 
} 