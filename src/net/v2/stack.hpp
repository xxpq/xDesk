#ifndef DESKX_NET_V2_STACK_HPP
#define DESKX_NET_V2_STACK_HPP

#include "frame.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#if defined(_WIN32) || defined(_WIN64)
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
#endif

namespace deskx {
namespace net {
namespace v2 {

enum class StackState {
    IDLE = 0,
    CONNECTING = 1,
    CONNECTED = 2,
    FAILED = 3,
};

struct StackStats {
    uint64_t bytes_sent = 0;
    uint64_t bytes_recv = 0;
    uint64_t packets_sent = 0;
    uint64_t packets_recv = 0;
    uint64_t send_failed = 0;
    uint64_t recv_failed = 0;
    uint32_t latency_ms = 0;
};

class IProtocolStack {
public:
    using StatusCallback = std::function<void(LinkStatus, LinkStatus)>;

    virtual ~IProtocolStack() = default;

    virtual bool init() = 0;
    virtual void close() = 0;

    virtual bool connect(const std::string& host, uint16_t port) = 0;
    virtual bool listen(uint16_t port, int backlog = 4) = 0;
    virtual bool accept() = 0;

    virtual int send(const uint8_t* data, size_t len) = 0;
    virtual int recv(uint8_t* buff, size_t max_len) = 0;

    virtual bool isConnected() const = 0;
    virtual LinkStatus status() const = 0;
    virtual StackStats getStats() const = 0;
    virtual uint32_t latency() const = 0;
    virtual Channel type() const = 0;

    virtual bool sendFrame(const FrameHeader& header, const uint8_t* payload) = 0;
    virtual bool recvFrame(FrameHeader& header, uint8_t* payload) = 0;

    virtual void setStatusCallback(StatusCallback cb) = 0;

    static std::unique_ptr<IProtocolStack> createTCP();
    static std::unique_ptr<IProtocolStack> createKCP();
};

uint32_t calcChecksum(const uint8_t* data, size_t len);

} // namespace v2
} // namespace net
} // namespace deskx

#endif // DESKX_NET_V2_STACK_HPP
