/**
 * DeskX Network Adapter v2
 * 
 * 网络层适配器 - 桥接新 v2 网络层与原有 net.hpp 接口
 * 支持向后兼容和平滑迁移
 */

#ifndef DESKX_NET_ADAPTER_V2_HPP
#define DESKX_NET_ADAPTER_V2_HPP

#include <memory>
#include <functional>
#include <mutex>
#include <atomic>
#include <vector>
#include <thread>
#include "macro.hpp"
#include "args.hpp"
#include "net.hpp"
#include "net/v2.hpp"

namespace net {

// 适配器配置
struct AdapterConfig {
    // 是否启用 v2 网络层
    bool enable_v2 = true;
    
    // 默认传输模式
    deskx::net::v2::TransportMode mode = deskx::net::v2::TransportMode::DUAL_STACK;
    
    // TCP 端口
    uint16_t tcp_port = 7900;
    
    // KCP 端口  
    uint16_t kcp_port = 7901;
    
    // 心跳间隔 (ms)
    uint32_t heartbeat_interval = 1000;
    
    // 连接超时 (ms)
    uint32_t connect_timeout = 5000;
    
    // 重连间隔 (ms)
    uint32_t reconnect_interval = 3000;
};

// 网络适配器类
class AdapterV2 {
public:
    using StatusCallback = std::function<void(status)>;
    using RecvCallback = std::function<status(byte*, int)>;
    
private:
    // v2 管理器
    std::unique_ptr<deskx::net::v2::ProtocolManager> manager_;
    
    // 配置
    AdapterConfig config_;
    
    // 当前模式
    args::type mode_ = args::type::UNKNOWN;
    
    // 连接状态
    std::atomic<bool> connected_{false};
    
    // 工作线程
    std::thread worker_thread_;
    std::atomic<bool> running_{false};
    
    // 原始回调
    RecvCallback original_recv_callback_;
    
    // 互斥锁
    mutable std::mutex mutex_;
    
    // 统计
    struct Stats {
        uint64_t frames_sent_v2 = 0;
        uint64_t frames_recv_v2 = 0;
        uint64_t bytes_sent_v2 = 0;
        uint64_t bytes_recv_v2 = 0;
        uint64_t fallback_count = 0;
        uint64_t reconnect_count = 0;
    } stats_;

public:
    AdapterV2();
    ~AdapterV2();
    
    // 配置
    void setConfig(const AdapterConfig& config);
    AdapterConfig getConfig() const;
    
    // 初始化 (服务端)
    bool initServer(uint16_t tcp_port, uint16_t kcp_port);
    
    // 初始化 (客户端)
    bool initClient(const std::string& host, uint16_t tcp_port, uint16_t kcp_port);
    
    // 启动服务端
    bool startServer();
    
    // 连接 (客户端)
    bool connect();
    
    // 接收数据 (适配原有接口)
    status recv(byte* buff, int size);
    
    // 发送数据 (适配原有接口)
    status send(const byte* buff, int size);
    
    // 接受连接 (服务端)
    bool accept();
    
    // 断开连接
    void disconnect();
    
    // 关闭
    void close();
    
    // 获取连接状态
    bool isConnected() const;
    
    // 获取统计信息
    Stats getStats() const;
    
    // 重置统计
    void resetStats();
    
    // 获取活跃通道
    deskx::net::v2::Channel getActiveChannel() const;
    
    // 切换通道模式
    bool switchChannel(deskx::net::v2::Channel ch);
    
private:
    // 内部初始化
    bool initInternal(const std::string& host, uint16_t tcp_port, uint16_t kcp_port);
    
    // 工作线程
    void worker();
    
    // 接收回调包装
    void onReceive(deskx::net::v2::FrameType type, const uint8_t* data, size_t len);
    
    // 状态回调包装
    void onStatus(bool connected, deskx::net::v2::Channel ch);
};

// 全局适配器实例
extern AdapterV2& getAdapter();

} // namespace net

#endif // DESKX_NET_ADAPTER_V2_HPP
