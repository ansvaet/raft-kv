#pragma once
#include "../../include/kv/store.hpp"
#include "state_machine.hpp"

namespace kv {

    class KvStoreImpl : public IKvStore {
    public:
        KvStoreImpl();

        bool put(const std::string& key, const std::string& value,
            uint64_t client_id, uint64_t request_id) override;
        bool get(const std::string& key, std::string& value) const override;
        bool remove(const std::string& key, std::string& old_value,
            uint64_t client_id, uint64_t request_id) override;

        bool apply(const std::string& command_data, std::string& result) override;
        bool query(const std::string& query_data, std::string& result) const override;

        void clear() override;
        size_t size() const override;

        void print_all() const override;
        std::vector<std::string> get_all_keys() const override;

    private:
        KvStateMachine state_machine_;
    };

    inline std::shared_ptr<IKvStore> create_kv_store() {
        return std::make_shared<KvStoreImpl>();
    }

}