#include "net_adapter_v2.hpp"

#include <algorithm>
#include <chrono>

namespace net {

static thread_local std::vector<uint8_t> recv_buffer_;
static thread_local size_t recv_offset_ = 0;

AdapterV2::AdapterV2() { config_ = AdapterConfig(); }

AdapterV2::~AdapterV2() { close(); }

void AdapterV2::setConfig(const AdapterConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
}

AdapterConfig AdapterV2::getConfig() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

bool AdapterV2::initServer(uint16_t tcp_port, uint16_t kcp_port) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!config_.enable_v2) {
        INFO(WARN"v2 network layer is disabled");
        return false;
    }
    mode_ = args::type::SERVER;
    manager_ = std::make_unique<deskx::net::v2::ProtocolManager>();
    manager_->setMode(config_.mode);
    manager_->setReceiveCallback([this](deskx::net::v2::FrameType type, const uint8_t* data, size_t len) {
        onReceive(type, data, len);
    });
    manager_->setStatusCallback([this](bool connected, deskx::net::v2::Channel ch) {
        onStatus(connected, ch);
    });
    return manager_->initServer(tcp_port, kcp_port);
}

bool AdapterV2::initClient(const std::string& host, uint16_t tcp_port, uint16_t kcp_port) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!config_.enable_v2) {
        INFO(WARN"v2 network layer is disabled");
        return false;
    }
    mode_ = args::type::CLIENT;
    manager_ = std::make_unique<deskx::net::v2::ProtocolManager>();
    manager_->setMode(config_.mode);
    manager_->setReceiveCallback([this](deskx::net::v2::FrameType type, const uint8_t* data, size_t len) {
        onReceive(type, data, len);
    });
    manager_->setStatusCallback([this](bool connected, deskx::net::v2::Channel ch) {
        onStatus(connected, ch);
    });
    return manager_->init(host, tcp_port, kcp_port);
}

bool AdapterV2::startServer() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!manager_) return false;
    bool ok = manager_->startServer();
    if (ok) {
        running_ = true;
        worker_thread_ = std::thread(&AdapterV2::worker, this);
    }
    return ok;
}

bool AdapterV2::connect() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!manager_) return false;
    bool ok = manager_->connect();
    if (ok) {
        running_ = true;
        worker_thread_ = std::thread(&AdapterV2::worker, this);
    }
    return ok;
}

status AdapterV2::recv(byte* buff, int size) {
    if (!connected_) return status::FAIL;
    std::lock_guard<std::mutex> lock(mutex_);
    if (recv_offset_ < recv_buffer_.size()) {
        size_t avail = recv_buffer_.size() - recv_offset_;
        size_t n = std::min(static_cast<size_t>(size), avail);
        std::memcpy(buff, recv_buffer_.data() + recv_offset_, n);
        recv_offset_ += n;
        if (recv_offset_ >= recv_buffer_.size()) {
            recv_buffer_.clear();
            recv_offset_ = 0;
        }
        return status::OK;
    }
    return status::EMPTY;
}

status AdapterV2::send(const byte* buff, int size) {
    if (!connected_) return status::FAIL;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!manager_) return status::FAIL;
    bool ok = manager_->sendFrame(deskx::net::v2::FrameType::FRAME_DATA, buff, size);
    if (!ok) return status::FAIL;
    stats_.frames_sent_v2++;
    stats_.bytes_sent_v2 += static_cast<uint64_t>(size);
    return status::OK;
}

bool AdapterV2::accept() { return connected_; }

void AdapterV2::disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);
    connected_ = false;
    if (manager_) manager_->close();
}

void AdapterV2::close() {
    running_ = false;
    if (worker_thread_.joinable()) worker_thread_.join();
    std::lock_guard<std::mutex> lock(mutex_);
    connected_ = false;
    recv_buffer_.clear();
    recv_offset_ = 0;
    if (manager_) {
        manager_->close();
        manager_.reset();
    }
}

bool AdapterV2::isConnected() const { return connected_; }

AdapterV2::Stats AdapterV2::getStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

void AdapterV2::resetStats() {
    std::lock_guard<std::mutex> lock(mutex_);
    stats_ = Stats();
}

deskx::net::v2::Channel AdapterV2::getActiveChannel() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!manager_) return deskx::net::v2::Channel::BOTH;
    return manager_->getActiveChannel();
}

bool AdapterV2::switchChannel(deskx::net::v2::Channel ch) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!manager_) return false;
    manager_->setPreferredProtocol(ch);
    return true;
}

bool AdapterV2::initInternal(const std::string&, uint16_t, uint16_t) { return true; }

void AdapterV2::worker() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void AdapterV2::onReceive(deskx::net::v2::FrameType, const uint8_t* data, size_t len) {
    std::lock_guard<std::mutex> lock(mutex_);
    stats_.frames_recv_v2++;
    stats_.bytes_recv_v2 += static_cast<uint64_t>(len);
    recv_buffer_.insert(recv_buffer_.end(), data, data + len);
}

void AdapterV2::onStatus(bool connected, deskx::net::v2::Channel ch) {
    std::lock_guard<std::mutex> lock(mutex_);
    bool was = connected_;
    connected_ = connected;
    if (!was && connected) {
        INFO(std::string("Network v2: Connected via ") +
             (ch == deskx::net::v2::Channel::TCP ? "TCP" : "KCP"));
    } else if (was && !connected) {
        INFO("Network v2: Disconnected");
        stats_.reconnect_count++;
    }
}

AdapterV2& getAdapter() {
    static AdapterV2 inst;
    return inst;
}

} // namespace net
