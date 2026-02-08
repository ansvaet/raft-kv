#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <functional>

namespace raft {

    namespace network {
        struct VirtualMessage {
            uint32_t from_id;
            uint32_t to_id;
            std::string type;
            std::string data;

            VirtualMessage() = default;
            VirtualMessage(uint32_t from, uint32_t to, const std::string& t, const std::string& d)
                : from_id(from), to_id(to), type(t), data(d) {
            }
        };

    }

    enum class NodeState {
        FOLLOWER,
        CANDIDATE,
        LEADER
    };

    enum class MessageType {
        VOTE_REQUEST,
        VOTE_RESPONSE,
        APPEND_ENTRIES,
        APPEND_ENTRIES_RESPONSE
    };

    struct LogEntry {
        uint64_t term;
        std::string data;

        LogEntry() : term(0) {}
        LogEntry(uint64_t t, const std::string& d) : term(t), data(d) {}
    };

    struct VoteRequest {
        uint64_t term;
        uint32_t candidate_id;
        uint64_t last_log_index;
        uint64_t last_log_term;

        VoteRequest() : term(0), candidate_id(0), last_log_index(0), last_log_term(0) {}
        VoteRequest(uint64_t t, uint32_t cid, uint64_t lli, uint64_t llt)
            : term(t), candidate_id(cid), last_log_index(lli), last_log_term(llt) {
        }
    };

    struct VoteResponse {
        uint64_t term;
        bool vote_granted;

        VoteResponse() : term(0), vote_granted(false) {}
        VoteResponse(uint64_t t, bool granted) : term(t), vote_granted(granted) {}
    };

    struct AppendEntriesRequest {
        uint64_t term;
        uint32_t leader_id;
        uint64_t prev_log_index;
        uint64_t prev_log_term;
        uint64_t leader_commit;
        std::vector<LogEntry> entries;

        AppendEntriesRequest() : term(0), leader_id(0), prev_log_index(0),
            prev_log_term(0), leader_commit(0) {
        }
    };

    struct AppendEntriesResponse {
        uint64_t term;
        bool success;
        uint64_t match_index;

        AppendEntriesResponse() : term(0), success(false), match_index(0) {}
        AppendEntriesResponse(uint64_t t, bool s, uint64_t mi)
            : term(t), success(s), match_index(mi) {
        }
    };

    // Callback интерфейсы
    class IStateMachine {
    public:
        virtual ~IStateMachine() = default;
        virtual bool apply(const std::string& command_data, std::string& result) = 0;
        virtual bool query(const std::string& query_data, std::string& result) const = 0;
        virtual void clear() = 0;
    };

    class INetworkTransport {
    public:
        virtual ~INetworkTransport() = default;

        virtual bool send(const network::VirtualMessage& msg) = 0;

        virtual bool receive(uint32_t node_id, network::VirtualMessage& msg) = 0;

        virtual void register_node(uint32_t node_id) = 0;
    };

}