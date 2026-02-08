#include "log_manager.hpp"

namespace raft {

    const LogEntry LogManager::DUMMY_ENTRY(0, "");

    LogManager::LogManager() {
        entries_.push_back(DUMMY_ENTRY);  // Index 0 is dummy
    }

    void LogManager::append(const LogEntry& entry) {
        std::lock_guard<std::mutex> lock(mutex_);
        entries_.push_back(entry);
    }

    bool LogManager::get_entry(uint64_t index, LogEntry& entry) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (index == 0 || index >= entries_.size()) {
            return false;
        }
        entry = entries_[index];
        return true;
    }

    uint64_t LogManager::get_last_index() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return entries_.size() - 1;
    }

    uint64_t LogManager::get_last_term() const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (entries_.size() <= 1) return 0;
        return entries_.back().term;
    }

    bool LogManager::get_entries_from(uint64_t start_index, std::vector<LogEntry>& entries) const {
        std::lock_guard<std::mutex> lock(mutex_);

        if (start_index >= entries_.size()) {
            return false;
        }

        entries.assign(entries_.begin() + start_index, entries_.end());
        return true;
    }

    void LogManager::truncate_from(uint64_t start_index) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (start_index < entries_.size()) {
            entries_.resize(start_index);
        }
    }

    bool LogManager::check_consistency(uint64_t prev_log_index, uint64_t prev_log_term) const {
        std::lock_guard<std::mutex> lock(mutex_);

        if (prev_log_index == 0) {
            return true;
        }

        if (prev_log_index >= entries_.size()) {
            return false;
        }

        return entries_[prev_log_index].term == prev_log_term;
    }

    size_t LogManager::size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return entries_.size() - 1;  // Exclude dummy entry
    }

    void LogManager::clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        entries_.clear();
        entries_.push_back(DUMMY_ENTRY);
    }

} 