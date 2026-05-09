#include "manager.hpp"

#include <chrono>
#include <vector>

namespace deskx {
namespace net {
namespace v2 {

ProtocolManager::ProtocolManager()
    : tcpPort_(DEFAULT_TCP_PORT),
      udpPort_(DEFAULT_UDP_PORT),
      isServer_(false),
      activeChannel_(Channel::NONE),
      connected_(false),
      mode_(TransportMode::HYBRID_AUTO),
      preferred_(Channel::TCP),
      recvRunning_(false),
      hbRunning_(false),
      hbIntervalMs_(500) {}

ProtocolManager::~ProtocolManager() { close(); }

bool ProtocolManager::initClient(const std::string& serverIp, uint16_t tcpPort, uint16_t udpPort) {
    std::lock_guard<std::mutex> lock(mutex_);
    serverIp_ = serverIp;
    tcpPort_ = tcpPort;
    udpPort_ = udpPort;
    isServer_ = false;
    tcpStack_ = std::make_shared<TCPStack>();
    kcpStack_ = std::make_shared<KCPStack>(kcpConfig_);
    dispatcher_.setTCPStack(tcpStack_);
    dispatcher_.setKCPStack(kcpStack_);
    tcpStack_->setStatusCallback([this](LinkStatus o, LinkStatus n) {
        onChannelStatusChanged(Channel::TCP, o, n);
    });
    kcpStack_->setStatusCallback([this](LinkStatus o, LinkStatus n) {
        onChannelStatusChanged(Channel::KCP, o, n);
    });
    return true;
}

bool ProtocolManager::initServer(uint16_t tcpPort, uint16_t udpPort) {
    std::lock_guard<std::mutex> lock(mutex_);
    tcpPort_ = tcpPort;
    udpPort_ = udpPort;
    isServer_ = true;
    tcpStack_ = std::make_shared<TCPStack>();
    kcpStack_ = std::make_shared<KCPStack>(kcpConfig_);
    dispatcher_.setTCPStack(tcpStack_);
    dispatcher_.setKCPStack(kcpStack_);
    tcpStack_->setStatusCallback([this](LinkStatus o, LinkStatus n) {
        onChannelStatusChanged(Channel::TCP, o, n);
    });
    kcpStack_->setStatusCallback([this](LinkStatus o, LinkStatus n) {
        onChannelStatusChanged(Channel::KCP, o, n);
    });
    return tcpStack_->listen(tcpPort_, 4) && kcpStack_->listen(udpPort_, 4);
}

bool ProtocolManager::init(const std::string& serverIp, uint16_t tcpPort, uint16_t udpPort) {
    return initClient(serverIp, tcpPort, udpPort);
}

bool ProtocolManager::connect() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!tcpStack_ || !kcpStack_) return false;
    bool tcpOk = tcpStack_->connect(serverIp_, tcpPort_);
    bool kcpOk = kcpStack_->connect(serverIp_, udpPort_);
    if (!tcpOk && !kcpOk) return false;
    activeChannel_ = tcpOk ? Channel::TCP : Channel::KCP;
    connected_ = true;
    recvRunning_ = true;
    recvThread_ = std::thread(&ProtocolManager::recvLoop, this);
    startHeartbeat(hbIntervalMs_);
    if (legacyStatusCallback_) legacyStatusCallback_(true, activeChannel_);
    return true;
}

bool ProtocolManager::startServer() { return accept(); }

bool ProtocolManager::accept() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!isServer_ || !tcpStack_ || !kcpStack_) return false;
    if (!tcpStack_->accept()) return false;
    kcpStack_->accept();
    activeChannel_ = Channel::TCP;
    connected_ = true;
    recvRunning_ = true;
    recvThread_ = std::thread(&ProtocolManager::recvLoop, this);
    startHeartbeat(hbIntervalMs_);
    if (legacyStatusCallback_) legacyStatusCallback_(true, activeChannel_);
    return true;
}

void ProtocolManager::disconnect() {
    stopHeartbeat();
    recvRunning_ = false;
    if (recvThread_.joinable()) recvThread_.join();
    std::lock_guard<std::mutex> lock(mutex_);
    if (tcpStack_) tcpStack_->close();
    if (kcpStack_) kcpStack_->close();
    connected_ = false;
    activeChannel_ = Channel::NONE;
    if (legacyStatusCallback_) legacyStatusCallback_(false, Channel::NONE);
}

void ProtocolManager::close() { disconnect(); }

bool ProtocolManager::reconnect() {
    disconnect();
    return isServer_ ? accept() : connect();
}

bool ProtocolManager::send(const uint8_t* data, size_t len) {
    Channel ch = activeChannel_.load();
    auto stack = dispatcher_.getStack(ch);
    if (!stack || !stack->isConnected()) {
        performFailover(ch);
        ch = activeChannel_.load();
        stack = dispatcher_.getStack(ch);
        if (!stack || !stack->isConnected()) return false;
    }
    return stack->send(data, len) > 0;
}

bool ProtocolManager::sendFrame(FrameType type, const uint8_t* payload, size_t payloadSize) {
    FrameHeader h {};
    initHeader(h, type);
    h.channel = static_cast<uint8_t>(activeChannel_.load());
    h.payload_size = static_cast<uint32_t>(payloadSize);
    h.checksum = payload && payloadSize > 0 ? calcChecksum(payload, payloadSize) : 0;
    Channel ch = activeChannel_.load();
    auto stack = dispatcher_.getStack(ch);
    if (!stack || !stack->isConnected()) return false;
    return stack->sendFrame(h, payload);
}

int ProtocolManager::recv(uint8_t* buff, size_t maxLen) {
    Channel ch = activeChannel_.load();
    auto stack = dispatcher_.getStack(ch);
    if (!stack || !stack->isConnected()) return -1;
    return stack->recv(buff, maxLen);
}

bool ProtocolManager::recvFrame(FrameHeader& header, uint8_t* payload) {
    Channel ch = activeChannel_.load();
    auto stack = dispatcher_.getStack(ch);
    if (!stack || !stack->isConnected()) return false;
    return stack->recvFrame(header, payload);
}

void ProtocolManager::setMode(TransportMode mode) {
    mode_ = mode;
    dispatcher_.setMode(mode);
}

TransportMode ProtocolManager::mode() const { return mode_; }

void ProtocolManager::setPreferredProtocol(Channel ch) { preferred_ = ch; }

Channel ProtocolManager::preferredProtocol() const { return preferred_; }

void ProtocolManager::setKCPConfig(const KCPConfig& config) {
    kcpConfig_ = config;
    if (kcpStack_) kcpStack_->setConfig(config);
}

KCPConfig ProtocolManager::getKCPConfig() const { return kcpConfig_; }

bool ProtocolManager::isConnected() const { return connected_.load(); }

bool ProtocolManager::isServer() const { return isServer_; }

uint8_t ProtocolManager::activeChannels() const {
    uint8_t m = 0;
    if (tcpStack_ && tcpStack_->isConnected()) m |= static_cast<uint8_t>(Channel::TCP);
    if (kcpStack_ && kcpStack_->isConnected()) m |= static_cast<uint8_t>(Channel::KCP);
    return m;
}

LinkStatus ProtocolManager::tcpStatus() const {
    return tcpStack_ ? tcpStack_->status() : LinkStatus::DOWN;
}

LinkStatus ProtocolManager::kcpStatus() const {
    return kcpStack_ ? kcpStack_->status() : LinkStatus::DOWN;
}

Channel ProtocolManager::getActiveChannel() const { return activeChannel_.load(); }

TrafficDispatcher::ChannelStats ProtocolManager::tcpStats() const {
    return dispatcher_.getChannelStats(Channel::TCP);
}

TrafficDispatcher::ChannelStats ProtocolManager::kcpStats() const {
    return dispatcher_.getChannelStats(Channel::KCP);
}

void ProtocolManager::startHeartbeat(uint32_t intervalMs) {
    if (hbRunning_.load()) return;
    hbIntervalMs_ = intervalMs;
    hbRunning_ = true;
    hbThread_ = std::thread(&ProtocolManager::heartbeatLoop, this);
}

void ProtocolManager::stopHeartbeat() {
    hbRunning_ = false;
    if (hbThread_.joinable()) hbThread_.join();
}

bool ProtocolManager::isHeartbeatRunning() const { return hbRunning_.load(); }

void ProtocolManager::setDataCallback(DataCallback cb) { dataCallback_ = std::move(cb); }

void ProtocolManager::setStatusCallback(StatusCallback cb) { statusCallback_ = std::move(cb); }

void ProtocolManager::setReceiveCallback(LegacyReceiveCallback cb) {
    legacyReceiveCallback_ = std::move(cb);
}

void ProtocolManager::setStatusCallback(LegacyStatusCallback cb) {
    legacyStatusCallback_ = std::move(cb);
}

bool ProtocolManager::isLANIP(const std::string& ip) {
    in_addr addr {};
    if (inet_pton(AF_INET, ip.c_str(), &addr) != 1) return false;
    uint32_t u = ntohl(addr.s_addr);
    if ((u & 0xFF000000) == 0x0A000000) return true;
    if ((u & 0xFFF00000) == 0xAC100000) return true;
    if ((u & 0xFFFF0000) == 0xC0A80000) return true;
    if ((u & 0xFF000000) == 0x7F000000) return true;
    return false;
}

Channel ProtocolManager::autoSelectProtocol() const {
    if (isLANIP(serverIp_)) return Channel::TCP;
    return Channel::KCP;
}

void ProtocolManager::recvLoop() {
    std::vector<uint8_t> buff(65536);
    while (recvRunning_.load()) {
        int got = -1;
        Channel ch = Channel::NONE;
        if (kcpStack_ && kcpStack_->isConnected()) {
            got = kcpStack_->recv(buff.data(), buff.size());
            ch = Channel::KCP;
        }
        if (got <= 0 && tcpStack_ && tcpStack_->isConnected()) {
            got = tcpStack_->recv(buff.data(), buff.size());
            ch = Channel::TCP;
        }
        if (got > 0) {
            if (legacyReceiveCallback_) {
                legacyReceiveCallback_(FrameType::FRAME_DATA, buff.data(), static_cast<size_t>(got));
            }
            if (dataCallback_) {
                dataCallback_(buff.data(), static_cast<size_t>(got), FrameType::FRAME_DATA, ch);
            }
            activeChannel_ = ch;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
}

void ProtocolManager::heartbeatLoop() {
    while (hbRunning_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(hbIntervalMs_));
        if (!hbRunning_.load() || !connected_.load()) continue;
        Heartbeat hb {};
        initHeader(hb.header, FrameType::HEARTBEAT_REQ);
        hb.header.channel = static_cast<uint8_t>(activeChannel_.load());
        hb.header.payload_size = sizeof(Heartbeat) - sizeof(FrameHeader);
        hb.latency_tcp = tcpStack_ ? tcpStack_->latency() : 0;
        hb.latency_kcp = kcpStack_ ? kcpStack_->latency() : 0;
        hb.link_status = static_cast<uint8_t>(activeChannel_.load());
        hb.available_channels = activeChannels();
        hb.current_mode = static_cast<uint8_t>(mode_);
        send(reinterpret_cast<const uint8_t*>(&hb), sizeof(hb));
    }
}

void ProtocolManager::onChannelStatusChanged(Channel ch, LinkStatus oldStatus, LinkStatus nowStatus) {
    if (statusCallback_) statusCallback_(ch, oldStatus, nowStatus);
    if (nowStatus == LinkStatus::UP) {
        activeChannel_ = ch;
        connected_ = true;
    } else if (ch == activeChannel_.load() && nowStatus == LinkStatus::FAILED) {
        performFailover(ch);
    }
}

void ProtocolManager::performFailover(Channel failedChannel) {
    Channel backup = failedChannel == Channel::TCP ? Channel::KCP : Channel::TCP;
    auto stack = dispatcher_.getStack(backup);
    if (stack && stack->isConnected()) {
        activeChannel_ = backup;
    } else {
        connected_ = false;
    }
}

} // namespace v2
} // namespace net
} // namespace deskx
