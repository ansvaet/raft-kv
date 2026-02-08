#include "state_machine.hpp"
#include "../../include/kv/command.hpp"
#include <iostream>

namespace kv {

    KvStateMachine::KvStateMachine() {}

    bool KvStateMachine::apply(const std::string& command_data, std::string& result) {
        Command cmd;
        if (!Command::deserialize(command_data, cmd)) {
            std::cerr << "[KvStateMachine] Failed to deserialize command" << std::endl;
            return false;
        }

        if (!cmd.is_valid()) {
            std::cerr << "[KvStateMachine] Invalid command" << std::endl;
            return false;
        }

        if (cmd.client_id != 0) {
            if (check_idempotency(cmd.client_id, cmd.request_id, result)) {
            /*    std::cout << "[KvStateMachine] Idempotent request from client "
                    << cmd.client_id << std::endl;*/
                return true;
            }
        }

        bool success = false;

        switch (cmd.type) {
        case CommandType::PUT: {
            std::unique_lock lock(data_mutex_);
            data_[cmd.key] = cmd.value;
            result = cmd.value;
            success = true;
            //std::cout << "[KvStateMachine] PUT: " << cmd.key << " = " << cmd.value << std::endl;
            break;
        }
        case CommandType::GET: {
            std::shared_lock lock(data_mutex_);
            auto it = data_.find(cmd.key);
            if (it != data_.end()) {
                result = it->second;
                success = true;
            }
            break;
        }
        case CommandType::DELETE: {
            std::unique_lock lock(data_mutex_);
            auto it = data_.find(cmd.key);
            if (it != data_.end()) {
                result = it->second;
                data_.erase(it);
                success = true;
            }
            break;
        }
        }

        if (success && cmd.client_id != 0) {
            update_idempotency(cmd.client_id, cmd.request_id, result);
        }

        return success;
    }

    bool KvStateMachine::query(const std::string& query_data, std::string& result) const {
        Command cmd;
        if (!Command::deserialize(query_data, cmd)) {
            return false;
        }

        if (cmd.type != CommandType::GET) {
            return false;
        }

        std::shared_lock lock(data_mutex_);
        auto it = data_.find(cmd.key);
        if (it != data_.end()) {
            result = it->second;
            return true;
        }

        return false;
    }

    void KvStateMachine::clear() {
        std::unique_lock lock1(data_mutex_, std::defer_lock);
        std::unique_lock lock2(clients_mutex_, std::defer_lock);
        std::lock(lock1, lock2);

        data_.clear();
        client_states_.clear();
    }

    bool KvStateMachine::put(const std::string& key, const std::string& value,
        uint64_t client_id, uint64_t request_id) {
        Command cmd(CommandType::PUT, key, value, client_id, request_id);
        std::string result;
        return apply(cmd.serialize(), result);
    }

    bool KvStateMachine::get(const std::string& key, std::string& value) const {
        std::shared_lock lock(data_mutex_);
        auto it = data_.find(key);
        if (it != data_.end()) {
            value = it->second;
            return true;
        }
        return false;
    }

    bool KvStateMachine::remove(const std::string& key, std::string& old_value,
        uint64_t client_id, uint64_t request_id) {
        Command cmd(CommandType::DELETE, key, "", client_id, request_id);
        return apply(cmd.serialize(), old_value);
    }

    size_t KvStateMachine::size() const {
        std::shared_lock lock(data_mutex_);
        return data_.size();
    }

    void KvStateMachine::print_all() const {
        std::shared_lock lock(data_mutex_);

        std::cout << "\n KV Store (" << data_.size() << " items)" << std::endl;
        for (const auto& [key, value] : data_) {
            std::cout << "  " << key << " = " << value << std::endl;
        }
    }

    std::vector<std::string> KvStateMachine::get_all_keys() const {
        std::shared_lock lock(data_mutex_);
        std::vector<std::string> keys;
        keys.reserve(data_.size());

        for (const auto& [key, _] : data_) {
            keys.push_back(key);
        }

        return keys;
    }

    bool KvStateMachine::check_idempotency(uint64_t client_id, uint64_t request_id, std::string& result) {
        std::shared_lock lock(clients_mutex_);

        auto it = client_states_.find(client_id);
        if (it != client_states_.end() && it->second.last_request_id == request_id) {
            if (it->second.has_last_response) {
                result = it->second.last_response;
                return true;
            }
        }

        return false;
    }

    void KvStateMachine::update_idempotency(uint64_t client_id, uint64_t request_id,
        const std::string& response) {
        std::unique_lock lock(clients_mutex_);

        auto& state = client_states_[client_id];
        state.last_request_id = request_id;
        state.last_response = response;
        state.has_last_response = true;
    }

} 