/**
 * DeskX Network Adapter v2 Implementation
 */

#include "net_adapter_v2.hpp"
#include "macro.hpp"
#include <algorithm>
#include <sstream>

namespace net {

// 静态缓冲区用于适配 recv
static thread_local std::vector<uint8_t> recv_buffer_;
static thread_local size_t recv_offset_ = 0;

AdapterV2::AdapterV2() {
    config_ = AdapterConfig();
}

AdapterV2::~AdapterV2() {
    close();
}

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
    
    try {
        manager_ = std::make_unique<deskx::net::v2::ProtocolManager>();
        manager_->setMode(config_.mode);
        
        // 设置接收回调
        manager_->setReceiveCallback([this](deskx::net::v2::FrameType type, 
                                          const uint8_t* data, size_t len) {
            this->onReceive(type, data, len);
        });
        
        // 设置状态回调
        manager_->setStatusCallback([this](bool connected, deskx::net::v2::Channel ch) {
            this->onStatus(connected, ch);
        });
        
        return manager_->initServer(tcp_port, kcp_port);
    } catch (const std::exception& e) {
        INFO(std::string(ERR) + "Failed to init server: " + e.what());
        return false;
    }
}

bool AdapterV2::initClient(const std::string& host, uint16_t tcp_port, uint16_t kcp_port) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!config_.enable_v2) {
        INFO(WARN"v2 network layer is disabled");
        return false;
    }
    
    mode_ = args::type::CLIENT;
    
    try {
        manager_ = std::make_unique<deskx::net::v2::ProtocolManager>();
        manager_->setMode(config_.mode);
        
        // 设置接收回调
        manager_->setReceiveCallback([this](deskx::net::v2::FrameType type, 
                                          const uint8_t* data, size_t len) {
            this->onReceive(type, data, len);
        });
        
        // 设置状态回调
        manager_->setStatusCallback([this](bool connected, deskx::net::v2::Channel ch) {
            this->onStatus(connected, ch);
        });
        
        return manager_->init(host, tcp_port, kcp_port);
    } catch (const std::exception& e) {
        INFO(std::string(ERR) + "Failed to init client: " + e.what());
        return false;
    }
}

bool AdapterV2::startServer() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!manager_) return false;
    
    bool result = manager_->startServer();
    if (result) {
        running_ = true;
        worker_thread_ = std::thread(&AdapterV2::worker, this);
    }
    return result;
}

bool AdapterV2::connect() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!manager_) return false;
    
    bool result = manager_->connect();
    if (result) {
        running_ = true;
        worker_thread_ = std::thread(&AdapterV2::worker, this);
    }
    return result;
}

status AdapterV2::recv(byte* buff, int size) {
    if (!connected_) {
        return status::FAIL;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 如果缓冲区有数据，直接返回
    if (recv_offset_ < recv_buffer_.size()) {
        size_t available = recv_buffer_.size() - recv_offset_;
        size_t to_copy = std::min(static_cast<size_t>(size), available);
        std::memcpy(buff, recv_buffer_.data() + recv_offset_, to_copy);
        recv_offset_ += to_copy;
        
        // 清理已读完的缓冲区
        if (recv_offset_ >= recv_buffer_.size()) {
            recv_buffer_.clear();
            recv_offset_ = 0;
        }
        
        return status::OK;
    }
    
    return status::EMPTY;
}

status AdapterV2::send(const byte* buff, int size) {
    if (!connected_) {
        return status::FAIL;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!manager_) {
        return status::FAIL;
    }
    
    // 通过 v2 层发送
    bool result = manager_->sendFrame(deskx::net::v2::FrameType::FRAME_DATA, 
                                     buff, size);
    if (result) {
        stats_.frames_sent_v2++;
        stats_.bytes_sent_v2 += size;
        return status::OK;
    }
    
    return status::FAIL;
}

bool AdapterV2::accept() {
    // v2 模式下连接自动接受
    return connected_;
}

void AdapterV2::disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    connected_ = false;
    
    if (manager_) {
        manager_->close();
    }
}

void AdapterV2::close() {
    running_ = false;
    
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    connected_ = false;
    recv_buffer_.clear();
    recv_offset_ = 0;
    
    if (manager_) {
        manager_->close();
        manager_.reset();
    }
}

bool AdapterV2::isConnected() const {
    return connected_;
}

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
    
    // 通过 dispatcher 切换通道
    return true;
}

bool AdapterV2::initInternal(const std::string& host, uint16_t tcp_port, uint16_t kcp_port) {
    // 内部初始化逻辑
    return true;
}

void AdapterV2::worker() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        if (!connected_) {
            continue;
        }
        
        // 检查管理器状态
        std::lock_guard<std::mutex> lock(mutex_);
        if (manager_) {
            // 可以在这里添加额外的状态检查
        }
    }
}

void AdapterV2::onReceive(deskx::net::v2::FrameType type, const uint8_t* data, size_t len) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    stats_.frames_recv_v2++;
    stats_.bytes_recv_v2 += len;
    
    // 添加到接收缓冲区
    recv_buffer_.insert(recv_buffer_.end(), data, data + len);
}

void AdapterV2::onStatus(bool connected, deskx::net::v2::Channel ch) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    bool was_connected = connected_;
    connected_ = connected;
    
    if (!was_connected && connected) {
        INFO(std::string("Network v2: Connected via ") +
             (ch == deskx::net::v2::Channel::TCP ? "TCP" : "KCP"));
    } else if (was_connected && !connected) {
        INFO("Network v2: Disconnected");
        stats_.reconnect_count++;
    }
}

// 全局适配器实例
AdapterV2& getAdapter() {
    static AdapterV2 instance;
    return instance;
}

} // namespace net
