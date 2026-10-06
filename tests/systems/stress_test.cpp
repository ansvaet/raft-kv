#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>
#include <memory>
#include <vector>
#include <string>
#include <atomic>
#include <random>
#include "../../src/network/virtual_transport.hpp"
#include "../../src/raft/node_impl.hpp"
#include "../../src/kv/kv_store.hpp"
#include "../integration/test_utils.hpp"

using namespace raft;
using namespace raft::test;
void stress_test() {
    std::cout << "\n=== Нагрузочное тестирование ===\n";

    // Запуск кластера (как выше)
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
    if (!leader) { std::cerr << "No leader!"; return; }

    const int THREADS = 10;
    const int OPS_PER_THREAD = 1000;
    std::atomic<long> total_ops{ 0 };
    std::atomic<long> failed{ 0 };
    auto start = std::chrono::steady_clock::now();

    auto worker = [&](int tid) {
        std::mt19937 rng(tid);
        std::uniform_int_distribution<int> key_dist(0, 100);
        std::uniform_int_distribution<int> op_dist(0, 1); // 0=PUT, 1=GET
        std::string result;
        for (int i = 0; i < OPS_PER_THREAD; ++i) {
            int key = key_dist(rng);
            std::string k = "key" + std::to_string(key);
            if (op_dist(rng) == 0) {
                std::string v = "val" + std::to_string(i);
                if (!leader->propose(create_put_command(k, v), result))
                    failed++;
            }
            else {
                std::string dummy;
                if (!leader->query(create_get_command(k), dummy))
                    failed++; // GET может вернуть false, если нет ключа — это нормально, не считаем ошибкой
                // Считаем только реальные ошибки протокола
            }
            total_ops++;
        }
        };

    std::vector<std::thread> threads;
    for (int t = 0; t < THREADS; ++t)
        threads.emplace_back(worker, t);
    for (auto& t : threads) t.join();

    auto end = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(end - start).count();
    double throughput = total_ops / elapsed;

    std::cout << "Всего операций: " << total_ops << "\n";
    std::cout << "Ошибок: " << failed << "\n";
    std::cout << "Время: " << elapsed << " сек\n";
    std::cout << "Пропускная способность: " << throughput << " ops/sec\n";

    for (auto& n : nodes) n->stop();
    std::cout << "Нагрузочный тест завершён.\n";
}