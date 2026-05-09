#ifndef DESKX_NET_V2_MANAGER_HPP
#define DESKX_NET_V2_MANAGER_HPP

#include "dispatcher.hpp"
#include "kcp_stack.hpp"
#include "tcp_stack.hpp"

#include <atomic>
#include <mutex>
#include <thread>

namespace deskx {
namespace net {
namespace v2 {

class ProtocolManager {
public:
    using DataCallback = std::function<void(const uint8_t* data, size_t len, FrameType type, Channel ch)>;
    using StatusCallback = std::function<void(Channel ch, LinkStatus oldStatus, LinkStatus newStatus)>;
    using LegacyReceiveCallback = std::function<void(FrameType type, const uint8_t* data, size_t len)>;
    using LegacyStatusCallback = std::function<void(bool connected, Channel activeChannel)>;

    ProtocolManager();
    ~ProtocolManager();

    bool initClient(const std::string& serverIp, uint16_t tcpPort, uint16_t udpPort);
    bool initServer(uint16_t tcpPort, uint16_t udpPort);
    bool init(const std::string& serverIp, uint16_t tcpPort, uint16_t udpPort);

    bool connect();
    bool startServer();
    bool accept();
    void disconnect();
    void close();
    bool reconnect();

    bool send(const uint8_t* data, size_t len);
    bool sendFrame(FrameType type, const uint8_t* payload, size_t payloadSize);
    int recv(uint8_t* buff, size_t maxLen);
    bool recvFrame(FrameHeader& header, uint8_t* payload = nullptr);

    void setMode(TransportMode mode);
    TransportMode mode() const;
    void setPreferredProtocol(Channel ch);
    Channel preferredProtocol() const;
    void setKCPConfig(const KCPConfig& config);
    KCPConfig getKCPConfig() const;

    bool isConnected() const;
    bool isServer() const;
    uint8_t activeChannels() const;
    LinkStatus tcpStatus() const;
    LinkStatus kcpStatus() const;
    Channel getActiveChannel() const;

    TrafficDispatcher::ChannelStats tcpStats() const;
    TrafficDispatcher::ChannelStats kcpStats() const;

    void startHeartbeat(uint32_t intervalMs = 500);
    void stopHeartbeat();
    bool isHeartbeatRunning() const;

    void setDataCallback(DataCallback cb);
    void setStatusCallback(StatusCallback cb);
    void setReceiveCallback(LegacyReceiveCallback cb);
    void setStatusCallback(LegacyStatusCallback cb);

    static bool isLANIP(const std::string& ip);
    Channel autoSelectProtocol() const;

private:
    void recvLoop();
    void heartbeatLoop();
    void onChannelStatusChanged(Channel ch, LinkStatus oldStatus, LinkStatus nowStatus);
    void performFailover(Channel failedChannel);

    std::string serverIp_;
    uint16_t tcpPort_;
    uint16_t udpPort_;
    bool isServer_;

    std::shared_ptr<TCPStack> tcpStack_;
    std::shared_ptr<KCPStack> kcpStack_;
    TrafficDispatcher dispatcher_;

    std::atomic<Channel> activeChannel_;
    std::atomic<bool> connected_;
    TransportMode mode_;
    Channel preferred_;
    KCPConfig kcpConfig_;

    std::thread recvThread_;
    std::atomic<bool> recvRunning_;

    std::thread hbThread_;
    std::atomic<bool> hbRunning_;
    uint32_t hbIntervalMs_;

    mutable std::mutex mutex_;
    DataCallback dataCallback_;
    StatusCallback statusCallback_;
    LegacyReceiveCallback legacyReceiveCallback_;
    LegacyStatusCallback legacyStatusCallback_;
};

} // namespace v2
} // namespace net
} // namespace deskx

#endif // DESKX_NET_V2_MANAGER_HPP
