#pragma once
#include <cstdint>
#include <string>

namespace raft {

    enum class NodeState {
        FOLLOWER,
        CANDIDATE,
        LEADER
    };


    struct LogEntry {
        uint64_t term;          
        std::string command;   

        LogEntry(uint64_t t, const std::string& cmd)
            : term(t), command(cmd) {
        }
    };

   
    struct VoteRequest {
        uint64_t term;           
        uint32_t candidate_id;   
        uint64_t last_log_index; 
        uint64_t last_log_term; 
    };

    struct VoteResponse {
        uint64_t term;          // срок получателя 
        bool vote_granted;      // true = голос отдан
    };

    struct AppendEntriesRequest {
        uint64_t term;
        uint32_t leader_id;
        uint64_t prev_log_index;
        uint64_t prev_log_term;
       
    };

    struct AppendEntriesResponse {
        uint64_t term;
        bool success;
    };

} 