#include "kcp_stack.hpp"

#include <chrono>
#include <cstring>
#include <vector>

#if defined(_WIN32) || defined(_WIN64)
#pragma comment(lib, "ws2_32.lib")
static class WSAInitKCP {
public:
    WSAInitKCP() {
        WSADATA d;
        WSAStartup(MAKEWORD(2, 2), &d);
    }
    ~WSAInitKCP() { WSACleanup(); }
} g_wsa_kcp_init;
#define CLOSE_FD ::closesocket
#else
#define CLOSE_FD ::close
#endif

namespace deskx {
namespace net {
namespace v2 {

namespace {
constexpr socket_t kInvalidSocket =
#if defined(_WIN32) || defined(_WIN64)
    INVALID_SOCKET;
#else
    -1;
#endif

inline uint32_t nowMs() {
    using namespace std::chrono;
    return static_cast<uint32_t>(duration_cast<milliseconds>(
        steady_clock::now().time_since_epoch()).count());
}
}

KCPStack::KCPStack()
    : sock_(kInvalidSocket),
      kcp_(nullptr),
      isServer_(false),
      status_(LinkStatus::DOWN) {
    std::memset(&remoteAddr_, 0, sizeof(remoteAddr_));
}

KCPStack::KCPStack(const KCPConfig& cfg) : KCPStack() { config_ = cfg; }

KCPStack::~KCPStack() { close(); }

bool KCPStack::init() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (sock_ != kInvalidSocket && kcp_ != nullptr) return true;
    sock_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_ == kInvalidSocket) {
        updateStatus(LinkStatus::FAILED);
        return false;
    }
    kcp_ = ikcp_create(config_.conv, this);
    if (!kcp_) {
        CLOSE_FD(sock_);
        sock_ = kInvalidSocket;
        updateStatus(LinkStatus::FAILED);
        return false;
    }
    ikcp_nodelay(kcp_, config_.nodelay, config_.interval, config_.resend, config_.nc);
    ikcp_wndsize(kcp_, config_.snd_wnd, config_.rcv_wnd);
    ikcp_setmtu(kcp_, config_.mtu);
    kcp_->output = &KCPStack::udpOutput;
    updateStatus(LinkStatus::DOWN);
    return true;
}

void KCPStack::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (kcp_) {
        ikcp_release(kcp_);
        kcp_ = nullptr;
    }
    if (sock_ != kInvalidSocket) {
        CLOSE_FD(sock_);
        sock_ = kInvalidSocket;
    }
    updateStatus(LinkStatus::DOWN);
}

bool KCPStack::connect(const std::string& host, uint16_t port) {
    std::lock_guard<std::mutex> lock(mutex_);
    isServer_ = false;
    if (!kcp_ || sock_ == kInvalidSocket) {
        if (!init()) return false;
    }
    std::memset(&remoteAddr_, 0, sizeof(remoteAddr_));
    remoteAddr_.sin_family = AF_INET;
    remoteAddr_.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &remoteAddr_.sin_addr) != 1) {
        updateStatus(LinkStatus::FAILED);
        return false;
    }
    updateStatus(LinkStatus::UP);
    return true;
}

bool KCPStack::listen(uint16_t port, int) {
    std::lock_guard<std::mutex> lock(mutex_);
    isServer_ = true;
    if (!kcp_ || sock_ == kInvalidSocket) {
        if (!init()) return false;
    }
    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (::bind(sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        updateStatus(LinkStatus::FAILED);
        return false;
    }
    updateStatus(LinkStatus::DOWN);
    return true;
}

bool KCPStack::accept() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!isServer_ || sock_ == kInvalidSocket) return false;
    sockaddr_in clientAddr {};
    socklen_t len = sizeof(clientAddr);
    char probe[1];
    int ret = recvfrom(sock_, probe, sizeof(probe), MSG_PEEK, reinterpret_cast<sockaddr*>(&clientAddr), &len);
    if (ret <= 0) return false;
    remoteAddr_ = clientAddr;
    updateStatus(LinkStatus::UP);
    return true;
}

int KCPStack::send(const uint8_t* data, size_t len) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (sock_ == kInvalidSocket || kcp_ == nullptr || status_ != LinkStatus::UP) return -1;
    int ret = ikcp_send(kcp_, reinterpret_cast<const char*>(data), static_cast<int>(len));
    ikcp_update(kcp_, nowMs());
    if (ret == 0) {
        stats_.packets_sent++;
        stats_.bytes_sent += len;
        return static_cast<int>(len);
    }
    stats_.send_failed++;
    return -1;
}

int KCPStack::recv(uint8_t* buff, size_t max_len) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (sock_ == kInvalidSocket || kcp_ == nullptr) return -1;

    char udpBuff[2048];
    sockaddr_in src {};
    socklen_t srcLen = sizeof(src);
    int udpLen = recvfrom(sock_, udpBuff, sizeof(udpBuff), 0, reinterpret_cast<sockaddr*>(&src), &srcLen);
    if (udpLen > 0) {
        if (remoteAddr_.sin_family == 0) remoteAddr_ = src;
        handleInput(udpBuff, udpLen);
    }

    ikcp_update(kcp_, nowMs());
    int ret = ikcp_recv(kcp_, reinterpret_cast<char*>(buff), static_cast<int>(max_len));
    if (ret > 0) {
        stats_.packets_recv++;
        stats_.bytes_recv += static_cast<uint64_t>(ret);
    } else if (ret < 0) {
        stats_.recv_failed++;
    }
    return ret;
}

bool KCPStack::isConnected() const { return status() == LinkStatus::UP; }

LinkStatus KCPStack::status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

StackStats KCPStack::getStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

uint32_t KCPStack::latency() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_.latency_ms;
}

Channel KCPStack::type() const { return Channel::KCP; }

bool KCPStack::sendFrame(const FrameHeader& header, const uint8_t* payload) {
    std::vector<uint8_t> out(sizeof(FrameHeader) + header.payload_size);
    FrameHeader netHeader = header;
    htonFrameHeader(netHeader);
    std::memcpy(out.data(), &netHeader, sizeof(netHeader));
    if (header.payload_size > 0 && payload != nullptr) {
        std::memcpy(out.data() + sizeof(netHeader), payload, header.payload_size);
    }
    return send(out.data(), out.size()) == static_cast<int>(out.size());
}

bool KCPStack::recvFrame(FrameHeader& header, uint8_t* payload) {
    std::vector<uint8_t> tmp(65536);
    int len = recv(tmp.data(), tmp.size());
    if (len < static_cast<int>(sizeof(FrameHeader))) return false;
    std::memcpy(&header, tmp.data(), sizeof(header));
    ntohFrameHeader(header);
    if (header.payload_size == 0) return true;
    if (!payload) return false;
    if (len < static_cast<int>(sizeof(FrameHeader) + header.payload_size)) return false;
    std::memcpy(payload, tmp.data() + sizeof(FrameHeader), header.payload_size);
    return true;
}

void KCPStack::setStatusCallback(StatusCallback cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    statusCallback_ = std::move(cb);
}

void KCPStack::setConfig(const KCPConfig& cfg) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = cfg;
    if (kcp_) {
        ikcp_nodelay(kcp_, config_.nodelay, config_.interval, config_.resend, config_.nc);
        ikcp_wndsize(kcp_, config_.snd_wnd, config_.rcv_wnd);
        ikcp_setmtu(kcp_, config_.mtu);
    }
}

int KCPStack::udpOutput(const char* buf, int len, ikcpcb*, void* user) {
    auto* self = static_cast<KCPStack*>(user);
    if (!self || self->sock_ == kInvalidSocket || self->remoteAddr_.sin_family == 0) return -1;
    int ret = sendto(self->sock_, buf, len, 0,
        reinterpret_cast<sockaddr*>(&self->remoteAddr_), sizeof(self->remoteAddr_));
    return ret == len ? 0 : -1;
}

int KCPStack::handleInput(const char* buf, int len) {
    if (!kcp_) return -1;
    return ikcp_input(kcp_, buf, len);
}

void KCPStack::updateKCP(uint32_t now) {
    if (kcp_) ikcp_update(kcp_, now);
}

void KCPStack::updateStatus(LinkStatus now) {
    LinkStatus old = status_;
    status_ = now;
    if (statusCallback_ && old != now) {
        statusCallback_(old, now);
    }
}

} // namespace v2
} // namespace net
} // namespace deskx
