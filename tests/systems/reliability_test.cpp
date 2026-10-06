#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>
#include <memory>
#include <vector>
#include <string>
#include "../../src/network/virtual_transport.hpp"
#include "../../src/raft/node_impl.hpp"
#include "../../src/kv/kv_store.hpp"
#include "../integration/test_utils.hpp"

using namespace raft;
using namespace raft::test;
void reliability_test() {
    std::cout << "\n=== Тестирование надёжности и восстановления ===\n";

    auto transport = std::make_shared<network::VirtualTransport>();
    for (int i = 0; i < 3; ++i) transport->register_node(i);
    std::vector<std::shared_ptr<IRaftNode>> nodes;
    std::vector<std::shared_ptr<IStateMachine>> sms;
    for (int i = 0; i < 3; ++i) {
        auto sm = std::make_shared<kv::KvStateMachine>();
        sms.push_back(sm);
        nodes.push_back(create_raft_node(i, 3, transport, sm));
    }
    for (auto& n : nodes) n->start();
    std::this_thread::sleep_for(std::chrono::seconds(2));

    auto leader = find_leader(nodes);
    int leader_idx = -1;
    for (size_t i = 0; i < nodes.size(); ++i)
        if (nodes[i] == leader) leader_idx = i;
    std::cout << "Лидер: узел " << leader_idx << "\n";

    std::string result;
    assert(leader->propose(create_put_command("failover", "before_crash"), result));
    std::this_thread::sleep_for(std::chrono::seconds(1));

    std::cout << "Убиваем лидера...\n";
    leader->stop();

    bool elected = false;
    for (int i = 0; i < 50; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        auto new_leader = find_leader(nodes);
        if (new_leader && new_leader->get_id() != leader->get_id()) {
            elected = true;
            break;
        }
    }
    assert(elected);

    auto new_leader = find_leader(nodes);
    std::string val;
    assert(new_leader->query(create_get_command("failover"), val));
    assert(val == "before_crash");
    std::cout << "✓ Данные сохранены, новый лидер избран.\n";

    leader->start();
    std::this_thread::sleep_for(std::chrono::seconds(2));

    for (size_t i = 0; i < nodes.size(); ++i) {
        std::string v;
        sms[i]->query(create_get_command("failover"), v);
        assert(v == "before_crash");
    }
    std::cout << "✓ Все узлы синхронизированы после восстановления.\n";

    for (auto& n : nodes) n->stop();
}