#include "dispatcher.hpp"

#include <algorithm>

namespace deskx {
namespace net {
namespace v2 {

TrafficDispatcher::TrafficDispatcher()
    : mode_(TransportMode::HYBRID_AUTO),
      enabled_{true, true, false},
      weights_{1.0f, 1.0f, 0.0f} {}

TrafficDispatcher::~TrafficDispatcher() = default;

void TrafficDispatcher::setTCPStack(std::shared_ptr<IProtocolStack> stack) {
    std::lock_guard<std::mutex> lock(mutex_);
    tcpStack_ = std::move(stack);
}

void TrafficDispatcher::setKCPStack(std::shared_ptr<IProtocolStack> stack) {
    std::lock_guard<std::mutex> lock(mutex_);
    kcpStack_ = std::move(stack);
}

void TrafficDispatcher::addStack(std::shared_ptr<IProtocolStack> stack, Channel ch) {
    if (ch == Channel::TCP) {
        setTCPStack(std::move(stack));
    } else if (ch == Channel::KCP) {
        setKCPStack(std::move(stack));
    }
}

void TrafficDispatcher::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    tcpStack_.reset();
    kcpStack_.reset();
    stats_ = {};
}

std::shared_ptr<IProtocolStack> TrafficDispatcher::getStack(Channel ch) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (ch == Channel::TCP) return tcpStack_;
    if (ch == Channel::KCP) return kcpStack_;
    return nullptr;
}

void TrafficDispatcher::setMode(TransportMode mode) {
    std::lock_guard<std::mutex> lock(mutex_);
    mode_ = mode;
}

TransportMode TrafficDispatcher::mode() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return mode_;
}

int TrafficDispatcher::send(const uint8_t* data, size_t len) {
    return sendVia(selectChannel(), data, len);
}

int TrafficDispatcher::sendVia(Channel ch, const uint8_t* data, size_t len) {
    auto stack = getStack(ch);
    if (!stack || !stack->isConnected()) {
        std::lock_guard<std::mutex> lock(mutex_);
        stats_[channelIndex(ch)].failed++;
        return -1;
    }
    int ret = stack->send(data, len);
    std::lock_guard<std::mutex> lock(mutex_);
    auto& s = stats_[channelIndex(ch)];
    if (ret > 0) {
        s.packets++;
        s.bytes += static_cast<uint64_t>(ret);
    } else {
        s.failed++;
    }
    return ret;
}

void TrafficDispatcher::enableChannel(Channel ch, bool enable) {
    std::lock_guard<std::mutex> lock(mutex_);
    enabled_[channelIndex(ch)] = enable;
}

bool TrafficDispatcher::isChannelEnabled(Channel ch) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return enabled_[channelIndex(ch)];
}

void TrafficDispatcher::setWeight(Channel ch, float weight) {
    std::lock_guard<std::mutex> lock(mutex_);
    weights_[channelIndex(ch)] = std::max(0.0f, weight);
}

TrafficDispatcher::ChannelStats TrafficDispatcher::getChannelStats(Channel ch) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_[channelIndex(ch)];
}

Channel TrafficDispatcher::selectChannel() const {
    std::lock_guard<std::mutex> lock(mutex_);
    switch (mode_) {
    case TransportMode::LAN_ONLY:
        return enabled_[0] ? Channel::TCP : Channel::NONE;
    case TransportMode::WAN_ONLY:
        return enabled_[1] ? Channel::KCP : Channel::NONE;
    case TransportMode::DUAL_STACK:
    case TransportMode::HYBRID_AUTO:
    default: {
        bool tcpOk = enabled_[0] && tcpStack_ && tcpStack_->isConnected();
        bool kcpOk = enabled_[1] && kcpStack_ && kcpStack_->isConnected();
        if (tcpOk && kcpOk) {
            return weights_[1] > weights_[0] ? Channel::KCP : Channel::TCP;
        }
        if (tcpOk) return Channel::TCP;
        if (kcpOk) return Channel::KCP;
        return Channel::NONE;
    }
    }
}

size_t TrafficDispatcher::channelIndex(Channel ch) {
    if (ch == Channel::TCP) return 0;
    if (ch == Channel::KCP) return 1;
    return 2;
}

DataAggregator::DataAggregator() = default;
DataAggregator::~DataAggregator() = default;

int DataAggregator::available(Channel ch) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(recvBuffers_[channelIndex(ch)].size());
}

int DataAggregator::recv(Channel ch, uint8_t* buff, size_t max_len) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& src = recvBuffers_[channelIndex(ch)];
    if (src.empty()) return 0;
    size_t n = std::min(max_len, src.size());
    if (buff && n > 0) {
        std::memcpy(buff, src.data(), n);
    }
    src.erase(src.begin(), src.begin() + static_cast<long long>(n));
    stats_.bytes_received += n;
    return static_cast<int>(n);
}

bool DataAggregator::recvFrame(FrameHeader&, uint8_t*) { return false; }

bool DataAggregator::isDuplicate(uint32_t sequence, Channel ch) {
    std::lock_guard<std::mutex> lock(mutex_);
    uint64_t key = (static_cast<uint64_t>(static_cast<uint8_t>(ch)) << 32) | sequence;
    if (dedup_.find(key) != dedup_.end()) return true;
    dedup_[key] = true;
    if (dedup_.size() > 2048) {
        dedup_.clear();
    }
    return false;
}

DataAggregator::Stats DataAggregator::getStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

void DataAggregator::resetStats() {
    std::lock_guard<std::mutex> lock(mutex_);
    stats_ = {};
}

size_t DataAggregator::channelIndex(Channel ch) {
    if (ch == Channel::TCP) return 0;
    if (ch == Channel::KCP) return 1;
    return 2;
}

} // namespace v2
} // namespace net
} // namespace deskx
