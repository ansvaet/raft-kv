#include "test_utils.hpp"


namespace raft {
    namespace test {
        void test_state_machine_and_log_integration();
        void test_single_node_raft_integration();
        void test_cluster_election();
        void test_log_replication();
        void test_leader_failure();
    } 
} 



void print_test_summary(int passed, int total) {
    std::cout << " TEST SUMMARY: " << passed << "/" << total << " passed" << std::endl;
}

int main() {

    int passed = 0;
    int total = 5;

    try {

        raft::test::test_state_machine_and_log_integration();
        passed++;

        raft::test::test_single_node_raft_integration();
        passed++;

        raft::test::test_cluster_election();
        passed++;

        raft::test::test_log_replication();
        passed++;

        raft::test::test_leader_failure();
        passed++;

        print_test_summary(passed, total);
        return 0;

    }
    catch (const std::exception& e) {
        std::cerr << "\nTest failed with exception: " << e.what() << std::endl;
        print_test_summary(passed, total);
        return 1;
    }
}