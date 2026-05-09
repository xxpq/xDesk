/**
 * DeskX Network v2 - CRC32 Implementation
 */

#include "stack.hpp"

namespace deskx {
namespace net {
namespace v2 {

// CRC32 表
static uint32_t crc32_table[256] = {0};

static void init_crc32_table() {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
        crc32_table[i] = crc;
    }
}

static bool crc32_initialized = false;

uint32_t calcChecksum(const uint8_t* data, size_t len) {
    if (!crc32_initialized) {
        init_crc32_table();
        crc32_initialized = true;
    }
    
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

} // namespace v2
} // namespace net
} // namespace deskx
