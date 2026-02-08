#pragma once
#include "../../include/kv/store.hpp"
#include "../../include/raft/types.hpp"
#include <unordered_map>
#include <shared_mutex>
#include <vector>

namespace kv {

    class KvStateMachine : public raft::IStateMachine {
    public:
        KvStateMachine();

        bool apply(const std::string& command_data, std::string& result) override;
        bool query(const std::string& query_data, std::string& result) const override;
        void clear() override;

        bool put(const std::string& key, const std::string& value,
            uint64_t client_id, uint64_t request_id);
        bool get(const std::string& key, std::string& value) const;
        bool remove(const std::string& key, std::string& old_value,
            uint64_t client_id, uint64_t request_id);

        size_t size() const;
        void print_all() const;
        std::vector<std::string> get_all_keys() const;

    private:
        struct ClientState {
            uint64_t last_request_id;
            std::string last_response;
            bool has_last_response;

            ClientState() : last_request_id(0), has_last_response(false) {}
        };

        bool check_idempotency(uint64_t client_id, uint64_t request_id, std::string& result);
        void update_idempotency(uint64_t client_id, uint64_t request_id,
            const std::string& response);

        std::unordered_map<std::string, std::string> data_;
        std::unordered_map<uint64_t, ClientState> client_states_;

        mutable std::shared_mutex data_mutex_;
        mutable std::shared_mutex clients_mutex_;
    };

} 