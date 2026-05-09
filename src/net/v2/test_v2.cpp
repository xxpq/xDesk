/**
 * DeskX Network v2 Integration Test
 */

#include "net/v2.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <cstring>

using namespace deskx::net;

// 全局状态
std::atomic<bool> server_ready{false};
std::atomic<bool> client_connected{false};
std::atomic<bool> test_passed{true};

// 统计
struct TestStats {
    uint64_t frames_sent = 0;
    uint64_t frames_recv = 0;
    uint64_t bytes_sent = 0;
    uint64_t bytes_recv = 0;
} server_stats, client_stats;

void print_stats(const char* prefix, const TestStats& s) {
    std::cout << prefix 
              << " frames: " << s.frames_recv 
              << " bytes: " << s.bytes_recv << std::endl;
}

// 服务端线程
void run_server(uint16_t tcp_port, uint16_t kcp_port) {
    std::cout << "[Server] Starting on TCP:" << tcp_port << " KCP:" << kcp_port << std::endl;
    
    ProtocolManager mgr;
    mgr.setMode(TransportMode::DUAL_STACK);
    
    // 设置帧接收回调
    mgr.setReceiveCallback([&](FrameType type, const uint8_t* data, size_t len) {
        client_stats.frames_recv++;
        client_stats.bytes_recv += len;
        
        // 回显测试：发送响应
        if (type == FrameType::FRAME_VIDEO) {
            std::string response = "ACK";
            mgr.sendFrame(FrameType::FRAME_CONTROL, 
                         reinterpret_cast<const uint8_t*>(response.data()),
                         response.size());
        }
    });
    
    // 状态回调
    mgr.setStatusCallback([&](bool connected, Channel active_ch) {
        if (connected) {
            std::cout << "[Server] Client connected via " 
                      << (active_ch == Channel::TCP ? "TCP" : "KCP") << std::endl;
            server_ready = true;
        } else {
            std::cout << "[Server] Client disconnected" << std::endl;
            server_ready = false;
        }
    });
    
    // 初始化为服务端
    if (!mgr.initServer(tcp_port, kcp_port)) {
        std::cerr << "[Server] Init failed" << std::endl;
        return;
    }
    
    // 接受连接
    if (!mgr.startServer()) {
        std::cerr << "[Server] Start failed" << std::endl;
        return;
    }
    
    std::cout << "[Server] Listening..." << std::endl;
    
    // 运行一段时间
    std::this_thread::sleep_for(std::chrono::seconds(10));
    
    // 发送测试数据
    if (server_ready) {
        std::cout << "[Server] Sending test frames..." << std::endl;
        for (int i = 0; i < 100; i++) {
            uint8_t test_data[1024];
            std::memset(test_data, i & 0xFF, sizeof(test_data));
            
            if (mgr.sendFrame(FrameType::FRAME_VIDEO, test_data, sizeof(test_data))) {
                server_stats.frames_sent++;
                server_stats.bytes_sent += sizeof(test_data);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    
    // 打印统计
    print_stats("[Server] Final:", server_stats);
    
    mgr.close();
    std::cout << "[Server] Shutdown complete" << std::endl;
}

// 客户端线程
void run_client(const std::string& host, uint16_t tcp_port, uint16_t kcp_port) {
    std::cout << "[Client] Connecting to " << host << ":" << tcp_port << "," << kcp_port << std::endl;
    
    // 等待服务端就绪
    while (!server_ready) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (!server_ready) {
            std::cout << "[Client] Waiting for server..." << std::endl;
        }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    ProtocolManager mgr;
    mgr.setMode(TransportMode::DUAL_STACK);
    
    // 设置帧接收回调
    mgr.setReceiveCallback([&](FrameType type, const uint8_t* data, size_t len) {
        server_stats.frames_recv++;
        server_stats.bytes_recv += len;
    });
    
    // 状态回调
    mgr.setStatusCallback([&](bool connected, Channel active_ch) {
        if (connected) {
            std::cout << "[Client] Connected to server via " 
                      << (active_ch == Channel::TCP ? "TCP" : "KCP") << std::endl;
            client_connected = true;
        } else {
            std::cout << "[Client] Disconnected" << std::endl;
            client_connected = false;
        }
    });
    
    // 初始化为客户端
    if (!mgr.init(host, tcp_port, kcp_port)) {
        std::cerr << "[Client] Init failed" << std::endl;
        return;
    }
    
    // 连接
    if (!mgr.connect()) {
        std::cerr << "[Client] Connect failed" << std::endl;
        return;
    }
    
    std::cout << "[Client] Connected, sending test frames..." << std::endl;
    
    // 发送测试数据
    for (int i = 0; i < 100; i++) {
        uint8_t test_data[1024];
        std::memset(test_data, (i + 1) & 0xFF, sizeof(test_data));
        
        if (mgr.sendFrame(FrameType::FRAME_VIDEO, test_data, sizeof(test_data))) {
            client_stats.frames_sent++;
            client_stats.bytes_sent += sizeof(test_data);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    // 等待响应
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // 打印统计
    print_stats("[Client] Final:", client_stats);
    
    mgr.close();
    std::cout << "[Client] Shutdown complete" << std::endl;
}

int main(int argc, char* argv[]) {
    std::cout << "=== DeskX Network v2 Integration Test ===" << std::endl;
    std::cout << "Version: " << DESKX_NET_VERSION_MAJOR << "." 
              << DESKX_NET_VERSION_MINOR << "." << DESKX_NET_VERSION_PATCH << std::endl;
    
    bool is_server = (argc < 2 || std::string(argv[1]) != "client");
    uint16_t tcp_port = 7900;
    uint16_t kcp_port = 7901;
    
    if (is_server) {
        std::cout << "Running as SERVER" << std::endl;
        run_server(tcp_port, kcp_port);
    } else {
        std::cout << "Running as CLIENT" << std::endl;
        std::string host = (argc > 2) ? argv[2] : "127.0.0.1";
        run_client(host, tcp_port, kcp_port);
    }
    
    // 汇总统计
    std::cout << "\n=== Test Summary ===" << std::endl;
    std::cout << "Server: sent " << server_stats.frames_sent << " frames (" 
              << server_stats.bytes_sent << " bytes), recv " 
              << server_stats.frames_recv << " frames" << std::endl;
    std::cout << "Client: sent " << client_stats.frames_sent << " frames (" 
              << client_stats.bytes_sent << " bytes), recv " 
              << client_stats.frames_recv << " frames" << std::endl;
    
    if (server_stats.frames_sent > 0 && client_stats.frames_sent > 0) {
        std::cout << "\n✅ Test PASSED" << std::endl;
        return 0;
    } else {
        std::cout << "\n❌ Test FAILED" << std::endl;
        return 1;
    }
}
