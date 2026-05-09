#include "frame.hpp"
#include "stack.hpp"
#include "tcp_stack.hpp"
#include "kcp_stack.hpp"
#include "dispatcher.hpp"

#include <cstring>
#include <iostream>

using namespace deskx::net::v2;

int main() {
    std::cout << "=== DeskX Network v2 Compile Test ===" << std::endl;

    std::cout << "Test 1: FrameHeader size = " << sizeof(FrameHeader) << " bytes";
    if (sizeof(FrameHeader) != 24) {
        std::cout << " [FAIL]" << std::endl;
        return 1;
    }
    std::cout << " [PASS]" << std::endl;

    FrameHeader hdr {};
    initHeader(hdr, FrameType::FRAME_VIDEO);
    std::cout << "Test 2: Header init ";
    if (hdr.magic != MAGIC_DESK || hdr.type != static_cast<uint8_t>(FrameType::FRAME_VIDEO)) {
        std::cout << "[FAIL]" << std::endl;
        return 1;
    }
    std::cout << "[PASS]" << std::endl;

    hdr.payload_size = 1024;
    hdr.sequence = 42;
    hdr.timestamp = 1234567890;
    FrameHeader saved = hdr;
    htonFrameHeader(hdr);
    ntohFrameHeader(hdr);
    std::cout << "Test 3: Byte order ";
    if (hdr.payload_size != saved.payload_size || hdr.sequence != saved.sequence) {
        std::cout << "[FAIL]" << std::endl;
        return 1;
    }
    std::cout << "[PASS]" << std::endl;

    uint8_t data[] = {1, 2, 3, 4, 5};
    uint32_t crc = calcChecksum(data, sizeof(data));
    std::cout << "Test 4: CRC32 = 0x" << std::hex << crc << std::dec;
    if (crc == 0) {
        std::cout << " [FAIL]" << std::endl;
        return 1;
    }
    std::cout << " [PASS]" << std::endl;

    std::cout << "Test 5: TCPStack ";
    TCPStack tcp;
    if (!tcp.init()) {
        std::cout << "[FAIL]" << std::endl;
        return 1;
    }
    tcp.close();
    std::cout << "[PASS]" << std::endl;

    std::cout << "Test 6: KCPStack ";
    KCPStack kcp;
    if (!kcp.init()) {
        std::cout << "[FAIL]" << std::endl;
        return 1;
    }
    kcp.close();
    std::cout << "[PASS]" << std::endl;

    std::cout << "Test 7: TrafficDispatcher ";
    TrafficDispatcher disp;
    disp.setMode(TransportMode::DUAL_STACK);
    if (disp.mode() != TransportMode::DUAL_STACK) {
        std::cout << "[FAIL]" << std::endl;
        return 1;
    }
    std::cout << "[PASS]" << std::endl;

    std::cout << "Test 8: DataAggregator ";
    DataAggregator agg;
    if (agg.available(Channel::TCP) != 0) {
        std::cout << "[FAIL]" << std::endl;
        return 1;
    }
    std::cout << "[PASS]" << std::endl;

    std::cout << "\n=== ALL TESTS PASSED ===" << std::endl;
    return 0;
}
