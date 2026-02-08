#pragma once
#include <vector>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <unordered_map>
#include <string>
#include <cstdint>
#include <../include/raft/types.hpp>

namespace raft::network {


    class MessageQueue {
    private:
        std::queue<VirtualMessage> queue_;
        mutable std::mutex mutex_;
        std::condition_variable cv_;

    public:
        void push(const VirtualMessage& msg);
        bool pop(VirtualMessage& msg);
        bool empty() const;
        size_t size() const;
    };

    class VirtualTransport : public raft::INetworkTransport  {
    private:
        std::unordered_map<uint32_t, std::shared_ptr<MessageQueue>> queues_;
        mutable std::mutex mutex_;

    public:
        VirtualTransport();

        bool send(const VirtualMessage& msg);
        bool receive(uint32_t node_id, VirtualMessage& msg);


        void register_node(uint32_t node_id);

        void inject_message(const VirtualMessage& msg);
        void clear();
        size_t get_queue_size(uint32_t node_id) const;
    };

} 