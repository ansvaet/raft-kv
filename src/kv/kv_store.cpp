#include "kv_store.hpp"

namespace kv {

    KvStoreImpl::KvStoreImpl() {}

    bool KvStoreImpl::put(const std::string& key, const std::string& value,
        uint64_t client_id, uint64_t request_id) {
        return state_machine_.put(key, value, client_id, request_id);
    }

    bool KvStoreImpl::get(const std::string& key, std::string& value) const {
        return state_machine_.get(key, value);
    }

    bool KvStoreImpl::remove(const std::string& key, std::string& old_value,
        uint64_t client_id, uint64_t request_id) {
        return state_machine_.remove(key, old_value, client_id, request_id);
    }

    bool KvStoreImpl::apply(const std::string& command_data, std::string& result) {
        return state_machine_.apply(command_data, result);
    }

    bool KvStoreImpl::query(const std::string& query_data, std::string& result) const {
        return state_machine_.query(query_data, result);
    }

    void KvStoreImpl::clear() {
        state_machine_.clear();
    }

    size_t KvStoreImpl::size() const {
        return state_machine_.size();
    }

    void KvStoreImpl::print_all() const {
        state_machine_.print_all();
    }

    std::vector<std::string> KvStoreImpl::get_all_keys() const {
        return state_machine_.get_all_keys();
    }

}