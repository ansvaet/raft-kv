#include <thread>
#include <chrono>
#include <memory>
#include <vector>
#include <string>
#include "../../src/network/virtual_transport.hpp"
#include "../../src/raft/node_impl.hpp"
#include "../../src/kv/kv_store.hpp"
#include "../integration/test_utils.hpp"
using namespace raft::test;
using namespace raft;   
void config_test() {
    std::cout << "\n=== Тестирование конфигурации ===\n";
    for (int size : {3, 5}) {
        std::cout << "\n--- Кластер из " << size << " узлов ---\n";
        auto transport = std::make_shared<network::VirtualTransport>();
        for (int i = 0; i < size; ++i) transport->register_node(i);
        std::vector<std::shared_ptr<IRaftNode>> nodes;
        for (int i = 0; i < size; ++i) {
            auto sm = std::make_shared<kv::KvStateMachine>();
            nodes.push_back(create_raft_node(i, size, transport, sm));
        }
        for (auto& n : nodes) n->start();
        std::this_thread::sleep_for(std::chrono::seconds(3));

        auto leader = find_leader(nodes);
        assert(leader != nullptr);
        std::string result;
        assert(leader->propose(create_put_command("cfg", "ok"), result));
        std::this_thread::sleep_for(std::chrono::seconds(1));

        // Проверяем репликацию на всех узлах
        for (auto& sm : nodes) {
            std::string val;
            sm->query(create_get_command("cfg"), val);
            assert(val == "ok");
        }
        std::cout << "✓ Кластер из " << size << " узлов работает корректно.\n";
        for (auto& n : nodes) n->stop();
    }
}