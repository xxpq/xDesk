#ifndef DESKX_NET_V2_KCP_STACK_HPP
#define DESKX_NET_V2_KCP_STACK_HPP

#include "stack.hpp"
#include "ikcp.h"

#include <mutex>

namespace deskx {
namespace net {
namespace v2 {

struct KCPConfig {
    uint32_t conv = 1;
    int nodelay = 1;
    int interval = 20;
    int resend = 2;
    int nc = 1;
    int snd_wnd = 32;
    int rcv_wnd = 32;
    int mtu = 1400;
};

class KCPStack : public IProtocolStack {
public:
    KCPStack();
    explicit KCPStack(const KCPConfig& cfg);
    ~KCPStack() override;

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
    void setConfig(const KCPConfig& cfg);

private:
    static int udpOutput(const char* buf, int len, ikcpcb* kcp, void* user);
    int handleInput(const char* buf, int len);
    void updateKCP(uint32_t now);
    void updateStatus(LinkStatus now);

    socket_t sock_;
    sockaddr_in remoteAddr_;
    ikcpcb* kcp_;
    KCPConfig config_;
    bool isServer_;
    LinkStatus status_;
    mutable std::mutex mutex_;
    StackStats stats_;
    StatusCallback statusCallback_;
};

inline std::unique_ptr<IProtocolStack> IProtocolStack::createKCP() {
    return std::make_unique<KCPStack>();
}

} // namespace v2
} // namespace net
} // namespace deskx

#endif // DESKX_NET_V2_KCP_STACK_HPP
