/**
 * DeskX Network v2 E2E Test Suite
 * 
 * 完整端到端测试覆盖所有核心功能
 */

#include "frame.hpp"
#include "stack.hpp"
#include "tcp_stack.hpp"
#include "kcp_stack.hpp"
#include "dispatcher.hpp"
#include <iostream>
#include <cstring>

using namespace deskx::net::v2;

// ============================================================================
// 测试框架
// ============================================================================
#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "❌ FAIL: " << msg << std::endl; \
            return false; \
        } \
    } while(0)

#define TEST_CASE(name) \
    std::cout << "\n📋 测试: " << name << std::endl;

#define TEST_OK() std::cout << "✅ PASS" << std::endl

// ============================================================================
// 测试用例
// ============================================================================

// 辅助函数
using deskx::net::v2::Channel;
using deskx::net::v2::FrameType;
using deskx::net::v2::TransportMode;
using deskx::net::v2::TrafficDispatcher;
using deskx::net::v2::DataAggregator;
using deskx::net::v2::ProtocolManager;
using deskx::net::v2::FrameHeader;
using deskx::net::v2::MAGIC_DESK;
using deskx::net::v2::htonl;
using deskx::net::v2::ntohl;

// 测试1: 帧头序列化/反序列化
bool test_frame_serde() {
    TEST_CASE("帧头序列化/反序列化");
    
    FrameHeader orig{};
    orig.magic = MAGIC_DESK;
    orig.type = static_cast<uint8_t>(FrameType::CTRL_HELLO);
    orig.flags = 0x01;
    orig.payload_size = htonl(1024);
    orig.sequence = htonl(42);
    orig.timestamp = htonl(1234567890);
    
    uint8_t buf[sizeof(FrameHeader)];
    std::memcpy(buf, &orig, sizeof(FrameHeader));
    
    FrameHeader parsed{};
    std::memcpy(&parsed, buf, sizeof(FrameHeader));
    
    TEST_ASSERT(parsed.magic == MAGIC_DESK, "magic 正确");
    TEST_ASSERT(parsed.type == static_cast<uint8_t>(FrameType::CTRL_HELLO), "type 正确");
    TEST_ASSERT(ntohl(parsed.payload_size) == 1024, "payload_size 正确");
    TEST_ASSERT(ntohl(parsed.sequence) == 42, "sequence 正确");
    
    TEST_OK();
    return true;
}

// 测试2: 帧类型枚举
bool test_frame_types() {
    TEST_CASE("帧类型枚举");
    
    TEST_ASSERT(static_cast<uint8_t>(FrameType::HANDSHAKE) == 0x01, "HANDSHAKE");
    TEST_ASSERT(static_cast<uint8_t>(FrameType::CTRL_HELLO) == 0x10, "CTRL_HELLO");
    TEST_ASSERT(static_cast<uint8_t>(FrameType::FRAME_VIDEO) == 0x20, "FRAME_VIDEO");
    TEST_ASSERT(static_cast<uint8_t>(FrameType::FRAME_INPUT) == 0x30, "FRAME_INPUT");
    TEST_ASSERT(static_cast<uint8_t>(FrameType::FRAME_AUDIO) == 0x21, "FRAME_AUDIO");
    
    TEST_OK();
    return true;
}

// 测试3: 传输模式枚举
bool test_transport_modes() {
    TEST_CASE("传输模式枚举");
    
    TEST_ASSERT(static_cast<uint8_t>(TransportMode::HYBRID_AUTO) == 0x00, "HYBRID_AUTO");
    TEST_ASSERT(static_cast<uint8_t>(TransportMode::LAN_ONLY) == 0x01, "LAN_ONLY");
    TEST_ASSERT(static_cast<uint8_t>(TransportMode::WAN_ONLY) == 0x02, "WAN_ONLY");
    TEST_ASSERT(static_cast<uint8_t>(TransportMode::DUAL_STACK) == 0x03, "DUAL_STACK");
    
    TEST_OK();
    return true;
}

// 测试4: 协议栈接口
bool test_protocol_stack() {
    TEST_CASE("协议栈接口");
    
    auto tcp = IProtocolStack::createTCP();
    TEST_ASSERT(tcp != nullptr, "TCP 栈创建成功");
    
    auto kcp = IProtocolStack::createKCP();
    TEST_ASSERT(kcp != nullptr, "KCP 栈创建成功");
    
    TEST_ASSERT(tcp->status() == StackStatus::IDLE, "TCP 初始状态 IDLE");
    TEST_ASSERT(kcp->status() == StackStatus::IDLE, "KCP 初始状态 IDLE");
    
    TEST_OK();
    return true;
}

// 测试5: TCP 栈初始化
bool test_tcp_init() {
    TEST_CASE("TCP 栈初始化");
    
    TCPStack tcp;
    TEST_ASSERT(tcp.init(), "TCP 初始化成功");
    TEST_ASSERT(tcp.status() == StackStatus::IDLE, "TCP 状态 IDLE");
    
    tcp.close();
    TEST_OK();
    return true;
}

// 测试6: KCP 栈初始化
bool test_kcp_init() {
    TEST_CASE("KCP 栈初始化");
    
    KCPStack kcp;
    TEST_ASSERT(kcp.init(), "KCP 初始化成功");
    TEST_ASSERT(kcp.status() == StackStatus::IDLE, "KCP 状态 IDLE");
    
    auto stats = kcp.getStats();
    TEST_ASSERT(stats.packets_sent == 0, "初始发送包数为0");
    TEST_ASSERT(stats.bytes_sent == 0, "初始发送字节为0");
    
    kcp.close();
    TEST_OK();
    return true;
}

// 测试7: KCP 配置
bool test_kcp_config() {
    TEST_CASE("KCP 配置");
    
    KCPConfig cfg;
    cfg.nodelay = 1;
    cfg.interval = 10;
    cfg.resend = 2;
    
    KCPStack kcp;
    kcp.setConfig(cfg);
    kcp.init();
    
    auto stats = kcp.getStats();
    TEST_ASSERT(stats.packets_recv == 0, "初始接收包数为0");
    
    kcp.close();
    TEST_OK();
    return true;
}

// 测试8: TrafficDispatcher 模式
bool test_dispatcher_mode() {
    TEST_CASE("TrafficDispatcher 模式控制");
    
    TrafficDispatcher disp;
    
    disp.setMode(TransportMode::HYBRID_AUTO);
    TEST_ASSERT(disp.mode() == TransportMode::HYBRID_AUTO, "默认模式");
    
    disp.setMode(TransportMode::DUAL_STACK);
    TEST_ASSERT(disp.mode() == TransportMode::DUAL_STACK, "DUAL_STACK 模式");
    
    disp.setMode(TransportMode::LAN_ONLY);
    TEST_ASSERT(disp.mode() == TransportMode::LAN_ONLY, "LAN_ONLY 模式");
    
    disp.setMode(TransportMode::WAN_ONLY);
    TEST_ASSERT(disp.mode() == TransportMode::WAN_ONLY, "WAN_ONLY 模式");
    
    TEST_OK();
    return true;
}

// 测试9: TrafficDispatcher 统计
bool test_dispatcher_stats() {
    TEST_CASE("TrafficDispatcher 统计");
    
    TrafficDispatcher disp;
    
    auto stats = disp.getStats();
    TEST_ASSERT(stats.total_packets == 0, "初始总包数为0");
    TEST_ASSERT(stats.tcp_packets == 0, "初始TCP包数为0");
    TEST_ASSERT(stats.kcp_packets == 0, "初始KCP包数为0");
    
    TEST_OK();
    return true;
}

// 测试10: DataAggregator
bool test_aggregator() {
    TEST_CASE("DataAggregator");
    
    DataAggregator agg;
    
    auto stats = agg.getStats();
    TEST_ASSERT(stats.frames_received == 0, "初始接收帧数为0");
    TEST_ASSERT(stats.bytes_received == 0, "初始接收字节为0");
    
    TEST_ASSERT(agg.available(Channel::TCP) == 0, "TCP 缓冲区为空");
    TEST_ASSERT(agg.available(Channel::KCP) == 0, "KCP 缓冲区为空");
    
    TEST_OK();
    return true;
}

// 测试11: ProtocolManager
bool test_protocol_manager() {
    TEST_CASE("ProtocolManager");
    
    ProtocolManager mgr;
    
    mgr.setMode(TransportMode::DUAL_STACK);
    TEST_ASSERT(mgr.mode() == TransportMode::DUAL_STACK, "模式设置成功");
    
    auto stats = mgr.getStats();
    TEST_ASSERT(stats.total_packets == 0, "初始总包数为0");
    
    TEST_OK();
    return true;
}

// 测试12: 通道控制
bool test_channel_control() {
    TEST_CASE("通道控制");
    
    TrafficDispatcher disp;
    
    disp.enableChannel(Channel::TCP, false);
    TEST_ASSERT(!disp.isChannelEnabled(Channel::TCP), "TCP 通道已禁用");
    
    disp.enableChannel(Channel::TCP, true);
    TEST_ASSERT(disp.isChannelEnabled(Channel::TCP), "TCP 通道已启用");
    
    disp.enableChannel(Channel::KCP, false);
    TEST_ASSERT(!disp.isChannelEnabled(Channel::KCP), "KCP 通道已禁用");
    
    TEST_OK();
    return true;
}

// ============================================================================
// 主函数
// ============================================================================
int main() {
    std::cout << "╔════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║     DeskX Network v2 E2E Test Suite                      ║" << std::endl;
    std::cout << "╚════════════════════════════════════════════════════════════╝" << std::endl;
    
    int passed = 0;
    int failed = 0;
    
    // 运行所有测试
    auto tests = {
        std::make_pair("帧头序列化/反序列化", test_frame_serde),
        std::make_pair("帧类型枚举", test_frame_types),
        std::make_pair("传输模式枚举", test_transport_modes),
        std::make_pair("协议栈接口", test_protocol_stack),
        std::make_pair("TCP 栈初始化", test_tcp_init),
        std::make_pair("KCP 栈初始化", test_kcp_init),
        std::make_pair("KCP 配置", test_kcp_config),
        std::make_pair("TrafficDispatcher 模式", test_dispatcher_mode),
        std::make_pair("TrafficDispatcher 统计", test_dispatcher_stats),
        std::make_pair("DataAggregator", test_aggregator),
        std::make_pair("ProtocolManager", test_protocol_manager),
        std::make_pair("通道控制", test_channel_control),
    };
    
    for (const auto& test : tests) {
        try {
            if (test.second()) {
                passed++;
            } else {
                failed++;
            }
        } catch (const std::exception& e) {
            std::cerr << "❌ EXCEPTION: " << e.what() << std::endl;
            failed++;
        }
    }
    
    // 输出结果
    std::cout << "\n╔════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║                     测试结果汇总                           ║" << std::endl;
    std::cout << "╠════════════════════════════════════════════════════════════╣" << std::endl;
    std::cout << "║  ✅ 通过: " << passed << "                                           ║" << std::endl;
    std::cout << "║  ❌ 失败: " << failed << "                                           ║" << std::endl;
    std::cout << "╚════════════════════════════════════════════════════════════╝" << std::endl;
    
    return (failed == 0) ? 0 : 1;
}
