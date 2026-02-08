#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
#include <vector>
#include <sstream>
#include <iomanip>

#include "../include/raft/types.hpp"
#include "network/virtual_transport.hpp"
#include "raft/node_impl.hpp"
#include "kv/kv_store.hpp"

using namespace raft;
using namespace std::chrono;
using json = nlohmann::json;

class InteractiveCLI {
private:
    std::vector<std::shared_ptr<IRaftNode>> nodes_;
    std::vector<std::shared_ptr<IStateMachine>> state_machines_;
    std::shared_ptr<network::VirtualTransport> transport_;
    bool running_ = true;
    uint64_t request_counter_ = 1;

public:
    InteractiveCLI(int total_nodes = 3) {
        transport_ = std::make_shared<network::VirtualTransport>();

        for (int i = 0; i < total_nodes; i++) {
            transport_->register_node(i);

            auto state_machine = std::make_shared<kv::KvStateMachine>();
            state_machines_.push_back(state_machine);

            auto node = create_raft_node(i, total_nodes, transport_, state_machine);
            nodes_.push_back(std::move(node));
        }
    }

    void start_cluster() {
        std::cout << "\n Starting Raft cluster with " << nodes_.size() << " nodes..." << std::endl;

        for (auto& node : nodes_) {
            node->start();
        }

        std::this_thread::sleep_for(std::chrono::seconds(2));
        std::cout << "Cluster started!" << std::endl;
        print_status();
    }

    void stop_cluster() {
        std::cout << "\nStopping cluster..." << std::endl;
        for (auto& node : nodes_) {
            node->stop();
        }
        std::cout << "Cluster stopped!" << std::endl;
    }

    void print_status() {
        std::cout << "\nCLUSTER STATUS:" << std::endl;
        std::cout << "------------------------" << std::endl;

        for (size_t i = 0; i < nodes_.size(); i++) {
            std::string state;
            switch (nodes_[i]->get_state()) {
            case NodeState::FOLLOWER: state = "FOLLOWER"; break;
            case NodeState::CANDIDATE: state = "CANDIDATE"; break;
            case NodeState::LEADER: state = "LEADER"; break;
            }

            std::cout << "Node " << i << ": "
                << std::left << std::setw(10) << state
                << " Term: " << std::setw(3) << nodes_[i]->get_current_term()
                << " Commit: " << nodes_[i]->get_commit_index();

            if (nodes_[i]->is_leader()) {
                std::cout << " - LEADER";
            }
            std::cout << std::endl;
        }
    }

    void print_kv_store() {
        std::cout << "\n KV STORE CONTENTS:" << std::endl;
        std::cout << "-----------------------" << std::endl;

        for (size_t i = 0; i < state_machines_.size(); i++) {
            if (auto kv_sm = dynamic_cast<kv::KvStateMachine*>(state_machines_[i].get())) {
                std::cout << "Node " << i << ":" << std::endl;
                kv_sm->print_all();
            }
        }
    }

    std::string create_put_command(const std::string& key, const std::string& value, uint64_t client_id = 1000) {
        json j;
        j["type"] = "PUT";
        j["key"] = key;
        j["value"] = value;
        j["client_id"] = client_id;
        j["request_id"] = request_counter_++;
        return j.dump();
    }

    std::string create_get_command(const std::string& key, uint64_t client_id = 1000) {
        json j;
        j["type"] = "GET";
        j["key"] = key;
        j["client_id"] = client_id;
        j["request_id"] = request_counter_++;
        return j.dump();
    }

    std::string create_delete_command(const std::string& key, uint64_t client_id = 1000) {
        json j;
        j["type"] = "DELETE";
        j["key"] = key;
        j["client_id"] = client_id;
        j["request_id"] = request_counter_++;
        return j.dump();
    }

    void send_command(const std::string& cmd_line) {

        std::shared_ptr<IRaftNode> leader;
        for (auto& node : nodes_) {
            if (node->is_leader()) {
                leader = node;
                break;
            }
        }

        if (!leader) {
            std::cout << "No leader elected yet!" << std::endl;
            return;
        }

        std::istringstream iss(cmd_line);
        std::string cmd, key, value;
        iss >> cmd >> key;

        std::string command_json;
        std::string result;
        bool success = false;

        if (cmd == "get") {
            command_json = create_get_command(key);
            success = leader->query(command_json, result);
            if (success && !result.empty()) {
                std::cout << "" << key << " = " << result << std::endl;
            }
            else {
                std::cout << "Key not found: " << key << std::endl;
            }
        }
        else if (cmd == "put" || cmd == "set") {
            iss >> value;
            if (value.empty()) {
                std::cout << "Usage: put <key> <value>" << std::endl;
                return;
            }
            command_json = create_put_command(key, value);
            success = leader->propose(command_json, result);
            if (success) {
                std::cout << "PUT " << key << " = " << value << std::endl;
            }
            else {
                std::cout << "PUT failed: " << result << std::endl;
            }
        }
        else if (cmd == "del" || cmd == "delete") {
            command_json = create_delete_command(key);
            success = leader->propose(command_json, result);
            if (success) {
                if (result.empty()) {
                    std::cout << "Key deleted: " << key << std::endl;
                }
                else {
                    std::cout << "DELETED " << key << " = " << result << std::endl;
                }
            }
            else {
                std::cout << "DELETE failed" << std::endl;
            }
        }
        else {
            std::cout << "Unknown command. Use: put/get/del" << std::endl;
        }
    }

    void kill_leader() {
        for (size_t i = 0; i < nodes_.size(); i++) {
            if (nodes_[i]->is_leader()) {
                std::cout << "\nKilling leader (node " << i << ")..." << std::endl;
                nodes_[i]->stop();

                std::this_thread::sleep_for(std::chrono::seconds(3));

                nodes_[i]->start();
                std::cout << "Node " << i << " restarted" << std::endl;

                std::this_thread::sleep_for(std::chrono::seconds(2));
                return;
            }
        }
        std::cout << "No leader found to kill!" << std::endl;
    }

    void auto_test() {
        std::cout << "\nRunning auto-test..." << std::endl;

        std::vector<std::string> test_commands = {
            "put name Alice",
            "put age 25",
            "put city Moscow",
            "get name",
            "get age",
            "del city",
            "get city" 
        };

        for (const auto& cmd : test_commands) {
            std::cout << "\n> " << cmd << std::endl;
            send_command(cmd);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        std::cout << "\nAuto-test completed!" << std::endl;
        print_kv_store();
    }

    void run() {

        std::cout << " RAFT KV STORE -  CLI" << std::endl;

        start_cluster();

        std::cout << "\nRun demo-test? (y/n): ";
        std::string answer;
        std::getline(std::cin, answer);
        if (answer == "y" || answer == "Y") {
            auto_test();
        }

        while (running_) {
            std::cout << "\n Commands: status | kv | put <k> <v> | get <k> | del <k> | kill | test | help | exit" << std::endl;
            std::cout << "> ";

            std::string input;
            if (!std::getline(std::cin, input)) break;

            if (input.empty()) continue;

            if (input == "exit" || input == "quit") {
                running_ = false;
            }
            else if (input == "status") {
                print_status();
            }
            else if (input == "kv") {
                print_kv_store();
            }
            else if (input == "kill") {
                kill_leader();
            }
            else if (input == "test") {
                auto_test();
            }
            else if (input == "help") {
                print_help();
            }
            else if (input.substr(0, 3) == "put" ||
                input.substr(0, 3) == "get" ||
                input.substr(0, 3) == "del") {
                send_command(input);
            }
            else if (input.substr(0, 3) == "set") {
                std::string put_cmd = "put" + input.substr(3);
                send_command(put_cmd);
            }
            else {
                std::cout << "Unknown command. Type 'help' for commands." << std::endl;
            }
        }

        stop_cluster();
    }

    void print_help() {
        std::cout << "\nAVAILABLE COMMANDS:" << std::endl;
        std::cout << "------------------------" << std::endl;
        std::cout << "status      - Show cluster status" << std::endl;
        std::cout << "kv          - Show KV store contents" << std::endl;
        std::cout << "put k v     - Put key-value (JSON format)" << std::endl;
        std::cout << "get k       - Get value by key" << std::endl;
        std::cout << "del k       - Delete key" << std::endl;
        std::cout << "kill        - Kill current leader" << std::endl;
        std::cout << "test        - Run auto-test sequence" << std::endl;
        std::cout << "help        - Show this help" << std::endl;
        std::cout << "exit        - Exit program" << std::endl;
    }
};

int main() {
    try {
        InteractiveCLI cli(3);
        cli.run();
    }
    catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}