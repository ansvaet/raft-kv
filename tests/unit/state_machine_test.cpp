#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include "../../src/kv/state_machine.hpp"
#include "../../include/kv/command.hpp"
#include "../integration/test_utils.hpp"

using namespace kv;
using namespace raft::test;

class KvStateMachineTest : public ::testing::Test {
protected:
    void SetUp() override {
        sm_ = std::make_unique<KvStateMachine>();
    }

    std::unique_ptr<KvStateMachine> sm_;
};


TEST_F(KvStateMachineTest, PutAndGetBasic) {
    std::string result;

    // PUT
    bool success = sm_->apply(create_put_command("key1", "value1"), result);
    EXPECT_TRUE(success);
    EXPECT_EQ(result, "value1");

    // GET
    std::string value;
    success = sm_->query(create_get_command("key1"), value);
    EXPECT_TRUE(success);
    EXPECT_EQ(value, "value1");
}


TEST_F(KvStateMachineTest, UpdateExistingKey) {
    std::string result;

    sm_->apply(create_put_command("counter", "10"), result);
    sm_->apply(create_put_command("counter", "20"), result);

    std::string value;
    sm_->query(create_get_command("counter"), value);
    EXPECT_EQ(value, "20");
}

TEST_F(KvStateMachineTest, DeleteKey) {
    std::string result;

    sm_->apply(create_put_command("temp", "data"), result);

    std::string old_value;
    bool success = sm_->apply(create_delete_command("temp"), old_value);
    EXPECT_TRUE(success);
    EXPECT_EQ(old_value, "data");

    std::string value;
    success = sm_->query(create_get_command("temp"), value);
    EXPECT_FALSE(success);
}


TEST_F(KvStateMachineTest, GetNonExistentKey) {
    std::string value;
    bool success = sm_->query(create_get_command("nonexistent"), value);
    EXPECT_FALSE(success);
}


TEST_F(KvStateMachineTest, IdempotentPut) {
    std::string result1, result2;
    uint64_t client_id = 100;
    uint64_t request_id = 1;

 
    bool success1 = sm_->apply(
        create_put_command_with_id("idempotent", "first", client_id, request_id),
        result1
    );
    EXPECT_TRUE(success1);
    EXPECT_EQ(result1, "first");

    bool success2 = sm_->apply(
        create_put_command_with_id("idempotent", "second", client_id, request_id),
        result2
    );
    EXPECT_TRUE(success2);
    EXPECT_EQ(result2, "first");  
}


TEST_F(KvStateMachineTest, ConcurrentReads) {

    std::string result;
    for (int i = 0; i < 100; i++) {
        sm_->apply(create_put_command("key" + std::to_string(i), "value" + std::to_string(i)), result);
    }

    std::atomic<int> success_count{ 0 };
    std::vector<std::thread> threads;

    for (int t = 0; t < 10; t++) {
        threads.emplace_back([this, t, &success_count]() {
            for (int i = 0; i < 10; i++) {
                int key_num = t * 10 + i;
                std::string value;
                if (sm_->query(create_get_command("key" + std::to_string(key_num)), value)) {
                    success_count++;
                }
            }
            });
    }

    for (auto& th : threads) {
        th.join();
    }

    EXPECT_EQ(success_count, 100); 
}

TEST_F(KvStateMachineTest, ConcurrentReadsAndWrites) {
    std::atomic<int> write_count{ 0 };
    std::atomic<int> read_count{ 0 };
    std::vector<std::thread> threads;

    for (int w = 0; w < 5; w++) {
        threads.emplace_back([this, w, &write_count]() {
            for (int i = 0; i < 50; i++) {
                std::string result;
                std::string key = "shared_" + std::to_string(i % 10);
                std::string value = "writer_" + std::to_string(w) + "_" + std::to_string(i);
                if (sm_->apply(create_put_command(key, value), result)) {
                    write_count++;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            });
    }

    for (int r = 0; r < 5; r++) {
        threads.emplace_back([this, &read_count]() {
            for (int i = 0; i < 50; i++) {
                std::string value;
                if (sm_->query(create_get_command("shared_" + std::to_string(i % 10)), value)) {
                    read_count++;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            });
    }

    for (auto& th : threads) {
        th.join();
    }

    EXPECT_GT(write_count, 0);
    EXPECT_GT(read_count, 0);
    std::cout << "  Writes: " << write_count << ", Reads: " << read_count << std::endl;
}

TEST_F(KvStateMachineTest, StorageSize) {
    std::string result;

    EXPECT_EQ(sm_->size(), 0);

    sm_->apply(create_put_command("k1", "v1"), result);
    EXPECT_EQ(sm_->size(), 1);

    sm_->apply(create_put_command("k2", "v2"), result);
    EXPECT_EQ(sm_->size(), 2);

    sm_->apply(create_delete_command("k1"), result);
    EXPECT_EQ(sm_->size(), 1);
}

TEST_F(KvStateMachineTest, GetAllKeys) {
    std::string result;

    sm_->apply(create_put_command("alpha", "1"), result);
    sm_->apply(create_put_command("beta", "2"), result);
    sm_->apply(create_put_command("gamma", "3"), result);

    auto keys = sm_->get_all_keys();
    EXPECT_EQ(keys.size(), 3);


    EXPECT_TRUE(std::find(keys.begin(), keys.end(), "alpha") != keys.end());
    EXPECT_TRUE(std::find(keys.begin(), keys.end(), "beta") != keys.end());
    EXPECT_TRUE(std::find(keys.begin(), keys.end(), "gamma") != keys.end());
}


TEST_F(KvStateMachineTest, ClearStorage) {
    std::string result;

    sm_->apply(create_put_command("k1", "v1"), result);
    sm_->apply(create_put_command("k2", "v2"), result);
    sm_->apply(create_put_command("k3", "v3"), result);

    EXPECT_EQ(sm_->size(), 3);

    sm_->clear();
    EXPECT_EQ(sm_->size(), 0);

    std::string value;
    bool found = sm_->query(create_get_command("k1"), value);
    EXPECT_FALSE(found);
}