#ifndef DESKX_NET_V2_TCP_STACK_HPP
#define DESKX_NET_V2_TCP_STACK_HPP

#include "stack.hpp"

#include <mutex>

namespace deskx {
namespace net {
namespace v2 {

class TCPStack : public IProtocolStack {
public:
    TCPStack();
    ~TCPStack() override;

    bool init() override;
    void close() override;

    bool connect(const std::string& host, uint16_t port) override;
    bool listen(uint16_t port, int backlog = 4) override;
    bool accept() override;

    int send(const uint8_t* data, size_t len) override;
    int recv(uint8_t* buff, size_t max_len) override;

    bool isConnected() const override;
    LinkStatus status() const override;
    StackStats getStats() const override;
    uint32_t latency() const override;
    Channel type() const override;

    bool sendFrame(const FrameHeader& header, const uint8_t* payload) override;
    bool recvFrame(FrameHeader& header, uint8_t* payload) override;

    void setStatusCallback(StatusCallback cb) override;

private:
    void updateStatus(LinkStatus now);
    socket_t activeSocket() const;
    bool setNoDelay(socket_t fd, bool enable);
    bool setKeepAlive(socket_t fd, bool enable);

    socket_t listenSock_;
    socket_t connSock_;
    bool isServer_;
    LinkStatus status_;
    mutable std::mutex mutex_;
    StackStats stats_;
    StatusCallback statusCallback_;
};

inline std::unique_ptr<IProtocolStack> IProtocolStack::createTCP() {
    return std::make_unique<TCPStack>();
}

} // namespace v2
} // namespace net
} // namespace deskx

#endif // DESKX_NET_V2_TCP_STACK_HPP
