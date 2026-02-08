#include "serializer.hpp"
#include <nlohmann/json.hpp>
#include <iostream>

namespace raft {

    using json = nlohmann::json;

    std::string Serializer::serialize_vote_request(const VoteRequest& req) {
        json j;
        j["term"] = req.term;
        j["candidate_id"] = req.candidate_id;
        j["last_log_index"] = req.last_log_index;
        j["last_log_term"] = req.last_log_term;
        return j.dump();
    }

    bool Serializer::deserialize_vote_request(const std::string& data, VoteRequest& req) {
        try {
            json j = json::parse(data);
            req.term = j["term"];
            req.candidate_id = j["candidate_id"];
            req.last_log_index = j["last_log_index"];
            req.last_log_term = j["last_log_term"];
            return true;
        }
        catch (const json::exception& e) {
            std::cerr << "[JSON Error] Failed to parse VoteRequest: " << e.what() << std::endl;
            return false;
        }
    }

    std::string Serializer::serialize_vote_response(const VoteResponse& resp) {
        json j;
        j["term"] = resp.term;
        j["vote_granted"] = resp.vote_granted;
        return j.dump();
    }

    bool Serializer::deserialize_vote_response(const std::string& data, VoteResponse& resp) {
        try {
            json j = json::parse(data);
            resp.term = j["term"];
            resp.vote_granted = j["vote_granted"];
            return true;
        }
        catch (const json::exception& e) {
            std::cerr << "[JSON Error] Failed to parse VoteResponse: " << e.what() << std::endl;
            return false;
        }
    }

    std::string Serializer::serialize_append_entries(const AppendEntriesRequest& req) {
        json j;
        j["term"] = req.term;
        j["leader_id"] = req.leader_id;
        j["prev_log_index"] = req.prev_log_index;
        j["prev_log_term"] = req.prev_log_term;
        j["leader_commit"] = req.leader_commit;

        json entries_array = json::array();
        for (const auto& entry : req.entries) {
            json entry_json;
            entry_json["term"] = entry.term;
            entry_json["data"] = entry.data; 
            entries_array.push_back(entry_json);
        }
        j["entries"] = entries_array;

        return j.dump();
    }

    bool Serializer::deserialize_append_entries(const std::string& data, AppendEntriesRequest& req) {
        try {
            json j = json::parse(data);
            req.term = j["term"];
            req.leader_id = j["leader_id"];
            req.prev_log_index = j["prev_log_index"];
            req.prev_log_term = j["prev_log_term"];
            req.leader_commit = j["leader_commit"];

            // Десериализуем entries
            req.entries.clear();
            if (j.contains("entries") && j["entries"].is_array()) {
                for (const auto& entry_json : j["entries"]) {
                    LogEntry entry;
                    entry.term = entry_json["term"];
                    entry.data = entry_json["data"];
                    req.entries.push_back(entry);
                }
            }

            return true;
        }
        catch (const json::exception& e) {
            std::cerr << "[JSON Error] Failed to parse AppendEntries: " << e.what() << std::endl;
            return false;
        }
    }

    std::string Serializer::serialize_append_entries_response(const AppendEntriesResponse& resp) {
        json j;
        j["term"] = resp.term;
        j["success"] = resp.success;
        j["match_index"] = resp.match_index;
        return j.dump();
    }

    bool Serializer::deserialize_append_entries_response(const std::string& data, AppendEntriesResponse& resp) {
        try {
            json j = json::parse(data);
            resp.term = j["term"];
            resp.success = j["success"];
            resp.match_index = j["match_index"];
            return true;
        }
        catch (const json::exception& e) {
            std::cerr << "[JSON Error] Failed to parse AppendEntriesResponse: " << e.what() << std::endl;
            return false;
        }
    }


} 