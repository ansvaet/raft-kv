#pragma once
#include <vector>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <unordered_map>

namespace network {

   
    struct Message {
        uint32_t from_id;
        uint32_t to_id;
        std::string type;  
        std::string data;  
    };


    class MessageQueue {
    private:
        std::queue<Message> queue_;
        mutable std::mutex mutex_;
        std::condition_variable cv_;

    public:
        void push(const Message& msg) {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push(msg);
            cv_.notify_one();
        }

        bool pop(Message& msg) {
            std::unique_lock<std::mutex> lock(mutex_);
            if (queue_.empty()) {
                return false;
            }
            msg = queue_.front();
            queue_.pop();
            return true;
        }

        bool empty() const {
            std::lock_guard<std::mutex> lock(mutex_);
            return queue_.empty();
        }

        size_t size() const {
            std::lock_guard<std::mutex> lock(mutex_);
            return queue_.size();
        }
    };


    class VirtualNetwork {
    private:
        std::vector<std::shared_ptr<MessageQueue>> queues_;
        std::unordered_map<uint32_t, uint32_t> node_to_queue_;

    public:
        VirtualNetwork() = default;


        void register_node(uint32_t node_id) {
            queues_.push_back(std::make_shared<MessageQueue>());
            node_to_queue_[node_id] = queues_.size() - 1;
        }

        void send(uint32_t from_id, uint32_t to_id,
            const std::string& type, const std::string& data) {
            if (node_to_queue_.find(to_id) == node_to_queue_.end()) {
                return;  
            }

            Message msg{
                .from_id = from_id,
                .to_id = to_id,
                .type = type,
                .data = data
            };

            queues_[node_to_queue_[to_id]]->push(msg);
        }

        bool receive(uint32_t node_id, Message& msg) {
            if (node_to_queue_.find(node_id) == node_to_queue_.end()) {
                return false;
            }
            return queues_[node_to_queue_[node_id]]->pop(msg);
        }

    };

} 