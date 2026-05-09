#ifndef DESKX_NET_V2_FRAME_HPP
#define DESKX_NET_V2_FRAME_HPP

#include <cstdint>
#include <cstring>

#if defined(_WIN32) || defined(_WIN64)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

namespace deskx {
namespace net {
namespace v2 {

constexpr uint32_t MAGIC_DESK = 0x44455800u;
constexpr uint8_t PROTOCOL_VERSION = 2;
constexpr uint16_t DEFAULT_TCP_PORT = 7900;
constexpr uint16_t DEFAULT_UDP_PORT = 7901;

enum class FrameType : uint8_t {
    HANDSHAKE = 0x01,
    HANDSHAKE_ACK = 0x02,
    HEARTBEAT_REQ = 0x03,
    HEARTBEAT_ACK = 0x04,
    CTRL_DISCONNECT = 0x05,
    CTRL_HELLO = 0x06,
    FRAME_VIDEO = 0x10,
    FRAME_AUDIO = 0x11,
    FRAME_INPUT = 0x12,
    FRAME_FILE = 0x13,
    FRAME_DATA = 0x14,
    FRAME_RAW = 0x15,
    KEEPALIVE = 0xFF,
};

enum class TransportMode : uint8_t {
    HYBRID_AUTO = 0x04,
    LAN_ONLY = 0x01,
    WAN_ONLY = 0x02,
    DUAL_STACK = 0x03,
};

enum class Channel : uint8_t {
    NONE = 0x00,
    TCP = 0x01,
    KCP = 0x02,
    QUIC = 0x04,
    ALL = 0x07,
    BOTH = 0x03,
};

enum class LinkStatus : uint8_t {
    DOWN = 0x00,
    CONNECTING = 0x01,
    UP = 0x02,
    RECONNECTING = 0x03,
    FAILED = 0x04,
};

#pragma pack(push, 1)
struct FrameHeader {
    uint32_t magic;
    uint8_t version;
    uint8_t type;
    uint8_t flags;
    uint8_t channel;
    uint32_t payload_size;
    uint32_t sequence;
    uint32_t timestamp;
    uint32_t checksum;
};

struct Handshake {
    FrameHeader header;
    uint8_t supported_protocols;
    uint8_t preferred_protocol;
    uint8_t compression_level;
    uint8_t rgb_bits;
    uint8_t color_distance;
    uint8_t fps_limit;
    uint16_t viewport_width;
    uint16_t viewport_height;
    uint8_t reserved[8];
};

struct Heartbeat {
    FrameHeader header;
    uint32_t latency_tcp;
    uint32_t latency_kcp;
    uint32_t latency_quic;
    uint8_t link_status;
    uint8_t available_channels;
    uint8_t current_mode;
    uint8_t reserved;
};
#pragma pack(pop)

static_assert(sizeof(FrameHeader) == 24, "FrameHeader must be 24 bytes");

inline void initHeader(FrameHeader& hdr, FrameType type) {
    std::memset(&hdr, 0, sizeof(hdr));
    hdr.magic = MAGIC_DESK;
    hdr.version = PROTOCOL_VERSION;
    hdr.type = static_cast<uint8_t>(type);
}

inline void htonFrameHeader(FrameHeader& hdr) {
    hdr.magic = htonl(hdr.magic);
    hdr.payload_size = htonl(hdr.payload_size);
    hdr.sequence = htonl(hdr.sequence);
    hdr.timestamp = htonl(hdr.timestamp);
    hdr.checksum = htonl(hdr.checksum);
}

inline void ntohFrameHeader(FrameHeader& hdr) {
    hdr.magic = ntohl(hdr.magic);
    hdr.payload_size = ntohl(hdr.payload_size);
    hdr.sequence = ntohl(hdr.sequence);
    hdr.timestamp = ntohl(hdr.timestamp);
    hdr.checksum = ntohl(hdr.checksum);
}

} // namespace v2
} // namespace net
} // namespace deskx

#endif // DESKX_NET_V2_FRAME_HPP
