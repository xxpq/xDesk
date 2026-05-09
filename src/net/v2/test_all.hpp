/**
 * DeskX Network v2 - 完整编译测试
 * 包含所有核心功能的单元测试
 */

#ifndef DESKX_NET_V2_TEST_HPP
#define DESKX_NET_V2_TEST_HPP

#include "frame.hpp"
#include "stack.hpp"
#include "tcp_stack.hpp"
#include "kcp_stack.hpp"
#include <iostream>
#include <cstring>
#include <vector>
#include <chrono>

namespace deskx {
namespace net {
namespace test {

// ============================================================================
// 测试框架
// ============================================================================

static int g_passed = 0;
static int g_failed = 0;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (cond) { \
            g_passed++; \
            std::cout << "[PASS] " << msg << "\n"; \
        } else { \
            g_failed++; \
            std::cout << "[FAIL] " << msg << "\n"; \
        } \
    } while(0)

#define TEST_CASE(name) \
    std::cout << "\n=== " << name << " ===\n"

// ============================================================================
// 1. 帧结构测试
// ============================================================================

bool test_frame_header() {
    TEST_CASE("FrameHeader 结构测试");
    
    // 测试结构体大小
    FrameHeader header{};
    TEST_ASSERT(sizeof(header) == 24, "FrameHeader 大小应为 24 字节");
    
    // 测试序列化/反序列化
    header.magic = MAGIC_DESK;
    header.type = (uint8_t)FrameType::CTRL_HELLO;
    header.flags = 0x01;
    header.payload_size = htonl(1024);
    header.sequence = htonl(42);
    header.timestamp = htonl(1234567890);
    
    // 序列化
    uint8_t buf[sizeof(FrameHeader)];
    std::memcpy(buf, &header, sizeof(FrameHeader));
    
    // 反序列化
    FrameHeader parsed;
    std::memcpy(&parsed, buf, sizeof(FrameHeader));
    
    TEST_ASSERT(parsed.magic == MAGIC_DESK, "魔术常量正确");
    TEST_ASSERT(parsed.type == (uint8_t)FrameType::CTRL_HELLO, "帧类型正确");
    TEST_ASSERT(ntohl(parsed.payload_size) == 1024, "负载大小正确");
    TEST_ASSERT(ntohl(parsed.sequence) == 42, "序列号正确");
    
    return g_failed == 0;
}

// ============================================================================
// 2. 协议栈工厂测试
// ============================================================================

bool test_stack_factory() {
    TEST_CASE("协议栈工厂测试");
    
    // TCP 栈
    auto tcp = IProtocolStack::createTCP();
    TEST_ASSERT(tcp != nullptr, "TCP 栈创建成功");
    TEST_ASSERT(tcp->type() == Channel::TCP, "TCP 栈类型正确");
    
    // KCP 栈
    auto kcp = IProtocolStack::createKCP();
    TEST_ASSERT(kcp != nullptr, "KCP 栈创建成功");
    TEST_ASSERT(kcp->type() == Channel::KCP, "KCP 栈类型正确");
    
    return g_failed == 0;
}

// ============================================================================
// 3. TCP 栈初始化测试
// ============================================================================

bool test_tcp_init() {
    TEST_CASE("TCP 栈初始化测试");
    
    auto tcp = IProtocolStack::createTCP();
    TEST_ASSERT(tcp != nullptr, "TCP 栈创建成功");
    
    // 测试初始状态
    TEST_ASSERT(!tcp->isConnected(), "初始未连接");
    TEST_ASSERT(tcp->status() == LinkStatus::DOWN, "初始状态为 DOWN");
    
    // 获取初始统计
    auto stats = tcp->getStats();
    TEST_ASSERT(stats.bytes_sent == 0, "初始发送字节为 0");
    TEST_ASSERT(stats.bytes_recv == 0, "初始接收字节为 0");
    
    return g_failed == 0;
}

// ============================================================================
// 4. KCP 栈初始化测试
// ============================================================================

bool test_kcp_init() {
    TEST_CASE("KCP 栈初始化测试");
    
    auto kcp = IProtocolStack::createKCP();
    TEST_ASSERT(kcp != nullptr, "KCP 栈创建成功");
    
    // 测试初始状态
    TEST_ASSERT(!kcp->isConnected(), "初始未连接");
    TEST_ASSERT(kcp->status() == LinkStatus::DOWN, "初始状态为 DOWN");
    
    // 获取初始统计
    auto stats = kcp->getStats();
    TEST_ASSERT(stats.bytes_sent == 0, "初始发送字节为 0");
    TEST_ASSERT(stats.bytes_recv == 0, "初始接收字节为 0");
    
    return g_failed == 0;
}

// ============================================================================
// 5. 流量分发器测试
// ============================================================================

bool test_dispatcher() {
    TEST_CASE("TrafficDispatcher 测试");
    
    TrafficDispatcher disp;
    
    // 测试模式设置
    disp.setMode(TransportMode::HYBRID_AUTO);
    TEST_ASSERT(disp.mode() == TransportMode::HYBRID_AUTO, "HYBRID_AUTO 模式");
    
    disp.setMode(TransportMode::DUAL_STACK);
    TEST_ASSERT(disp.mode() == TransportMode::DUAL_STACK, "DUAL_STACK 模式");
    
    disp.setMode(TransportMode::LAN_ONLY);
    TEST_ASSERT(disp.mode() == TransportMode::LAN_ONLY, "LAN_ONLY 模式");
    
    disp.setMode(TransportMode::WAN_ONLY);
    TEST_ASSERT(disp.mode() == TransportMode::WAN_ONLY, "WAN_ONLY 模式");
    
    // 获取统计
    auto stats = disp.getStats();
    TEST_ASSERT(stats.tcp_packets == 0, "TCP 包计数为 0");
    TEST_ASSERT(stats.kcp_packets == 0, "KCP 包计数为 0");
    
    return g_failed == 0;
}

// ============================================================================
// 6. 数据聚合器测试
// ============================================================================

bool test_aggregator() {
    TEST_CASE("DataAggregator 测试");
    
    DataAggregator agg;
    
    // 测试序列号去重
    uint32_t seq = 100;
    TEST_ASSERT(!agg.isDuplicate(seq, Channel::TCP), "首次序列号非重复");
    
    // 再次检查同一序列号
    TEST_ASSERT(agg.isDuplicate(seq, Channel::TCP), "重复序列号检测");
    
    // 测试统计
    auto stats = agg.getStats();
    TEST_ASSERT(stats.frames_dropped > 0, "有丢弃帧统计");
    
    return g_failed == 0;
}

// ============================================================================
// 7. 帧类型枚举测试
// ============================================================================

bool test_frame_types() {
    TEST_CASE("帧类型枚举测试");
    
    // 控制帧
    TEST_ASSERT((uint8_t)FrameType::HANDSHAKE == 0x01, "HANDSHAKE 类型");
    TEST_ASSERT((uint8_t)FrameType::HANDSHAKE_ACK == 0x02, "HANDSHAKE_ACK 类型");
    TEST_ASSERT((uint8_t)FrameType::HEARTBEAT_REQ == 0x03, "HEARTBEAT_REQ 类型");
    TEST_ASSERT((uint8_t)FrameType::HEARTBEAT_ACK == 0x04, "HEARTBEAT_ACK 类型");
    TEST_ASSERT((uint8_t)FrameType::CTRL_DISCONNECT == 0x05, "CTRL_DISCONNECT 类型");
    TEST_ASSERT((uint8_t)FrameType::CTRL_HELLO == 0x06, "CTRL_HELLO 类型");
    
    // 数据帧
    TEST_ASSERT((uint8_t)FrameType::FRAME_VIDEO == 0x10, "FRAME_VIDEO 类型");
    TEST_ASSERT((uint8_t)FrameType::FRAME_AUDIO == 0x11, "FRAME_AUDIO 类型");
    TEST_ASSERT((uint8_t)FrameType::FRAME_INPUT == 0x12, "FRAME_INPUT 类型");
    TEST_ASSERT((uint8_t)FrameType::FRAME_FILE == 0x13, "FRAME_FILE 类型");
    
    // 传输模式
    TEST_ASSERT((uint8_t)TransportMode::LAN_ONLY == 0x01, "LAN_ONLY 模式");
    TEST_ASSERT((uint8_t)TransportMode::WAN_ONLY == 0x02, "WAN_ONLY 模式");
    TEST_ASSERT((uint8_t)TransportMode::DUAL_STACK == 0x03, "DUAL_STACK 模式");
    TEST_ASSERT((uint8_t)TransportMode::HYBRID_AUTO == 0x04, "HYBRID_AUTO 模式");
    
    return g_failed == 0;
}

// ============================================================================
// 8. 链路状态测试
// ============================================================================

bool test_link_status() {
    TEST_CASE("链路状态枚举测试");
    
    TEST_ASSERT((uint8_t)LinkStatus::DOWN == 0x00, "DOWN 状态");
    TEST_ASSERT((uint8_t)LinkStatus::CONNECTING == 0x01, "CONNECTING 状态");
    TEST_ASSERT((uint8_t)LinkStatus::UP == 0x02, "UP 状态");
    TEST_ASSERT((uint8_t)LinkStatus::RECONNECTING == 0x03, "RECONNECTING 状态");
    TEST_ASSERT((uint8_t)LinkStatus::FAILED == 0x04, "FAILED 状态");
    
    return g_failed == 0;
}

// ============================================================================
// 9. 通道枚举测试
// ============================================================================

bool test_channel_types() {
    TEST_CASE("通道类型枚举测试");
    
    TEST_ASSERT((uint8_t)Channel::NONE == 0x00, "NONE 通道");
    TEST_ASSERT((uint8_t)Channel::TCP == 0x01, "TCP 通道");
    TEST_ASSERT((uint8_t)Channel::KCP == 0x02, "KCP 通道");
    TEST_ASSERT((uint8_t)Channel::QUIC == 0x04, "QUIC 通道");
    TEST_ASSERT((uint8_t)Channel::ALL == 0x07, "ALL 通道");
    
    return g_failed == 0;
}

// ============================================================================
// 10. 版本信息测试
// ============================================================================

bool test_version() {
    TEST_CASE("版本信息测试");
    
    TEST_ASSERT(DESKX_NET_VERSION_MAJOR == 2, "主版本号正确");
    TEST_ASSERT(DESKX_NET_VERSION_MINOR == 0, "次版本号正确");
    
    std::cout << "DeskX Network v" << DESKX_NET_VERSION_MAJOR << "."
              << DESKX_NET_VERSION_MINOR << "." << DESKX_NET_VERSION_PATCH << "\n";
    
    return g_failed == 0;
}

// ============================================================================
// 主测试函数
// ============================================================================

int run_all_tests() {
    std::cout << "╔════════════════════════════════════════════════════════════╗\n";
    std::cout << "║        DeskX Network v2 - 完整功能测试套件                  ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════╝\n";
    
    g_passed = 0;
    g_failed = 0;
    
    // 运行所有测试
    test_frame_header();
    test_stack_factory();
    test_tcp_init();
    test_kcp_init();
    test_dispatcher();
    test_aggregator();
    test_frame_types();
    test_link_status();
    test_channel_types();
    test_version();
    
    // 打印结果
    std::cout << "\n╔════════════════════════════════════════════════════════════╗\n";
    std::cout << "║                      测试结果                              ║\n";
    std::cout << "╠════════════════════════════════════════════════════════════╣\n";
    std::cout << "║  通过: " << g_passed << "                                             ║\n";
    std::cout << "║  失败: " << g_failed << "                                             ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════╝\n";
    
    return g_failed == 0 ? 0 : 1;
}

}
}
}

#endif // DESKX_NET_V2_TEST_HPP
