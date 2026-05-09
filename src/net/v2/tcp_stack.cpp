#include "tcp_stack.hpp"

#include <cstring>
#include <vector>

#if defined(_WIN32) || defined(_WIN64)
#pragma comment(lib, "ws2_32.lib")
static class WSAInitTCP {
public:
    WSAInitTCP() {
        WSADATA d;
        WSAStartup(MAKEWORD(2, 2), &d);
    }
    ~WSAInitTCP() { WSACleanup(); }
} g_wsa_tcp_init;
#define CLOSE_FD closesocket
#else
#include <netinet/tcp.h>
#include <sys/time.h>
#define CLOSE_FD close
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
}

TCPStack::TCPStack()
    : listenSock_(kInvalidSocket),
      connSock_(kInvalidSocket),
      isServer_(false),
      status_(LinkStatus::DOWN) {}

TCPStack::~TCPStack() { close(); }

bool TCPStack::init() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!isServer_) {
        if (connSock_ != kInvalidSocket) return true;
        connSock_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (connSock_ == kInvalidSocket) {
            updateStatus(LinkStatus::FAILED);
            return false;
        }
    } else {
        if (listenSock_ != kInvalidSocket) return true;
        listenSock_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listenSock_ == kInvalidSocket) {
            updateStatus(LinkStatus::FAILED);
            return false;
        }
    }
    updateStatus(LinkStatus::DOWN);
    return true;
}

void TCPStack::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (connSock_ != kInvalidSocket) {
        CLOSE_FD(connSock_);
        connSock_ = kInvalidSocket;
    }
    if (listenSock_ != kInvalidSocket) {
        CLOSE_FD(listenSock_);
        listenSock_ = kInvalidSocket;
    }
    updateStatus(LinkStatus::DOWN);
}

bool TCPStack::connect(const std::string& host, uint16_t port) {
    std::lock_guard<std::mutex> lock(mutex_);
    isServer_ = false;
    if (connSock_ == kInvalidSocket) {
        connSock_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (connSock_ == kInvalidSocket) {
            updateStatus(LinkStatus::FAILED);
            return false;
        }
    }
    updateStatus(LinkStatus::CONNECTING);
    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        updateStatus(LinkStatus::FAILED);
        return false;
    }
    if (::connect(connSock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        updateStatus(LinkStatus::FAILED);
        return false;
    }
    setNoDelay(connSock_, true);
    setKeepAlive(connSock_, true);
    updateStatus(LinkStatus::UP);
    return true;
}

bool TCPStack::listen(uint16_t port, int backlog) {
    std::lock_guard<std::mutex> lock(mutex_);
    isServer_ = true;
    if (listenSock_ == kInvalidSocket) {
        listenSock_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listenSock_ == kInvalidSocket) {
            updateStatus(LinkStatus::FAILED);
            return false;
        }
    }
    int opt = 1;
    setsockopt(listenSock_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));
    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (::bind(listenSock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        updateStatus(LinkStatus::FAILED);
        return false;
    }
    if (::listen(listenSock_, backlog) != 0) {
        updateStatus(LinkStatus::FAILED);
        return false;
    }
    updateStatus(LinkStatus::DOWN);
    return true;
}

bool TCPStack::accept() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (listenSock_ == kInvalidSocket) return false;
    sockaddr_in clientAddr {};
    socklen_t len = sizeof(clientAddr);
    connSock_ = ::accept(listenSock_, reinterpret_cast<sockaddr*>(&clientAddr), &len);
    if (connSock_ == kInvalidSocket) {
        updateStatus(LinkStatus::FAILED);
        return false;
    }
    setNoDelay(connSock_, true);
    setKeepAlive(connSock_, true);
    updateStatus(LinkStatus::UP);
    return true;
}

int TCPStack::send(const uint8_t* data, size_t len) {
    std::lock_guard<std::mutex> lock(mutex_);
    socket_t fd = activeSocket();
    if (fd == kInvalidSocket) return -1;
    int ret = ::send(fd, reinterpret_cast<const char*>(data), static_cast<int>(len), 0);
    if (ret > 0) {
        stats_.packets_sent++;
        stats_.bytes_sent += static_cast<uint64_t>(ret);
    } else {
        stats_.send_failed++;
    }
    return ret;
}

int TCPStack::recv(uint8_t* buff, size_t max_len) {
    std::lock_guard<std::mutex> lock(mutex_);
    socket_t fd = activeSocket();
    if (fd == kInvalidSocket) return -1;
    int ret = ::recv(fd, reinterpret_cast<char*>(buff), static_cast<int>(max_len), 0);
    if (ret > 0) {
        stats_.packets_recv++;
        stats_.bytes_recv += static_cast<uint64_t>(ret);
    } else if (ret == 0) {
        updateStatus(LinkStatus::DOWN);
    } else {
        stats_.recv_failed++;
    }
    return ret;
}

bool TCPStack::isConnected() const { return status() == LinkStatus::UP; }

LinkStatus TCPStack::status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

StackStats TCPStack::getStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

uint32_t TCPStack::latency() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_.latency_ms;
}

Channel TCPStack::type() const { return Channel::TCP; }

bool TCPStack::sendFrame(const FrameHeader& header, const uint8_t* payload) {
    FrameHeader netHeader = header;
    htonFrameHeader(netHeader);
    if (send(reinterpret_cast<const uint8_t*>(&netHeader), sizeof(netHeader)) != sizeof(netHeader)) {
        return false;
    }
    if (header.payload_size == 0) return true;
    return send(payload, header.payload_size) == static_cast<int>(header.payload_size);
}

bool TCPStack::recvFrame(FrameHeader& header, uint8_t* payload) {
    if (recv(reinterpret_cast<uint8_t*>(&header), sizeof(header)) != sizeof(header)) {
        return false;
    }
    ntohFrameHeader(header);
    if (header.payload_size == 0) return true;
    if (!payload) return false;
    return recv(payload, header.payload_size) == static_cast<int>(header.payload_size);
}

void TCPStack::setStatusCallback(StatusCallback cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    statusCallback_ = std::move(cb);
}

void TCPStack::updateStatus(LinkStatus now) {
    LinkStatus old = status_;
    status_ = now;
    if (statusCallback_ && old != now) {
        statusCallback_(old, now);
    }
}

socket_t TCPStack::activeSocket() const {
    return connSock_ != kInvalidSocket ? connSock_ : listenSock_;
}

bool TCPStack::setNoDelay(socket_t fd, bool enable) {
    int flag = enable ? 1 : 0;
    return setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&flag), sizeof(flag)) == 0;
}

bool TCPStack::setKeepAlive(socket_t fd, bool enable) {
    int flag = enable ? 1 : 0;
    return setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, reinterpret_cast<const char*>(&flag), sizeof(flag)) == 0;
}

} // namespace v2
} // namespace net
} // namespace deskx
