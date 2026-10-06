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
void test_put_command_tdd() {
    kv::KvStateMachine kv;
    std::string result;

    bool success = kv.apply(create_put_command("name", "Alice"), result);

    assert(success == true);
    assert(result == "Alice");

    std::string value;
    kv.query(create_get_command("name"), value);
    assert(value == "Alice");

    std::cout << "✓ PUT command works correctly!" << std::endl;
}
void functional_test() {
    std::cout << "\n=== Функциональное тестирование ===\n";

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
    assert(leader != nullptr);

    std::string result;
    assert(leader->propose(create_put_command("city", "Moscow"), result));
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::string val;
    assert(leader->query(create_get_command("city"), val));
    assert(val == "Moscow");
    std::cout << "✓ PUT/GET passed\n";

    assert(leader->propose(create_put_command("city", "Saint-Petersburg"), result));
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    assert(leader->query(create_get_command("city"), val));
    assert(val == "Saint-Petersburg");
    std::cout << "✓ UPDATE passed\n";

    assert(leader->propose(create_delete_command("city"), result));
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    assert(!leader->query(create_get_command("city"), val));
    std::cout << "✓ DELETE passed\n";

    uint64_t client = 999, req = 1;
    auto cmd1 = create_put_command_with_id("idem", "first", client, req);
    auto cmd2 = create_put_command_with_id("idem", "second", client, req);
    std::string res1, res2;
    assert(leader->propose(cmd1, res1));
    assert(leader->propose(cmd2, res2));
    std::cout << "✓ Idempotency passed\n";

 
    for (auto& n : nodes) n->stop();
    std::cout << "Функциональные тесты успешно пройдены.\n";
}