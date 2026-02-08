#include "virtual_transport.hpp"
#include <iostream>

namespace raft::network {

    void MessageQueue::push(const VirtualMessage& msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(msg);
    }

    bool MessageQueue::pop(VirtualMessage& msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            return false;
        }

        msg = queue_.front();
        queue_.pop();
        return true;
    }

    bool MessageQueue::empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    size_t MessageQueue::size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    VirtualTransport::VirtualTransport() {}

    bool VirtualTransport::send(const VirtualMessage& msg) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = queues_.find(msg.to_id);
        if (it == queues_.end()) {
            std::cerr << "[Network] Attempt to send to unregistered node: " << msg.to_id << std::endl;
            return false;
        }

        it->second->push(msg);
        return true;
    }

    bool VirtualTransport::receive(uint32_t node_id, VirtualMessage& msg) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = queues_.find(node_id);
        if (it == queues_.end()) {
            return false;
        }

        return it->second->pop(msg);
    }


    void VirtualTransport::register_node(uint32_t node_id) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (queues_.find(node_id) == queues_.end()) {
            queues_[node_id] = std::make_shared<MessageQueue>();
        }
    }

    void VirtualTransport::inject_message(const VirtualMessage& msg) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = queues_.find(msg.to_id);
        if (it != queues_.end()) {
            it->second->push(msg);
        }
    }

    void VirtualTransport::clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        queues_.clear();
    }

    size_t VirtualTransport::get_queue_size(uint32_t node_id) const {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = queues_.find(node_id);
        if (it != queues_.end()) {
            return it->second->size();
        }
        return 0;
    }

} 