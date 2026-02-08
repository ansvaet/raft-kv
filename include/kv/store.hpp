#pragma once
#include "command.hpp"
#include <string>
#include <vector>

namespace kv {

    class IKvStore {
    public:
        virtual ~IKvStore() = default;

     
        virtual bool put(const std::string& key, const std::string& value,
            uint64_t client_id, uint64_t request_id) = 0;
        virtual bool get(const std::string& key, std::string& value) const = 0;
        virtual bool remove(const std::string& key, std::string& old_value,
            uint64_t client_id, uint64_t request_id) = 0;

        virtual bool apply(const std::string& command_data, std::string& result) = 0;
        virtual bool query(const std::string& query_data, std::string& result) const = 0;

       
        virtual void clear() = 0;
        virtual size_t size() const = 0;

        
        virtual void print_all() const = 0;
        virtual std::vector<std::string> get_all_keys() const = 0;
    };

    
    std::shared_ptr<IKvStore> create_kv_store();

} 