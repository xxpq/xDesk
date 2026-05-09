#ifndef DESKX_NET_V2_DISPATCHER_HPP
#define DESKX_NET_V2_DISPATCHER_HPP

#include "stack.hpp"

#include <array>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace deskx {
namespace net {
namespace v2 {

class TrafficDispatcher {
public:
    struct ChannelStats {
        uint64_t packets = 0;
        uint64_t bytes = 0;
        uint64_t failed = 0;
    };

    TrafficDispatcher();
    ~TrafficDispatcher();

    void setTCPStack(std::shared_ptr<IProtocolStack> stack);
    void setKCPStack(std::shared_ptr<IProtocolStack> stack);
    void addStack(std::shared_ptr<IProtocolStack> stack, Channel ch);
    void clear();

    std::shared_ptr<IProtocolStack> getStack(Channel ch) const;

    void setMode(TransportMode mode);
    TransportMode mode() const;

    int send(const uint8_t* data, size_t len);
    int sendVia(Channel ch, const uint8_t* data, size_t len);

    void enableChannel(Channel ch, bool enable);
    bool isChannelEnabled(Channel ch) const;

    void setWeight(Channel ch, float weight);
    ChannelStats getChannelStats(Channel ch) const;

private:
    Channel selectChannel() const;
    static size_t channelIndex(Channel ch);

    std::shared_ptr<IProtocolStack> tcpStack_;
    std::shared_ptr<IProtocolStack> kcpStack_;
    TransportMode mode_;
    std::array<bool, 3> enabled_;
    std::array<float, 3> weights_;
    std::array<ChannelStats, 3> stats_;
    mutable std::mutex mutex_;
};

class DataAggregator {
public:
    struct Stats {
        uint64_t frames_received = 0;
        uint64_t frames_completed = 0;
        uint64_t frames_dropped = 0;
        uint64_t bytes_received = 0;
    };

    DataAggregator();
    ~DataAggregator();

    int available(Channel ch) const;
    int recv(Channel ch, uint8_t* buff, size_t max_len);
    bool recvFrame(FrameHeader& header, uint8_t* payload);
    bool isDuplicate(uint32_t sequence, Channel ch);
    Stats getStats() const;
    void resetStats();

private:
    static size_t channelIndex(Channel ch);

    std::array<std::vector<uint8_t>, 3> recvBuffers_;
    std::unordered_map<uint64_t, bool> dedup_;
    Stats stats_;
    mutable std::mutex mutex_;
};

} // namespace v2
} // namespace net
} // namespace deskx

#endif // DESKX_NET_V2_DISPATCHER_HPP
