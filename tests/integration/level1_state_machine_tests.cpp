#include "test_utils.hpp"
#include <cassert>

namespace raft {
    namespace test {

        void test_state_machine_and_log_integration() {
            std::cout << "\n LEVEL 1: Testing State Machine + Log Integration" << std::endl;


            auto state_machine = std::make_shared<kv::KvStateMachine>();
            std::string result;  

            std::cout << "Test 1.1: Applying commands to State Machine" << std::endl;

            state_machine->apply(create_put_command("test_key", "test_value"), result);

            std::string value;
            bool found = state_machine->query(create_get_command("test_key"), value);
            assert(found && value == "test_value");
            std::cout << " PUT command applied correctly" << std::endl;

            state_machine->apply(create_delete_command("test_key"), result);
            found = state_machine->query(create_get_command("test_key"), value);
            assert(!found);
            std::cout << " DELETE command applied correctly" << std::endl;

            std::cout << "Test 1.2: Multiple commands sequence" << std::endl;

            state_machine->apply(create_put_command("counter", "1"), result);
            state_machine->apply(create_put_command("counter", "2"), result);
            state_machine->apply(create_put_command("counter", "3"), result);

            state_machine->query(create_get_command("counter"), value);
            assert(value == "3");
            std::cout << " Last value overwrites previous ones" << std::endl;

            std::cout << "\nTest 1.3: Key independence" << std::endl;

            state_machine->apply(create_put_command("key1", "value1"), result);
            state_machine->apply(create_put_command("key2", "value2"), result);

            state_machine->query(create_get_command("key1"), value);
            assert(value == "value1");

            state_machine->query(create_get_command("key2"), value);
            assert(value == "value2");
            std::cout << " Keys are independent" << std::endl;

        }

    } 
} 