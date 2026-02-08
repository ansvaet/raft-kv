#pragma once
#include "../../include/raft/types.hpp"
#include <string>
#include <nlohmann/json.hpp>

namespace raft {

    class Serializer {
    public:
        // Vote messages
        std::string serialize_vote_request(const VoteRequest& req);
        bool deserialize_vote_request(const std::string& data, VoteRequest& req);

        std::string serialize_vote_response(const VoteResponse& resp);
        bool deserialize_vote_response(const std::string& data, VoteResponse& resp);

        // AppendEntries messages
        std::string serialize_append_entries(const AppendEntriesRequest& req);
        bool deserialize_append_entries(const std::string& data, AppendEntriesRequest& req);

        std::string serialize_append_entries_response(const AppendEntriesResponse& resp);
        bool deserialize_append_entries_response(const std::string& data, AppendEntriesResponse& resp);

    };

} 