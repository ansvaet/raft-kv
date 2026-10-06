#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

#include "raft/consensus.hpp"
#include "raft/log_manager.hpp"
#include "raft/serializer.hpp"
#include "raft/node.hpp"
#include "network/virtual_transport.hpp"
#include "kv/state_machine.hpp"
#include "kv/command.hpp"

using namespace raft;
using namespace std::chrono_literals;

namespace {

    // Таймаут выборов, который в рамках теста никогда не истечёт
    constexpr uint32_t kNeverTimeoutMs = 60'000;
    constexpr uint32_t kShortTimeoutMs = 30;

    struct TestNode {
        std::shared_ptr<LogManager> log;
        std::unique_ptr<ConsensusEngine> engine;
    };

    class ConsensusTest : public ::testing::Test {
    protected:
        std::shared_ptr<network::VirtualTransport> transport =
            std::make_shared<network::VirtualTransport>();
        std::shared_ptr<Serializer> serializer = std::make_shared<Serializer>();

        TestNode make_node(uint32_t id, uint32_t total_nodes = 3,
            uint32_t timeout_ms = kNeverTimeoutMs,
            std::shared_ptr<network::VirtualTransport> net = nullptr,
            uint32_t propose_timeout_ms = 5000) {
            if (!net) net = transport;
            for (uint32_t i = 0; i < total_nodes; ++i) net->register_node(i);

            ConsensusEngine::Config cfg;
            cfg.node_id = id;
            cfg.total_nodes = total_nodes;
            cfg.election_timeout_min = timeout_ms;
            cfg.election_timeout_max = timeout_ms;
            cfg.propose_timeout = propose_timeout_ms;

            TestNode node;
            node.log = std::make_shared<LogManager>();
            node.engine = std::make_unique<ConsensusEngine>(
                cfg, node.log, std::make_shared<kv::KvStateMachine>(), net, serializer);
            return node;
        }

        // Ждём истечения таймаута и делаем один tick
        static void tick_after_timeout(ConsensusEngine& engine) {
            std::this_thread::sleep_for(std::chrono::milliseconds(kShortTimeoutMs + 20));
            engine.tick();
        }

        void send_vote_request(ConsensusEngine& engine, uint64_t term, uint32_t candidate) {
            VoteRequest req(term, candidate, 0, 0);
            engine.handle_message("VoteRequest", serializer->serialize_vote_request(req), candidate);
        }

        void send_append_entries(ConsensusEngine& engine, uint64_t term, uint32_t leader,
            uint64_t prev_index, uint64_t prev_term, std::vector<LogEntry> entries) {
            AppendEntriesRequest req;
            req.term = term;
            req.leader_id = leader;
            req.prev_log_index = prev_index;
            req.prev_log_term = prev_term;
            req.leader_commit = 0;
            req.entries = std::move(entries);
            engine.handle_message("AppendEntries", serializer->serialize_append_entries(req), leader);
        }

        // Забирает из очереди узла `to` последний VoteResponse
        bool pop_vote_granted(uint32_t to, network::VirtualTransport& net) {
            network::VirtualMessage msg;
            bool granted = false;
            bool found = false;
            while (net.receive(to, msg)) {
                if (msg.type != "VoteResponse") continue;
                VoteResponse resp;
                EXPECT_TRUE(serializer->deserialize_vote_response(msg.data, resp));
                granted = resp.vote_granted;
                found = true;
            }
            EXPECT_TRUE(found) << "no VoteResponse sent to node " << to;
            return granted;
        }

        // Подтверждение от фолловера, что у него есть записи до match_index
        void ack(ConsensusEngine& leader, uint32_t from, uint64_t match_index) {
            AppendEntriesResponse resp(leader.get_current_term(), true, match_index);
            leader.handle_message("AppendEntriesResponse",
                serializer->serialize_append_entries_response(resp), from);
        }

        static std::string put(const std::string& key, const std::string& value) {
            return kv::Command(kv::CommandType::PUT, key, value).serialize();
        }

        static void drain(network::VirtualTransport& net, uint32_t node_id) {
            network::VirtualMessage msg;
            while (net.receive(node_id, msg)) {}
        }

        // Делает узел лидером: таймаут -> кандидат -> голоса от всех остальных
        void make_leader(TestNode& node, uint32_t total_nodes, network::VirtualTransport& net) {
            tick_after_timeout(*node.engine);
            ASSERT_EQ(node.engine->get_state(), NodeState::CANDIDATE);
            VoteResponse yes(node.engine->get_current_term(), true);
            for (uint32_t from = 1; from < total_nodes; ++from) {
                node.engine->handle_message("VoteResponse",
                    serializer->serialize_vote_response(yes), from);
            }
            ASSERT_TRUE(node.engine->is_leader());
            for (uint32_t i = 0; i < total_nodes; ++i) drain(net, i);
        }
    };

} // namespace

// Узел 0 голосует за себя; voted_for_ == 0 не должен означать «ещё не голосовал»
TEST_F(ConsensusTest, GrantsAtMostOneVotePerTermWhenFirstCandidateIsNodeZero) {
    auto node = make_node(1);

    send_vote_request(*node.engine, 1, 0);
    EXPECT_TRUE(pop_vote_granted(0, *transport));

    send_vote_request(*node.engine, 1, 2);
    EXPECT_FALSE(pop_vote_granted(2, *transport)) << "second vote granted in term 1";
}

TEST_F(ConsensusTest, CandidateDoesNotVoteForAnotherCandidateInSameTerm) {
    auto node = make_node(0, 3, kShortTimeoutMs);
    tick_after_timeout(*node.engine);
    ASSERT_EQ(node.engine->get_state(), NodeState::CANDIDATE);
    ASSERT_EQ(node.engine->get_current_term(), 1u);

    send_vote_request(*node.engine, 1, 1);
    EXPECT_FALSE(pop_vote_granted(1, *transport));
}

// Без нового term при повторных выборах split vote никогда не разрешается
TEST_F(ConsensusTest, CandidateStartsNewTermWhenElectionTimesOut) {
    auto node = make_node(0, 3, kShortTimeoutMs);

    tick_after_timeout(*node.engine);
    ASSERT_EQ(node.engine->get_state(), NodeState::CANDIDATE);
    EXPECT_EQ(node.engine->get_current_term(), 1u);

    tick_after_timeout(*node.engine);
    EXPECT_EQ(node.engine->get_state(), NodeState::CANDIDATE);
    EXPECT_EQ(node.engine->get_current_term(), 2u);
}

TEST_F(ConsensusTest, CandidateStepsDownOnAppendEntriesFromLeaderOfSameTerm) {
    auto node = make_node(1, 3, kShortTimeoutMs);
    tick_after_timeout(*node.engine);
    ASSERT_EQ(node.engine->get_state(), NodeState::CANDIDATE);

    send_append_entries(*node.engine, node.engine->get_current_term(), 2, 0, 0, {});

    EXPECT_EQ(node.engine->get_state(), NodeState::FOLLOWER);
    EXPECT_EQ(node.engine->get_leader_id(), 2u);
}

TEST_F(ConsensusTest, SingleNodeClusterElectsItself) {
    auto node = make_node(0, 1, kShortTimeoutMs);
    tick_after_timeout(*node.engine);
    EXPECT_TRUE(node.engine->is_leader());
}

// Запоздавший AppendEntries с уже имеющимися записями не должен обрезать лог
TEST_F(ConsensusTest, StaleAppendEntriesDoesNotTruncateMatchingEntries) {
    auto node = make_node(1);
    send_append_entries(*node.engine, 1, 0, 0, 0,
        { LogEntry(1, "a"), LogEntry(1, "b"), LogEntry(1, "c") });
    ASSERT_EQ(node.log->get_last_index(), 3u);

    send_append_entries(*node.engine, 1, 0, 0, 0, { LogEntry(1, "a") });

    EXPECT_EQ(node.log->get_last_index(), 3u);
}

TEST_F(ConsensusTest, AppendEntriesReplacesConflictingSuffix) {
    auto node = make_node(1);
    send_append_entries(*node.engine, 1, 0, 0, 0,
        { LogEntry(1, "a"), LogEntry(1, "b"), LogEntry(1, "c") });

    send_append_entries(*node.engine, 2, 2, 1, 1, { LogEntry(2, "x") });

    EXPECT_EQ(node.log->get_last_index(), 2u);
    EXPECT_EQ(node.log->get_last_term(), 2u);
    LogEntry entry;
    ASSERT_TRUE(node.log->get_entry(2, entry));
    EXPECT_EQ(entry.data, "x");
}

// Таймер heartbeat был static и делился между всеми узлами процесса
TEST_F(ConsensusTest, HeartbeatTimerIsPerNode) {
    auto net_a = std::make_shared<network::VirtualTransport>();
    auto net_b = std::make_shared<network::VirtualTransport>();
    auto a = make_node(0, 2, kShortTimeoutMs, net_a);
    auto b = make_node(0, 2, kShortTimeoutMs, net_b);
    make_leader(a, 2, *net_a);
    make_leader(b, 2, *net_b);

    std::this_thread::sleep_for(100ms);  // > heartbeat_interval
    a.engine->tick();
    b.engine->tick();

    EXPECT_GT(net_a->get_queue_size(1), 0u);
    EXPECT_GT(net_b->get_queue_size(1), 0u) << "node B heartbeat suppressed by node A";
}

TEST(RaftNodeTest, StoppedLeaderDoesNotReportLeadership) {
    auto transport = std::make_shared<network::VirtualTransport>();
    std::vector<std::unique_ptr<IRaftNode>> nodes;
    for (uint32_t i = 0; i < 3; ++i) {
        transport->register_node(i);
        nodes.push_back(create_raft_node(i, 3, transport, std::make_shared<kv::KvStateMachine>()));
    }
    for (auto& n : nodes) n->start();

    IRaftNode* leader = nullptr;
    for (int i = 0; i < 100 && !leader; ++i) {
        std::this_thread::sleep_for(50ms);
        for (auto& n : nodes) if (n->is_leader()) leader = n.get();
    }
    ASSERT_NE(leader, nullptr) << "no leader elected";

    leader->stop();
    EXPECT_FALSE(leader->is_leader());

    for (auto& n : nodes) n->stop();
}

// ------------------------------------------------------------------
// Асинхронный propose
// ------------------------------------------------------------------

TEST_F(ConsensusTest, ProposeOnFollowerFailsWithLeaderHint) {
    auto node = make_node(1);
    send_append_entries(*node.engine, 1, 2, 0, 0, {});

    std::optional<ProposeResult> got;
    node.engine->propose(put("k", "v"), [&](ProposeResult r) { got = std::move(r); });

    ASSERT_TRUE(got.has_value()) << "callback must run immediately on a follower";
    EXPECT_EQ(got->status, ProposeStatus::NOT_LEADER);
    EXPECT_EQ(got->leader_hint, 2u);
    EXPECT_EQ(node.log->get_last_index(), 0u);
}

TEST_F(ConsensusTest, ProposeCompletesAfterMajorityAckWithApplyResult) {
    auto node = make_node(0, 3, kShortTimeoutMs);
    make_leader(node, 3, *transport);

    std::optional<ProposeResult> got;
    node.engine->propose(put("k", "v"), [&](ProposeResult r) { got = std::move(r); });
    node.engine->tick();  // рассылает AppendEntries
    EXPECT_GT(transport->get_queue_size(1), 0u);
    EXPECT_FALSE(got.has_value()) << "must not complete before a majority has the entry";

    ack(*node.engine, 1, 1);

    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got->status, ProposeStatus::OK);
    EXPECT_TRUE(got->applied);
    EXPECT_EQ(got->result, "v");
}

// Раньше propose возвращал успех, как только commit_index доходил до индекса,
// даже если по этому индексу закоммичена чужая запись
TEST_F(ConsensusTest, ProposeReportsLeadershipLostWhenEntryIsOverwritten) {
    auto node = make_node(0, 3, kShortTimeoutMs);
    make_leader(node, 3, *transport);
    uint64_t old_term = node.engine->get_current_term();

    std::optional<ProposeResult> got;
    node.engine->propose(put("k", "mine"), [&](ProposeResult r) { got = std::move(r); });
    node.engine->tick();

    // Новый лидер term+1 перезаписывает индекс 1 своей записью и коммитит её
    AppendEntriesRequest req;
    req.term = old_term + 1;
    req.leader_id = 2;
    req.leader_commit = 1;
    req.entries = { LogEntry(old_term + 1, put("k", "theirs")) };
    node.engine->handle_message("AppendEntries", serializer->serialize_append_entries(req), 2);
    node.engine->tick();

    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got->status, ProposeStatus::LEADERSHIP_LOST);
    EXPECT_EQ(got->leader_hint, 2u);
}

TEST_F(ConsensusTest, ProposeTimesOutWithoutMajority) {
    auto node = make_node(0, 3, kShortTimeoutMs, transport, /*propose_timeout_ms=*/50);
    make_leader(node, 3, *transport);

    std::optional<ProposeResult> got;
    node.engine->propose(put("k", "v"), [&](ProposeResult r) { got = std::move(r); });
    node.engine->tick();
    EXPECT_FALSE(got.has_value());

    std::this_thread::sleep_for(80ms);
    node.engine->tick();

    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got->status, ProposeStatus::TIMEOUT);
}

TEST_F(ConsensusTest, ProposalsInOneIterationShareOneAppendEntries) {
    auto node = make_node(0, 3, kShortTimeoutMs);
    make_leader(node, 3, *transport);

    for (int i = 0; i < 5; ++i) {
        node.engine->propose(put("k" + std::to_string(i), "v"), [](ProposeResult) {});
    }
    node.engine->tick();

    EXPECT_EQ(transport->get_queue_size(1), 1u);
    EXPECT_EQ(node.log->get_last_index(), 5u);
}

namespace {

    std::string put_command_for_test(int i) {
        return kv::Command(kv::CommandType::PUT, "key" + std::to_string(i), "v").serialize();
    }

    struct Cluster {
        std::shared_ptr<network::VirtualTransport> transport =
            std::make_shared<network::VirtualTransport>();
        std::vector<std::unique_ptr<IRaftNode>> nodes;

        explicit Cluster(uint32_t n) {
            for (uint32_t i = 0; i < n; ++i) {
                transport->register_node(i);
                nodes.push_back(create_raft_node(i, n, transport,
                    std::make_shared<kv::KvStateMachine>()));
            }
            for (auto& node : nodes) node->start();
        }
        ~Cluster() { for (auto& node : nodes) node->stop(); }

        IRaftNode* wait_for_leader() {
            for (int i = 0; i < 100; ++i) {
                for (auto& n : nodes) if (n->is_leader()) return n.get();
                std::this_thread::sleep_for(50ms);
            }
            return nullptr;
        }
    };

} // namespace

TEST(RaftNodeTest, ConcurrentProposalsAllCommitExactlyOnce) {
    Cluster cluster(3);
    IRaftNode* leader = cluster.wait_for_leader();
    ASSERT_NE(leader, nullptr);

    constexpr int kThreads = 8;
    constexpr int kPerThread = 50;
    std::atomic<int> ok{ 0 };
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t] {
            for (int i = 0; i < kPerThread; ++i) {
                std::string result;
                if (leader->propose(put_command_for_test(t * kPerThread + i), result)) ok++;
            }
        });
    }
    for (auto& th : threads) th.join();

    EXPECT_EQ(ok.load(), kThreads * kPerThread);
    EXPECT_EQ(leader->get_commit_index(), static_cast<uint64_t>(kThreads * kPerThread));
}

// Блокирующий propose раньше ждал коммита циклом со sleep(50ms)
TEST(RaftNodeTest, SequentialProposeLatencyIsLow) {
    Cluster cluster(3);
    IRaftNode* leader = cluster.wait_for_leader();
    ASSERT_NE(leader, nullptr);

    constexpr int kOps = 100;
    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < kOps; ++i) {
        std::string result;
        ASSERT_TRUE(leader->propose(put_command_for_test(i), result));
    }
    auto per_op = (std::chrono::steady_clock::now() - start) / kOps;

    // При опросе транспорта раз в 1 мс коммит занимает единицы миллисекунд;
    // порог с запасом для медленных CI и санитайзеров
    EXPECT_LT(per_op, 25ms);
}

TEST(RaftNodeTest, ProposeToStoppedNodeFailsImmediately) {
    Cluster cluster(3);
    IRaftNode* leader = cluster.wait_for_leader();
    ASSERT_NE(leader, nullptr);
    leader->stop();

    std::optional<ProposeResult> got;
    leader->propose_async(put_command_for_test(0), [&](ProposeResult r) { got = std::move(r); });

    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got->status, ProposeStatus::STOPPED);
}

TEST(RaftNodeTest, StopCompletesPendingProposals) {
    // Кластер из 3 узлов, но запущен только будущий лидер: большинства нет,
    // и предложение висит до остановки
    auto transport = std::make_shared<network::VirtualTransport>();
    for (uint32_t i = 0; i < 3; ++i) transport->register_node(i);
    auto node = create_raft_node(0, 3, transport, std::make_shared<kv::KvStateMachine>());
    node->start();

    // Делаем узел лидером, отвечая за узел 1 на его VoteRequest
    Serializer serializer;
    for (int i = 0; i < 100 && !node->is_leader(); ++i) {
        std::this_thread::sleep_for(20ms);
        network::VirtualMessage msg;
        while (transport->receive(1, msg)) {
            if (msg.type != "VoteRequest") continue;
            VoteRequest req;
            ASSERT_TRUE(serializer.deserialize_vote_request(msg.data, req));
            transport->send(network::VirtualMessage(1, 0, "VoteResponse",
                serializer.serialize_vote_response(VoteResponse(req.term, true))));
        }
    }
    ASSERT_TRUE(node->is_leader());

    std::promise<ProposeResult> done;
    auto future = done.get_future();
    node->propose_async(put_command_for_test(0), [&](ProposeResult r) { done.set_value(std::move(r)); });
    std::this_thread::sleep_for(50ms);
    node->stop();

    ASSERT_EQ(future.wait_for(1s), std::future_status::ready) << "pending proposal was lost";
    EXPECT_EQ(future.get().status, ProposeStatus::STOPPED);
}
