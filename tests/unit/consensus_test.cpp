#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <thread>
#include <vector>

#include "raft/consensus.hpp"
#include "raft/log_manager.hpp"
#include "raft/serializer.hpp"
#include "raft/node.hpp"
#include "network/virtual_transport.hpp"
#include "kv/state_machine.hpp"

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
            std::shared_ptr<network::VirtualTransport> net = nullptr) {
            if (!net) net = transport;
            for (uint32_t i = 0; i < total_nodes; ++i) net->register_node(i);

            ConsensusEngine::Config cfg;
            cfg.node_id = id;
            cfg.total_nodes = total_nodes;
            cfg.election_timeout_min = timeout_ms;
            cfg.election_timeout_max = timeout_ms;

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
