#pragma once
#include <cstdint>
#include <iostream>
#include <string>
#include <nlohmann/json.hpp>

namespace kv {

    using json = nlohmann::json;

    enum class CommandType : uint8_t {
        PUT = 1,
        GET = 2,
        DELETE = 3
    };

    inline std::string command_type_to_string(CommandType type) {
        switch (type) {
        case CommandType::PUT: return "PUT";
        case CommandType::GET: return "GET";
        case CommandType::DELETE: return "DELETE";
        default: return "UNKNOWN";
        }
    }

    inline CommandType string_to_command_type(const std::string& str) {
        if (str == "PUT") return CommandType::PUT;
        if (str == "GET") return CommandType::GET;
        if (str == "DELETE") return CommandType::DELETE;
        return CommandType::GET; 
    }

    struct Command {
        CommandType type;
        std::string key;
        std::string value;
        uint64_t client_id;
        uint64_t request_id;

        Command() : type(CommandType::GET), client_id(0), request_id(0) {}

        Command(CommandType t, const std::string& k, const std::string& v = "",
            uint64_t cid = 0, uint64_t rid = 0)
            : type(t), key(k), value(v), client_id(cid), request_id(rid) {
        }

        std::string serialize() const {
            json j;
            j["type"] = command_type_to_string(type);
            j["key"] = key;

            if (!value.empty() || type == CommandType::PUT) {
                j["value"] = value;
            }

            j["client_id"] = client_id;
            j["request_id"] = request_id;
            return j.dump();
        }

        static bool deserialize(const std::string& data, Command& cmd) {
            try {
                json j = json::parse(data);

                if (!j.contains("type") || !j.contains("key") ||
                    !j.contains("client_id") || !j.contains("request_id")) {
                    return false;
                }

                cmd.type = string_to_command_type(j["type"]);
                cmd.key = j["key"];
                cmd.value = j.value("value", "");
                cmd.client_id = j["client_id"];
                cmd.request_id = j["request_id"];

                return true;
            }
            catch (const json::exception& e) {
                std::cerr << "[JSON Error] Failed to parse command: " << e.what()
                    << "\nData: " << data << std::endl;
                return false;
            }
        }

        bool is_valid() const {
            return !key.empty();
        }

        std::string to_string() const {
            return command_type_to_string(type) + " '" + key + "' = '" + value
                + "' (client:" + std::to_string(client_id)
                + ", req:" + std::to_string(request_id) + ")";
        }
    };

} 