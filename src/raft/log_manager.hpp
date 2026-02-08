#pragma once
#include "../../include/raft/types.hpp"
#include <vector>
#include <mutex>
#include <cstdint>

namespace raft {

    class LogManager {
    public:
        LogManager();

        // Basic operations
        void append(const LogEntry& entry);
        bool get_entry(uint64_t index, LogEntry& entry) const;
        uint64_t get_last_index() const;
        uint64_t get_last_term() const;

        // Replication
        bool get_entries_from(uint64_t start_index, std::vector<LogEntry>& entries) const;
        void truncate_from(uint64_t start_index);

        // Consistency check
        bool check_consistency(uint64_t prev_log_index, uint64_t prev_log_term) const;

        // Stats
        size_t size() const;
        void clear();

    private:
        mutable std::mutex mutex_;
        std::vector<LogEntry> entries_;

        // Index 0 is dummy entry (term 0)
        static const LogEntry DUMMY_ENTRY;
    };

}